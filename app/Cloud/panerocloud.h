/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROCLOUD_H
#define PANEROCLOUD_H

#include <QHash>
#include <QIcon>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <KConfigCore/KSharedConfig>

#include "panerorcclient.h"

#include <functional>

class KFilePlacesModel;
class QFileSystemWatcher;
class QProcess;
class QTimer;
class QWidget;

/**
 * Cloud drives (rclone remotes, mounted with `rclone mount`) and server
 * connections (KIO URLs: WebDAV, SFTP, SMB, FTP) for the sidebar "Cloud"
 * section (D-014 C2 + C3).
 *
 * - rclone remotes live in Panero's own encrypted rclone configuration
 *   (PaneroRclone, dev/CLOUD-SECURITY-AUDIT.md); the user's own rclone setup
 *   is never read or changed (except by an explicit import).
 * - Server connections are stored in panerorc, group [Panero Cloud]
 *   (Servers / Labels). Passwords are never stored by Panero: KIO asks and can
 *   keep them in KWallet.
 *
 * Usage: PaneroCloud::instance()->entries(); call unmountAll() on shutdown.
 */
class PaneroCloud : public QObject
{
    Q_OBJECT
public:
    struct Entry {
        QString id;            ///< "rclone:<remote>" or "server:<url>"
        QString label;         ///< display name
        QString type;          ///< "rclone" or "server"
        QString rcloneRemote;  ///< rclone: remote name (without ':')
        QString rcloneType;    ///< rclone: backend ("drive", "onedrive", "webdav", ...)
        QUrl url;              ///< server: KIO url; rclone: local mount url while mounted
        bool isRclone() const { return type == QLatin1String("rclone"); }
        bool isServer() const { return type == QLatin1String("server"); }
    };

    static PaneroCloud *instance();

    /// Icon of a cloud service by rclone backend type ("drive", "onedrive", ...).
    static QIcon iconForType(const QString &type);

    /// Use another config (tests). Default: KSharedConfig::openConfig() (= krConfig in the app).
    void setConfig(KSharedConfigPtr config);

    // ---- rclone availability --------------------------------------------
    /// Panero's rclone is installed and recent enough.
    static bool rcloneAvailable();
    static QString rcloneProgram();
    /// fusermount3 or fusermount is found (needed by `rclone mount`).
    static bool fuseAvailable();
    /// Makes sure Panero's rclone, fuse3 and the encrypted configuration
    /// (key in the keyring) are ready, offering to install fuse3 via
    /// PackageKit. Shows what is wrong otherwise. True when ready.
    bool ensureRclone(QWidget *parent);

    // ---- entries ----------------------------------------------------------
    /// Cached entries: servers (config order), then rclone remotes (sorted).
    QList<Entry> entries() const;
    Entry entry(const QString &id) const;
    /// Re-reads remotes asynchronously (`rclone listremotes --long`); emits entriesChanged().
    void refresh();
    /// Same, but blocks (up to @p timeoutMs). Returns false when rclone failed.
    bool refreshNow(int timeoutMs = 5000);
    /// Names of the configured rclone remotes (from the last refresh).
    QStringList remoteNames() const;

    /// Adds (or relabels, if the URL is already there) a server connection.
    Entry addServer(const QUrl &url, const QString &label);
    /// Sets the display label of an entry (servers and rclone remotes).
    void setLabel(const Entry &entry, const QString &label);
    /// Servers: removed from the config. rclone: unmounted and deleted with
    /// `rclone config delete` (the caller confirms first, see confirmAndRemove()).
    bool removeEntry(const Entry &entry, QString *error = nullptr);
    /// Asks "Remove <label>?", then signs out: local copies are synchronised
    /// (and deleted per setting), the token is revoked at the provider where
    /// possible, the remote and its cache are deleted. True when removed.
    bool confirmAndRemove(QWidget *parent, const Entry &entry);
    /// Signs out of every cloud drive (one confirmation for all).
    void signOutAll(QWidget *parent);

    /// rclone process id of a mounted remote (0 when not mounted).
    qint64 mountPid(const QString &remote) const;

    /// Google Drive: native Google Docs/Sheets/Slides have no size in a
    /// mount (rclone exports them on download, see rclone's "Limitations of
    /// Google Docs") and cannot be opened from it. True when @p localPath may
    /// be one (0 bytes, export extension, on a Google Drive mount).
    bool mayBeGoogleDocument(const QString &localPath, qint64 size) const;
    /// Opens a native Google document in the browser (Google's editor).
    /// False when @p localPath is not one (open it normally then).
    bool openGoogleDocument(QWidget *parent, const QString &localPath, qint64 size);
    /// Saves an exported copy (.docx/.xlsx/.pptx/...) of a native Google
    /// document into @p destDir.
    bool saveGoogleDocumentCopy(QWidget *parent, const QString &localPath, const QString &destDir);

    /// Makes the rclone mount holding @p localPath forget its folder listings.
    void flushDirectoryCache(const QString &localPath);
    /// Reads the folder @p localPath of a mounted drive again from the
    /// provider (in the background); @p done runs afterwards in the GUI thread
    /// unless @p context was deleted. False when no mount holds the folder.
    bool refreshFolder(const QString &localPath, QObject *context, const std::function<void()> &done);
    /// True while a mounted drive is still reading its whole folder tree.
    bool isReadingTree(const QString &id) const;
    /// True when a mounted drive read its whole folder tree successfully.
    bool hasTree(const QString &id) const;

    /// Shows mounted drives as places (OnlyInApp=Panero) so the path bar
    /// starts with the drive's name and not with the mount folder. Places of
    /// an earlier session are removed. Off by default (tests).
    void setPlacesEnabled(bool enabled);

    /// Mount folders of the mounted remotes (remote name -> folder).
    QHash<QString, QString> mountDirs() const;

    // ---- opening ----------------------------------------------------------
    /// Server: its KIO url. rclone: the local mount url if mounted, else an invalid url
    /// (call mount() and wait for mounted()).
    QUrl urlFor(const Entry &entry, QWidget *parent = nullptr) const;

    /// Mounts an rclone remote below mountBaseDir(); emits mounted() or mountFailed().
    void mount(const Entry &entry);
    bool isMounted(const QString &id) const;
    bool isMounting(const QString &id) const;
    QString mountPath(const QString &id) const;
    void unmount(const QString &id);
    /// Unmounts everything Panero mounted (blocking, a few seconds at most). Call on shutdown.
    void unmountAll();

    /// Timeout of the mount detection (default 15 s).
    void setMountTimeout(int ms) { m_mountTimeoutMs = ms; }

signals:
    void entriesChanged();
    void mounted(const QString &id, const QUrl &localUrl);
    void mountFailed(const QString &id, const QString &message);
    void unmounted(const QString &id);

private:
    explicit PaneroCloud(QObject *parent = nullptr);
    ~PaneroCloud() override;

    struct Mount {
        QProcess *process = nullptr;
        QTimer *poll = nullptr;
        QString dir;
        qint64 started = 0;
        bool mounted = false;
        bool stopping = false;
        bool readingTree = false;
        bool treeRead = false;
        QStringList stderrTail;
        PaneroRc::Credentials rc;
    };

    void rebuildEntries();
    void parseRemotes(const QString &output);
    void watchConfigFile();
    void cancelAsyncRefresh();
    void pollMount(const QString &id);
    void mountProcessFinished(const QString &id, int exitCode);
    void finishMount(const QString &id);
    static bool isMountPoint(const QString &dir);
    static bool runFusermountUnmount(const QString &dir, bool lazy);
    KConfigGroup group() const;
    /// Reads the whole folder tree of a new mount in the background.
    void readTree(const QString &id);
    void addPlace(const QString &id);
    void removePlace(const QString &dir);
    void updatePlaces();

    KSharedConfigPtr m_config;
    QList<Entry> m_entries;
    QStringList m_remoteNames;
    QHash<QString, QString> m_remoteTypes;
    QProcess *m_listProcess = nullptr;
    bool m_refreshPending = false;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QHash<QString, Mount *> m_mounts;
    int m_mountTimeoutMs = 15000;
    KFilePlacesModel *m_places = nullptr;
};

Q_DECLARE_METATYPE(PaneroCloud::Entry)

#endif // PANEROCLOUD_H
