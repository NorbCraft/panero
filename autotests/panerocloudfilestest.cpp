/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Windows cloud folders (D-020): which items must never be read (that would
// download a OneDrive placeholder), the badge from the Cloud Files attributes,
// Explorer's pin actions and how the sync folders are found. Pure logic, so it
// runs on Linux too; the real placeholders are tested by dev/windows/gui-test.py.

#include "panerocloudfiles.h"

#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QTest>

using namespace PaneroCloudFiles;

class PaneroCloudFilesTest : public QObject
{
    Q_OBJECT
private slots:
    void placeholders_data()
    {
        QTest::addColumn<quint32>("attributes");
        QTest::addColumn<bool>("placeholder");
        // what OneDrive shows in Explorer: online-only file (cloud icon)
        QTest::newRow("online-only") << (AttrRecallOnDataAccess | AttrUnpinned | AttrReparsePoint | 0x20u) << true;
        QTest::newRow("offline") << (AttrOffline | AttrReparsePoint) << true;
        QTest::newRow("pinned, download pending") << (AttrRecallOnDataAccess | AttrPinned) << true;
        // downloaded: reading is safe
        QTest::newRow("available") << (AttrReparsePoint | 0x20u) << false;
        QTest::newRow("always kept") << (AttrPinned | AttrReparsePoint) << false;
        QTest::newRow("folder recalled on open") << (AttrDirectory | AttrRecallOnOpen) << false;
        QTest::newRow("unknown") << InvalidAttributes << false;
    }
    void placeholders()
    {
        QFETCH(quint32, attributes);
        QFETCH(bool, placeholder);
        QCOMPARE(isPlaceholder(attributes), placeholder);
    }

    void states_data()
    {
        QTest::addColumn<quint32>("attributes");
        QTest::addColumn<bool>("isDir");
        QTest::addColumn<int>("state");
        QTest::newRow("file online-only") << (AttrRecallOnDataAccess | AttrUnpinned) << false << int(State::OnlineOnly);
        QTest::newRow("file offline") << AttrOffline << false << int(State::OnlineOnly);
        QTest::newRow("file available") << 0x20u << false << int(State::Available);
        QTest::newRow("file freed later") << AttrUnpinned << false << int(State::Available);
        QTest::newRow("file always kept") << (AttrPinned | 0x20u) << false << int(State::Pinned);
        QTest::newRow("file kept, downloading") << (AttrPinned | AttrRecallOnDataAccess) << false << int(State::OnlineOnly);
        QTest::newRow("folder available") << AttrDirectory << true << int(State::Available);
        QTest::newRow("folder always kept") << (AttrDirectory | AttrPinned) << true << int(State::Pinned);
        QTest::newRow("folder freed up") << (AttrDirectory | AttrUnpinned) << true << int(State::OnlineOnly);
        QTest::newRow("folder not listed yet") << (AttrDirectory | AttrRecallOnOpen) << true << int(State::OnlineOnly);
        QTest::newRow("unknown") << InvalidAttributes << false << int(State::NotCloud);
    }
    void states()
    {
        QFETCH(quint32, attributes);
        QFETCH(bool, isDir);
        QFETCH(int, state);
        QCOMPARE(int(stateFromAttributes(attributes, isDir)), state);
    }

    void pinActions()
    {
        const quint32 online = AttrRecallOnDataAccess | AttrUnpinned | AttrReparsePoint;
        // "Always keep on this device": PINNED set, UNPINNED cleared, the rest kept
        QCOMPARE(withPinState(online, Pin::AlwaysKeep), AttrRecallOnDataAccess | AttrPinned | AttrReparsePoint);
        // "Free up space": UNPINNED set, PINNED cleared
        QCOMPARE(withPinState(AttrPinned | AttrReadOnly, Pin::FreeUp), AttrUnpinned | AttrReadOnly);
        // unchecking "Always keep": neither
        QCOMPARE(withPinState(AttrPinned | AttrHidden, Pin::Clear), AttrHidden);
        QCOMPARE(withPinState(0, Pin::Clear), 0u);
    }

    void providerOfflineItems()
    {
        // the provider's own items in the Explorer menu: Panero then adds none
        QVERIFY(isProviderOfflineItem(QStringLiteral("Always keep on this device"), QString()));
        QVERIFY(isProviderOfflineItem(QStringLiteral("&Free up space"), QString()));
        QVERIFY(isProviderOfflineItem(QStringLiteral("Make available offline\tCtrl+O"), QString()));
        QVERIFY(isProviderOfflineItem(QStringLiteral("Make online-only..."), QString()));
        QVERIFY(isProviderOfflineItem(QStringLiteral("Mindig ezen az eszközön"), QStringLiteral("FreeUpSpace")));
        // other shell items, including the "pin to" verbs
        QVERIFY(!isProviderOfflineItem(QStringLiteral("Pin to Start"), QStringLiteral("pintostartscreen")));
        QVERIFY(!isProviderOfflineItem(QStringLiteral("Pin to Quick access"), QStringLiteral("pintohome")));
        QVERIFY(!isProviderOfflineItem(QStringLiteral("Free up space on disk C:"), QString()));
        QVERIFY(!isProviderOfflineItem(QStringLiteral("Properties"), QStringLiteral("properties")));
        QVERIFY(!isProviderOfflineItem(QString(), QString()));
    }

    void paths()
    {
        QCOMPARE(normalizePath(QStringLiteral("c:\\Users\\me\\OneDrive\\")), QStringLiteral("C:/Users/me/OneDrive"));
        QCOMPARE(normalizePath(QStringLiteral("G")), QStringLiteral("G:/"));
        QCOMPARE(normalizePath(QStringLiteral("g:")), QStringLiteral("G:/"));
        QCOMPARE(normalizePath(QStringLiteral("G:\\")), QStringLiteral("G:/"));
        const QString root = QStringLiteral("C:\\Users\\me\\OneDrive");
        QVERIFY(isInside(QStringLiteral("C:/Users/me/OneDrive"), root));
        QVERIFY(isInside(QStringLiteral("c:/users/ME/onedrive/Documents/a.txt"), root));
        QVERIFY(!isInside(QStringLiteral("C:/Users/me/OneDrive2/a.txt"), root));
        QVERIFY(!isInside(QStringLiteral("C:/Users/me"), root));
        QVERIFY(isInside(QStringLiteral("G:/My Drive/x.pdf"), QStringLiteral("G:/")));
        QVERIFY(!isInside(QStringLiteral("C:/x"), QString()));
    }

    void providerIds()
    {
        QCOMPARE(providerFromId(QStringLiteral("OneDrive!S-1-5-21-1-2-3-1001!Personal|ABC")), QStringLiteral("OneDrive"));
        QCOMPARE(providerFromId(QStringLiteral("GoogleDrive!S-1-5-21-1!x")), QStringLiteral("GoogleDrive"));
        QCOMPARE(providerFromId(QStringLiteral("Plain")), QStringLiteral("Plain"));
    }

    void googleDrive()
    {
        const QByteArray json = R"J({"per_account_preferences":[{"key":"111","value":{"mount_point_path":"G"}},
                                    {"key":"222","value":{"mount_point_path":"H:","other":1}}]})J";
        QCOMPARE(parseGoogleDrivePreferences(json), (QStringList{QStringLiteral("G"), QStringLiteral("H:")}));
        QVERIFY(parseGoogleDrivePreferences("not json").isEmpty());
        QVERIFY(parseGoogleDrivePreferences("{}").isEmpty());
    }

    void dropbox()
    {
        const QByteArray json = R"J({"personal":{"path":"C:\\Users\\me\\Dropbox","host":1},
                                    "business":{"path":"C:\\Users\\me\\Dropbox (Contoso)","is_team":true}})J";
        const QList<SyncRoot> roots = parseDropboxInfo(json);
        QCOMPARE(roots.size(), 2);
        QCOMPARE(roots[0].path, QStringLiteral("C:/Users/me/Dropbox"));
        QCOMPARE(roots[0].name, QStringLiteral("Dropbox"));
        QCOMPARE(roots[1].name, QStringLiteral("Dropbox (Contoso)"));
        QVERIFY(parseDropboxInfo("[]").isEmpty());
    }

    void merge()
    {
        const QString me = QStringLiteral("S-1-5-21-1-2-3-1001");
        RegistryRoot personal;
        personal.id = QStringLiteral("OneDrive!") + me + QStringLiteral("!Personal|ABC");
        personal.displayName = QStringLiteral("OneDrive - Personal");
        personal.userRoots.insert(me, QStringLiteral("C:\\Users\\me\\OneDrive"));
        personal.userRoots.insert(QStringLiteral("S-1-5-21-9"), QStringLiteral("C:\\Users\\other\\OneDrive"));
        RegistryRoot work; // no display name: "OneDrive - <organisation>" is the folder name
        work.id = QStringLiteral("OneDrive!") + me + QStringLiteral("!Business1|XYZ");
        work.userRoots.insert(me, QStringLiteral("C:\\Users\\me\\OneDrive - Contoso"));
        RegistryRoot test;
        test.id = QStringLiteral("PaneroTest!") + me + QStringLiteral("!x");
        test.displayName = QStringLiteral("Panero Test Cloud");
        test.userRoots.insert(me, QStringLiteral("C:\\Users\\me\\PaneroTestCloud"));
        QHash<QString, QString> env;
        env.insert(QStringLiteral("OneDrive"), QStringLiteral("C:\\Users\\me\\OneDrive")); // the same folder again
        env.insert(QStringLiteral("OneDriveConsumer"), QStringLiteral("c:\\users\\me\\onedrive\\"));
        const QList<SyncRoot> roots = mergeRoots({personal, work, test}, me, env, {QStringLiteral("G")},
                                                 parseDropboxInfo(R"J({"personal":{"path":"C:\\Users\\me\\Dropbox"}})J"));
        QStringList names, paths;
        for (const SyncRoot &r : roots) {
            names << r.name;
            paths << r.path;
        }
        QCOMPARE(names, (QStringList{QStringLiteral("Dropbox"), QStringLiteral("Google Drive"), QStringLiteral("OneDrive - Contoso"),
                                     QStringLiteral("OneDrive - Personal"), QStringLiteral("Panero Test Cloud")}));
        QVERIFY(!paths.contains(QStringLiteral("C:/Users/other/OneDrive"))); // another user's folder
        QCOMPARE(roots[1].path, QStringLiteral("G:/"));
        QVERIFY(roots[1].virtualDrive);
        QVERIFY(!roots[3].virtualDrive);
        QCOMPARE(roots[3].provider, QStringLiteral("OneDrive"));
        QCOMPARE(roots[4].provider, QStringLiteral("PaneroTest"));

        // only the environment (OneDrive not registered yet)
        QHash<QString, QString> onlyEnv;
        onlyEnv.insert(QStringLiteral("OneDrive"), QStringLiteral("D:\\OneDrive"));
        const QList<SyncRoot> envRoots = mergeRoots({}, me, onlyEnv, {}, {});
        QCOMPARE(envRoots.size(), 1);
        QCOMPARE(envRoots[0].name, QStringLiteral("OneDrive"));
        QCOMPARE(envRoots[0].path, QStringLiteral("D:/OneDrive"));
    }

    void badges()
    {
        auto drawn = [](State state) {
            QPixmap pm(32, 32);
            pm.fill(Qt::transparent);
            decorate(pm, state);
            const QImage img = pm.toImage();
            int painted = 0, green = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x) {
                    const QRgb px = img.pixel(x, y);
                    painted += qAlpha(px) > 0;
                    green += qAlpha(px) > 128 && qGreen(px) > qRed(px) + 40 && qGreen(px) > qBlue(px) + 40;
                }
            return QPair<int, int>(painted, green);
        };
        QCOMPARE(drawn(State::NotCloud).first, 0);
        QVERIFY(drawn(State::OnlineOnly).first > 0);
        QCOMPARE(drawn(State::OnlineOnly).second, 0); // a blue cloud
        QVERIFY(drawn(State::Available).second > 0);  // green outline check
        QVERIFY(drawn(State::Pinned).second * 2 > 3 * drawn(State::Available).second); // filled green check
    }

#ifndef Q_OS_WIN
    void linuxHasNoSyncFolders()
    {
        // Linux behaviour does not change: nothing is a Windows sync folder
        QVERIFY(syncRoots().isEmpty());
        QVERIFY(!avoidReading(QStringLiteral("/home/me/OneDrive/a.txt")));
        QCOMPARE(int(state(QStringLiteral("/home/me/OneDrive/a.txt"), false)), int(State::NotCloud));
        QCOMPARE(attributes(QStringLiteral("/tmp")), InvalidAttributes);
        QCOMPARE(setPinState({QStringLiteral("/tmp")}, Pin::AlwaysKeep), 0);
    }
#endif
};

QTEST_MAIN(PaneroCloudFilesTest)
#include "panerocloudfilestest.moc"
