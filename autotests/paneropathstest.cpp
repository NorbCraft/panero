/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneropaths.h"

#include <QDir>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QTest>

class PaneroPathsTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(m_home.isValid());
        QDir(m_home.path()).mkpath(QStringLiteral("Documents/Invoices/2026"));
        qputenv("HOME", QFile::encodeName(m_home.path()));
    }

    void insideHomeIsHome()
    {
        // also inside folders the places list knows (Documents): upstream rooted there
        const QUrl home = QUrl::fromLocalFile(QDir::cleanPath(m_home.path()));
        QCOMPARE(PaneroPaths::homeOrDriveRoot(home), home);
        QCOMPARE(PaneroPaths::homeOrDriveRoot(QUrl::fromLocalFile(m_home.path() + QStringLiteral("/Documents"))), home);
        QCOMPARE(PaneroPaths::homeOrDriveRoot(QUrl::fromLocalFile(m_home.path() + QStringLiteral("/Documents/Invoices/2026/"))), home);
    }

    void outsideHomeIsDriveRoot()
    {
        const QString usr = QStringLiteral("/usr/share");
        QCOMPARE(PaneroPaths::homeOrDriveRoot(QUrl::fromLocalFile(usr)), QUrl::fromLocalFile(QStorageInfo(usr).rootPath()));
        // a sibling whose name starts like Home is not inside Home
        const QUrl sibling = QUrl::fromLocalFile(QDir::cleanPath(m_home.path()) + QStringLiteral("-other"));
        QVERIFY(PaneroPaths::homeOrDriveRoot(sibling) != QUrl::fromLocalFile(QDir::cleanPath(m_home.path())));
    }

    void remoteIsLocationRoot()
    {
        QCOMPARE(PaneroPaths::homeOrDriveRoot(QUrl(QStringLiteral("sftp://user@host/srv/data/x"))),
                 QUrl(QStringLiteral("sftp://user@host/")));
    }

private:
    QTemporaryDir m_home;
};

QTEST_GUILESS_MAIN(PaneroPathsTest)
#include "paneropathstest.moc"
