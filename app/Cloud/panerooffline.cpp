/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerooffline.h"
#include "panerocloud.h"
#include "panerocloudutils.h"
#include "panerorclone.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QProgressDialog>
#include <QTimer>

#include <KConfigCore/KConfigGroup>
#include <KI18n/KLocalizedString>
#include <KWidgetsAddons/KMessageBox>

namespace
{
const char GROUP_PREFIX[] = "Panero Cloud Offline ";
const int PERIODIC_MS = 10 * 60 * 1000;
const int FIRST_SYNC_DELAY_MS = 60 * 1000;
const int CHANGE_DEBOUNCE_MS = 30 * 1000;
const int MAX_WATCHED_DIRS = 300;
const qint64 OPEN_RESYNC_AFTER_MS = 2 * 60 * 1000;

QString cleanRelative(const QString &path)
{
    QString rel = QDir::cleanPath(path);
    while (rel.startsWith(QLatin1Char('/')))
        rel.remove(0, 1);
    if (rel == QLatin1String("."))
        rel.clear();
    return rel;
}

bool isInside(const QString &rel, const QString &pin)
{
    return pin.isEmpty() || rel == pin || rel.startsWith(pin + QLatin1Char('/'));
}

int countConflicts(const QString &text)
{
    int n = 0;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        if (l.contains(QLatin1String(".conflict")) && l.contains(QLatin1String("Renaming")))
            ++n;
    }
    return n;
}
} // namespace

PaneroOffline *PaneroOffline::instance()
{
    static PaneroOffline *s_instance = nullptr;
    if (!s_instance)
        s_instance = new PaneroOffline(QCoreApplication::instance());
    return s_instance;
}

PaneroOffline::PaneroOffline(QObject *parent)
    : QObject(parent), m_config(KSharedConfig::openConfig())
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &PaneroOffline::periodicSync);
    m_timer->start(PERIODIC_MS);
    QTimer::singleShot(FIRST_SYNC_DELAY_MS, this, &PaneroOffline::periodicSync);

    m_changeTimer = new QTimer(this);
    m_changeTimer->setSingleShot(true);
    m_changeTimer->setInterval(CHANGE_DEBOUNCE_MS);
    connect(m_changeTimer, &QTimer::timeout, this, [this]() {
        const QStringList keys = m_changedKeys;
        m_changedKeys.clear();
        for (const QString &k : keys) {
            const int sep = k.indexOf(QLatin1Char('\n'));
            syncNow(k.left(sep), k.mid(sep + 1));
        }
    });
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &dir) {
        QString remote, rel;
        bool inMirror = false;
        if (!cloudPath(dir, &remote, &rel, &inMirror) || !inMirror)
            return;
        const QString pin = pinFor(remote, rel);
        if (pin.isNull() || (m_process && m_current.remote == remote))
            return;
        const QString k = key(remote, pin);
        if (!m_changedKeys.contains(k))
            m_changedKeys << k;
        m_changeTimer->start();
    });
    QTimer::singleShot(0, this, &PaneroOffline::watchMirrors);
}

void PaneroOffline::setConfig(KSharedConfigPtr config)
{
    m_config = config ? config : KSharedConfig::openConfig();
}

KConfigGroup PaneroOffline::group(const QString &remote) const
{
    return KConfigGroup(m_config, QLatin1String(GROUP_PREFIX) + remote);
}

QString PaneroOffline::key(const QString &remote, const QString &pin)
{
    return remote + QLatin1Char('\n') + pin;
}

PaneroOffline::Settings PaneroOffline::settings(const QString &remote) const
{
    const KConfigGroup g = group(remote);
    Settings s;
    s.keepOpened = g.readEntry("Keep Opened", s.keepOpened);
    s.cacheLimitGB = qBound(1, g.readEntry("Cache Limit GB", s.cacheLimitGB), 1000);
    s.removeOnSignOut = g.readEntry("Remove On Sign Out", s.removeOnSignOut);
    return s;
}

void PaneroOffline::setSettings(const QString &remote, const Settings &s)
{
    KConfigGroup g = group(remote);
    g.writeEntry("Keep Opened", s.keepOpened);
    g.writeEntry("Cache Limit GB", qBound(1, s.cacheLimitGB, 1000));
    g.writeEntry("Remove On Sign Out", s.removeOnSignOut);
    g.sync();
    emit changed(remote);
}

QStringList PaneroOffline::mountCacheArgs(const QString &remote) const
{
    const Settings s = settings(remote);
    return {QStringLiteral("--cache-dir"), PaneroRclone::cacheDir(remote), QStringLiteral("--vfs-cache-mode"), QStringLiteral("full"),
            QStringLiteral("--vfs-cache-max-age"), s.keepOpened ? QStringLiteral("720h") : QStringLiteral("1h"),
            QStringLiteral("--vfs-cache-max-size"), QString::number(s.cacheLimitGB) + QLatin1Char('G'),
            QStringLiteral("--vfs-cache-poll-interval"), QStringLiteral("1m")};
}

QStringList PaneroOffline::pins(const QString &remote) const
{
    return group(remote).readEntry("Pins", QStringList());
}

void PaneroOffline::setPins(const QString &remote, const QStringList &pins)
{
    KConfigGroup g = group(remote);
    g.writeEntry("Pins", pins);
    g.sync();
    watchMirrors();
    emit changed(remote);
}

QString PaneroOffline::mirrorRoot(const QString &remote)
{
    return QDir::homePath() + QLatin1String("/Cloud Offline/") + PaneroCloudUtils::safeFileName(remote);
}

QString PaneroOffline::mirrorPath(const QString &remote, const QString &relativePath)
{
    const QString rel = cleanRelative(relativePath);
    return rel.isEmpty() ? mirrorRoot(remote) : mirrorRoot(remote) + QLatin1Char('/') + rel;
}

QString PaneroOffline::pinFor(const QString &remote, const QString &relativePath) const
{
    const QString rel = cleanRelative(relativePath);
    const QStringList list = pins(remote);
    for (const QString &p : list) {
        if (isInside(rel, p))
            return p.isNull() ? QStringLiteral("") : p;
    }
    return QString();
}

bool PaneroOffline::isPinned(const QString &remote, const QString &relativePath) const
{
    return !pinFor(remote, relativePath).isNull();
}

bool PaneroOffline::cloudPath(const QString &localPath, QString *remote, QString *relativePath, bool *inMirror) const
{
    const QString path = QDir::cleanPath(localPath);
    const QHash<QString, QString> mounts = PaneroCloud::instance()->mountDirs();
    for (auto it = mounts.constBegin(); it != mounts.constEnd(); ++it) {
        if (path == it.value() || path.startsWith(it.value() + QLatin1Char('/'))) {
            *remote = it.key();
            *relativePath = cleanRelative(path.mid(it.value().length()));
            if (inMirror)
                *inMirror = false;
            return true;
        }
    }
    const QString base = QDir::homePath() + QLatin1String("/Cloud Offline/");
    if (path.startsWith(base)) {
        const QString rest = path.mid(base.length());
        const int slash = rest.indexOf(QLatin1Char('/'));
        const QString r = slash < 0 ? rest : rest.left(slash);
        const QStringList remotes = PaneroCloud::instance()->remoteNames();
        for (const QString &name : remotes) {
            if (PaneroCloudUtils::safeFileName(name) == r) {
                *remote = name;
                *relativePath = slash < 0 ? QString() : cleanRelative(rest.mid(slash + 1));
                if (inMirror)
                    *inMirror = true;
                return true;
            }
        }
    }
    return false;
}

PaneroOffline::State PaneroOffline::state(const QString &localPath, qint64 size, bool isDir) const
{
    QString remote, rel;
    bool inMirror = false;
    if (!cloudPath(localPath, &remote, &rel, &inMirror))
        return State::NotCloud;
    if (inMirror)
        return isPinned(remote, rel) ? State::Pinned : State::NotCloud;
    if (rel.isEmpty())
        return State::NotCloud; // the drive itself
    if (isPinned(remote, rel))
        return State::Pinned;
    if (isDir)
        return State::OnlineOnly;
    const QString cacheFile = PaneroCloudUtils::vfsCacheFile(PaneroRclone::cacheDir(remote), m_cacheSub.value(remote, remote), rel);
    return PaneroCloudUtils::isFullyCached(cacheFile, size) ? State::Available : State::OnlineOnly;
}

void PaneroOffline::setCacheSubPath(const QString &remote, const QString &subPath)
{
    if (subPath.isEmpty() || subPath == remote)
        m_cacheSub.remove(remote);
    else
        m_cacheSub.insert(remote, subPath);
}

QString PaneroOffline::redirect(const QString &localPath)
{
    QString remote, rel;
    bool inMirror = false;
    if (!cloudPath(localPath, &remote, &rel, &inMirror) || inMirror)
        return localPath;
    const QString pin = pinFor(remote, rel);
    if (pin.isNull())
        return localPath;
    const QString mirror = mirrorPath(remote, rel);
    const PinStatus st = status(remote, pin);
    if (!QFileInfo(mirror).isDir() || (!st.lastSync.isValid() && st.syncing))
        return localPath; // first download still running
    if (!st.lastSync.isValid() || st.lastSync.msecsTo(QDateTime::currentDateTime()) > OPEN_RESYNC_AFTER_MS)
        syncNow(remote, pin);
    return mirror;
}

void PaneroOffline::decorate(QPixmap &icon, State state)
{
    if (state == State::NotCloud || icon.isNull())
        return;
    const qreal dpr = icon.devicePixelRatio();
    const qreal w = icon.width() / dpr;
    const qreal d = qMax<qreal>(7.0, w * 0.45);
    const QRectF r(0.5, w - d - 0.5, d, d);
    QPainter p(&icon);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor green(0x2e, 0x9e, 0x4f);
    const QColor blue(0x3a, 0x7b, 0xd5);
    if (state == State::OnlineOnly) {
        // a small cloud
        QPainterPath cloud;
        const qreal x = r.left(), y = r.top() + d * 0.2, cw = d, ch = d * 0.62;
        cloud.addEllipse(QRectF(x + cw * 0.05, y + ch * 0.35, cw * 0.42, ch * 0.62));
        cloud.addEllipse(QRectF(x + cw * 0.25, y, cw * 0.5, ch * 0.75));
        cloud.addEllipse(QRectF(x + cw * 0.53, y + ch * 0.3, cw * 0.42, ch * 0.66));
        cloud.addRect(QRectF(x + cw * 0.25, y + ch * 0.6, cw * 0.5, ch * 0.37));
        p.setPen(QPen(blue, qMax<qreal>(1.0, d / 9.0)));
        p.setBrush(Qt::white);
        p.drawPath(cloud.simplified());
        return;
    }
    const bool pinned = state == State::Pinned;
    p.setPen(QPen(green, qMax<qreal>(1.0, d / 8.0)));
    p.setBrush(pinned ? QBrush(green) : QBrush(Qt::white));
    p.drawEllipse(r.adjusted(0.5, 0.5, -0.5, -0.5));
    QPainterPath check;
    check.moveTo(r.left() + d * 0.27, r.top() + d * 0.52);
    check.lineTo(r.left() + d * 0.44, r.top() + d * 0.69);
    check.lineTo(r.left() + d * 0.74, r.top() + d * 0.35);
    p.setPen(QPen(pinned ? Qt::white : green, qMax<qreal>(1.2, d / 7.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(check);
}

bool PaneroOffline::pin(const QString &remote, const QString &relativePath, QString *error)
{
    const QString rel = cleanRelative(relativePath);
    QStringList list = pins(remote);
    for (const QString &p : qAsConst(list)) {
        if (isInside(rel, p)) {
            if (error)
                *error = i18n("This folder is already kept on this device.");
            return false;
        }
    }
    // a new pin replaces pins inside it
    for (int i = list.count() - 1; i >= 0; --i) {
        if (isInside(list.at(i), rel))
            list.removeAt(i);
    }
    if (!QDir().mkpath(mirrorPath(remote, rel))) {
        if (error)
            *error = i18n("Could not create the folder %1.", mirrorPath(remote, rel));
        return false;
    }
    list << rel;
    KConfigGroup g = group(remote);
    QStringList synced = g.readEntry("Synced Pins", QStringList());
    synced.removeAll(rel);
    g.writeEntry("Synced Pins", synced);
    setPins(remote, list);
    syncNow(remote, rel);
    return true;
}

bool PaneroOffline::deleteMirror(const QString &remote, const QString &relativePath, QString *error)
{
    const QString path = mirrorPath(remote, relativePath);
    if (!QFileInfo::exists(path))
        return true;
    const QString root = QFileInfo(mirrorRoot(remote)).canonicalFilePath();
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (root.isEmpty() || canonical.isEmpty() || (canonical != root && !canonical.startsWith(root + QLatin1Char('/')))) {
        if (error)
            *error = i18n("The local copy is not where Panero expects it; it was not deleted.");
        return false;
    }
    if (!QDir(canonical).removeRecursively()) {
        if (error)
            *error = i18n("Some files of the local copy could not be deleted.");
        return false;
    }
    // remove now empty parent folders up to ~/Cloud Offline
    QDir parent = QFileInfo(canonical).dir();
    const QString base = QDir::homePath() + QLatin1String("/Cloud Offline");
    while (parent.absolutePath().startsWith(base) && parent.absolutePath() != base && parent.isEmpty()) {
        const QString name = parent.dirName();
        if (!parent.cdUp() || !parent.rmdir(name))
            break;
    }
    if (QDir(base).exists() && QDir(base).isEmpty())
        QDir().rmdir(base);
    return true;
}

bool PaneroOffline::unpin(QWidget *parent, const QString &remote, const QString &relativePath)
{
    const QString rel = cleanRelative(relativePath);
    if (!pins(remote).contains(rel))
        return false;
    QString error;
    // upload local changes first
    QProgressDialog progress(i18n("Uploading changes of the local copy..."), QString(), 0, 0, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    progress.setCancelButton(nullptr);
    progress.show();
    const bool synced = syncAllBlocking(remote, &error);
    progress.close();
    if (!synced) {
        const auto answer = KMessageBox::warningTwoActions(
            parent,
            i18n("<p>The local copy could not be synchronised:</p><p>%1</p><p>Changes made on this device may not be in the cloud "
                 "yet. Delete the local copy anyway?</p>",
                 error.toHtmlEscaped()),
            i18n("Free Up Space"), KGuiItem(i18n("Delete Anyway"), QStringLiteral("edit-delete")), KStandardGuiItem::cancel());
        if (answer != KMessageBox::PrimaryAction)
            return false;
    }
    QStringList list = pins(remote);
    list.removeAll(rel);
    KConfigGroup g = group(remote);
    QStringList synced2 = g.readEntry("Synced Pins", QStringList());
    synced2.removeAll(rel);
    g.writeEntry("Synced Pins", synced2);
    setPins(remote, list);
    m_status.remove(key(remote, rel));
    // forget bisync's listings, so a later pin starts with a fresh --resync
    QDir(PaneroRclone::cacheDir(remote) + QLatin1String("/bisync/")
         + QString::fromLatin1(QCryptographicHash::hash(rel.toUtf8(), QCryptographicHash::Sha1).toHex()))
        .removeRecursively();
    if (!deleteMirror(remote, rel, &error)) {
        KMessageBox::error(parent, error, i18n("Free Up Space"));
        return false;
    }
    return true;
}

void PaneroOffline::syncNow(const QString &remote, const QString &relativePath)
{
    const QStringList list = relativePath.isNull() ? pins(remote) : QStringList{cleanRelative(relativePath)};
    for (const QString &p : list) {
        if (!pins(remote).contains(p))
            continue;
        bool queued = (m_process && m_current.remote == remote && m_current.pin == p);
        for (const Job &j : qAsConst(m_queue))
            queued = queued || (j.remote == remote && j.pin == p);
        if (queued)
            continue;
        Job job;
        job.remote = remote;
        job.pin = p;
        job.resync = !group(remote).readEntry("Synced Pins", QStringList()).contains(p);
        m_queue << job;
        m_status[key(remote, p)].syncing = true;
    }
    emit changed(remote);
    startNext();
}

QStringList PaneroOffline::bisyncArgs(const Job &job) const
{
    const QString workdir = PaneroRclone::cacheDir(job.remote) + QLatin1String("/bisync/")
        + QString::fromLatin1(QCryptographicHash::hash(job.pin.toUtf8(), QCryptographicHash::Sha1).toHex());
    QStringList args{QStringLiteral("bisync"),
                     job.remote + QLatin1Char(':') + job.pin,
                     mirrorPath(job.remote, job.pin),
                     QStringLiteral("--workdir"),
                     workdir,
                     QStringLiteral("--resilient"),
                     QStringLiteral("--recover"),
                     QStringLiteral("--create-empty-src-dirs"),
                     QStringLiteral("--max-lock"),
                     QStringLiteral("2m"),
                     QStringLiteral("--conflict-resolve"),
                     QStringLiteral("none"),
                     QStringLiteral("--conflict-loser"),
                     QStringLiteral("num")};
    if (job.resync)
        args << QStringLiteral("--resync");
    return args;
}

void PaneroOffline::startNext()
{
    if (m_process || m_queue.isEmpty())
        return;
    m_current = m_queue.takeFirst();
    // a missing local copy must never be synchronised as "everything deleted"
    if (!m_current.resync && !QFileInfo(mirrorPath(m_current.remote, m_current.pin)).isDir()) {
        PinStatus &st = m_status[key(m_current.remote, m_current.pin)];
        st.syncing = false;
        st.error = i18n("The local copy is missing. Use \"Free Up Space\" and keep the folder again to download it.");
        emit changed(m_current.remote);
        QTimer::singleShot(0, this, &PaneroOffline::startNext);
        return;
    }
    QDir().mkpath(mirrorPath(m_current.remote, m_current.pin));
    QDir().mkpath(PaneroRclone::cacheDir(m_current.remote) + QLatin1String("/bisync"));
    m_process = new QProcess(this);
    PaneroRclone::prepare(*m_process, bisyncArgs(m_current));
    m_process->setStandardOutputFile(QProcess::nullDevice());
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
        const QString err = QString::fromUtf8(m_process->readAllStandardError());
        m_process->deleteLater();
        m_process = nullptr;
        jobFinished(status == QProcess::NormalExit ? code : -1, err);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || !m_process)
            return;
        m_process->deleteLater();
        m_process = nullptr;
        jobFinished(-1, i18n("rclone could not be started."));
    });
    m_process->start();
}

void PaneroOffline::jobFinished(int exitCode, const QString &stderrText)
{
    const Job job = m_current;
    PinStatus &st = m_status[key(job.remote, job.pin)];
    st.syncing = false;
    if (exitCode == 0) {
        st.lastSync = QDateTime::currentDateTime();
        st.error.clear();
        st.conflicts += countConflicts(stderrText);
        if (job.resync) {
            KConfigGroup g = group(job.remote);
            QStringList synced = g.readEntry("Synced Pins", QStringList());
            if (!synced.contains(job.pin))
                synced << job.pin;
            g.writeEntry("Synced Pins", synced);
            g.sync();
        }
    } else if (!job.resync && stderrText.contains(QLatin1String("--resync"))) {
        // bisync lost its listings (e.g. after a crash): start again with --resync
        Job again = job;
        again.resync = true;
        m_queue.prepend(again);
        st.syncing = true;
    } else {
        QString summary = PaneroCloudUtils::rcloneErrorSummary(stderrText, 2);
        st.error = summary.isEmpty() ? i18n("Synchronisation failed (exit code %1).", exitCode) : summary;
    }
    watchMirrors();
    emit changed(job.remote);
    startNext();
}

bool PaneroOffline::syncAllBlocking(const QString &remote, QString *error)
{
    syncNow(remote);
    QEventLoop loop;
    QTimer poll;
    connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (!isBusy(remote))
            loop.quit();
    });
    poll.start(200);
    if (isBusy(remote))
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    QStringList errors;
    for (const QString &p : pins(remote)) {
        const PinStatus st = status(remote, p);
        if (!st.error.isEmpty())
            errors << (p.isEmpty() ? i18n("(whole drive)") : p) + QLatin1String(": ") + st.error;
    }
    if (error)
        *error = errors.join(QLatin1Char('\n'));
    return errors.isEmpty();
}

bool PaneroOffline::isBusy(const QString &remote) const
{
    if (m_process && m_current.remote == remote)
        return true;
    for (const Job &j : m_queue) {
        if (j.remote == remote)
            return true;
    }
    return false;
}

void PaneroOffline::forgetRemote(const QString &remote, bool deleteLocalCopies)
{
    for (int i = m_queue.count() - 1; i >= 0; --i) {
        if (m_queue.at(i).remote == remote)
            m_queue.removeAt(i);
    }
    if (m_process && m_current.remote == remote) {
        m_process->disconnect(this);
        m_process->terminate();
        if (!m_process->waitForFinished(5000))
            m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (deleteLocalCopies) {
        QString error;
        deleteMirror(remote, QString(), &error);
    }
    const QStringList keys = m_status.keys();
    for (const QString &k : keys) {
        if (k.startsWith(remote + QLatin1Char('\n')))
            m_status.remove(k);
    }
    m_config->deleteGroup(QLatin1String(GROUP_PREFIX) + remote);
    m_config->sync();
    watchMirrors();
    emit changed(remote);
    startNext();
}

PaneroOffline::PinStatus PaneroOffline::status(const QString &remote, const QString &relativePath) const
{
    return m_status.value(key(remote, cleanRelative(relativePath)));
}

void PaneroOffline::periodicSync()
{
    const QStringList remotes = PaneroCloud::instance()->remoteNames();
    for (const QString &r : remotes) {
        if (!pins(r).isEmpty())
            syncNow(r);
    }
}

void PaneroOffline::watchMirrors()
{
    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());
    QStringList dirs;
    const QStringList remotes = PaneroCloud::instance()->remoteNames();
    for (const QString &r : remotes) {
        for (const QString &p : pins(r)) {
            const QString root = mirrorPath(r, p);
            if (!QFileInfo(root).isDir())
                continue;
            dirs << root;
            QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
            while (it.hasNext() && dirs.count() < MAX_WATCHED_DIRS)
                dirs << it.next();
        }
    }
    if (!dirs.isEmpty())
        m_watcher->addPaths(dirs);
}
