/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROCLOUD_H
#define PANEROCLOUD_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <KConfigCore/KSharedConfig>

class QFileSystemWatcher;
class QProcess;
class QTimer;
class QWidget;

/**
 * Cloud drives (rclone remotes, mounted with `rclone mount`) and server
 * connections (KIO URLs: WebDAV, SFTP, SMB, FTP) for the sidebar "Cloud"
 * section (D-014 C2 + C3).
 *
 * - rclone remotes come from the user's normal rclone configuration
 *   ($RCLONE_CONFIG or ~/.config/rclone/rclone.conf), so remotes created with
 *   rclone elsewhere show up as well.
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

    /// Use another config (tests). Default: KSharedConfig::openConfig() (= krConfig in the app).
    void setConfig(KSharedConfigPtr config);

    // ---- rclone availability --------------------------------------------
    /// rclone is found in PATH.
    static bool rcloneAvailable();
    static QString rcloneProgram();
    /// fusermount3 or fusermount is found (needed by `rclone mount`).
    static bool fuseAvailable();
    /// Makes sure rclone (and fuse3) are installed, offering to install the
    /// missing packages via PackageKit. True when everything is available.
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
    /// Asks "Remove <label>?" and removes the entry. True when removed.
    bool confirmAndRemove(QWidget *parent, const Entry &entry);

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
        QStringList stderrTail;
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
};

Q_DECLARE_METATYPE(PaneroCloud::Entry)

#endif // PANEROCLOUD_H
