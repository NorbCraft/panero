/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANERORCCLIENT_H
#define PANERORCCLIENT_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <functional>

class QObject;

/**
 * Remote control of a running `rclone mount` (rclone's rc API), used to read
 * a drive's whole folder tree in the background and to refresh single folders
 * (vfs/refresh) without flushing everything.
 *
 * Security: the rc server of a mount listens only on a unix socket inside the
 * 0700 mount base folder ($XDG_RUNTIME_DIR/panero-cloud/.control), never on
 * TCP, and requires a random user name and password per mount. rclone gets
 * only a hash of them (--rc-htpasswd, a 0600 file next to the socket); the
 * password itself stays in Panero's memory - it is in no command line,
 * environment or file.
 */
namespace PaneroRc
{
struct Credentials {
    QString socket;
    QByteArray user;
    QByteArray password;
    bool isValid() const
    {
        return !socket.isEmpty() && !user.isEmpty() && !password.isEmpty();
    }
};

/// Random credentials for a new mount (hex, 128 bits each).
Credentials newCredentials(const QString &socket);
/// The htpasswd line rclone checks the credentials against ({SHA} hash).
QByteArray htpasswdLine(const Credentials &credentials);
/// Writes it to @p path (mode 0600). False on failure.
bool writeHtpasswd(const Credentials &credentials, const QString &path);

/// The HTTP request for @p method with the JSON @p params.
QByteArray buildRequest(const Credentials &credentials, const QString &method, const QJsonObject &params);
/// Splits a complete HTTP response; false when it is not one.
bool parseResponse(const QByteArray &raw, int *status, QJsonObject *body);

/// Calls @p method in the background; @p done runs in the GUI thread with the
/// HTTP status (0 when the socket could not be used) and the JSON answer,
/// unless @p context was deleted.
void call(const Credentials &credentials, const QString &method, const QJsonObject &params, QObject *context,
          const std::function<void(int status, const QJsonObject &answer)> &done, int timeoutMs = 300000);
}

#endif // PANERORCCLIENT_H
