/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROOFFLINE_H
#define PANEROOFFLINE_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include <KConfigCore/KSharedConfig>

class QProcess;
class QTimer;
class QWidget;
class QFileSystemWatcher;

/**
 * Offline files for cloud drives, in the style of OneDrive's "Files On-Demand"
 * (dev/CLOUD-SECURITY-AUDIT.md R10):
 *
 * - Online-only: listed through the mount, downloaded only when opened.
 * - Available on this device: files opened once stay in the remote's VFS
 *   cache (when "Keep opened files" is on), within a size limit.
 * - Always keep on this device: a pinned folder is kept as a local copy in
 *   ~/Cloud Offline/<remote>/<path>, synchronised both ways with
 *   `rclone bisync`; it works offline, and Panero opens the local copy when
 *   the pinned folder is opened inside the cloud drive.
 *
 * Per-remote settings and pins are stored in panerorc,
 * group "[Panero Cloud Offline <remote>]".
 */
class PaneroOffline : public QObject
{
    Q_OBJECT
public:
    struct Settings {
        bool keepOpened = true;      ///< keep opened files in the cache (30 days)
        int cacheLimitGB = 10;       ///< VFS cache size limit
        bool removeOnSignOut = true; ///< delete local copies when the cloud is removed
    };
    enum class State { NotCloud, OnlineOnly, Available, Pinned };
    struct PinStatus {
        QDateTime lastSync;
        QString error;
        bool syncing = false;
        int conflicts = 0;
    };

    static PaneroOffline *instance();
    void setConfig(KSharedConfigPtr config);

    Settings settings(const QString &remote) const;
    void setSettings(const QString &remote, const Settings &settings);
    /// rclone mount options for the remote's cache, from its settings.
    QStringList mountCacheArgs(const QString &remote) const;

    QStringList pins(const QString &remote) const;
    /// ~/Cloud Offline/<remote>
    static QString mirrorRoot(const QString &remote);
    static QString mirrorPath(const QString &remote, const QString &relativePath);
    /// True when @p relativePath is a pinned folder or inside one.
    bool isPinned(const QString &remote, const QString &relativePath) const;
    /// The pinned folder that contains @p relativePath (or empty).
    QString pinFor(const QString &remote, const QString &relativePath) const;

    /// Badge state of a local path (inside a mount or a local copy).
    State state(const QString &localPath, qint64 size, bool isDir) const;
    /// A path inside a pinned folder of a mounted cloud -> its local copy
    /// (when the copy exists); any other path is returned unchanged.
    QString redirect(const QString &localPath);
    /// Paints the state badge onto @p icon.
    static void decorate(QPixmap &icon, State state);

    /// Pins a folder and starts its first synchronisation.
    bool pin(const QString &remote, const QString &relativePath, QString *error = nullptr);
    /// Final synchronisation, then the local copy is deleted.
    bool unpin(QWidget *parent, const QString &remote, const QString &relativePath);
    /// Queues a synchronisation of one pin (or all pins of the remote).
    void syncNow(const QString &remote, const QString &relativePath = QString());
    /// Synchronises all pins of the remote now and waits (sign-out).
    bool syncAllBlocking(const QString &remote, QString *error);
    /// Removes pins, settings and (per setting) the local copies of a remote.
    void forgetRemote(const QString &remote, bool deleteLocalCopies);
    PinStatus status(const QString &remote, const QString &relativePath) const;
    bool isBusy(const QString &remote) const;

    /// The remote's folder inside its VFS cache (default: the remote name).
    void setCacheSubPath(const QString &remote, const QString &subPath);

    /// Remote and path inside it for a local path in a mount or local copy.
    bool cloudPath(const QString &localPath, QString *remote, QString *relativePath, bool *inMirror = nullptr) const;

signals:
    void changed(const QString &remote);

private:
    explicit PaneroOffline(QObject *parent = nullptr);
    struct Job {
        QString remote;
        QString pin;
        bool resync = false;
    };
    KConfigGroup group(const QString &remote) const;
    void setPins(const QString &remote, const QStringList &pins);
    void startNext();
    void jobFinished(int exitCode, const QString &stderrText);
    QStringList bisyncArgs(const Job &job) const;
    void periodicSync();
    void watchMirrors();
    static QString key(const QString &remote, const QString &pin);
    bool deleteMirror(const QString &remote, const QString &relativePath, QString *error);

    KSharedConfigPtr m_config;
    QList<Job> m_queue;
    QProcess *m_process = nullptr;
    Job m_current;
    QHash<QString, PinStatus> m_status;
    QTimer *m_timer = nullptr;
    QTimer *m_changeTimer = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    QStringList m_changedKeys;
    QHash<QString, QString> m_cacheSub;
};

#endif // PANEROOFFLINE_H
