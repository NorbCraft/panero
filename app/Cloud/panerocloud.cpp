/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerocloud.h"
#include "panerocloudutils.h"
#include "../paneropackageinstaller.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

#include <KConfigCore/KConfigGroup>
#include <KI18n/KLocalizedString>
#include <KWidgetsAddons/KMessageBox>

#include <csignal>
#include <sys/prctl.h>

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
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
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
    return QStandardPaths::findExecutable(QStringLiteral("rclone"));
}

bool PaneroCloud::rcloneAvailable()
{
    return !rcloneProgram().isEmpty();
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
    const bool haveRclone = rcloneAvailable();
    const bool haveFuse = fuseAvailable();
    if (haveRclone && haveFuse)
        return true;

    QString text;
    if (!haveRclone)
        text = i18n("<p>Panero uses the free <b>rclone</b> tool for cloud drives. Install it now?</p>");
    else
        text = i18n("<p>Opening cloud drives needs the <b>FUSE</b> file system tools (package fuse3). Install them now?</p>");
    text += i18n("<p>It is installed from your system's software sources; your system will ask you to authorize the installation.</p>");
    const auto answer = KMessageBox::questionTwoActions(parent, text, haveRclone ? i18n("Install FUSE?") : i18n("Install rclone?"),
                                                        KGuiItem(i18n("Install"), QStringLiteral("download")),
                                                        KStandardGuiItem::cancel());
    if (answer != KMessageBox::PrimaryAction)
        return false;

    if (!haveRclone && !installPackage(parent, QStringLiteral("rclone"), QStringLiteral("rclone"), &PaneroCloud::rcloneAvailable))
        return false;
    if (!fuseAvailable() && !installPackage(parent, QStringLiteral("fuse3"), QStringLiteral("FUSE"), &PaneroCloud::fuseAvailable))
        return false;
    refresh();
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
    if (rclone.isEmpty()) {
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
                if (status == QProcess::NormalExit && exitCode == 0)
                    parseRemotes(QString::fromUtf8(p->readAllStandardOutput()));
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
    m_listProcess->start(rclone, {QStringLiteral("listremotes"), QStringLiteral("--long")});
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
    if (rclone.isEmpty()) {
        m_remoteNames.clear();
        m_remoteTypes.clear();
        rebuildEntries();
        return false;
    }
    QProcess p;
    p.start(rclone, {QStringLiteral("listremotes"), QStringLiteral("--long")});
    if (!p.waitForFinished(timeoutMs) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        p.kill();
        p.waitForFinished(1000);
        return false;
    }
    parseRemotes(QString::fromUtf8(p.readAllStandardOutput()));
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
    const QString file = PaneroCloudUtils::rcloneConfigPath();
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
    QProcess p;
    p.start(rclone, {QStringLiteral("config"), QStringLiteral("delete"), e.rcloneRemote});
    if (!p.waitForFinished(10000) || p.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(p.readAllStandardError()).trimmed();
        p.kill();
        p.waitForFinished(1000);
        return false;
    }
    PaneroCloudUtils::setRemoteLabel(g, e.rcloneRemote, QString());
    m_remoteNames.removeAll(e.rcloneRemote);
    m_remoteTypes.remove(e.rcloneRemote);
    rebuildEntries();
    return true;
}

bool PaneroCloud::confirmAndRemove(QWidget *parent, const Entry &e)
{
    const QString text = e.isRclone()
        ? i18n("<p>Remove the cloud drive <b>%1</b>?</p><p>Panero signs out by deleting the rclone remote "
               "<tt>%2</tt>. Your files in the cloud are not touched.</p>",
               e.label.toHtmlEscaped(), e.rcloneRemote.toHtmlEscaped())
        : i18n("<p>Remove the connection <b>%1</b>?</p><p>Files on the server are not touched.</p>", e.label.toHtmlEscaped());
    const auto answer = KMessageBox::warningTwoActions(parent, text, i18n("Remove Connection"),
                                                       KGuiItem(i18n("Remove"), QStringLiteral("list-remove")),
                                                       KStandardGuiItem::cancel());
    if (answer != KMessageBox::PrimaryAction)
        return false;
    QString error;
    if (!removeEntry(e, &error)) {
        KMessageBox::error(parent, i18n("The connection could not be removed.") + (error.isEmpty() ? QString() : QLatin1String("\n\n") + error));
        return false;
    }
    return true;
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
    m->process->start(rclone, {QStringLiteral("mount"), e.rcloneRemote + QLatin1Char(':'), dir,
                               QStringLiteral("--vfs-cache-mode"), QStringLiteral("full"),
                               QStringLiteral("--dir-cache-time"), QStringLiteral("30s")});
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
    if (isMountPoint(m->dir))
        runFusermountUnmount(m->dir, true);
    QDir().rmdir(m->dir); // only removes it when empty, i.e. not mounted
    delete m;
    rebuildEntries();
}

void PaneroCloud::unmount(const QString &id)
{
    Mount *m = m_mounts.value(id);
    if (!m)
        return;
    const bool wasMounted = m->mounted;
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
