/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerooffline.h"
#include "panerocloud.h"
#include "panerocloudutils.h"
#include "panerorclone.h"
#include "../FileSystem/panerocloudfiles.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QProgressDialog>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <QLocale>

#include <atomic>
#include <memory>
#include <sys/stat.h>

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

/// A pin path from the configuration: relative, without ".." components.
bool validPin(const QString &pin)
{
    if (pin.startsWith(QLatin1Char('/')) || pin.contains(QChar(0)) || pin.contains(QLatin1Char('\n')))
        return false;
    const QStringList parts = pin.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        if (part == QLatin1String("..") || part == QLatin1String("."))
            return false;
    }
    return cleanRelative(pin) == pin;
}

const char ID_FILE[] = ".panero-cloud-id";

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
    connect(this, &PaneroOffline::changed, this, &PaneroOffline::invalidateStates);
    m_repaintTimer = new QTimer(this);
    m_repaintTimer->setSingleShot(true);
    m_repaintTimer->setInterval(150);
    connect(m_repaintTimer, &QTimer::timeout, this, &PaneroOffline::statesUpdated);
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
    QStringList list = group(remote).readEntry("Pins", QStringList());
    for (int i = list.count() - 1; i >= 0; --i) {
        if (!validPin(list.at(i)))
            list.removeAt(i);
    }
    return list;
}

QString PaneroOffline::remoteId(const QString &remote, bool create)
{
    KConfigGroup g = group(remote);
    QString id = g.readEntry("Id", QString());
    if (id.isEmpty() && create) {
        id = QString::number(QRandomGenerator::system()->generate64(), 16) + QString::number(QRandomGenerator::system()->generate64(), 16);
        g.writeEntry("Id", id);
        g.sync();
    }
    return id;
}

bool PaneroOffline::mirrorBelongsTo(const QString &remote)
{
    const QString id = remoteId(remote, false);
    if (id.isEmpty())
        return false;
    QFile f(mirrorRoot(remote) + QLatin1Char('/') + QLatin1String(ID_FILE));
    return f.open(QIODevice::ReadOnly) && QString::fromLatin1(f.readAll()).trimmed() == id;
}

bool PaneroOffline::claimMirror(const QString &remote, QString *error)
{
    const QString root = mirrorRoot(remote);
    const QString id = remoteId(remote, true);
    QDir dir(root);
    if (dir.exists() && !mirrorBelongsTo(remote) && !dir.isEmpty()) {
        if (error)
            *error = i18n("The folder %1 already exists from an earlier sign-in or another program. Move or delete it first, so its "
                          "files are not mixed with this cloud drive.",
                          root);
        return false;
    }
    if (!QDir().mkpath(root)) {
        if (error)
            *error = i18n("Could not create the folder %1.", root);
        return false;
    }
    QFile f(root + QLatin1Char('/') + QLatin1String(ID_FILE));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = i18n("Could not write %1.", f.fileName());
        return false;
    }
    f.write(id.toLatin1() + '\n');
    return true;
}

QString PaneroOffline::backupDir(const QString &remote)
{
    return QDir::homePath() + QLatin1String("/Cloud Offline/.panero-backup/") + PaneroCloudUtils::safeFileName(remote) + QLatin1Char('/')
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
}

QString PaneroOffline::workdir(const Job &job) const
{
    return PaneroRclone::cacheDir(job.remote) + QLatin1String("/bisync/")
        + QString::fromLatin1(QCryptographicHash::hash(job.pin.toUtf8(), QCryptographicHash::Sha1).toHex());
}

void PaneroOffline::pruneRemotes(const QStringList &existing)
{
    const QStringList groups = m_config->groupList();
    for (const QString &g : groups) {
        if (!g.startsWith(QLatin1String(GROUP_PREFIX)))
            continue;
        const QString remote = g.mid(int(qstrlen(GROUP_PREFIX)));
        if (existing.contains(remote) || isBusy(remote))
            continue;
        // the remote was deleted (e.g. in a terminal): its pins and bisync
        // state must not be reused by a new remote of the same name
        m_config->deleteGroup(g);
        QDir(PaneroRclone::cacheDir(remote) + QLatin1String("/bisync")).removeRecursively();
    }
    m_config->sync();
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
            if (PaneroCloudUtils::safeFileName(name) == r && const_cast<PaneroOffline *>(this)->mirrorBelongsTo(name)) {
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

void PaneroOffline::invalidateStates()
{
    m_stateCache.clear();
    // folder badges stay visible until they are recomputed (no flicker)
    for (auto it = m_folderStates.begin(); it != m_folderStates.end(); ++it)
        it->second = 0;
    if (m_repaintTimer && !m_repaintTimer->isActive())
        m_repaintTimer->start(); // repaint: badges are asked for again
}

PaneroOffline::State PaneroOffline::folderState(const QString &localPath, const QString &remote, const QString &rel) const
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto known = m_folderStates.constFind(localPath);
    // files opened in other programs are cached without a signal: recheck
    // now and then (stops at the first file that is not cached)
    const bool fresh = known != m_folderStates.constEnd() && now - known->second < 20000;
    if (!fresh && !m_folderPending.contains(localPath)) {
        static QThreadPool *pool = nullptr;
        if (!pool) {
            pool = new QThreadPool;
            pool->setMaxThreadCount(2);
        }
        if (m_folderPending.size() < 200) {
            m_folderPending.insert(localPath);
            const QString cacheFolder = PaneroCloudUtils::vfsCacheFile(PaneroRclone::cacheDir(remote), cacheSub(remote), rel);
            auto *self = const_cast<PaneroOffline *>(this);
            QtConcurrent::run(pool, [self, localPath, cacheFolder]() {
                const PaneroCloudUtils::FolderCache c = PaneroCloudUtils::folderCacheState(localPath, cacheFolder);
                const State s = c == PaneroCloudUtils::FolderCache::Complete ? State::Available : State::OnlineOnly;
                QMetaObject::invokeMethod(self, [self, localPath, s]() {
                    self->m_folderPending.remove(localPath);
                    const auto old = self->m_folderStates.constFind(localPath);
                    const bool changedState = old == self->m_folderStates.constEnd() ? s != State::OnlineOnly : old->first != s;
                    if (self->m_folderStates.size() > 5000)
                        self->m_folderStates.clear();
                    self->m_folderStates.insert(localPath, qMakePair(s, QDateTime::currentMSecsSinceEpoch()));
                    if (changedState) {
                        self->m_stateCache.remove(localPath);
                        if (!self->m_repaintTimer->isActive())
                            self->m_repaintTimer->start();
                    }
                }, Qt::QueuedConnection);
            });
        }
    }
    return known != m_folderStates.constEnd() ? known->first : State::OnlineOnly;
}

PaneroOffline::State PaneroOffline::state(const QString &localPath, qint64 size, bool isDir) const
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto cached = m_stateCache.constFind(localPath);
    if (cached != m_stateCache.constEnd() && now - cached->second < 3000)
        return cached->first;
    const State s = computeState(localPath, size, isDir);
    if (m_stateCache.size() > 5000)
        m_stateCache.clear();
    m_stateCache.insert(localPath, qMakePair(s, now));
    return s;
}

PaneroOffline::State PaneroOffline::computeState(const QString &localPath, qint64 size, bool isDir) const
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
        return folderState(localPath, remote, rel);
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
    // the local copy is opened only once its first download has completed
    if (!QFileInfo(mirror).isDir() || !group(remote).readEntry("Synced Pins", QStringList()).contains(pin))
        return localPath;
    if (!st.lastSync.isValid() || st.lastSync.msecsTo(QDateTime::currentDateTime()) > OPEN_RESYNC_AFTER_MS)
        syncNow(remote, pin);
    return mirror;
}

void PaneroOffline::decorate(QPixmap &icon, State state)
{
    // shared with the Windows sync folders (same states, same drawing)
    PaneroCloudFiles::decorate(icon, PaneroCloudFiles::State(int(state)));
}

namespace
{
/// Shared state of a background "make available offline" run.
struct PrefetchState {
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    std::atomic<int> files{0};
    std::atomic<int> filesDone{0};
    std::atomic<qint64> bytes{0};
    std::atomic<qint64> bytesDone{0};
    std::atomic<int> failed{0};
    std::atomic<bool> tooLarge{false};
};
}

void PaneroOffline::makeAvailableOffline(QWidget *parent, const QStringList &localPaths)
{
    QString remote, rel;
    QStringList paths;
    for (const QString &p : localPaths) {
        QString r;
        bool inMirror = false;
        if (cloudPath(p, &r, &rel, &inMirror) && !inMirror && !rel.isEmpty()) {
            remote = r;
            paths << p;
        }
    }
    if (paths.isEmpty())
        return;
    const Settings s = settings(remote);
    if (!s.keepOpened) {
        const auto answer = KMessageBox::questionTwoActions(
            parent, i18n("<p>\"Keep files I open available on this device\" is off for this drive, so downloaded files are deleted "
                         "when the drive is disconnected.</p><p>Turn it on now?</p>"),
            i18n("Make Available Offline"), KGuiItem(i18n("Turn On")), KStandardGuiItem::cancel());
        if (answer != KMessageBox::PrimaryAction)
            return;
        Settings on = s;
        on.keepOpened = true;
        setSettings(remote, on);
    }
    const qint64 limit = qint64(s.cacheLimitGB) * 1024 * 1024 * 1024;
    const QString cacheRoot = PaneroRclone::cacheDir(remote);
    const QString sub = cacheSub(remote);
    const QString mountDir = PaneroCloud::instance()->mountDirs().value(remote);

    auto state = std::make_shared<PrefetchState>();
    auto *progress = new QProgressDialog(i18n("Looking for files..."), i18n("Cancel"), 0, 1000, parent);
    progress->setWindowTitle(i18n("Make Available Offline"));
    progress->setAttribute(Qt::WA_DeleteOnClose);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setValue(0);
    connect(progress, &QProgressDialog::canceled, progress, [state]() {
        state->cancel = true;
    });
    progress->show();

    QtConcurrent::run([state, paths, mountDir, cacheRoot, sub, limit]() {
        QStringList files;
        QList<qint64> sizes;
        const auto add = [&](const QString &f, qint64 size) {
            if (size <= 0) // empty files and Google documents (no size, cannot be cached)
                return;
            const QString rel = f.mid(mountDir.length() + 1);
            if (PaneroCloudUtils::isFullyCached(PaneroCloudUtils::vfsCacheFile(cacheRoot, sub, rel), size))
                return;
            files << f;
            sizes << size;
            state->bytes += size;
        };
        for (const QString &p : paths) {
            if (state->cancel)
                break;
            const QFileInfo fi(p);
            if (fi.isDir()) {
                QDirIterator it(p, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                while (it.hasNext() && !state->cancel) {
                    it.next();
                    add(it.filePath(), it.fileInfo().size());
                }
            } else {
                add(p, fi.size());
            }
        }
        state->files = int(files.size());
        if (state->bytes > limit * 9 / 10) {
            state->tooLarge = true;
            state->done = true;
            return;
        }
        QByteArray buffer(1024 * 1024, Qt::Uninitialized);
        for (int i = 0; i < files.size() && !state->cancel; ++i) {
            QFile f(files.at(i));
            bool ok = f.open(QIODevice::ReadOnly);
            qint64 read = 0;
            while (ok && !state->cancel) {
                const qint64 n = f.read(buffer.data(), buffer.size());
                if (n < 0)
                    ok = false;
                if (n <= 0)
                    break;
                read += n;
                state->bytesDone += n;
            }
            if (!ok)
                ++state->failed;
            state->bytesDone += qMax<qint64>(0, sizes.at(i) - read); // keep the total consistent
            ++state->filesDone;
        }
        state->done = true;
    });

    auto *timer = new QTimer(progress);
    connect(timer, &QTimer::timeout, this, [this, timer, progress, state, remote, limit, parent]() {
        if (state->files > 0 || state->done) {
            const qint64 total = qMax<qint64>(1, state->bytes);
            progress->setValue(int(1000 * state->bytesDone / total));
            progress->setLabelText(i18n("Downloading file %1 of %2 (%3 of %4)...", state->filesDone + (state->done ? 0 : 1), int(state->files),
                                        QLocale().formattedDataSize(state->bytesDone), QLocale().formattedDataSize(state->bytes)));
        }
        if (!state->done)
            return;
        timer->stop();
        invalidateStates();
        emit changed(remote);
        progress->close();
        if (state->tooLarge)
            KMessageBox::information(parent,
                                     i18n("The files need %1, more than the space for opened files of this drive (%2). Raise the limit in "
                                          "Offline Files... or choose fewer files.",
                                          QLocale().formattedDataSize(state->bytes), QLocale().formattedDataSize(limit)),
                                     i18n("Make Available Offline"));
        else if (state->failed > 0)
            KMessageBox::information(parent, i18np("1 file could not be downloaded.", "%1 files could not be downloaded.", int(state->failed)),
                                     i18n("Make Available Offline"));
    });
    timer->start(200);
}

QString PaneroOffline::freeUpCache(const QStringList &localPaths, qint64 *freedBytes)
{
    qint64 freed = 0;
    int removed = 0, skippedDirty = 0;
    QString lastRemote;
    for (const QString &p : localPaths) {
        QString remote, rel;
        bool inMirror = false;
        if (!cloudPath(p, &remote, &rel, &inMirror) || inMirror || rel.isEmpty())
            continue;
        lastRemote = remote;
        const QString cacheRoot = PaneroRclone::cacheDir(remote);
        const QString sub = cacheSub(remote);
        const QString data = PaneroCloudUtils::vfsCacheFile(cacheRoot, sub, rel);
        if (data.isEmpty())
            continue;
        QStringList cacheFiles;
        if (QFileInfo(data).isDir()) {
            QDirIterator it(data, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext())
                cacheFiles << it.next();
        } else if (QFileInfo::exists(data)) {
            cacheFiles << data;
        }
        const QString vfsPrefix = cacheRoot + QLatin1String("/vfs/");
        for (const QString &cf : qAsConst(cacheFiles)) {
            const QString meta = cacheRoot + QLatin1String("/vfsMeta/") + cf.mid(vfsPrefix.length());
            if (PaneroCloudUtils::isDirtyMeta(meta)) {
                ++skippedDirty; // not uploaded yet: never delete
                continue;
            }
            // rclone may still hold the cache file open: removing it is safe, rclone
            // checks on the next open whether its cache file still exists
            // (vfscache item.open) and downloads again; a reading app keeps
            // reading the removed copy until it closes it
            struct stat st;
            const qint64 used = ::stat(QFile::encodeName(cf).constData(), &st) == 0 ? qint64(st.st_blocks) * 512 : 0;
            // rclone notices the removal and downloads the file again when needed
            if (QFile::remove(cf)) {
                QFile::remove(meta);
                freed += used;
                ++removed;
            }
        }
    }
    invalidateStates();
    if (!lastRemote.isEmpty())
        emit changed(lastRemote);
    if (freedBytes)
        *freedBytes = freed;
    QString summary = removed == 0 ? i18n("Nothing of this was stored on this device.")
                                   : i18np("Removed the local copy of 1 file (%2).", "Removed the local copies of %1 files (%2).", removed,
                                           QLocale().formattedDataSize(freed));
    if (skippedDirty)
        summary += QLatin1Char(' ') + i18np("1 file was kept because it has not been uploaded yet.",
                                            "%1 files were kept because they have not been uploaded yet.", skippedDirty);
    return summary;
}

bool PaneroOffline::pin(const QString &remote, const QString &relativePath, QString *error)
{
    const QString rel = cleanRelative(relativePath);
    if (!validPin(rel)) {
        if (error)
            *error = i18n("This folder cannot be kept on this device.");
        return false;
    }
    if (!claimMirror(remote, error))
        return false;
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
    const QDir target(mirrorPath(remote, rel));
    if (target.exists() && !target.isEmpty()) {
        // the first synchronisation copies the cloud over the local folder
        if (error)
            *error = i18n("The folder %1 is not empty. Move its files away first.", target.absolutePath());
        return false;
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
    m_blocked.insert(key(remote, rel));
    if (!synced) {
        const auto answer = KMessageBox::warningTwoActions(
            parent,
            i18n("<p>The local copy could not be synchronised:</p><p>%1</p><p>Changes made on this device may not be in the cloud "
                 "yet. Delete the local copy anyway?</p>",
                 error.toHtmlEscaped()),
            i18n("Free Up Space"), KGuiItem(i18n("Delete Anyway"), QStringLiteral("edit-delete")), KStandardGuiItem::cancel());
        if (answer != KMessageBox::PrimaryAction) {
            m_blocked.remove(key(remote, rel));
            return false;
        }
    }
    // from here on, no synchronisation of this pin may start (also not from a
    // timer or the file watcher while a message box is open)
    const QString k = key(remote, rel);
    m_blocked.insert(k);
    for (int i = m_queue.count() - 1; i >= 0; --i) {
        if (m_queue.at(i).remote == remote && m_queue.at(i).pin == rel)
            m_queue.removeAt(i);
    }
    if (m_process && m_current.remote == remote && m_current.pin == rel) {
        m_process->disconnect(this);
        m_process->terminate();
        if (!m_process->waitForFinished(10000))
            m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
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
    Job gone;
    gone.remote = remote;
    gone.pin = rel;
    QDir(workdir(gone)).removeRecursively();
    const bool owned = mirrorBelongsTo(remote);
    if (!owned)
        error = i18n("The local copy does not belong to this cloud drive; it was not deleted.");
    const bool deleted = owned && deleteMirror(remote, rel, &error);
    if (deleted && pins(remote).isEmpty()) {
        // nothing kept any more: remove the id marker and the empty folder
        QDir mirrorDir(mirrorRoot(remote));
        if (mirrorDir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot) == QStringList{QLatin1String(ID_FILE)}) {
            mirrorDir.remove(QLatin1String(ID_FILE));
            QDir().rmdir(mirrorRoot(remote));
        }
        const QString base = QDir::homePath() + QLatin1String("/Cloud Offline");
        if (QDir(base).exists() && QDir(base).isEmpty())
            QDir().rmdir(base);
    }
    m_blocked.remove(k);
    startNext();
    if (!deleted) {
        KMessageBox::error(parent, error, i18n("Free Up Space"));
        return false;
    }
    return true;
}

void PaneroOffline::syncNow(const QString &remote, const QString &relativePath)
{
    const QStringList list = relativePath.isNull() ? pins(remote) : QStringList{cleanRelative(relativePath)};
    for (const QString &p : list) {
        if (!pins(remote).contains(p) || m_blocked.contains(key(remote, p)))
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

void PaneroOffline::repair(const QString &remote, const QString &relativePath)
{
    const QString p = cleanRelative(relativePath);
    if (!pins(remote).contains(p) || m_blocked.contains(key(remote, p)) || isBusy(remote))
        return;
    Job job;
    job.remote = remote;
    job.pin = p;
    job.resync = true;
    job.repair = true;
    m_queue << job;
    PinStatus &st = m_status[key(remote, p)];
    st.syncing = true;
    st.needsRepair = false;
    emit changed(remote);
    startNext();
}

QStringList PaneroOffline::bisyncArgs(const Job &job) const
{
    const QString workdir = this->workdir(job);
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
    if (job.repair) {
        // never let one side silently replace the other: the newer file wins,
        // and local files that are replaced are kept in a backup folder
        args << QStringLiteral("--resync") << QStringLiteral("--resync-mode") << QStringLiteral("newer") << QStringLiteral("--backup-dir2")
             << backupDir(job.remote);
    } else if (job.resync) {
        // only the very first run of a new pin (into an empty folder)
        args << QStringLiteral("--resync");
    }
    return args;
}

void PaneroOffline::startNext()
{
    if (m_process || m_queue.isEmpty())
        return;
    m_current = m_queue.takeFirst();
    if (!pins(m_current.remote).contains(m_current.pin) || m_blocked.contains(key(m_current.remote, m_current.pin))) {
        m_status[key(m_current.remote, m_current.pin)].syncing = false;
        QTimer::singleShot(0, this, &PaneroOffline::startNext);
        return;
    }
    // the local copy must be inside the remote's folder below ~/Cloud Offline
    {
        QDir().mkpath(mirrorRoot(m_current.remote));
        const QString root = QFileInfo(mirrorRoot(m_current.remote)).canonicalFilePath();
        const QString target = QFileInfo(mirrorPath(m_current.remote, m_current.pin)).absoluteFilePath();
        const QString canonical = QFileInfo(target).exists() ? QFileInfo(target).canonicalFilePath() : target;
        if (root.isEmpty() || !mirrorBelongsTo(m_current.remote) || (canonical != root && !canonical.startsWith(root + QLatin1Char('/')))) {
            PinStatus &st = m_status[key(m_current.remote, m_current.pin)];
            st.syncing = false;
            st.error = i18n("The local copy is not where Panero expects it; it was not synchronised.");
            emit changed(m_current.remote);
            QTimer::singleShot(0, this, &PaneroOffline::startNext);
            return;
        }
    }
    // a missing local copy must never be synchronised as "everything deleted"
    if (!m_current.resync && !QFileInfo(mirrorPath(m_current.remote, m_current.pin)).isDir()) {
        PinStatus &st = m_status[key(m_current.remote, m_current.pin)];
        st.syncing = false;
        st.error = i18n("The local copy is missing. Use \"Free Up Space\" and keep the folder again to download it.");
        emit changed(m_current.remote);
        QTimer::singleShot(0, this, &PaneroOffline::startNext);
        return;
    }
    // the first run copies the cloud over the local folder: only while that
    // folder is still empty (e.g. not after a first download that broke off and
    // was then edited); otherwise use the careful repair mode
    if (m_current.resync && !m_current.repair) {
        const QDir target(mirrorPath(m_current.remote, m_current.pin));
        if (target.exists() && !target.isEmpty())
            m_current.repair = true;
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
        st.needsRepair = false;
        st.conflicts += countConflicts(stderrText);
        if (job.resync) {
            KConfigGroup g = group(job.remote);
            QStringList synced = g.readEntry("Synced Pins", QStringList());
            if (!synced.contains(job.pin))
                synced << job.pin;
            g.writeEntry("Synced Pins", synced);
            g.sync();
        }
    } else if (stderrText.contains(QLatin1String("Must run --resync")) || stderrText.contains(QLatin1String("run --resync to recover"))
               || stderrText.contains(QLatin1String("Bisync critical error"))) {
        // never resync automatically: a resync can replace changed files of one side
        st.needsRepair = true;
        st.error = i18n("Synchronisation stopped and needs a repair (Offline Files > Repair). Nothing was overwritten.");
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
    // only a folder that carries this remote's id (never one kept from an
    // earlier account of the same name)
    if (deleteLocalCopies && mirrorBelongsTo(remote)) {
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
