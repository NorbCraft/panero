/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerocloud.h"
#include "panerocloudutils.h"
#include "../paneropackageinstaller.h"
#include "panerocloudrevoke.h"
#include "panerooffline.h"
#include "panerorclone.h"
#include "panerocloudsetup.h"
#include "../FileSystem/paneroslowpaths.h"
#include <QEventLoop>
#include <QCryptographicHash>
#include <QJsonArray>

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QApplication>
#include <QProgressDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QProcess>
#include <QRegularExpression>
#include <QJsonObject>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

#include <KConfigCore/KConfigGroup>
#include <KIOFileWidgets/KFilePlacesModel>
#include <KI18n/KLocalizedString>
#include <KWidgetsAddons/KMessageBox>

#include <csignal>
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif

namespace
{
const char CLOUD_GROUP[] = "Panero Cloud";
const int POLL_INTERVAL_MS = 200;
const int STDERR_TAIL_LINES = 8;

/// rclone child processes get SIGTERM when Panero dies, even after a crash,
/// so no `rclone mount` keeps running unattended (rclone unmounts on SIGTERM).
class ChildProcess : public QProcess
{
public:
    using QProcess::QProcess;

protected:
    void setupChildProcess() override
    {
#ifdef Q_OS_LINUX
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
    }
};

QString fusermountProgram()
{
    QString p = QStandardPaths::findExecutable(QStringLiteral("fusermount3"));
    if (p.isEmpty())
        p = QStandardPaths::findExecutable(QStringLiteral("fusermount"));
    return p;
}

QString entryIdForRemote(const QString &remote)
{
    return QLatin1String("rclone:") + remote;
}

QString entryIdForServer(const QUrl &url)
{
    return QLatin1String("server:") + url.toString();
}
} // namespace

// static libraries need an explicit resource initialisation (outside namespaces)
static void initCloudIcons()
{
    static bool done = false;
    if (!done) {
        Q_INIT_RESOURCE(cloudicons);
        done = true;
    }
}

QIcon PaneroCloud::iconForType(const QString &type)
{
    initCloudIcons();
    if (type == QLatin1String("drive"))
        return QIcon(QStringLiteral(":/panero-cloud/google-drive.svg"));
    if (type == QLatin1String("onedrive"))
        return QIcon(QStringLiteral(":/panero-cloud/onedrive.svg"));
    // every other service keeps the folder-cloud icon used so far
    return QIcon::fromTheme(QStringLiteral("folder-cloud"), QIcon::fromTheme(QStringLiteral("folder-remote")));
}

PaneroCloud *PaneroCloud::instance()
{
    static PaneroCloud *s_instance = nullptr;
    if (!s_instance) {
        s_instance = new PaneroCloud(QCoreApplication::instance());
        qRegisterMetaType<PaneroCloud::Entry>();
    }
    return s_instance;
}

PaneroCloud::PaneroCloud(QObject *parent)
    : QObject(parent), m_config(KSharedConfig::openConfig())
{
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(300);
    connect(m_refreshTimer, &QTimer::timeout, this, &PaneroCloud::refresh);
    rebuildEntries();
    watchConfigFile();
    refresh();
    // cloud folders shown in a panel are re-read from the provider in the
    // background (DefaultFileSystem, stale-while-revalidate)
    PaneroSlowPaths::setRevalidator([this](const QString &dir, QObject *context, std::function<void()> done) {
        // Providers that report changes are polled by rclone every 30 s; a
        // forced re-read is not needed there and would hold rclone's lock on
        // the folder for the whole network listing (17-35 s seen on Google
        // Drive), so everything in it - listings, files other programs open -
        // waits meanwhile. Ctrl+R still forces one (reloadByUser()).
        for (auto it = m_mounts.constBegin(); it != m_mounts.constEnd(); ++it) {
            const Mount *m = it.value();
            if (m->notifies && (dir == m->dir || dir.startsWith(m->dir + QLatin1Char('/'))))
                return false;
        }
        return refreshFolder(dir, context, done);
    });
    PaneroSlowPaths::setNameProvider([this](const QString &mountRoot) {
        for (auto it = m_mounts.constBegin(); it != m_mounts.constEnd(); ++it) {
            if (it.value()->dir == mountRoot)
                return entry(it.key()).label;
        }
        return QString();
    });
}

PaneroCloud::~PaneroCloud()
{
    unmountAll();
}

void PaneroCloud::setConfig(KSharedConfigPtr config)
{
    m_config = config ? config : KSharedConfig::openConfig();
    rebuildEntries();
}

KConfigGroup PaneroCloud::group() const
{
    return KConfigGroup(m_config, CLOUD_GROUP);
}

// ---- availability ------------------------------------------------------------

QString PaneroCloud::rcloneProgram()
{
    return PaneroRclone::program();
}

bool PaneroCloud::rcloneAvailable()
{
    return PaneroRclone::usable();
}

bool PaneroCloud::fuseAvailable()
{
    return !fusermountProgram().isEmpty();
}

// Installs one package through PackageKit; false with a message box on failure.
static bool installPackage(QWidget *parent, const QString &package, const QString &display, bool (*check)())
{
    QString error;
    const auto result = PaneroPackageInstaller::install(parent, package, display, &error);
    if (result == PaneroPackageInstaller::Result::Cancelled)
        return false;
    if (check())
        return true;
    QString reason = error.toHtmlEscaped();
    if (reason.isEmpty())
        reason = i18n("%1 could not be installed automatically.", display);
    KMessageBox::error(parent,
                       reason
                           + i18n("<p>You can install it with the <b>Software</b> application, or in a terminal with:</p>"
                                  "<p><tt>sudo apt install %1</tt></p>",
                                  package),
                       i18n("%1 Not Installed", display));
    return false;
}

bool PaneroCloud::ensureRclone(QWidget *parent)
{
    QString problem;
    if (!PaneroRclone::usable(&problem)) {
        KMessageBox::error(parent, problem, i18n("Cloud Drives"));
        return false;
    }
    if (!fuseAvailable()) {
        const auto answer = KMessageBox::questionTwoActions(
            parent,
            i18n("<p>Opening cloud drives needs the <b>FUSE</b> file system tools (package fuse3). Install them now?</p>"
                 "<p>They are installed from your system's software sources; your system will ask you to authorize the installation.</p>"),
            i18n("Install FUSE?"), KGuiItem(i18n("Install"), QStringLiteral("download")), KStandardGuiItem::cancel());
        if (answer != KMessageBox::PrimaryAction)
            return false;
        if (!installPackage(parent, QStringLiteral("fuse3"), QStringLiteral("FUSE"), &PaneroCloud::fuseAvailable))
            return false;
    }
    QString error;
    const auto state = PaneroRclone::ensureStore(&error);
    if (state == PaneroRclone::StoreState::KeyMissing) {
        const auto answer = KMessageBox::warningTwoActions(
            parent,
            i18n("<p>%1</p><p>Start again with an empty cloud list? You then add your cloud drives again. Nothing in the clouds "
                 "is changed; you can remove Panero's old access in each provider's account settings.</p>",
                 error.toHtmlEscaped()),
            i18n("Cloud Drives"), KGuiItem(i18n("Start Again"), QStringLiteral("edit-clear")), KStandardGuiItem::cancel());
        if (answer != KMessageBox::PrimaryAction)
            return false;
        PaneroRclone::resetStore();
        refreshNow();
        return ensureRclone(parent);
    }
    if (state != PaneroRclone::StoreState::Ready) {
        KMessageBox::error(parent, error, i18n("Cloud Drives"));
        return false;
    }
    return true;
}

// ---- entries -----------------------------------------------------------------

QList<PaneroCloud::Entry> PaneroCloud::entries() const
{
    return m_entries;
}

PaneroCloud::Entry PaneroCloud::entry(const QString &id) const
{
    for (const Entry &e : m_entries) {
        if (e.id == id)
            return e;
    }
    return Entry();
}

QStringList PaneroCloud::remoteNames() const
{
    return m_remoteNames;
}

void PaneroCloud::rebuildEntries()
{
    QList<Entry> list;
    const KConfigGroup g = group();
    const auto servers = PaneroCloudUtils::readServers(g);
    for (const auto &s : servers) {
        Entry e;
        e.type = QStringLiteral("server");
        e.id = entryIdForServer(s.url);
        e.label = s.label;
        e.url = s.url;
        list << e;
    }
    for (const QString &name : qAsConst(m_remoteNames)) {
        Entry e;
        e.type = QStringLiteral("rclone");
        e.id = entryIdForRemote(name);
        e.rcloneRemote = name;
        e.rcloneType = m_remoteTypes.value(name);
        e.label = PaneroCloudUtils::remoteLabel(g, name);
        if (isMounted(e.id))
            e.url = QUrl::fromLocalFile(m_mounts.value(e.id)->dir);
        list << e;
    }
    m_entries = list;
    emit entriesChanged();
}

void PaneroCloud::parseRemotes(const QString &output)
{
    m_remoteNames.clear();
    m_remoteTypes.clear();
    const auto remotes = PaneroCloudUtils::parseListRemotes(output);
    for (const auto &r : remotes) {
        m_remoteNames << r.name;
        m_remoteTypes.insert(r.name, r.type);
    }
    std::sort(m_remoteNames.begin(), m_remoteNames.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
}

void PaneroCloud::refresh()
{
    const QString rclone = rcloneProgram();
    if (rclone.isEmpty() || !PaneroRclone::configExists()) {
        if (!m_remoteNames.isEmpty()) {
            m_remoteNames.clear();
            m_remoteTypes.clear();
        }
        rebuildEntries();
        return;
    }
    if (m_listProcess) {
        m_refreshPending = true; // the config may have changed since it started
        return;
    }
    m_refreshPending = false;
    m_listProcess = new QProcess(this);
    connect(m_listProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
                QProcess *p = m_listProcess;
                m_listProcess = nullptr;
                p->deleteLater();
                if (m_refreshPending) {
                    refresh(); // outdated result: ask again
                    return;
                }
                if (status == QProcess::NormalExit && exitCode == 0) {
                    parseRemotes(QString::fromUtf8(p->readAllStandardOutput()));
                    PaneroOffline::instance()->pruneRemotes(m_remoteNames);
                }
                rebuildEntries();
                watchConfigFile();
            });
    connect(m_listProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_listProcess)
            return;
        m_listProcess->deleteLater();
        m_listProcess = nullptr;
        rebuildEntries();
    });
    PaneroRclone::prepare(*m_listProcess, {QStringLiteral("listremotes"), QStringLiteral("--long")});
    m_listProcess->start();
}

void PaneroCloud::cancelAsyncRefresh()
{
    m_refreshPending = false;
    if (!m_listProcess)
        return;
    QProcess *p = m_listProcess;
    m_listProcess = nullptr;
    p->disconnect(this);
    p->kill();
    p->waitForFinished(1000);
    p->deleteLater();
}

bool PaneroCloud::refreshNow(int timeoutMs)
{
    cancelAsyncRefresh(); // its result could be older than this one
    const QString rclone = rcloneProgram();
    if (rclone.isEmpty() || !PaneroRclone::configExists()) {
        m_remoteNames.clear();
        m_remoteTypes.clear();
        rebuildEntries();
        return !rclone.isEmpty();
    }
    QProcess p;
    PaneroRclone::prepare(p, {QStringLiteral("listremotes"), QStringLiteral("--long")});
    p.start();
    if (!p.waitForFinished(timeoutMs) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        p.kill();
        p.waitForFinished(1000);
        return false;
    }
    parseRemotes(QString::fromUtf8(p.readAllStandardOutput()));
    PaneroOffline::instance()->pruneRemotes(m_remoteNames);
    rebuildEntries();
    watchConfigFile();
    return true;
}

void PaneroCloud::watchConfigFile()
{
    // rclone rewrites its config by renaming a temporary file, so watch the
    // directory as well and re-add the file each time.
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, m_refreshTimer, QOverload<>::of(&QTimer::start));
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, m_refreshTimer, QOverload<>::of(&QTimer::start));
    }
    const QString file = PaneroRclone::configPath();
    const QString dir = QFileInfo(file).absolutePath();
    if (QFileInfo::exists(dir) && !m_watcher->directories().contains(dir))
        m_watcher->addPath(dir);
    if (QFileInfo::exists(file) && !m_watcher->files().contains(file))
        m_watcher->addPath(file);
}

PaneroCloud::Entry PaneroCloud::addServer(const QUrl &url, const QString &label)
{
    QUrl clean = url;
    clean.setPassword(QString());
    KConfigGroup g = group();
    auto servers = PaneroCloudUtils::readServers(g);
    const QString text = label.trimmed().isEmpty() ? PaneroCloudUtils::defaultServerLabel(clean) : label.trimmed();
    bool found = false;
    for (auto &s : servers) {
        if (s.url == clean) {
            s.label = text;
            found = true;
        }
    }
    if (!found)
        servers << PaneroCloudUtils::Server{clean, text};
    PaneroCloudUtils::writeServers(g, servers);
    rebuildEntries();
    return entry(entryIdForServer(clean));
}

void PaneroCloud::setLabel(const Entry &e, const QString &label)
{
    KConfigGroup g = group();
    if (e.isServer()) {
        addServer(e.url, label);
        return;
    }
    if (e.isRclone()) {
        PaneroCloudUtils::setRemoteLabel(g, e.rcloneRemote, label.trimmed());
        rebuildEntries();
        updatePlaces();
    }
}

bool PaneroCloud::removeEntry(const Entry &e, QString *error)
{
    KConfigGroup g = group();
    if (e.isServer()) {
        auto servers = PaneroCloudUtils::readServers(g);
        for (int i = servers.count() - 1; i >= 0; --i) {
            if (entryIdForServer(servers.at(i).url) == e.id)
                servers.removeAt(i);
        }
        PaneroCloudUtils::writeServers(g, servers);
        rebuildEntries();
        return true;
    }
    if (!e.isRclone())
        return false;

    if (isMounted(e.id) || isMounting(e.id))
        unmount(e.id);
    const QString rclone = rcloneProgram();
    if (rclone.isEmpty()) {
        if (error)
            *error = i18n("rclone is not installed.");
        return false;
    }
    QByteArray err;
    if (PaneroRclone::run({QStringLiteral("config"), QStringLiteral("delete"), e.rcloneRemote}, nullptr, &err, 30000) != 0) {
        if (error)
            *error = PaneroCloudUtils::rcloneErrorSummary(QString::fromUtf8(err));
        return false;
    }
    // the remote's cache (downloaded file contents, bisync listings)
    QDir(PaneroRclone::cacheDir(e.rcloneRemote)).removeRecursively();
    PaneroCloudUtils::setRemoteLabel(g, e.rcloneRemote, QString());
    m_remoteNames.removeAll(e.rcloneRemote);
    m_remoteTypes.remove(e.rcloneRemote);
    rebuildEntries();
    return true;
}

// Signs out of one rclone remote after the confirmation; returns the line for the summary.
static QString signOutRemote(QWidget *parent, PaneroCloud *cloud, const PaneroCloud::Entry &e, bool *ok, QUrl *manage)
{
    *ok = false;
    PaneroOffline *offline = PaneroOffline::instance();
    const PaneroOffline::Settings settings = offline->settings(e.rcloneRemote);
    if (cloud->isMounted(e.id) || cloud->isMounting(e.id))
        cloud->unmount(e.id);

    // 1. files written to the drive but not uploaded yet live only in the cache
    const int dirty = PaneroCloudUtils::dirtyCacheEntries(PaneroRclone::cacheDir(e.rcloneRemote));
    if (dirty > 0) {
        const auto answer = KMessageBox::warningTwoActions(
            parent,
            i18np("<p>1 file written to <b>%2</b> has not been uploaded yet. Connect the drive again and wait a moment, then "
                  "remove it.</p><p>Remove anyway? The file is lost.</p>",
                  "<p>%1 files written to <b>%2</b> have not been uploaded yet. Connect the drive again and wait a moment, then "
                  "remove it.</p><p>Remove anyway? The files are lost.</p>",
                  dirty, e.label.toHtmlEscaped()),
            i18n("Remove Cloud Drive"), KGuiItem(i18n("Remove Anyway"), QStringLiteral("edit-delete")), KStandardGuiItem::cancel());
        if (answer != KMessageBox::PrimaryAction)
            return i18n("<b>%1</b>: not removed (files waiting for upload).", e.label.toHtmlEscaped());
    }

    // 2. upload changes of the local copies (they are deleted or kept below)
    if (!offline->pins(e.rcloneRemote).isEmpty()) {
        QProgressDialog progress(i18n("Uploading changes of the offline files of %1...", e.label), QString(), 0, 0, parent);
        progress.setWindowModality(Qt::WindowModal);
        progress.setCancelButton(nullptr);
        progress.setMinimumDuration(300);
        progress.show();
        QString syncError;
        const bool synced = offline->syncAllBlocking(e.rcloneRemote, &syncError);
        progress.close();
        if (!synced && settings.removeOnSignOut) {
            const auto answer = KMessageBox::warningTwoActions(
                parent,
                i18n("<p>The offline files of <b>%1</b> could not be synchronised:</p><p>%2</p><p>Changes made on this device may not "
                     "be in the cloud yet. Keep the offline files on this device (in %3)?</p>",
                     e.label.toHtmlEscaped(), syncError.toHtmlEscaped(), PaneroOffline::mirrorRoot(e.rcloneRemote).toHtmlEscaped()),
                i18n("Remove Cloud Drive"), KGuiItem(i18n("Keep Them"), QStringLiteral("document-save")),
                KGuiItem(i18n("Delete Them"), QStringLiteral("edit-delete")));
            offline->forgetRemote(e.rcloneRemote, answer != KMessageBox::PrimaryAction);
        } else {
            offline->forgetRemote(e.rcloneRemote, settings.removeOnSignOut);
        }
    } else {
        offline->forgetRemote(e.rcloneRemote, settings.removeOnSignOut);
    }

    // 3. revoke the token at the provider
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const PaneroCloudRevoke::Result revoked = PaneroCloudRevoke::revoke(e.rcloneRemote, e.rcloneType);
    QApplication::restoreOverrideCursor();
    *manage = revoked.manageUrl;

    // 4. delete the remote and its cache
    QString error;
    if (!cloud->removeEntry(e, &error))
        return i18n("<b>%1</b>: could not be removed: %2", e.label.toHtmlEscaped(), error.toHtmlEscaped());
    *ok = true;

    switch (revoked.outcome) {
    case PaneroCloudRevoke::Outcome::Revoked:
        return i18n("<b>%1</b>: signed out; the provider confirmed that Panero's access is revoked.", e.label.toHtmlEscaped());
    case PaneroCloudRevoke::Outcome::Manual:
        return i18n("<b>%1</b>: removed from Panero. %2 Please remove the access on the provider's page: <a href=\"%3\">%3</a>",
                    e.label.toHtmlEscaped(), revoked.message.toHtmlEscaped(), revoked.manageUrl.toString().toHtmlEscaped());
    case PaneroCloudRevoke::Outcome::NotNeeded:
        return i18n("<b>%1</b>: removed.", e.label.toHtmlEscaped());
    case PaneroCloudRevoke::Outcome::Failed:
        break;
    }
    QString line = i18n("<b>%1</b>: removed from Panero, but the provider did not confirm the sign-out (%2).", e.label.toHtmlEscaped(),
                        revoked.message.toHtmlEscaped());
    if (revoked.manageUrl.isValid())
        line += QLatin1Char(' ') + i18n("Please remove the access on the provider's page: <a href=\"%1\">%1</a>", revoked.manageUrl.toString().toHtmlEscaped());
    return line;
}

static void showSignOutSummary(QWidget *parent, const QStringList &lines)
{
    KMessageBox::information(parent, QLatin1String("<p>") + lines.join(QLatin1String("</p><p>")) + QLatin1String("</p>"), i18n("Signed Out"),
                             QString(), KMessageBox::Notify | KMessageBox::AllowLink);
}

bool PaneroCloud::confirmAndRemove(QWidget *parent, const Entry &e)
{
    QString text;
    if (e.isRclone()) {
        const PaneroOffline::Settings settings = PaneroOffline::instance()->settings(e.rcloneRemote);
        text = i18n("<p>Remove the cloud drive <b>%1</b> and sign out?</p>"
                    "<p>Panero cancels its access at the provider where the provider allows this, then deletes the sign-in and "
                    "the downloaded files from this computer. Your files in the cloud are not touched.</p>",
                    e.label.toHtmlEscaped());
        if (!PaneroOffline::instance()->pins(e.rcloneRemote).isEmpty())
            text += settings.removeOnSignOut ? i18n("<p>The offline files in %1 are synchronised once more and then deleted.</p>",
                                                    PaneroOffline::mirrorRoot(e.rcloneRemote).toHtmlEscaped())
                                             : i18n("<p>The offline files in %1 are synchronised once more and stay on this computer.</p>",
                                                    PaneroOffline::mirrorRoot(e.rcloneRemote).toHtmlEscaped());
        if (e.rcloneType == QLatin1String("drive"))
            text += i18n("<p>Google cancels the access of rclone for this Google account as a whole, so other rclone setups that "
                         "use the same account and rclone's standard sign-in are signed out too.</p>");
    } else {
        text = i18n("<p>Remove the connection <b>%1</b>?</p><p>Files on the server are not touched.</p>", e.label.toHtmlEscaped());
    }
    const auto answer = KMessageBox::warningTwoActions(parent, text, e.isRclone() ? i18n("Remove Cloud Drive") : i18n("Remove Connection"),
                                                       KGuiItem(e.isRclone() ? i18n("Sign Out and Remove") : i18n("Remove"),
                                                                QStringLiteral("list-remove")),
                                                       KStandardGuiItem::cancel());
    if (answer != KMessageBox::PrimaryAction)
        return false;
    if (e.isServer()) {
        QString error;
        if (!removeEntry(e, &error)) {
            KMessageBox::error(parent, i18n("The connection could not be removed.") + (error.isEmpty() ? QString() : QLatin1String("\n\n") + error));
            return false;
        }
        return true;
    }
    if (!ensureRclone(parent))
        return false;
    bool ok = false;
    QUrl manage;
    const QString line = signOutRemote(parent, this, e, &ok, &manage);
    showSignOutSummary(parent, {line});
    return ok;
}

void PaneroCloud::signOutAll(QWidget *parent)
{
    QList<Entry> remotes;
    for (const Entry &e : qAsConst(m_entries)) {
        if (e.isRclone())
            remotes << e;
    }
    if (remotes.isEmpty()) {
        KMessageBox::information(parent, i18n("No cloud drives are set up."), i18n("Sign Out of All Clouds"));
        return;
    }
    QStringList names;
    for (const Entry &e : qAsConst(remotes))
        names << e.label;
    const auto answer = KMessageBox::warningTwoActionsList(
        parent,
        i18n("Sign out of all cloud drives? Panero cancels its access at each provider where possible and deletes the sign-ins and "
             "downloaded files from this computer. Files in the clouds are not touched."),
        names, i18n("Sign Out of All Clouds"), KGuiItem(i18n("Sign Out of All"), QStringLiteral("system-log-out")), KStandardGuiItem::cancel());
    if (answer != KMessageBox::PrimaryAction || !ensureRclone(parent))
        return;
    QStringList lines;
    for (const Entry &e : qAsConst(remotes)) {
        bool ok = false;
        QUrl manage;
        lines << signOutRemote(parent, this, e, &ok, &manage);
    }
    showSignOutSummary(parent, lines);
}

qint64 PaneroCloud::mountPid(const QString &remote) const
{
    const Mount *m = m_mounts.value(entryIdForRemote(remote));
    return m && m->mounted && m->process ? m->process->processId() : 0;
}

bool PaneroCloud::mayBeGoogleDocument(const QString &localPath, qint64 size) const
{
    if (size != 0)
        return false;
    static const QStringList exportExtensions{QStringLiteral("docx"), QStringLiteral("xlsx"), QStringLiteral("pptx"),
                                              QStringLiteral("svg"),  QStringLiteral("odt"),  QStringLiteral("ods"),
                                              QStringLiteral("odp"),  QStringLiteral("pdf"),  QStringLiteral("json")};
    if (!exportExtensions.contains(QFileInfo(localPath).suffix().toLower()))
        return false;
    QString remote, rel;
    bool inMirror = false;
    if (!PaneroOffline::instance()->cloudPath(localPath, &remote, &rel, &inMirror) || inMirror || rel.isEmpty())
        return false;
    return m_remoteTypes.value(remote) == QLatin1String("drive");
}

// Drive id and native type of a file ("application/vnd.google-apps.*"), via rclone.
static bool googleDocumentInfo(const QString &localPath, QString *remote, QString *rel, QString *id, QString *googleType)
{
    if (!PaneroOffline::instance()->cloudPath(localPath, remote, rel))
        return false;
    QByteArray out;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const int rc = PaneroRclone::run({QStringLiteral("lsjson"), *remote + QLatin1Char(':') + *rel, QStringLiteral("--stat"),
                                      QStringLiteral("--metadata")},
                                     &out, nullptr, 30000);
    QApplication::restoreOverrideCursor();
    if (rc != 0)
        return false;
    const QJsonObject o = QJsonDocument::fromJson(out).object();
    *googleType = o.value(QStringLiteral("Metadata")).toObject().value(QStringLiteral("content-type")).toString();
    // shortcuts: "<target id>\t<shortcut id>"
    *id = o.value(QStringLiteral("ID")).toString().section(QLatin1Char('\t'), 0, 0);
    static const QRegularExpression idRe(QStringLiteral("^[A-Za-z0-9_\\-]{10,200}$"));
    return googleType->startsWith(QLatin1String("application/vnd.google-apps.")) && idRe.match(*id).hasMatch();
}

bool PaneroCloud::openGoogleDocument(QWidget *parent, const QString &localPath, qint64 size)
{
    Q_UNUSED(parent)
    if (!mayBeGoogleDocument(localPath, size))
        return false;
    QString remote, rel, id, type;
    if (!googleDocumentInfo(localPath, &remote, &rel, &id, &type))
        return false;
    const QString kind = type.mid(int(qstrlen("application/vnd.google-apps.")));
    QString url;
    if (kind == QLatin1String("document"))
        url = QStringLiteral("https://docs.google.com/document/d/%1/edit").arg(id);
    else if (kind == QLatin1String("spreadsheet"))
        url = QStringLiteral("https://docs.google.com/spreadsheets/d/%1/edit").arg(id);
    else if (kind == QLatin1String("presentation"))
        url = QStringLiteral("https://docs.google.com/presentation/d/%1/edit").arg(id);
    else if (kind == QLatin1String("drawing"))
        url = QStringLiteral("https://docs.google.com/drawings/d/%1/edit").arg(id);
    else
        url = QStringLiteral("https://drive.google.com/open?id=%1").arg(id);
    return QDesktopServices::openUrl(QUrl(url));
}

bool PaneroCloud::saveGoogleDocumentCopy(QWidget *parent, const QString &localPath, const QString &destDir)
{
    QString remote, rel, id, type;
    if (!googleDocumentInfo(localPath, &remote, &rel, &id, &type)) {
        KMessageBox::error(parent, i18n("This is not a Google Docs, Sheets or Slides file."), i18n("Save a Copy"));
        return false;
    }
    QString target = destDir + QLatin1Char('/') + QFileInfo(localPath).fileName();
    if (QFileInfo::exists(target)) {
        const auto answer = KMessageBox::warningTwoActions(parent, i18n("%1 already exists. Replace it?", target), i18n("Save a Copy"),
                                                           KStandardGuiItem::overwrite(), KStandardGuiItem::cancel());
        if (answer != KMessageBox::PrimaryAction)
            return false;
    }
    QProgressDialog progress(i18n("Downloading %1...", QFileInfo(localPath).fileName()), QString(), 0, 0, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.show();
    QProcess p;
    PaneroRclone::prepare(p, {QStringLiteral("copyto"), remote + QLatin1Char(':') + rel, target});
    p.start();
    p.closeWriteChannel();
    while (!p.waitForFinished(100))
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    progress.close();
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        KMessageBox::error(parent, PaneroCloudUtils::rcloneErrorSummary(QString::fromUtf8(p.readAllStandardError())), i18n("Save a Copy"));
        return false;
    }
    return true;
}

void PaneroCloud::flushDirectoryCache(const QString &localPath)
{
    for (auto it = m_mounts.constBegin(); it != m_mounts.constEnd(); ++it) {
        const Mount *m = it.value();
        if (!m->mounted || m->stopping || !m->process)
            continue;
        if (localPath == m->dir || localPath.startsWith(m->dir + QLatin1Char('/'))) {
            // rclone mount: SIGHUP flushes all directory caches
            const qint64 pid = m->process->processId();
            if (pid > 0)
                ::kill(pid_t(pid), SIGHUP);
            return;
        }
    }
}

bool PaneroCloud::refreshFolder(const QString &localPath, QObject *context, const std::function<void()> &done)
{
    for (auto it = m_mounts.constBegin(); it != m_mounts.constEnd(); ++it) {
        const Mount *m = it.value();
        if (!m->mounted || m->stopping)
            continue;
        if (localPath != m->dir && !localPath.startsWith(m->dir + QLatin1Char('/')))
            continue;
        if (!m->rc.isValid())
            return false;
        QJsonObject params;
        const QString rel = localPath.mid(m->dir.length() + 1);
        if (!rel.isEmpty())
            params.insert(QStringLiteral("dir"), rel); // without "dir": the drive's top folder
        PaneroRc::call(m->rc, QStringLiteral("vfs/refresh"), params, context, [done](int, const QJsonObject &) {
            if (done)
                done();
        }, 120000);
        return true;
    }
    return false;
}

bool PaneroCloud::isReadingTree(const QString &id) const
{
    const Mount *m = m_mounts.value(id);
    return m && m->mounted && m->readingTree;
}

bool PaneroCloud::hasTree(const QString &id) const
{
    const Mount *m = m_mounts.value(id);
    return m && m->mounted && m->treeRead;
}

void PaneroCloud::readTree(const QString &id)
{
    Mount *m = m_mounts.value(id);
    if (!m || !m->rc.isValid())
        return;
    // like OneDrive's Files On-Demand: know every folder up front, so opening
    // any folder later needs no network (rclone keeps the listings in memory,
    // changes arrive by polling or when a folder is shown again).
    // Each top folder is read on its own: one folder that cannot be listed
    // (OneDrive's locked "Personal Vault") would stop a single walk.
    m->readingTree = true;
    m->treeRead = false;
    const auto failed = [this, id]() {
        if (Mount *mm = m_mounts.value(id))
            mm->readingTree = false;
    };
    PaneroRc::call(m->rc, QStringLiteral("vfs/refresh"), QJsonObject(), this, [this, id, failed](int status, const QJsonObject &) {
        Mount *mm = m_mounts.value(id);
        if (!mm)
            return;
        if (status != 200)
            return failed();
        const QString dir = mm->dir;
        auto *watcher = new QFutureWatcher<QStringList>(this);
        connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, id, watcher, failed]() {
            const QStringList folders = watcher->result();
            watcher->deleteLater();
            Mount *m2 = m_mounts.value(id);
            if (!m2)
                return;
            if (folders.isEmpty()) {
                m2->readingTree = false;
                m2->treeRead = true;
                return;
            }
            QJsonObject params{{QStringLiteral("recursive"), QStringLiteral("true")}, {QStringLiteral("_async"), true}};
            for (int i = 0; i < folders.count(); ++i)
                params.insert(i == 0 ? QStringLiteral("dir") : QStringLiteral("dir%1").arg(i), folders.at(i));
            PaneroRc::call(m2->rc, QStringLiteral("vfs/refresh"), params, this, [this, id, failed](int st, const QJsonObject &answer) {
                const qint64 jobId = qint64(answer.value(QStringLiteral("jobid")).toDouble());
                if (st != 200 || jobId <= 0)
                    return failed();
                // poll the job until it is done (for the sidebar status)
                auto *timer = new QTimer(this);
                timer->setInterval(2000);
                connect(timer, &QTimer::timeout, this, [this, id, jobId, timer]() {
                    Mount *m3 = m_mounts.value(id);
                    if (!m3 || !m3->rc.isValid()) {
                        timer->deleteLater();
                        return;
                    }
                    PaneroRc::call(m3->rc, QStringLiteral("job/status"), QJsonObject{{QStringLiteral("jobid"), double(jobId)}}, timer,
                                   [this, id, timer](int code, const QJsonObject &job) {
                                       Mount *m4 = m_mounts.value(id);
                                       if (!m4) {
                                           timer->deleteLater();
                                           return;
                                       }
                                       if (code != 200 || job.value(QStringLiteral("finished")).toBool()) {
                                           m4->readingTree = false;
                                           // folders that cannot be listed are reported per folder
                                           m4->treeRead = code == 200;
                                           timer->deleteLater();
                                           emit entriesChanged();
                                       }
                                   }, 10000);
                });
                timer->start();
            }, 30000);
        });
        // the top level is in memory now; read its folder names off the GUI thread
        watcher->setFuture(QtConcurrent::run([dir]() {
            QStringList folders;
            const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks);
            for (const QFileInfo &fi : entries)
                folders << fi.fileName();
            return folders;
        }));
    }, 120000);
}

void PaneroCloud::setPlacesEnabled(bool enabled)
{
    if (!enabled) {
        delete m_places;
        m_places = nullptr;
        return;
    }
    if (m_places)
        return;
    m_places = new KFilePlacesModel(this);
    // places of an earlier session (Panero crashed or was killed)
    const QString base = PaneroCloudUtils::mountBaseDir() + QLatin1Char('/');
    for (int row = m_places->rowCount() - 1; row >= 0; --row) {
        const QModelIndex index = m_places->index(row, 0);
        if (m_places->url(index).isLocalFile() && m_places->url(index).toLocalFile().startsWith(base))
            m_places->removePlace(index);
    }
    updatePlaces();
}

static QString placeIcon(const QString &rcloneType)
{
    // the provider's own icon (a file: places take icon names or paths)
    const QString name = rcloneType == QLatin1String("drive") ? QStringLiteral("google-drive.svg")
        : rcloneType == QLatin1String("onedrive")              ? QStringLiteral("onedrive.svg")
                                                               : QString();
    if (name.isEmpty())
        return QStringLiteral("folder-cloud");
    const QString dir = PaneroCloudUtils::mountBaseDir() + QLatin1String("/.control");
    const QString path = dir + QLatin1Char('/') + name;
    if (!QFileInfo::exists(path)) {
        QDir().mkpath(dir);
        QFile::copy(QStringLiteral(":/panero-cloud/") + name, path);
    }
    return QFileInfo::exists(path) ? path : QStringLiteral("folder-cloud");
}

void PaneroCloud::addPlace(const QString &id)
{
    if (!m_places)
        return;
    const Mount *m = m_mounts.value(id);
    const Entry e = entry(id);
    if (!m || !m->mounted || e.id.isEmpty())
        return;
    const QUrl url = QUrl::fromLocalFile(m->dir);
    initCloudIcons();
    const QString icon = placeIcon(e.rcloneType);
    for (int row = 0; row < m_places->rowCount(); ++row) {
        const QModelIndex index = m_places->index(row, 0);
        if (m_places->url(index).adjusted(QUrl::StripTrailingSlash) == url) {
            if (m_places->text(index) != e.label)
                m_places->editPlace(index, e.label, url, icon, QCoreApplication::applicationName());
            return;
        }
    }
    m_places->addPlace(e.label, url, icon, QCoreApplication::applicationName());
}

void PaneroCloud::removePlace(const QString &dir)
{
    if (!m_places)
        return;
    const QUrl url = QUrl::fromLocalFile(dir);
    for (int row = m_places->rowCount() - 1; row >= 0; --row) {
        const QModelIndex index = m_places->index(row, 0);
        if (m_places->url(index).adjusted(QUrl::StripTrailingSlash) == url)
            m_places->removePlace(index);
    }
}

void PaneroCloud::updatePlaces()
{
    for (auto it = m_mounts.constBegin(); it != m_mounts.constEnd(); ++it) {
        if (it.value()->mounted && !it.value()->stopping)
            addPlace(it.key());
    }
}

QHash<QString, QString> PaneroCloud::mountDirs() const
{
    QHash<QString, QString> dirs;
    for (auto it = m_mounts.constBegin(); it != m_mounts.constEnd(); ++it) {
        if (it.value()->mounted && !it.value()->stopping && it.key().startsWith(QLatin1String("rclone:")))
            dirs.insert(it.key().mid(7), it.value()->dir);
    }
    return dirs;
}

// ---- opening / mounting ------------------------------------------------------

QUrl PaneroCloud::urlFor(const Entry &e, QWidget *) const
{
    if (e.isServer())
        return e.url;
    if (e.isRclone() && isMounted(e.id))
        return QUrl::fromLocalFile(m_mounts.value(e.id)->dir);
    return QUrl();
}

bool PaneroCloud::isMounted(const QString &id) const
{
    const Mount *m = m_mounts.value(id);
    return m && m->mounted && !m->stopping;
}

bool PaneroCloud::isMounting(const QString &id) const
{
    const Mount *m = m_mounts.value(id);
    return m && !m->mounted && !m->stopping;
}

QString PaneroCloud::mountPath(const QString &id) const
{
    const Mount *m = m_mounts.value(id);
    return m ? m->dir : QString();
}

bool PaneroCloud::isMountPoint(const QString &dir)
{
    QFile f(QStringLiteral("/proc/self/mountinfo"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    // /proc files report size 0: read until EOF
    return PaneroCloudUtils::parseMountInfo(f.readAll()).contains(dir);
}

bool PaneroCloud::runFusermountUnmount(const QString &dir, bool lazy)
{
    const QString fusermount = fusermountProgram();
    if (fusermount.isEmpty())
        return false;
    QStringList args{QStringLiteral("-u")};
    if (lazy)
        args << QStringLiteral("-z");
    args << dir;
    QProcess p;
    p.start(fusermount, args);
    return p.waitForFinished(5000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

void PaneroCloud::mount(const Entry &e)
{
    if (!e.isRclone())
        return;
    const QString id = e.id;
    if (isMounted(id)) {
        emit mounted(id, QUrl::fromLocalFile(m_mounts.value(id)->dir));
        return;
    }
    if (m_mounts.contains(id))
        return; // mounting or stopping; the signal follows

    const QString rclone = rcloneProgram();
    if (rclone.isEmpty()) {
        emit mountFailed(id, i18n("rclone is not installed."));
        return;
    }
    if (!fuseAvailable()) {
        emit mountFailed(id, i18n("The FUSE tools (fusermount3) are not installed."));
        return;
    }

    // one folder per remote; names are restricted by rclone, but stay safe
    QString folder = e.rcloneRemote;
    folder.replace(QLatin1Char('/'), QLatin1Char('_'));
    if (folder.isEmpty() || folder == QLatin1String(".") || folder == QLatin1String(".."))
        folder = QStringLiteral("remote");
    if (folder == QLatin1String(".control")) // Panero's own folder there
        folder = QStringLiteral("_control");
    const QString base = PaneroCloudUtils::mountBaseDir();
    QString dir = base + QLatin1Char('/') + folder;
    if (!QDir().mkpath(base)) {
        emit mountFailed(id, i18n("Could not create the folder %1.", base));
        return;
    }
    QFile::setPermissions(base, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    dir = QDir(QFileInfo(base).canonicalFilePath()).filePath(folder);
    if (isMountPoint(dir)) // left over from a crashed session
        runFusermountUnmount(dir, true);
    if (!QDir().mkpath(dir)) {
        emit mountFailed(id, i18n("Could not create the folder %1.", dir));
        return;
    }

    auto *m = new Mount;
    m->dir = dir;
    m->started = QDateTime::currentMSecsSinceEpoch();
    m->process = new ChildProcess(this);
    m->poll = new QTimer(this);
    m->poll->setInterval(POLL_INTERVAL_MS);
    m_mounts.insert(id, m);

    connect(m->process, &QProcess::readyReadStandardError, this, [this, id]() {
        Mount *mm = m_mounts.value(id);
        if (!mm)
            return;
        const QStringList lines = QString::fromUtf8(mm->process->readAllStandardError()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        mm->stderrTail << lines;
        while (mm->stderrTail.count() > STDERR_TAIL_LINES)
            mm->stderrTail.removeFirst();
    });
    connect(m->process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, id](int exitCode, QProcess::ExitStatus) {
                mountProcessFinished(id, exitCode);
            });
    connect(m->process, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            mountProcessFinished(id, -1);
    });
    connect(m->poll, &QTimer::timeout, this, [this, id]() {
        pollMount(id);
    });

    m->process->setProcessChannelMode(QProcess::SeparateChannels);
    m->process->setStandardOutputFile(QProcess::nullDevice());
    if (e.rcloneType == QLatin1String("pcloud")) {
        // rclone checks the pCloud host only during sign-in; an imported or
        // edited remote could name another server and receive the token
        QString hostError;
        if (!PaneroCloudSetup::checkPcloudHost(e.rcloneRemote, &hostError)) {
            emit mountFailed(id, hostError);
            return;
        }
    }
    if (e.rcloneType == QLatin1String("alias")) {
        QByteArray out;
        PaneroRclone::run({QStringLiteral("config"), QStringLiteral("redacted"), e.rcloneRemote}, &out, nullptr, 15000);
        PaneroOffline::instance()->setCacheSubPath(e.rcloneRemote,
                                                   PaneroCloudUtils::aliasCacheSubPath(PaneroCloudUtils::iniValue(QString::fromUtf8(out), QStringLiteral("remote"))));
    }
    QStringList args{QStringLiteral("mount"), e.rcloneRemote + QLatin1Char(':'), dir};
    args << PaneroOffline::instance()->mountCacheArgs(e.rcloneRemote);
    // Folder listings stay in memory: the whole tree is read after mounting
    // (readTree()), providers that report changes are polled every 30 s, and
    // on the others a folder shown in Panero is re-read in the background
    // (refreshFolder()). So opening a folder normally needs no network at all.
    const bool notifies = e.rcloneType == QLatin1String("drive") || e.rcloneType == QLatin1String("onedrive")
        || e.rcloneType == QLatin1String("dropbox") || e.rcloneType == QLatin1String("box");
    m->notifies = notifies;
    args << QStringLiteral("--dir-cache-time") << (notifies ? QStringLiteral("168h") : QStringLiteral("24h"));
    if (notifies)
        args << QStringLiteral("--poll-interval") << QStringLiteral("30s");
    // the tree is read with few requests where the provider allows it
    args << QStringLiteral("--fast-list");
    // cached files are checked by size and time only (no extra hash requests)
    args << QStringLiteral("--vfs-fast-fingerprint");
    // remote control for readTree()/refreshFolder(): only a unix socket in
    // the private (0700) mount base folder, random credentials per mount;
    // rclone gets only their hash (PaneroRc)
    const QString control = QDir(QFileInfo(base).canonicalFilePath()).filePath(QStringLiteral(".control"));
    if (QDir().mkpath(control)) {
        QFile::setPermissions(control, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QString socketName = folder + QLatin1String(".sock");
        if (control.length() + socketName.length() > 100) // sun_path limit
            socketName = QString::fromLatin1(QCryptographicHash::hash(folder.toUtf8(), QCryptographicHash::Sha1).toHex().left(16)) + QLatin1String(".sock");
        const QString socket = control + QLatin1Char('/') + socketName;
        QFile::remove(socket); // left over from a crashed session
        const PaneroRc::Credentials rc = PaneroRc::newCredentials(socket);
        const QString htpasswd = socket.left(socket.length() - 5) + QLatin1String(".htpasswd");
        if (PaneroRc::writeHtpasswd(rc, htpasswd)) {
            m->rc = rc;
            args << QStringLiteral("--rc") << QStringLiteral("--rc-addr") << (QLatin1String("unix://") + socket) << QStringLiteral("--rc-htpasswd")
                 << htpasswd;
        }
    }
    PaneroRclone::prepare(*m->process, args);
    m->process->start();
    m->poll->start();
}

void PaneroCloud::pollMount(const QString &id)
{
    Mount *m = m_mounts.value(id);
    if (!m || m->mounted || m->stopping)
        return;
    if (isMountPoint(m->dir)) {
        m->mounted = true;
        m->poll->stop();
        rebuildEntries();
        addPlace(id);
        readTree(id);
        emit mounted(id, QUrl::fromLocalFile(m->dir));
        return;
    }
    if (QDateTime::currentMSecsSinceEpoch() - m->started > m_mountTimeoutMs) {
        m->poll->stop();
        m->stopping = true;
        QString message = i18n("The cloud drive did not respond in time.");
        const QString details = PaneroCloudUtils::rcloneErrorSummary(m->stderrTail.join(QLatin1Char('\n')));
        if (!details.isEmpty())
            message += QLatin1String("\n\n") + details;
        m->process->terminate();
        if (!m->process->waitForFinished(3000))
            m->process->kill();
        // finished() may already have cleaned up
        if (m_mounts.value(id) == m)
            finishMount(id);
        emit mountFailed(id, message);
    }
}

void PaneroCloud::mountProcessFinished(const QString &id, int exitCode)
{
    Mount *m = m_mounts.value(id);
    if (!m)
        return;
    const bool wasMounted = m->mounted;
    const bool stopping = m->stopping;
    QString message;
    if (!stopping && !wasMounted) {
        message = PaneroCloudUtils::rcloneErrorSummary(m->stderrTail.join(QLatin1Char('\n')));
        if (message.isEmpty())
            message = exitCode == -1 ? i18n("rclone could not be started.") : i18n("rclone ended unexpectedly (exit code %1).", exitCode);
    }
    if (stopping && !wasMounted)
        return; // pollMount() timeout path handles it
    finishMount(id);
    if (!stopping && !wasMounted)
        emit mountFailed(id, message);
    else if (wasMounted)
        emit unmounted(id);
}

void PaneroCloud::finishMount(const QString &id)
{
    Mount *m = m_mounts.take(id);
    if (!m)
        return;
    m->poll->stop();
    m->poll->deleteLater();
    m->process->disconnect(this);
    if (m->process->state() != QProcess::NotRunning) {
        m->process->terminate();
        if (!m->process->waitForFinished(3000)) {
            m->process->kill();
            m->process->waitForFinished(1000);
        }
    }
    m->process->deleteLater();
    removePlace(m->dir);
    if (m->rc.isValid()) {
        QFile::remove(m->rc.socket);
        QFile::remove(m->rc.socket.left(m->rc.socket.length() - 5) + QLatin1String(".htpasswd"));
    }
    if (isMountPoint(m->dir))
        runFusermountUnmount(m->dir, true);
    QDir().rmdir(m->dir); // only removes it when empty, i.e. not mounted
    // without "Keep opened files", downloaded contents do not outlive the connection
    const QString remote = id.startsWith(QLatin1String("rclone:")) ? id.mid(7) : QString();
    if (!remote.isEmpty() && !PaneroOffline::instance()->settings(remote).keepOpened
        && PaneroCloudUtils::dirtyCacheEntries(PaneroRclone::cacheDir(remote)) == 0) {
        QDir(PaneroRclone::cacheDir(remote) + QLatin1String("/vfs")).removeRecursively();
        QDir(PaneroRclone::cacheDir(remote) + QLatin1String("/vfsMeta")).removeRecursively();
    }
    delete m;
    rebuildEntries();
}

void PaneroCloud::unmount(const QString &id)
{
    Mount *m = m_mounts.value(id);
    if (!m)
        return;
    const bool wasMounted = m->mounted;
    // let rclone finish uploading files written through the mount (they stay
    // in the cache otherwise and are uploaded at the next connection)
    if (wasMounted && id.startsWith(QLatin1String("rclone:"))) {
        const QString cache = PaneroRclone::cacheDir(id.mid(7));
        QEventLoop wait;
        for (int i = 0; i < 60; ++i) {
            // rclone may end while we wait (finishMount() then deletes the Mount)
            m = m_mounts.value(id);
            if (!m || m->process->state() != QProcess::Running || PaneroCloudUtils::dirtyCacheEntries(cache) == 0)
                break;
            QTimer::singleShot(500, &wait, &QEventLoop::quit);
            wait.exec(QEventLoop::ExcludeUserInputEvents);
        }
        m = m_mounts.value(id);
        if (!m)
            return;
    }
    m->stopping = true;
    m->poll->stop();
    if (isMountPoint(m->dir) && !runFusermountUnmount(m->dir, false))
        runFusermountUnmount(m->dir, true); // busy: detach now, finish when released
    finishMount(id);
    if (wasMounted)
        emit unmounted(id);
    else
        emit mountFailed(id, i18n("Cancelled."));
}

void PaneroCloud::unmountAll()
{
    const QStringList ids = m_mounts.keys();
    for (const QString &id : ids)
        unmount(id);
}
