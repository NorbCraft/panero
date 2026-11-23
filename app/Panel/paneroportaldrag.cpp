/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroportaldrag.h"

#include <QMimeData>

#ifndef Q_OS_WIN
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QFile>
#include <QVariantMap>

#include <fcntl.h>
#include <unistd.h>

namespace
{
const QString Service = QStringLiteral("org.freedesktop.portal.Documents");
const QString Path = QStringLiteral("/org/freedesktop/portal/documents");
const QString Interface = QStringLiteral("org.freedesktop.portal.FileTransfer");
} // namespace

QString PaneroPortalDrag::addFiles(QMimeData *mimeData, const QList<QUrl> &urls)
{
    QStringList paths;
    for (const QUrl &url : urls) {
        if (url.isLocalFile())
            paths << url.toLocalFile();
    }
    if (paths.isEmpty() || !mimeData)
        return QString();
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface()->isServiceRegistered(Service))
        return QString();

    // read-only; the transfer ends once the receiver has taken the files
    QDBusMessage start = QDBusMessage::createMethodCall(Service, Path, Interface, QStringLiteral("StartTransfer"));
    start << QVariantMap{{QStringLiteral("writable"), false}, {QStringLiteral("autostop"), true}};
    const QDBusReply<QString> key = bus.call(start, QDBus::Block, 2000);
    if (!key.isValid() || key.value().isEmpty())
        return QString();

    // O_PATH: a handle to the file without opening its contents (no download
    // from a cloud drive; the receiver reads it only if it uses the file)
    qDBusRegisterMetaType<QList<QDBusUnixFileDescriptor>>();
    QList<QDBusUnixFileDescriptor> fds;
    QList<int> raw;
    for (const QString &path : paths) {
        const int fd = ::open(QFile::encodeName(path).constData(), O_PATH | O_CLOEXEC);
        if (fd < 0)
            continue;
        raw << fd;
        fds << QDBusUnixFileDescriptor(fd); // dup()s the descriptor
    }
    for (int fd : raw)
        ::close(fd);
    if (fds.isEmpty())
        return QString();
    QDBusMessage add = QDBusMessage::createMethodCall(Service, Path, Interface, QStringLiteral("AddFiles"));
    add << key.value() << QVariant::fromValue(fds) << QVariantMap();
    const QDBusMessage added = bus.call(add, QDBus::Block, 5000);
    if (added.type() == QDBusMessage::ErrorMessage)
        return QString();

    mimeData->setData(QString::fromLatin1(MimeType), key.value().toUtf8());
    return key.value();
}
#else
QString PaneroPortalDrag::addFiles(QMimeData *, const QList<QUrl> &)
{
    return QString(); // Windows: every application can read the dragged paths
}
#endif
