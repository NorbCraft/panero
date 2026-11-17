/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroimport.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <KConfigCore/KConfig>
#include <KConfigCore/KConfigGroup>

class PaneroImportTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    PaneroImport::Paths m_paths;

    static void writeFile(const QString &path, const QByteArray &content)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(content);
    }
    static QByteArray hashOf(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return QByteArray();
        return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
    }

private Q_SLOTS:
    void init()
    {
        QVERIFY(m_dir.isValid());
        const QString base = m_dir.path() + QStringLiteral("/case-") + QString::number(qrand());
        m_paths.krusaderConfig = base + QStringLiteral("/config/krusaderrc");
        m_paths.paneroConfig = base + QStringLiteral("/config/panerorc");
        m_paths.krusaderData = base + QStringLiteral("/data/krusader");
        m_paths.paneroData = base + QStringLiteral("/data/panero");
        m_paths.pendingFlag = base + QStringLiteral("/config/panero-import-krusader.pending");
    }

    void noKrusaderConfig()
    {
        QStringList copied;
        QString error;
        QVERIFY(!PaneroImport::krusaderSettingsExist(m_paths));
        QVERIFY(!PaneroImport::importKrusaderSettings(m_paths, &copied, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(m_paths.paneroConfig));
    }

    void importCopiesAndKeepsOriginals()
    {
        writeFile(m_paths.krusaderConfig, "[Look&Feel]\nMouse Selection=2\n");
        writeFile(m_paths.krusaderData + QStringLiteral("/krbookmarks.xml"), "<xbel/>\n");
        writeFile(m_paths.krusaderData + QStringLiteral("/sub dir/useractions.xml"), "<KrusaderUserActions/>\n");
        const QByteArray cfgHash = hashOf(m_paths.krusaderConfig);
        const QByteArray bmHash = hashOf(m_paths.krusaderData + QStringLiteral("/krbookmarks.xml"));

        QStringList copied;
        QString error;
        QVERIFY2(PaneroImport::importKrusaderSettings(m_paths, &copied, &error), qPrintable(error));
        QCOMPARE(copied.size(), 3);

        // originals untouched
        QCOMPARE(hashOf(m_paths.krusaderConfig), cfgHash);
        QCOMPARE(hashOf(m_paths.krusaderData + QStringLiteral("/krbookmarks.xml")), bmHash);

        // copies present, config marked as imported
        KConfig imported(m_paths.paneroConfig, KConfig::SimpleConfig);
        QCOMPARE(KConfigGroup(&imported, "Look&Feel").readEntry("Mouse Selection", 0), 2);
        QVERIFY(!KConfigGroup(&imported, "Panero").readEntry("Imported From Krusader", QString()).isEmpty());
        QVERIFY(QFile::exists(m_paths.paneroData + QStringLiteral("/krbookmarks.xml")));
        QVERIFY(QFile::exists(m_paths.paneroData + QStringLiteral("/sub dir/useractions.xml")));
    }

    void existingPaneroFilesAreBackedUp()
    {
        writeFile(m_paths.krusaderConfig, "[A]\nx=1\n");
        writeFile(m_paths.paneroConfig, "[B]\ny=2\n");
        QString error;
        QVERIFY2(PaneroImport::importKrusaderSettings(m_paths, nullptr, &error), qPrintable(error));
        const QStringList backups = QDir(QFileInfo(m_paths.paneroConfig).absolutePath())
            .entryList({QStringLiteral("panerorc.before-krusader-import-*")}, QDir::Files);
        QCOMPARE(backups.size(), 1);
        KConfig backup(QFileInfo(m_paths.paneroConfig).absolutePath() + QLatin1Char('/') + backups.first(),
                       KConfig::SimpleConfig);
        QCOMPARE(KConfigGroup(&backup, "B").readEntry("y", 0), 2);
    }

    void pendingImportRunsOnceAndRemovesFlag()
    {
        writeFile(m_paths.krusaderConfig, "[A]\nx=1\n");
        QString error;
        QVERIFY(!PaneroImport::runPendingImport(m_paths, &error)); // nothing scheduled
        QVERIFY(PaneroImport::schedulePendingImport(m_paths));
        QVERIFY(QFile::exists(m_paths.pendingFlag));
        QVERIFY2(PaneroImport::runPendingImport(m_paths, &error), qPrintable(error));
        QVERIFY(!QFile::exists(m_paths.pendingFlag));
        QVERIFY(QFile::exists(m_paths.paneroConfig));
    }
};

QTEST_GUILESS_MAIN(PaneroImportTest)
#include "paneroimporttest.moc"
