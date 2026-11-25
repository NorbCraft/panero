/*
    SPDX-FileCopyrightText: 2000 Rafi Yanai <krusader@users.sourceforge.net>
    SPDX-FileCopyrightText: 2004-2022 Krusader Krew <https://krusader.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "defaultfilesystem.h"

// QtCore
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QStorageInfo>

#include <KConfigCore/KSharedConfig>
#include <KCoreAddons/KUrlMimeData>
#include <KI18n/KLocalizedString>
#include <KIO/DropJob>
#include <KIO/MkpathJob>
#include <KIO/FileUndoManager>
#include <KIO/JobUiDelegate>
#include <KIO/ListJob>
#include <KIOCore/KDiskFreeSpaceInfo>

#include <QDateTime>
#include <QFutureWatcher>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QTextStream>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <memory>
#include <unistd.h>

#include "paneroslowpaths.h"
#include <KIOCore/KFileItem>
#include <KIOCore/KMountPoint>
#include <KIOCore/KProtocolManager>
#include <KJobWidgets/KJobWidgets>
#include <kio_version.h>

#include "fileitem.h"
#include "../defaults.h"
#include "../krglobal.h"
#include "../krservices.h"
#include "../JobMan/krjob.h"

/// A cloud/network folder as read in a worker thread (Panero, D-018)
struct SlowListing {
    bool exists = false;
    bool opened = false;
    bool accessible = false;
    QList<FileSystem::LocalStat> entries;
    quint64 signature = 0;
};

namespace
{
/// Result of reading a cloud/network folder in a worker thread.

/// Reads @p path: names, then the status of each entry (no file opens).
void readSlowFolder(SlowListing &listing, const QString &path, bool showHidden)
{
    QStringList names;
#ifdef Q_OS_WIN
    // Windows (no cloud mounts there yet): Qt's listing, Unicode names
    const QDir qdir(path);
    if (!QFileInfo(path).isDir())
        return;
    listing.exists = true;
    listing.opened = listing.accessible = qdir.isReadable();
    if (!listing.accessible)
        return;
    for (const QString &name : qdir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                              QDir::NoSort)) {
        if (!showHidden && name.startsWith(QLatin1Char('.')))
            continue;
        names << name;
    }
#else
    const QByteArray encoded = path.toLocal8Bit();
    QT_STATBUF st;
    if (QT_STAT(encoded.constData(), &st) != 0 || !S_ISDIR(st.st_mode))
        return;
    listing.exists = true;
    QT_DIR *dir = QT_OPENDIR(encoded.constData());
    if (!dir)
        return;
    listing.opened = true;
    listing.accessible = ::access(encoded.constData(), X_OK) == 0;
    QT_DIRENT *dirEnt;
    while ((dirEnt = QT_READDIR(dir)) != nullptr) {
        const QString name = QString::fromLocal8Bit(dirEnt->d_name);
        if (name == QLatin1String(".") || name == QLatin1String(".."))
            continue;
        if (!showHidden && name.startsWith(QLatin1Char('.')))
            continue;
        names << name;
    }
    QT_CLOSEDIR(dir);
    if (!listing.accessible)
        return;
#endif

    QSet<QString> hidden;
    if (!showHidden && names.contains(QLatin1String(".hidden"))) {
        QFile dotHidden(path + QLatin1String("/.hidden"));
        if (dotHidden.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&dotHidden);
            while (!stream.atEnd()) {
                const QString line = stream.readLine();
                if (!line.isEmpty())
                    hidden.insert(line);
            }
        }
    }
    quint64 signature = quint64(names.count());
    for (const QString &name : qAsConst(names)) {
        if (hidden.contains(name))
            continue;
        const FileSystem::LocalStat entry = FileSystem::statLocalFile(name, path);
        // order independent: a sum of per entry hashes
        signature += quint64(qHash(name, 7)) * 1000003u ^ quint64(entry.size) ^ (quint64(entry.mtime) << 20)
            ^ (entry.isDir ? 0x9E3779B97F4A7C15ull : 0);
        listing.entries << entry;
    }
    listing.signature = signature;
}

/// Panel progress for a cloud folder being read ("Reading...", cancel button).
class PaneroListJob : public KIO::Job
{
public:
    PaneroListJob()
    {
        setCapabilities(KJob::Killable);
    }
    void finish()
    {
        done = true;
        emitResult();
    }
    bool killed = false;
    bool done = false;

protected:
    bool doKill() override
    {
        killed = true;
        return true;
    }
};

/// Panero: the last listing of each visited cloud/network folder (D-022). Going
/// back into a folder shows it at once; the background check that follows
/// (checkSlowFolder) replaces it if the provider reports changes.
struct CachedListing {
    std::shared_ptr<SlowListing> listing;
    qint64 time = 0;
};
QHash<QString, CachedListing> &slowListingCache()
{
    static QHash<QString, CachedListing> cache;
    return cache;
}
QString cacheKey(const QString &path, bool showHidden)
{
    return (showHidden ? QLatin1String("h:") : QLatin1String("-:")) + path;
}
void rememberListing(const QString &path, bool showHidden, const std::shared_ptr<SlowListing> &listing)
{
    if (!listing->accessible)
        return;
    QHash<QString, CachedListing> &cache = slowListingCache();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (cache.size() >= 300) { // drop the oldest quarter
        QList<qint64> times;
        for (const CachedListing &c : qAsConst(cache))
            times << c.time;
        std::sort(times.begin(), times.end());
        const qint64 cut = times.at(times.size() / 4);
        for (auto it = cache.begin(); it != cache.end();)
            it = it->time <= cut ? cache.erase(it) : ++it;
    }
    CachedListing entry;
    entry.listing = listing;
    entry.time = now;
    cache.insert(cacheKey(path, showHidden), entry);
}
std::shared_ptr<SlowListing> cachedListing(const QString &path, bool showHidden)
{
    const auto it = slowListingCache().constFind(cacheKey(path, showHidden));
    // older than 30 minutes: list again (the check would replace it anyway)
    if (it == slowListingCache().constEnd() || QDateTime::currentMSecsSinceEpoch() - it->time > 30 * 60 * 1000)
        return nullptr;
    return it->listing;
}
} // namespace

DefaultFileSystem::DefaultFileSystem()
{
    _type = FS_DEFAULT;
}

void DefaultFileSystem::copyFiles(const QList<QUrl> &urls, const QUrl &destination,
                            KIO::CopyJob::CopyMode mode, bool showProgressInfo,
                            JobMan::StartMode startMode)
{
    // resolve relative path before resolving symlinks
    const QUrl dest = resolveRelativePath(destination);

    KIO::JobFlags flags = showProgressInfo ? KIO::DefaultFlags : KIO::HideProgressInfo;

    KrJob *krJob = KrJob::createCopyJob(mode, urls, dest, flags);
    // destination can be a full path with filename when copying/moving a single file
    const QUrl destDir = dest.adjusted(QUrl::RemoveFilename);
    connect(krJob, &KrJob::started, this, [=](KIO::Job *job) { connectJobToDestination(job, destDir); });
    if (mode == KIO::CopyJob::Move) { // notify source about removed files
        connect(krJob, &KrJob::started, this, [=](KIO::Job *job) { connectJobToSources(job, urls); });
    }

    krJobMan->manageJob(krJob, startMode);
}

KIO::DropJob *DefaultFileSystem::dropFiles(const QUrl &destination, QDropEvent *event, QWidget *targetWidget)
{
    qDebug() << "destination=" << destination;

    // resolve relative path before resolving symlinks
    const QUrl dest = resolveRelativePath(destination);

    KIO::DropJob *job = KIO::drop(event, dest);
    KJobWidgets::setWindow(job, targetWidget);

    // NOTE: a DropJob "starts" with showing a menu. If the operation is chosen (copy/move/link)
    // the actual CopyJob starts automatically - we cannot manage the start of the CopyJob (see
    // documentation for KrJob)
    connect(job, &KIO::DropJob::copyJobStarted, this, [=](KIO::CopyJob *kJob) {
        connectJobToDestination(job, dest); // now we have to refresh the destination

        KrJob *krJob = KrJob::createDropJob(job, kJob);
        krJobMan->manageStartedJob(krJob, kJob);
        if (kJob->operationMode() == KIO::CopyJob::Move) { // notify source about removed files
            connectJobToSources(kJob, kJob->srcUrls());
        }
    });
    return job;
}

void DefaultFileSystem::addFiles(const QList<QUrl> &fileUrls, KIO::CopyJob::CopyMode mode,
                                 const QString &dir)
{
    QUrl destination(_currentDirectory);
    if (!dir.isEmpty()) {
        destination.setPath(QDir::cleanPath(destination.path() + '/' + dir));
        const QString scheme = destination.scheme();
        if (scheme == "tar" || scheme == "zip" || scheme == "krarc") {
            if (QDir(destination.path()).exists())
                // if we get out from the archive change the protocol
                destination.setScheme("file");
        }
    }

    destination = ensureTrailingSlash(destination); // destination is always a directory
    copyFiles(fileUrls, destination, mode);
}

void DefaultFileSystem::mkDir(const QString &name)
{
    KIO::Job *job;
    // Panero: recorded for Undo (Ctrl+Z, D-022)
    if (name.contains('/')) {
        job = KIO::mkpath(getUrl(name));
        KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Mkpath, {}, getUrl(name), job);
    } else {
        job = KIO::mkdir(getUrl(name));
        KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Mkdir, {}, getUrl(name), job);
    }
    connectJobToDestination(job, currentDirectory());
}

void DefaultFileSystem::rename(const QString &oldName, const QString &newName)
{
    const QUrl oldUrl = getUrl(oldName);
    const QUrl newUrl = getUrl(newName);
    KIO::Job *job = KIO::moveAs(oldUrl, newUrl, KIO::HideProgressInfo);
    connectJobToDestination(job, currentDirectory());

    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Rename, {oldUrl}, newUrl, job);
}

QUrl DefaultFileSystem::getUrl(const QString& name) const
{
    // NOTE: on non-local fs file URL does not have to be path + name!
    FileItem *fileItem = getFileItem(name);
    if (fileItem)
        return fileItem->getUrl();

    QUrl absoluteUrl(_currentDirectory);
    if (name.startsWith('/')) {
        absoluteUrl.setPath(name);
    } else {
        absoluteUrl.setPath(absoluteUrl.path() + '/' + name);
    }
    return absoluteUrl;
}

void DefaultFileSystem::updateFilesystemInfo()
{
    if (!KConfigGroup(krConfig, "Look&Feel").readEntry("ShowSpaceInformation", true)) {
        _mountPoint = "";
        emit fileSystemInfoChanged(i18n("Space information disabled"), "", 0, 0);
        return;
    }

    // TODO get space info for trash:/ with KIO spaceInfo job
    if (!_currentDirectory.isLocalFile()) {
        _mountPoint = "";
        emit fileSystemInfoChanged(i18n("No space information on non-local filesystems"), "", 0, 0);
        return;
    }

#ifdef Q_OS_WIN
    // Windows: drive letter as the "mount point" ("C:/"), NTFS/FAT/exFAT as type
    {
        const QStorageInfo storage(_currentDirectory.toLocalFile());
        if (!storage.isValid() || !storage.isReady()) {
            _mountPoint = "";
            emit fileSystemInfoChanged(i18n("Space information unavailable"), "", 0, 0);
            return;
        }
        _mountPoint = storage.rootPath();
        emit fileSystemInfoChanged("", QString::fromLatin1(storage.fileSystemType()),
                                   KIO::filesize_t(storage.bytesTotal()), KIO::filesize_t(storage.bytesAvailable()));
        return;
    }
#endif
    const QString path = _currentDirectory.path();

    // Panero: on a cloud/network mount statfs is a network request; use a
    // cached value and refresh it in the background
    if (PaneroSlowPaths::isSlowPath(path)) {
        struct SpaceInfo {
            KIO::filesize_t size = 0;
            KIO::filesize_t available = 0;
            qint64 time = 0;
            bool running = false;
        };
        static QHash<QString, SpaceInfo> cache;
        const QString root = PaneroSlowPaths::mountRoot(path);
        _mountPoint = root;
        SpaceInfo &entry = cache[root];
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (entry.time > 0)
            emit fileSystemInfoChanged("", QStringLiteral("cloud"), entry.size, entry.available);
        else
            emit fileSystemInfoChanged(i18n("Reading space information..."), QStringLiteral("cloud"), 0, 0);
        if (!entry.running && now - entry.time > 2 * 60 * 1000) {
            entry.running = true;
            struct Space {
                bool valid = false;
                KIO::filesize_t size = 0;
                KIO::filesize_t available = 0;
            };
            auto *watcher = new QFutureWatcher<Space>(this);
            connect(watcher, &QFutureWatcher<Space>::finished, this, [this, watcher, root]() {
                const Space info = watcher->result();
                watcher->deleteLater();
                SpaceInfo &e = cache[root];
                e.running = false;
                if (!info.valid)
                    return;
                e.size = info.size;
                e.available = info.available;
                e.time = QDateTime::currentMSecsSinceEpoch();
                if (_currentDirectory.isLocalFile() && PaneroSlowPaths::mountRoot(_currentDirectory.path()) == root)
                    emit fileSystemInfoChanged("", QStringLiteral("cloud"), e.size, e.available);
            });
            watcher->setFuture(QtConcurrent::run([root]() {
                const KDiskFreeSpaceInfo info = KDiskFreeSpaceInfo::freeSpaceInfo(root);
                Space space;
                space.valid = info.isValid();
                space.size = info.size();
                space.available = info.available();
                return space;
            }));
        }
        return;
    }

    const KDiskFreeSpaceInfo info = KDiskFreeSpaceInfo::freeSpaceInfo(path);
    if (!info.isValid()) {
        _mountPoint = "";
        emit fileSystemInfoChanged(i18n("Space information unavailable"), "", 0, 0);
        return;
    }
    _mountPoint = info.mountPoint();

    const KMountPoint::Ptr mountPoint = KMountPoint::currentMountPoints().findByPath(path);
    const QString fsType = mountPoint ? mountPoint->mountType() : "";

    emit fileSystemInfoChanged("", fsType, info.size(), info.available());
}

// ==== protected ====

bool DefaultFileSystem::refreshInternal(const QUrl &directory, bool onlyScan)
{
    qDebug() << "refresh internal to URL=" << directory.toDisplayString();
    if (!KProtocolManager::supportsListing(directory)) {
        emit error(i18n("Protocol not supported by Panero:\n%1", directory.url()));
        return false;
    }

    delete _watcher; // stop watching the old dir

    if (directory.isLocalFile()) {
        qDebug() << "start local refresh to URL=" << directory.toDisplayString();
        // we could read local directories with KIO but using Qt is a lot faster!
        return refreshLocal(directory, onlyScan);
    }

    _currentDirectory = cleanUrl(directory);

    // start the listing job
    KIO::ListJob *job = KIO::listDir(_currentDirectory, KIO::HideProgressInfo, showHiddenFiles());
    connect(job, &KIO::ListJob::entries, this, &DefaultFileSystem::slotAddFiles);
    connect(job, &KIO::ListJob::redirection, this, &DefaultFileSystem::slotRedirection);
    connect(job, &KIO::ListJob::permanentRedirection, this, &DefaultFileSystem::slotRedirection);
    connect(job, &KIO::Job::result, this, &DefaultFileSystem::slotListResult);

    // ensure connection credentials are asked only once
    if(!parentWindow.isNull()) {
        auto *ui = dynamic_cast<KIO::JobUiDelegate*>(job->uiDelegate());
        ui->setWindow(parentWindow);
    }

    emit refreshJobStarted(job);

    _listError = false;
    // ugly: we have to wait here until the list job is finished
    QEventLoop eventLoop;
    connect(job, &KJob::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec(); // blocking until quit()

    return !_listError;
}

// ==== protected slots ====

void DefaultFileSystem::slotListResult(KJob *job)
{
    qDebug() << "got list result";
    if (job && job->error()) {
        // we failed to refresh
        _listError = true;
        qDebug() << "error=" << job->errorString() << "; text=" << job->errorText();
        emit error(job->errorString()); // display error message (in panel)
    }
}

void DefaultFileSystem::slotAddFiles(KIO::Job *, const KIO::UDSEntryList& entries)
{
    for (const KIO::UDSEntry& entry : entries) {
        FileItem *fileItem = FileSystem::createFileItemFromKIO(entry, _currentDirectory);
        if (fileItem) {
            addFileItem(fileItem);
        }
    }
}

void DefaultFileSystem::slotRedirection(KIO::Job *job, const QUrl &url)
{
   qDebug() << "redirection to URL=" << url.toDisplayString();

   // some protocols (zip, tar) send redirect to local URL without scheme
   const QUrl newUrl = preferLocalUrl(url);

   if (newUrl.scheme() != _currentDirectory.scheme()) {
       // abort and start over again,
       // some protocols (iso, zip, tar) do this on transition to local fs
       job->kill();
       _isRefreshing = false;
       refresh(newUrl);
       return;
   }

    _currentDirectory = cleanUrl(newUrl);
}

void DefaultFileSystem::slotWatcherCreated(const QString& path)
{
    qDebug() << "path created (doing nothing): " << path;
}

void DefaultFileSystem::slotWatcherDirty(const QString& path)
{
    qDebug() << "path dirty: " << path;
    if (path == realPath()) {
        // this happens
        //   1. if a directory was created/deleted/renamed inside this directory.
        //   2. during and after a file operation (create/delete/rename/touch) inside this directory
        // KDirWatcher doesn't reveal the name of changed directories and we have to refresh.
        // (QFileSystemWatcher in Qt5.7 can't help here either)
        refresh();
        return;
    }

    const QString name = QUrl::fromLocalFile(path).fileName();

    FileItem *fileItem = getFileItem(name);
    if (!fileItem) {
        qWarning() << "file not found (unexpected), path=" << path;
        // this happens at least for cifs mounted filesystems: when a new file is created, a dirty
        // signal with its file path but no other signals are sent (buggy behaviour of KDirWatch)
        refresh();
        return;
    }

    // we have an updated file..
    FileItem *newFileItem = createLocalFileItem(name);
    addFileItem(newFileItem);
    emit updatedFileItem(newFileItem);

    delete fileItem;
}

void DefaultFileSystem::slotWatcherDeleted(const QString& path)
{
    qDebug() << "path deleted: " << path;
    if (path != _currentDirectory.toLocalFile()) {
        // ignore deletion of files here, a 'dirty' signal will be send anyway
        return;
    }

    // the current directory was deleted. Try a refresh, which will fail. An error message will
    // be emitted and the empty (non-existing) directory remains.
    refresh();
}

bool DefaultFileSystem::refreshLocal(const QUrl &directory, bool onlyScan) {
#ifdef Q_OS_WIN
    QString path = KrServices::urlToLocalPath(directory);
#else
    const QString path = KrServices::urlToLocalPath(directory);
#endif

    if (PaneroSlowPaths::isSlowPath(path))
        return refreshSlow(directory, path, onlyScan);
    if (_slowTimer)
        _slowTimer->stop();

#ifdef Q_OS_WIN
    if (!path.contains("/")) { // change C: to C:/
        path = path + QString("/");
    }
#endif

    // check if the new directory exists
    if (!QDir(path).exists()) {
        emit error(i18n("The folder %1 does not exist.", path));
        return false;
    }

    // mount if needed
    emit aboutToOpenDir(path);

    // set the current directory...
    _currentDirectory = directory;
    _currentDirectory.setPath(QDir::cleanPath(_currentDirectory.path()));

#ifdef Q_OS_WIN
    // Windows: QDir gives Unicode names and knows the "hidden" and "system" attributes
    // (hidden like Explorer: both are listed only when hidden files are shown).
    {
        const bool showHidden = showHiddenFiles();
        QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
        if (showHidden)
            filters |= QDir::Hidden | QDir::System;
        const QDir qdir(path);
        if (!qdir.isReadable()) {
            emit error(i18nc("%1=folder path", "Access to %1 denied", path));
            return false;
        }
        const QSet<QString> hiddenFiles = filesInDotHidden(path);
        for (const QString &name : qdir.entryList(filters, QDir::NoSort)) {
            if (!showHidden && (name.startsWith(QLatin1Char('.')) || hiddenFiles.contains(name)))
                continue;
            addFileItem(FileSystem::createLocalFileItem(name, path));
        }
    }
#else
    // Note: we are using low-level Qt functions here.
    // It's around twice as fast as using the QDir class.

    QT_DIR* dir = QT_OPENDIR(path.toLocal8Bit());
    if (!dir) {
        emit error(i18n("Cannot open the folder %1.", path));
        return false;
    }

    // change directory to the new directory
    const QString savedDir = QDir::currentPath();
    if (!QDir::setCurrent(path)) {
        emit error(i18nc("%1=folder path", "Access to %1 denied", path));
        QT_CLOSEDIR(dir);
        return false;
    }

    QT_DIRENT* dirEnt;
    QString name;
    const bool showHidden = showHiddenFiles();
    QSet<QString> hiddenFiles = filesInDotHidden(path);
    while ((dirEnt = QT_READDIR(dir)) != nullptr) {
        name = QString::fromLocal8Bit(dirEnt->d_name);

        // show hidden files?
        if (!showHidden && name.left(1) == ".") continue;
        // show file in .hidden file?
        if (!showHidden && hiddenFiles.contains(name)) continue;
        // we don't need the "." and ".." entries
        if (name == "." || name == "..") continue;

        FileItem* temp = createLocalFileItem(name);
        addFileItem(temp);
    }
    // clean up
    QT_CLOSEDIR(dir);
    QDir::setCurrent(savedDir);
#endif

    if (!onlyScan)
        startWatching();

    return true;
}

void DefaultFileSystem::startWatching()
{
    {
        // start watching the new dir for file changes
        _watcher = new KDirWatch(this);
        // if the current dir is a link path the watcher needs to watch the real path - and signal
        // parameters will be the real path
        _watcher->addDir(realPath(), KDirWatch::WatchFiles);
        connect(_watcher.data(), &KDirWatch::dirty, this, &DefaultFileSystem::slotWatcherDirty);
        // NOTE: not connecting 'created' signal. A 'dirty' is send after that anyway
        //connect(_watcher.data(), &KDirWatch::created, this, &DefaultFileSystem::slotWatcherCreated);
        connect(_watcher.data(), &KDirWatch::deleted, this, &DefaultFileSystem::slotWatcherDeleted);
        _watcher->startScan(false);
    }
}

bool DefaultFileSystem::refreshSlow(const QUrl &directory, const QString &path, bool onlyScan)
{
    // mount if needed
    emit aboutToOpenDir(path);

    const bool enteringAnotherFolder = !_currentDirectory.matches(directory, QUrl::StripTrailingSlash);
    _currentDirectory = directory;
    _currentDirectory.setPath(QDir::cleanPath(_currentDirectory.path()));

    const bool showHidden = showHiddenFiles();
    // a folder visited before: its last listing at once (not when refreshing the
    // same folder, e.g. after Panero's own copy: that must show the new state)
    std::shared_ptr<SlowListing> listing = enteringAnotherFolder ? cachedListing(path, showHidden) : nullptr;
    if (listing)
        return showSlowListing(listing, path, onlyScan);
    listing = std::make_shared<SlowListing>();
    QFutureWatcher<void> watcher;
    auto *job = new PaneroListJob;
    QPointer<PaneroListJob> jobGuard(job);
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
    connect(job, &KJob::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([listing, path, showHidden]() {
        readSlowFolder(*listing, path, showHidden);
    }));
    // "Reading..." and the cancel button only when it takes a moment
    QTimer::singleShot(250, job, [this, job]() {
        if (!job->done && !job->killed)
            emit refreshJobStarted(job);
    });
    if (!watcher.isFinished())
        loop.exec(); // the GUI stays usable; a new navigation kills the job

    if (!jobGuard || jobGuard->killed) {
        // cancelled: the worker finishes on its own, its result is dropped
        return false;
    }
    job->finish(); // deletes itself
    rememberListing(path, showHidden, listing);
    return showSlowListing(listing, path, onlyScan);
}

bool DefaultFileSystem::showSlowListing(const std::shared_ptr<SlowListing> &listing, const QString &path, bool onlyScan)
{
    if (!listing->exists) {
        emit error(i18n("The folder %1 does not exist.", path));
        return false;
    }
    if (!listing->opened) {
        emit error(i18n("Cannot open the folder %1.", path));
        return false;
    }
    if (!listing->accessible) {
        emit error(i18nc("%1=folder path", "Access to %1 denied", path));
        return false;
    }

    QStringList subDirs;
    for (const FileSystem::LocalStat &entry : qAsConst(listing->entries)) {
        FileItem *item = createFileItemFromStat(entry, path);
        addFileItem(item);
        if (item && item->isDir())
            subDirs << path + QLatin1Char('/') + entry.name;
    }
    _slowSignature = listing->signature;

    if (!onlyScan) {
        // list the next level ahead so opening a subfolder does not wait for
        // the network
        PaneroSlowPaths::prefetchListings(subDirs);
        // no KDirWatch here: addDir(WatchFiles) resolves the path and stats
        // every file in the GUI thread, each waiting for the drive (a frozen
        // window while rclone is busy, e.g. opening a file for another app).
        // Panero's own file operations refresh through fileSystemChanged
        // (hasAutoUpdate() is false), other changes show up by the check below.
        // show what is there now, then ask the provider for changes
        QTimer::singleShot(0, this, &DefaultFileSystem::revalidateSlowFolder);
        if (!_slowTimer) {
            _slowTimer = new QTimer(this);
            _slowTimer->setInterval(30000);
            connect(_slowTimer, &QTimer::timeout, this, [this]() {
                if (QGuiApplication::applicationState() == Qt::ApplicationActive)
                    revalidateSlowFolder();
            });
        }
        _slowTimer->start();
    }
    return true;
}

void DefaultFileSystem::revalidateSlowFolder()
{
    if (_slowChecking || _isRefreshing || !_currentDirectory.isLocalFile())
        return;
    const QString path = _currentDirectory.toLocalFile();
    if (!PaneroSlowPaths::isSlowPath(path))
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (path == _revalidatedPath && now - _revalidatedAt < 20000) {
        checkSlowFolder(); // rclone may have noticed a change by polling
        return;
    }
    _slowChecking = true;
    const bool started = PaneroSlowPaths::revalidate(path, this, [this, path]() {
        _slowChecking = false;
        _revalidatedPath = path;
        _revalidatedAt = QDateTime::currentMSecsSinceEpoch();
        checkSlowFolder();
    });
    if (!started) {
        _slowChecking = false;
        _revalidatedPath = path;
        _revalidatedAt = now;
        checkSlowFolder();
    }
}

void DefaultFileSystem::checkSlowFolder()
{
    if (_slowChecking || _isRefreshing || !_currentDirectory.isLocalFile())
        return;
    const QString path = _currentDirectory.toLocalFile();
    if (!PaneroSlowPaths::isSlowPath(path))
        return;
    _slowChecking = true;
    auto listing = std::make_shared<SlowListing>();
    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, listing, path]() {
        watcher->deleteLater();
        _slowChecking = false;
        if (_isRefreshing || _currentDirectory.toLocalFile() != path || !listing->accessible)
            return;
        rememberListing(path, showHiddenFiles(), listing);
        if (listing->signature != _slowSignature)
            refresh();
    });
    const bool showHidden = showHiddenFiles();
    watcher->setFuture(QtConcurrent::run([listing, path, showHidden]() {
        readSlowFolder(*listing, path, showHidden);
    }));
}

QSet<QString> DefaultFileSystem::filesInDotHidden(const QString &dir)
{
    // code "borrowed" from KIO, Copyright (C) by Bruno Nova <brunomb.nova@gmail.com>
    const QString path = dir + QLatin1String("/.hidden");
    QFile dotHiddenFile(path);

    if (dotHiddenFile.exists()) {
      if (dotHiddenFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QSet<QString> filesToHide;
        QTextStream stream(&dotHiddenFile);
        while (!stream.atEnd()) {
          QString name = stream.readLine();
          if (!name.isEmpty()) {
            filesToHide.insert(name);
          }
        }
        return filesToHide;
      }
    }

    return QSet<QString>();
}

FileItem *DefaultFileSystem::createLocalFileItem(const QString &name)
{
    return FileSystem::createLocalFileItem(name, _currentDirectory.path());
}

QString DefaultFileSystem::realPath()
{
    // NOTE: current dir must exist
    return QDir(_currentDirectory.toLocalFile()).canonicalPath();
}

QUrl DefaultFileSystem::resolveRelativePath(const QUrl &url)
{
    // if e.g. "/tmp/bin" is a link to "/bin",
    // resolve "/tmp/bin/.." to "/tmp" and not "/"
    return url.adjusted(QUrl::NormalizePathSegments);
}
