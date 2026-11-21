/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROCLOUDSETUP_H
#define PANEROCLOUDSETUP_H

#include "panerocloudutils.h"

#include <QString>
#include <QStringList>

/**
 * Creating and importing cloud remotes without exposing secrets
 * (dev/CLOUD-SECURITY-AUDIT.md R3, R4, R7, R8). No widgets: used by
 * PaneroAddCloudDialog and by the tests.
 */
namespace PaneroCloudSetup
{
/// rclone's OAuth helper port (fixed by rclone).
constexpr quint16 CALLBACK_PORT = 53682;

/// True when nothing listens on 127.0.0.1:53682 or [::1]:53682, so the
/// sign-in code can only reach rclone's own helper.
bool callbackPortFree(QString *why = nullptr);

/// Extra `rclone config create` arguments for an OAuth provider: scopes
/// (Drive: drive or drive.readonly; OneDrive: own files, optionally shared
/// files, no SharePoint) - never secrets.
QStringList oauthOptions(const QString &type, bool readOnly, bool includeShared);

/// Plain-language description of the access the provider grants.
QString accessDescription(const QString &type, bool readOnly, bool includeShared);

/// Why @p password cannot be used (empty when it can): rclone reads it as
/// one trimmed line, so newlines and surrounding spaces are refused.
QString passwordProblem(const QString &password);

/// Creates a WebDAV remote; the password goes to rclone through stdin only
/// and is checked afterwards (read back and compared). Deletes the remote on
/// failure.
bool createWebDavRemote(const QString &remote, const QString &url, const QString &vendor, const QString &user, const QString &password,
                        QString *error);

/// Full WebDAV address for the vendor ("https://" added, Nextcloud/ownCloud paths).
QString webDavAddress(const QString &address, const QString &vendor, const QString &user);

/// After a pCloud sign-in: the stored API host must be a pCloud host.
bool checkPcloudHost(const QString &remote, QString *error);

/// Lists one folder level to prove the sign-in works.
bool verifyRemote(const QString &remote, QString *error);

/// Deletes a remote from Panero's configuration (no revocation).
void deleteRemote(const QString &remote);

/// Remotes in the user's own rclone configuration (for import).
QList<PaneroCloudUtils::Remote> listUserRemotes(QString *error);

/**
 * Imports remotes from the user's own (plain) rclone configuration into
 * Panero's encrypted one. The combined configuration is assembled in a private
 * folder in $XDG_RUNTIME_DIR (memory), encrypted there by rclone and then moved
 * into place, so no plain copy is written to disk and no secret is passed in
 * an argument. Clouds must be disconnected first. Names that already exist get
 * a number. Optionally deletes the imported remotes from the old file.
 */
bool importRemotes(const QStringList &names, bool deleteFromSource, QStringList *imported, QString *error);

/// The command that opens rclone's interactive setup on Panero's encrypted
/// configuration, for services without a guided setup.
QStringList interactiveConfigCommand();
}

#endif // PANEROCLOUDSETUP_H
