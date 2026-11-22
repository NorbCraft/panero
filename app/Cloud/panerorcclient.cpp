/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerorcclient.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>

namespace
{
QByteArray randomHex(int bytes)
{
    QByteArray raw(bytes, Qt::Uninitialized);
    for (int i = 0; i < bytes; i += 4) {
        const quint32 v = QRandomGenerator::system()->generate();
        for (int j = 0; j < 4 && i + j < bytes; ++j)
            raw[i + j] = char((v >> (8 * j)) & 0xff);
    }
    return raw.toHex();
}

/// Decodes a "Transfer-Encoding: chunked" body; empty on malformed input.
QByteArray dechunk(const QByteArray &data, bool *ok)
{
    QByteArray out;
    int pos = 0;
    *ok = false;
    while (pos < data.size()) {
        const int eol = data.indexOf("\r\n", pos);
        if (eol < 0)
            return QByteArray();
        bool hexOk = false;
        const int size = data.mid(pos, eol - pos).split(';').first().trimmed().toInt(&hexOk, 16);
        if (!hexOk || size < 0)
            return QByteArray();
        pos = eol + 2;
        if (size == 0) {
            *ok = true;
            return out;
        }
        if (pos + size > data.size())
            return QByteArray();
        out += data.mid(pos, size);
        pos += size + 2;
    }
    return QByteArray();
}
} // namespace

namespace PaneroRc
{
Credentials newCredentials(const QString &socket)
{
    Credentials c;
    c.socket = socket;
    c.user = randomHex(16);
    c.password = randomHex(16);
    return c;
}

QByteArray htpasswdLine(const Credentials &credentials)
{
    // a SHA-1 of 128 random bits cannot be reversed or guessed
    return credentials.user + ":{SHA}" + QCryptographicHash::hash(credentials.password, QCryptographicHash::Sha1).toBase64() + '\n';
}

bool writeHtpasswd(const Credentials &credentials, const QString &path)
{
    QFile::remove(path);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return false;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray line = htpasswdLine(credentials);
    return f.write(line) == line.size();
}

QByteArray buildRequest(const Credentials &credentials, const QString &method, const QJsonObject &params)
{
    // method names are fixed strings in Panero ("vfs/refresh", ...)
    QByteArray path = method.toLatin1();
    path.replace(' ', "");
    path.replace('\r', "");
    path.replace('\n', "");
    const QByteArray body = QJsonDocument(params).toJson(QJsonDocument::Compact);
    QByteArray request;
    request += "POST /" + path + " HTTP/1.1\r\n";
    request += "Host: localhost\r\n";
    request += "Authorization: Basic " + (credentials.user + ':' + credentials.password).toBase64() + "\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    request += "Connection: close\r\n\r\n";
    request += body;
    return request;
}

bool parseResponse(const QByteArray &raw, int *status, QJsonObject *body)
{
    const int headerEnd = raw.indexOf("\r\n\r\n");
    if (headerEnd < 0 || !raw.startsWith("HTTP/1."))
        return false;
    const QList<QByteArray> lines = raw.left(headerEnd).split('\n');
    const QList<QByteArray> statusLine = lines.first().trimmed().split(' ');
    if (statusLine.count() < 2)
        return false;
    bool ok = false;
    const int code = statusLine.at(1).toInt(&ok);
    if (!ok)
        return false;
    bool chunked = false;
    for (int i = 1; i < lines.count(); ++i) {
        const QByteArray line = lines.at(i).trimmed().toLower();
        if (line.startsWith("transfer-encoding:") && line.contains("chunked"))
            chunked = true;
    }
    QByteArray payload = raw.mid(headerEnd + 4);
    if (chunked) {
        bool chunkOk = false;
        payload = dechunk(payload, &chunkOk);
        if (!chunkOk)
            return false;
    }
    if (status)
        *status = code;
    if (body)
        *body = QJsonDocument::fromJson(payload).object();
    return true;
}

void call(const Credentials &credentials, const QString &method, const QJsonObject &params, QObject *context,
          const std::function<void(int status, const QJsonObject &answer)> &done, int timeoutMs)
{
    QPointer<QObject> guard(context);
    auto *socket = new QLocalSocket;
    auto *buffer = new QByteArray;
    auto *timer = new QTimer(socket);
    timer->setSingleShot(true);
    auto finished = std::make_shared<bool>(false);
    auto finish = [socket, buffer, guard, done, finished](bool timedOut) {
        if (*finished)
            return;
        *finished = true;
        int status = 0;
        QJsonObject answer;
        if (!timedOut)
            parseResponse(*buffer, &status, &answer);
        delete buffer;
        socket->disconnect();
        socket->abort();
        socket->deleteLater();
        if (guard && done)
            done(status, answer);
    };
    QObject::connect(socket, &QLocalSocket::connected, socket, [socket, credentials, method, params]() {
        socket->write(buildRequest(credentials, method, params));
    });
    QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, buffer]() {
        *buffer += socket->readAll();
    });
    QObject::connect(socket, &QLocalSocket::disconnected, socket, [socket, buffer, finish]() {
        *buffer += socket->readAll();
        finish(false);
    });
    QObject::connect(socket, QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::errorOccurred), socket,
                     [socket, buffer, finish](QLocalSocket::LocalSocketError error) {
                         if (error == QLocalSocket::PeerClosedError) {
                             *buffer += socket->readAll();
                             finish(false);
                             return;
                         }
                         finish(true);
                     });
    QObject::connect(timer, &QTimer::timeout, socket, [finish]() {
        finish(true);
    });
    timer->start(timeoutMs);
    socket->connectToServer(credentials.socket);
}
} // namespace PaneroRc
