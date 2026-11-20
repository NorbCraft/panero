/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerocloud.h"
#include "panerocloudutils.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <KConfigCore/KConfigGroup>
#include <KConfigCore/KSharedConfig>

using namespace PaneroCloudUtils;

class PaneroCloudTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void parseListRemotes_data();
    void parseListRemotes();
    void sanitizeRemoteName_data();
    void sanitizeRemoteName();
    void uniqueRemoteName();
    void buildServerUrl_data();
    void buildServerUrl();
    void serverPersistence();
    void remoteLabels();
    void cloudServerEntries();
    void parseMountInfo();
    void parseAuthUrl();
    void rcloneErrorSummary();
    void mountBaseDir();
    void rcloneMountRoundTrip();
    void rcloneMountFailure();
    void rcloneRemove();
    void holdMountForCrashCheck();

private:
    bool runRclone(const QStringList &args);
    QTemporaryDir m_tmp;
    bool m_haveRclone = false;
};

void PaneroCloudTest::initTestCase()
{
    QVERIFY(m_tmp.isValid());
    // never touch the real HOME / config / rclone config
    const QString root = m_tmp.path();
    for (const char *d : {"home", "config", "cache", "data", "run"})
        QDir().mkpath(root + QLatin1Char('/') + QLatin1String(d));
    QFile::setPermissions(root + QStringLiteral("/run"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("HOME", QFile::encodeName(root + QStringLiteral("/home")));
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(root + QStringLiteral("/config")));
    qputenv("XDG_CACHE_HOME", QFile::encodeName(root + QStringLiteral("/cache")));
    qputenv("XDG_DATA_HOME", QFile::encodeName(root + QStringLiteral("/data")));
    qputenv("XDG_RUNTIME_DIR", QFile::encodeName(root + QStringLiteral("/run")));
    qputenv("RCLONE_CONFIG", QFile::encodeName(root + QStringLiteral("/rclone.conf")));
    m_haveRclone = PaneroCloud::rcloneAvailable();
    if (!m_haveRclone)
        qWarning("rclone not found in PATH: the mount tests are skipped");
}

void PaneroCloudTest::cleanupTestCase()
{
    // before the temporary directory goes away
    PaneroCloud::instance()->unmountAll();
}

bool PaneroCloudTest::runRclone(const QStringList &args)
{
    QProcess p;
    p.start(PaneroCloud::rcloneProgram(), args);
    return p.waitForFinished(15000) && p.exitCode() == 0;
}

void PaneroCloudTest::parseListRemotes_data()
{
    QTest::addColumn<QString>("output");
    QTest::addColumn<QStringList>("names");
    QTest::addColumn<QStringList>("types");
    QTest::newRow("empty") << QString() << QStringList() << QStringList();
    QTest::newRow("long") << QStringLiteral("My Disk: local\nmem1:    memory\nwd:      webdav\n")
                          << QStringList{QStringLiteral("My Disk"), QStringLiteral("mem1"), QStringLiteral("wd")}
                          << QStringList{QStringLiteral("local"), QStringLiteral("memory"), QStringLiteral("webdav")};
    QTest::newRow("plain") << QStringLiteral("gdrive:\n\n  onedrive:\n") << QStringList{QStringLiteral("gdrive"), QStringLiteral("onedrive")}
                           << QStringList{QString(), QString()};
    QTest::newRow("garbage") << QStringLiteral(":nothing\nno colon\n") << QStringList() << QStringList();
}

void PaneroCloudTest::parseListRemotes()
{
    QFETCH(QString, output);
    QFETCH(QStringList, names);
    QFETCH(QStringList, types);
    const auto remotes = PaneroCloudUtils::parseListRemotes(output);
    QStringList gotNames, gotTypes;
    for (const auto &r : remotes) {
        gotNames << r.name;
        gotTypes << r.type;
    }
    QCOMPARE(gotNames, names);
    QCOMPARE(gotTypes, types);
}

void PaneroCloudTest::sanitizeRemoteName_data()
{
    QTest::addColumn<QString>("label");
    QTest::addColumn<QString>("name");
    QTest::newRow("spaces") << QStringLiteral("Google Drive") << QStringLiteral("GoogleDrive");
    QTest::newRow("slash") << QStringLiteral("Nextcloud / WebDAV") << QStringLiteral("NextcloudWebDAV");
    QTest::newRow("accents") << QStringLiteral("Résumé Drive") << QStringLiteral("ResumeDrive");
    QTest::newRow("leading") << QStringLiteral("-.my-drive.1") << QStringLiteral("my-drive.1");
    QTest::newRow("nothing") << QStringLiteral("!!! ???") << QStringLiteral("Cloud");
    QTest::newRow("colon") << QStringLiteral("a:b") << QStringLiteral("ab");
}

void PaneroCloudTest::sanitizeRemoteName()
{
    QFETCH(QString, label);
    QFETCH(QString, name);
    QCOMPARE(PaneroCloudUtils::sanitizeRemoteName(label), name);
    QVERIFY(PaneroCloudUtils::isValidRemoteName(name));
}

void PaneroCloudTest::uniqueRemoteName()
{
    const QStringList existing{QStringLiteral("GoogleDrive"), QStringLiteral("GoogleDrive2")};
    QCOMPARE(PaneroCloudUtils::uniqueRemoteName(QStringLiteral("Google Drive"), existing), QStringLiteral("GoogleDrive3"));
    QCOMPARE(PaneroCloudUtils::uniqueRemoteName(QStringLiteral("OneDrive"), existing), QStringLiteral("OneDrive"));
    QVERIFY(!PaneroCloudUtils::isValidRemoteName(QStringLiteral("bad-name!")));
    QVERIFY(!PaneroCloudUtils::isValidRemoteName(QStringLiteral("-x")));
    QVERIFY(PaneroCloudUtils::isValidRemoteName(QStringLiteral("My Disk")));
}

void PaneroCloudTest::buildServerUrl_data()
{
    QTest::addColumn<QString>("scheme");
    QTest::addColumn<QString>("host");
    QTest::addColumn<int>("port");
    QTest::addColumn<QString>("folder");
    QTest::addColumn<QString>("user");
    QTest::addColumn<QString>("expected");
    QTest::newRow("sftp") << "sftp" << "nas.local" << 0 << "" << "" << "sftp://nas.local/";
    QTest::newRow("sftp-port-user") << "sftp" << " nas.local " << 2222 << "home/me" << "me" << "sftp://me@nas.local:2222/home/me";
    QTest::newRow("smb-share") << "smb" << "192.168.1.10" << 0 << "/Public/Photos" << "" << "smb://192.168.1.10/Public/Photos";
    QTest::newRow("host-port-path") << "ftp" << "ftp.example.com:2121/pub" << 0 << "" << "" << "ftp://ftp.example.com:2121/pub";
    QTest::newRow("user-at-host") << "sftp" << "bob@server" << 0 << "" << "" << "sftp://bob@server/";
    QTest::newRow("pasted-url") << "webdavs" << "https://cloud.example.com/remote.php/dav/files/u/" << 0 << "" << ""
                                << "webdavs://cloud.example.com/remote.php/dav/files/u/";
    QTest::newRow("pasted-password-dropped") << "webdavs" << "https://u:secret@cloud.example.com:8443/dav" << 0 << "" << ""
                                             << "webdavs://u@cloud.example.com:8443/dav";
    QTest::newRow("ipv6") << "sftp" << "[fe80::1]:22" << 0 << "" << "" << "sftp://[fe80::1]:22/";
    QTest::newRow("folder-overrides") << "webdav" << "host/a" << 0 << "b" << "" << "webdav://host/b";
    QTest::newRow("empty") << "sftp" << "  " << 0 << "" << "" << "";
}

void PaneroCloudTest::buildServerUrl()
{
    QFETCH(QString, scheme);
    QFETCH(QString, host);
    QFETCH(int, port);
    QFETCH(QString, folder);
    QFETCH(QString, user);
    QFETCH(QString, expected);
    const QUrl url = PaneroCloudUtils::buildServerUrl(scheme, host, port, folder, user);
    QCOMPARE(url.toString(), expected);
    QVERIFY(url.password().isEmpty());
}

void PaneroCloudTest::serverPersistence()
{
    const QString file = m_tmp.path() + QStringLiteral("/persist-rc");
    {
        KSharedConfigPtr cfg = KSharedConfig::openConfig(file, KConfig::SimpleConfig);
        KConfigGroup g(cfg, "Panero Cloud");
        QUrl withPass(QStringLiteral("sftp://me:hunter2@nas.local/home"));
        writeServers(g, {{withPass, QStringLiteral("NAS")}, {QUrl(QStringLiteral("smb://box/share")), QString()}});
    }
    QFile f(file);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray content = f.readAll();
    QVERIFY(!content.contains("hunter2"));
    QVERIFY(content.contains("[Panero Cloud]"));

    KSharedConfigPtr cfg = KSharedConfig::openConfig(file, KConfig::SimpleConfig);
    const auto servers = readServers(KConfigGroup(cfg, "Panero Cloud"));
    QCOMPARE(servers.count(), 2);
    QCOMPARE(servers.at(0).url.toString(), QStringLiteral("sftp://me@nas.local/home"));
    QCOMPARE(servers.at(0).label, QStringLiteral("NAS"));
    QCOMPARE(servers.at(1).label, QStringLiteral("box (share)")); // default label
}

void PaneroCloudTest::remoteLabels()
{
    KSharedConfigPtr cfg = KSharedConfig::openConfig(m_tmp.path() + QStringLiteral("/labels-rc"), KConfig::SimpleConfig);
    KConfigGroup g(cfg, "Panero Cloud");
    QCOMPARE(remoteLabel(g, QStringLiteral("GoogleDrive")), QStringLiteral("GoogleDrive"));
    setRemoteLabel(g, QStringLiteral("GoogleDrive"), QStringLiteral("Google Drive"));
    setRemoteLabel(g, QStringLiteral("OneDrive"), QStringLiteral("Work"));
    QCOMPARE(remoteLabel(g, QStringLiteral("GoogleDrive")), QStringLiteral("Google Drive"));
    setRemoteLabel(g, QStringLiteral("GoogleDrive"), QString());
    QCOMPARE(remoteLabel(g, QStringLiteral("GoogleDrive")), QStringLiteral("GoogleDrive"));
    QCOMPARE(remoteLabel(g, QStringLiteral("OneDrive")), QStringLiteral("Work"));
}

void PaneroCloudTest::cloudServerEntries()
{
    PaneroCloud *cloud = PaneroCloud::instance();
    cloud->setConfig(KSharedConfig::openConfig(m_tmp.path() + QStringLiteral("/cloud-rc"), KConfig::SimpleConfig));
    QSignalSpy changed(cloud, &PaneroCloud::entriesChanged);
    const PaneroCloud::Entry e = cloud->addServer(QUrl(QStringLiteral("webdavs://dav.example.com/files")), QStringLiteral("Files"));
    QVERIFY(changed.count() >= 1);
    QVERIFY(e.isServer());
    QCOMPARE(e.label, QStringLiteral("Files"));
    QCOMPARE(cloud->urlFor(e), QUrl(QStringLiteral("webdavs://dav.example.com/files")));
    // same url again: relabel, no duplicate
    cloud->addServer(QUrl(QStringLiteral("webdavs://dav.example.com/files")), QStringLiteral("My Files"));
    int servers = 0;
    for (const auto &x : cloud->entries())
        servers += x.isServer() ? 1 : 0;
    QCOMPARE(servers, 1);
    QCOMPARE(cloud->entry(e.id).label, QStringLiteral("My Files"));
    QVERIFY(cloud->removeEntry(cloud->entry(e.id)));
    QVERIFY(cloud->entry(e.id).id.isEmpty());
}

void PaneroCloudTest::parseMountInfo()
{
    const QByteArray info = "22 1 8:2 / / rw,relatime shared:1 - ext4 /dev/sda2 rw\n"
                            "90 22 0:50 / /run/user/1000/panero-cloud/My\\040Disk rw,nosuid,nodev - fuse.rclone My\\040Disk: rw\n";
    const QStringList points = PaneroCloudUtils::parseMountInfo(info);
    QCOMPARE(points, (QStringList{QStringLiteral("/"), QStringLiteral("/run/user/1000/panero-cloud/My Disk")}));
}

void PaneroCloudTest::parseAuthUrl()
{
    // verbatim rclone 1.60.1 output of `rclone config create gd drive` (stderr, not a tty)
    const QString out = QStringLiteral(
        "<5>NOTICE: Config file \"/tmp/x/rclone.conf\" not found - using defaults\n"
        "<5>NOTICE: If your browser doesn't open automatically go to the following link: "
        "http://127.0.0.1:53682/auth?state=-cUpCJDmlVNS6jQ6R2YFmA\n"
        "<5>NOTICE: Log in and authorize rclone for access\n"
        "<5>NOTICE: Waiting for code...\n");
    QCOMPARE(PaneroCloudUtils::parseAuthUrl(out), QUrl(QStringLiteral("http://127.0.0.1:53682/auth?state=-cUpCJDmlVNS6jQ6R2YFmA")));
    QVERIFY(!PaneroCloudUtils::parseAuthUrl(QStringLiteral("nothing here")).isValid());
}

void PaneroCloudTest::rcloneErrorSummary()
{
    // verbatim rclone 1.60.1 stderr of a failed `rclone lsd` (dates) and of `config create` (levels)
    const QString lsd = QStringLiteral(
        "2026/11/20 19:57:32 ERROR : : error listing: couldn't list files: Propfind \"http://127.0.0.1:9/x/\": dial tcp 127.0.0.1:9: connect: connection refused\n"
        "2026/11/20 19:57:32 Failed to lsd with 2 errors: last error was: couldn't list files: connection refused\n");
    QCOMPARE(PaneroCloudUtils::rcloneErrorSummary(lsd, 1), QStringLiteral("Failed to lsd with 2 errors: last error was: couldn't list files: connection refused"));
    QVERIFY(PaneroCloudUtils::rcloneErrorSummary(lsd, 3).startsWith(QStringLiteral("error listing: couldn't list files")));
    const QString create = QStringLiteral(
        "<5>NOTICE: Config file \"/tmp/x\" not found - using defaults\n"
        "Error: config name contains invalid characters\nUsage:\n  rclone config create name type\n"
        "<3>ERROR : something broke\n");
    QCOMPARE(PaneroCloudUtils::rcloneErrorSummary(create), QStringLiteral("Error: config name contains invalid characters"));
    QCOMPARE(PaneroCloudUtils::rcloneErrorSummary(QStringLiteral("<5>NOTICE: Waiting for code...\n")), QString());
    QCOMPARE(PaneroCloudUtils::cleanRcloneLogLine(QStringLiteral("<3>ERROR : boom")), QStringLiteral("boom"));
}

void PaneroCloudTest::mountBaseDir()
{
    QCOMPARE(PaneroCloudUtils::mountBaseDir(), m_tmp.path() + QStringLiteral("/run/panero-cloud"));
    QCOMPARE(PaneroCloudUtils::rcloneConfigPath(), m_tmp.path() + QStringLiteral("/rclone.conf"));
}

void PaneroCloudTest::rcloneMountRoundTrip()
{
    if (!m_haveRclone)
        QSKIP("rclone not installed");
    if (!PaneroCloud::fuseAvailable() || !QFileInfo(QStringLiteral("/dev/fuse")).isWritable())
        QSKIP("FUSE not usable here");

    // an "alias" remote pointing at a local folder stands in for a cloud drive
    const QString data = m_tmp.path() + QStringLiteral("/clouddata");
    QDir().mkpath(data + QStringLiteral("/sub"));
    QFile hello(data + QStringLiteral("/hello.txt"));
    QVERIFY(hello.open(QIODevice::WriteOnly));
    hello.write("hello from the cloud\n");
    hello.close();
    QVERIFY(runRclone({QStringLiteral("config"), QStringLiteral("create"), QStringLiteral("testcloud"), QStringLiteral("alias"),
                       QStringLiteral("remote"), data}));

    PaneroCloud *cloud = PaneroCloud::instance();
    QVERIFY(cloud->refreshNow());
    const PaneroCloud::Entry e = cloud->entry(QStringLiteral("rclone:testcloud"));
    QCOMPARE(e.rcloneRemote, QStringLiteral("testcloud"));
    QCOMPARE(e.rcloneType, QStringLiteral("alias"));
    QVERIFY(!cloud->urlFor(e).isValid());

    QSignalSpy mounted(cloud, &PaneroCloud::mounted);
    QSignalSpy failed(cloud, &PaneroCloud::mountFailed);
    cloud->mount(e);
    QVERIFY(cloud->isMounting(e.id));
    QTRY_VERIFY_WITH_TIMEOUT(mounted.count() == 1 || failed.count() == 1, 20000);
    if (!failed.isEmpty())
        QFAIL(qPrintable(failed.first().at(1).toString()));
    const QUrl localUrl = mounted.first().at(1).toUrl();
    const QString dir = localUrl.toLocalFile();
    QCOMPARE(dir, m_tmp.path() + QStringLiteral("/run/panero-cloud/testcloud"));
    QVERIFY(cloud->isMounted(e.id));
    QCOMPARE(cloud->urlFor(cloud->entry(e.id)), localUrl);

    // contents visible through the mount
    QFile through(dir + QStringLiteral("/hello.txt"));
    QVERIFY(through.open(QIODevice::ReadOnly));
    QCOMPARE(through.readAll(), QByteArray("hello from the cloud\n"));
    through.close();
    QVERIFY(QFileInfo(dir + QStringLiteral("/sub")).isDir());

    // writing through the mount reaches the "cloud" (VFS cache uploads after close)
    QFile written(dir + QStringLiteral("/new.txt"));
    QVERIFY(written.open(QIODevice::WriteOnly));
    written.write("written via panero\n");
    written.close();
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(data + QStringLiteral("/new.txt")), 15000);

    // mounting again just reports the existing mount
    cloud->mount(e);
    QCOMPARE(mounted.count(), 2);

    QSignalSpy unmounted(cloud, &PaneroCloud::unmounted);
    cloud->unmount(e.id);
    QCOMPARE(unmounted.count(), 1);
    QVERIFY(!cloud->isMounted(e.id));
    QVERIFY(!QFileInfo::exists(dir)); // mount point removed
    QFile mi(QStringLiteral("/proc/self/mountinfo"));
    QVERIFY(mi.open(QIODevice::ReadOnly));
    QVERIFY(!PaneroCloudUtils::parseMountInfo(mi.readAll()).contains(dir));
    QFile copied(data + QStringLiteral("/new.txt"));
    QVERIFY(copied.open(QIODevice::ReadOnly));
    QCOMPARE(copied.readAll(), QByteArray("written via panero\n"));

    // unmountAll() (shutdown path) with a live mount
    cloud->mount(e);
    QTRY_COMPARE_WITH_TIMEOUT(mounted.count(), 3, 20000);
    cloud->unmountAll();
    QVERIFY(!cloud->isMounted(e.id));
    QVERIFY(!QFileInfo::exists(dir));
}

void PaneroCloudTest::rcloneMountFailure()
{
    if (!m_haveRclone)
        QSKIP("rclone not installed");
    if (!PaneroCloud::fuseAvailable())
        QSKIP("FUSE not usable here");
    PaneroCloud::Entry bogus;
    bogus.type = QStringLiteral("rclone");
    bogus.id = QStringLiteral("rclone:nosuchremote");
    bogus.rcloneRemote = QStringLiteral("nosuchremote");
    PaneroCloud *cloud = PaneroCloud::instance();
    QSignalSpy failed(cloud, &PaneroCloud::mountFailed);
    cloud->mount(bogus);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 20000);
    const QString message = failed.first().at(1).toString();
    qDebug() << "mountFailed message:" << message;
    QVERIFY(!message.isEmpty());
    QVERIFY(!cloud->isMounting(bogus.id));
    QVERIFY(!QFileInfo::exists(PaneroCloudUtils::mountBaseDir() + QStringLiteral("/nosuchremote")));
}

void PaneroCloudTest::rcloneRemove()
{
    if (!m_haveRclone)
        QSKIP("rclone not installed");
    QVERIFY(runRclone({QStringLiteral("config"), QStringLiteral("create"), QStringLiteral("gone"), QStringLiteral("memory")}));
    PaneroCloud *cloud = PaneroCloud::instance();
    QVERIFY(cloud->refreshNow());
    PaneroCloud::Entry e = cloud->entry(QStringLiteral("rclone:gone"));
    QCOMPARE(e.rcloneType, QStringLiteral("memory"));
    cloud->setLabel(e, QStringLiteral("Gone Drive"));
    QCOMPARE(cloud->entry(e.id).label, QStringLiteral("Gone Drive"));
    QString error;
    QVERIFY2(cloud->removeEntry(cloud->entry(e.id), &error), qPrintable(error));
    QVERIFY(cloud->refreshNow());
    QVERIFY(!cloud->remoteNames().contains(QStringLiteral("gone")));
    QVERIFY(cloud->remoteNames().contains(QStringLiteral("testcloud")));
}

// Manual check that a crashed Panero leaves no rclone mount behind (PR_SET_PDEATHSIG):
// PANERO_CLOUD_TEST_HOLD=1 panerocloudtest holdMountForCrashCheck, then kill -9 the test.
void PaneroCloudTest::holdMountForCrashCheck()
{
    if (qEnvironmentVariableIsEmpty("PANERO_CLOUD_TEST_HOLD"))
        QSKIP("manual check, set PANERO_CLOUD_TEST_HOLD=1");
    if (!m_haveRclone)
        QSKIP("rclone not installed");
    const QString data = m_tmp.path() + QStringLiteral("/holddata");
    QDir().mkpath(data);
    QVERIFY(runRclone({QStringLiteral("config"), QStringLiteral("create"), QStringLiteral("hold"), QStringLiteral("alias"),
                       QStringLiteral("remote"), data}));
    PaneroCloud *cloud = PaneroCloud::instance();
    QVERIFY(cloud->refreshNow());
    QSignalSpy mounted(cloud, &PaneroCloud::mounted);
    cloud->mount(cloud->entry(QStringLiteral("rclone:hold")));
    QTRY_COMPARE_WITH_TIMEOUT(mounted.count(), 1, 20000);
    qInfo("MOUNTED %s pid %lld", qPrintable(cloud->mountPath(QStringLiteral("rclone:hold"))), QCoreApplication::applicationPid());
    QTest::qWait(60000);
}

QTEST_GUILESS_MAIN(PaneroCloudTest)

#include "panerocloudtest.moc"
