/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Dragged files are registered with the document portal (D-022). Needs a
// session bus with xdg-document-portal; skipped without one (CI).

#include "paneroportaldrag.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QFileInfo>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>

class PaneroPortalDragTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void localFilesGetAPortalKey()
    {
#ifdef Q_OS_WIN
        QSKIP("no portal on Windows");
#else
        if (!QDBusConnection::sessionBus().isConnected()
            || !QDBusConnection::sessionBus().interface()->isServiceRegistered(QStringLiteral("org.freedesktop.portal.Documents")))
            QSKIP("no document portal on this session bus");
        QTemporaryDir dir;
        const QString a = dir.filePath(QStringLiteral("scan one.pdf"));
        const QString b = dir.filePath(QStringLiteral("notes.txt"));
        for (const QString &p : {a, b}) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
        QMimeData mime;
        const QString key = PaneroPortalDrag::addFiles(&mime, {QUrl::fromLocalFile(a), QUrl::fromLocalFile(b),
                                                               QUrl(QStringLiteral("sftp://host/remote.txt"))});
        QVERIFY(!key.isEmpty());
        QCOMPARE(QString::fromUtf8(mime.data(QString::fromLatin1(PaneroPortalDrag::MimeType))), key);
        // an unsandboxed receiver gets the original paths back
        QDBusMessage get = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Documents"),
                                                          QStringLiteral("/org/freedesktop/portal/documents"),
                                                          QStringLiteral("org.freedesktop.portal.FileTransfer"),
                                                          QStringLiteral("RetrieveFiles"));
        get << key << QVariantMap();
        const QDBusReply<QStringList> files = QDBusConnection::sessionBus().call(get);
        QVERIFY2(files.isValid(), qPrintable(files.error().message()));
        QStringList got;
        for (const QString &f : files.value())
            got << QFileInfo(f).fileName();
        got.sort();
        QCOMPARE(got, (QStringList{QStringLiteral("notes.txt"), QStringLiteral("scan one.pdf")}));
#endif
    }

    void nothingForRemoteOnly()
    {
        QMimeData mime;
        QVERIFY(PaneroPortalDrag::addFiles(&mime, {QUrl(QStringLiteral("sftp://host/x"))}).isEmpty());
        QVERIFY(!mime.hasFormat(QString::fromLatin1(PaneroPortalDrag::MimeType)));
    }
};

QTEST_GUILESS_MAIN(PaneroPortalDragTest)
#include "paneroportaldragtest.moc"
