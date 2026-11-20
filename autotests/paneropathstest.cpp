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
#ifdef Q_OS_WIN
        // Windows takes Home from the user token (HOME is ignored); only path logic is tested
        m_homePath = QDir::homePath();
#else
        QVERIFY(m_home.isValid());
        QDir(m_home.path()).mkpath(QStringLiteral("Documents/Invoices/2026"));
        qputenv("HOME", QFile::encodeName(m_home.path()));
        m_homePath = m_home.path();
#endif
    }

    void insideHomeIsHome()
    {
        // also inside folders the places list knows (Documents): upstream rooted there
        const QUrl home = QUrl::fromLocalFile(QDir::cleanPath(m_homePath));
        QCOMPARE(PaneroPaths::homeOrDriveRoot(home), home);
        QCOMPARE(PaneroPaths::homeOrDriveRoot(QUrl::fromLocalFile(m_homePath + QStringLiteral("/Documents"))), home);
        QCOMPARE(PaneroPaths::homeOrDriveRoot(QUrl::fromLocalFile(m_homePath + QStringLiteral("/Documents/Invoices/2026/"))), home);
    }

    void outsideHomeIsDriveRoot()
    {
#ifdef Q_OS_WIN
        const QString usr = QDir::rootPath() + QStringLiteral("Windows/System32");
#else
        const QString usr = QStringLiteral("/usr/share");
#endif
        QCOMPARE(PaneroPaths::homeOrDriveRoot(QUrl::fromLocalFile(usr)), QUrl::fromLocalFile(QStorageInfo(usr).rootPath()));
        // a sibling whose name starts like Home is not inside Home
        const QUrl sibling = QUrl::fromLocalFile(QDir::cleanPath(m_homePath) + QStringLiteral("-other"));
        QVERIFY(PaneroPaths::homeOrDriveRoot(sibling) != QUrl::fromLocalFile(QDir::cleanPath(m_homePath)));
    }

    void emptyOrRelativeIsHome()
    {
        // a new tab reports an empty location first ("Malformed URL /." before)
        const QUrl home = QUrl::fromLocalFile(QDir::cleanPath(m_homePath));
        QCOMPARE(PaneroPaths::homeOrDriveRoot(QUrl()), home);
        QCOMPARE(PaneroPaths::homeOrDriveRoot(QUrl(QStringLiteral("."))), home);
    }

    void remoteIsLocationRoot()
    {
        QCOMPARE(PaneroPaths::homeOrDriveRoot(QUrl(QStringLiteral("sftp://user@host/srv/data/x"))),
                 QUrl(QStringLiteral("sftp://user@host/")));
    }

private:
    QTemporaryDir m_home;
    QString m_homePath;
};

QTEST_GUILESS_MAIN(PaneroPathsTest)
#include "paneropathstest.moc"
