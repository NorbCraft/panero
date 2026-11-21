/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROCLOUDREVOKE_H
#define PANEROCLOUDREVOKE_H

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>

class QWidget;

/**
 * Signing out at the provider (dev/CLOUD-SECURITY-AUDIT.md R5): deleting a
 * remote locally does not invalidate its token, so Panero revokes it where the
 * provider has an API for that, and otherwise points to the provider's
 * "connected apps" page.
 *
 * - Google Drive: POST https://oauth2.googleapis.com/revoke (refresh token)
 * - Dropbox: POST https://api.dropboxapi.com/2/auth/token/revoke (Bearer)
 * - Box: POST https://api.box.com/oauth2/revoke (client id/secret + token)
 * - pCloud: GET https://<pCloud host>/logout (Bearer), host checked first
 * - OneDrive: no revocation API for user tokens -> account page
 * - Nextcloud/WebDAV: app passwords are revoked in Nextcloud -> its page
 */
namespace PaneroCloudRevoke
{
enum class Outcome {
    Revoked,   ///< the provider confirmed it
    Manual,    ///< no API: the user finishes it on manageUrl
    NotNeeded, ///< nothing that can be revoked (e.g. a local test remote)
    Failed     ///< the request failed; manageUrl is the fallback
};

struct Result {
    Outcome outcome = Outcome::Failed;
    QString message;  ///< for the user (never contains secrets)
    QUrl manageUrl;   ///< provider page where access can be checked/removed
};

struct Request {
    QUrl url;
    QByteArray method; ///< "GET" or "POST"
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;
};

/// The provider page listing connected apps (or app passwords) for @p type.
QUrl manageUrl(const QString &type, const QJsonObject &remoteConfig);

/**
 * Builds the revocation request for a remote from its `rclone config dump`
 * object. False (with *why) when the provider has no API or no token is there.
 * @p boxClientSecret is rclone's Box client secret (revealed), used when the
 * remote has no client_secret of its own.
 */
bool buildRequest(const QString &type, const QJsonObject &remoteConfig, const QString &boxClientSecret, Request *request,
                  QString *why = nullptr);

/// Checks the provider's answer to a revocation request.
bool responseConfirmsRevocation(const QString &type, int httpStatus, const QByteArray &body);

/// Reads the remote's token through rclone, sends the request and wipes the
/// token. Blocking (a few seconds at most).
Result revoke(const QString &remote, const QString &type);

/// The request that asks the provider which account a token belongs to
/// (false when there is none for @p type).
bool buildIdentityRequest(const QString &type, const QJsonObject &remoteConfig, Request *request);
/// "Name <e-mail>" (or what the provider reports) from the identity answer.
QString parseIdentity(const QString &type, const QByteArray &body);
/// Asks the provider which account the remote is signed in to, so the user can
/// confirm it is their own (an attacker's account slipped into a sign-in
/// would receive the uploads). Empty with *error on failure.
QString accountIdentity(const QString &remote, const QString &type, QString *error = nullptr);

/// OneDrive: rclone's setup takes the first drive Microsoft lists; make sure
/// the remote uses the account's own main drive (/me/drive) and correct it if
/// not. True when the remote is (now) on the main drive.
bool ensureOneDriveMainDrive(const QString &remote, QString *error = nullptr);
/// Main drive id and type from the answer of GET /me/drive.
bool parseMainDrive(const QByteArray &body, QString *driveId, QString *driveType);
}

#endif // PANEROCLOUDREVOKE_H
