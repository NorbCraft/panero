/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROCLOUDUTILS_H
#define PANEROCLOUDUTILS_H

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>

class QJsonObject;

class KConfigGroup;

/**
 * Pure helpers of the cloud/server backend (no processes, no widgets), kept
 * separate so they can be unit tested.
 */
namespace PaneroCloudUtils
{
/// One rclone remote as listed by `rclone listremotes --long`.
struct Remote {
    QString name;
    QString type;
};

/// A server connection kept in Panero's configuration (never with a password).
struct Server {
    QUrl url;
    QString label;
};

/// Parses the output of `rclone listremotes --long` ("name:   type" per line;
/// names may contain spaces but never ':').
QList<Remote> parseListRemotes(const QString &output);

/// Turns a display name ("Google Drive") into a valid rclone remote name
/// ("GoogleDrive"): only A-Z a-z 0-9 _ - . are kept, no leading '-' or '.'.
/// Returns "Cloud" when nothing usable is left.
QString sanitizeRemoteName(const QString &label);

/// sanitizeRemoteName(), made unique against @p existing by appending 2, 3, ...
QString uniqueRemoteName(const QString &label, const QStringList &existing);

/// True when @p name is acceptable to rclone 1.60 as a new remote name
/// (the characters sanitizeRemoteName() keeps, plus inner spaces).
bool isValidRemoteName(const QString &name);

/**
 * Builds a KIO URL for a server connection. @p host may also be pasted as a
 * full URL ("https://cloud.example.com/remote.php/dav"); its host, port, path
 * and user are then taken over unless given explicitly. @p port <= 0 means the
 * protocol default. The folder gets a leading '/'. A password in the input is
 * dropped. Returns an invalid QUrl when no host is given.
 */
QUrl buildServerUrl(const QString &scheme, const QString &host, int port = 0, const QString &folder = QString(),
                    const QString &user = QString());

/// Default label of a server connection: "user@host" or "host" (plus folder name).
QString defaultServerLabel(const QUrl &url);

/// Reads/writes the server list (keys "Servers" and "Labels" of @p group, the
/// same layout as the "[Panero Pins]" group). Passwords are stripped on write.
QList<Server> readServers(const KConfigGroup &group);
void writeServers(KConfigGroup &group, const QList<Server> &servers);

/// Friendly labels of rclone remotes (keys "Remote Names"/"Remote Labels").
QString remoteLabel(const KConfigGroup &group, const QString &remote);
void setRemoteLabel(KConfigGroup &group, const QString &remote, const QString &label);

/// Mount points listed in /proc/self/mountinfo content (octal escapes decoded).
QStringList parseMountInfo(const QByteArray &mountinfo);

/// Finds the sign-in link in rclone's OAuth output
/// ("... go to the following link: http://127.0.0.1:53682/auth?state=...").
/// Only rclone's own local helper link is accepted (R4).
QUrl parseAuthUrl(const QString &output);

/// "1.76.0" from the output of `rclone version` ("rclone v1.76.0").
QString parseRcloneVersion(const QString &output);
/// <0, 0, >0 like strcmp, numerically per dot-separated part.
int compareVersions(const QString &a, const QString &b);
/// True when @p head (the start of an rclone config file) is an encrypted config.
bool isEncryptedRcloneConfig(const QByteArray &head);
/// A remote name as a single folder name: percent-encoded except A-Z a-z 0-9
/// - _ . ~, and a leading '.' encoded too, so different names never share a
/// folder and no name becomes "." or "..".
QString safeFileName(const QString &name);

/// Number of files in the remote's VFS cache that rclone has not uploaded yet
/// ("Dirty": true in <cache>/vfsMeta). Such a cache must not be deleted.
int dirtyCacheEntries(const QString &remoteCacheDir);

/// Replaces tokens, passwords, OAuth codes and bearer values in @p text by
/// "***" (rclone messages, before they are shown anywhere).
QString redactSecrets(const QString &text);

/// rclone's pCloud host rule (v1.76): one or more labels, then ".pcloud.com".
bool isAllowedPcloudHost(const QString &host);

/// Value of "key = value" in INI-like text (`rclone config redacted`), or empty.
QString iniValue(const QString &text, const QString &key);

/// rclone config file text for remotes given as the JSON object of
/// `rclone config dump` ({"name": {"type": ..., ...}}). Values are written
/// verbatim (already obscured where rclone obscures them).
QByteArray rcloneIniFromDump(const QJsonObject &remotes);

/// OneDrive access_scopes for Panero's choices (own files by default).
QString oneDriveScopes(bool readOnly, bool includeShared);

/// Path of the rclone VFS cache file for @p relativePath of @p remote below
/// the remote's cache folder (rclone 1.76 layout: <cache>/vfs/<remote>/<path>).
QString vfsCacheFile(const QString &remoteCacheDir, const QString &remote, const QString &relativePath);
/// Path of rclone's metadata file for a cache file (<cache>/vfsMeta/<remote>/<path>).
QString vfsMetaFile(const QString &remoteCacheDir, const QString &remote, const QString &relativePath);
/// True when rclone's metadata file marks the cache file as not uploaded yet.
bool isDirtyMeta(const QString &metaFile);

/// The VFS cache subfolder of an "alias" remote: rclone names the cache after
/// the backend the alias points to ("other:sub" -> "other/sub", "/data" ->
/// "local/data").
QString aliasCacheSubPath(const QString &aliasTarget);
/// True when @p cacheFile exists and has no holes up to @p size (fully downloaded).
bool isFullyCached(const QString &cacheFile, qint64 size);

/// One rclone log line without its "<5>" level / date prefix and "ERROR : " tag.
QString cleanRcloneLogLine(const QString &line);

/// The error part of rclone's stderr for showing to the user: NOTICE/INFO
/// lines and the usage text dropped, duplicates removed, last @p maxLines kept.
QString rcloneErrorSummary(const QString &stderrText, int maxLines = 3);

/// Directory where rclone remotes get mounted:
/// $XDG_RUNTIME_DIR/panero-cloud, else ~/.cache/panero/mounts.
QString mountBaseDir();

/// The user's own rclone config file (only read for "Import"): $RCLONE_CONFIG,
/// else ~/.rclone.conf if it exists, else $XDG_CONFIG_HOME/rclone/rclone.conf.
/// Panero's own configuration is PaneroRclone::configPath().
QString userRcloneConfigPath();
}

#endif // PANEROCLOUDUTILS_H
