/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerocloud.h"
#include "panerocloudrevoke.h"
#include "panerocloudsetup.h"
#include "panerocloudutils.h"

#include <QDir>
#include <QHostAddress>
#include <QJsonObject>
#include <QTcpServer>
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
    void versions();
    void encryptedConfigDetection();
    void redaction();
    void pcloudHosts();
    void iniAndDump();
    void oneDriveScopes();
    void passwordsAndAddresses();
    void offlineCacheFiles();
    void revokeRequests();
    void identityRequests();
    void callbackPort();

private:
    QTemporaryDir m_tmp;
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
}

void PaneroCloudTest::cleanupTestCase()
{
    // before the temporary directory goes away
    PaneroCloud::instance()->unmountAll();
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
    // only rclone's own loopback helper is accepted
    QVERIFY(!PaneroCloudUtils::parseAuthUrl(QStringLiteral("go to the following link: https://evil.example/auth?state=abcdefghij")).isValid());
    QVERIFY(!PaneroCloudUtils::parseAuthUrl(QStringLiteral("following link: http://localhost:53682/auth?state=abcdefghij")).isValid());
    QVERIFY(!PaneroCloudUtils::parseAuthUrl(QStringLiteral("following link: http://127.0.0.1:53683/auth?state=abcdefghij")).isValid());
    QVERIFY(!PaneroCloudUtils::parseAuthUrl(QStringLiteral("following link: http://127.0.0.1:53682/auth?state=abc")).isValid());
    QVERIFY(!PaneroCloudUtils::parseAuthUrl(QStringLiteral("following link: http://127.0.0.1:53682/auth?state=abcdefghij@evil.example")).isValid());
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
    QCOMPARE(PaneroCloudUtils::userRcloneConfigPath(), m_tmp.path() + QStringLiteral("/rclone.conf"));
}

void PaneroCloudTest::versions()
{
    QCOMPARE(parseRcloneVersion(QStringLiteral("rclone v1.76.0\n- os/version: ubuntu 24.04\n")), QStringLiteral("1.76.0"));
    QCOMPARE(parseRcloneVersion(QStringLiteral("rclone v1.60.1-DEV\n")), QStringLiteral("1.60.1"));
    QVERIFY(parseRcloneVersion(QStringLiteral("garbage")).isEmpty());
    QVERIFY(compareVersions(QStringLiteral("1.60.1"), QStringLiteral("1.75.2")) < 0);
    QVERIFY(compareVersions(QStringLiteral("1.75.2"), QStringLiteral("1.75.2")) == 0);
    QVERIFY(compareVersions(QStringLiteral("1.76.0"), QStringLiteral("1.75.2")) > 0);
    QVERIFY(compareVersions(QStringLiteral("1.100.0"), QStringLiteral("1.99.9")) > 0);
}

void PaneroCloudTest::encryptedConfigDetection()
{
    QVERIFY(isEncryptedRcloneConfig("# Encrypted rclone configuration File\n\nRCLONE_ENCRYPT_V0:\nabc"));
    QVERIFY(!isEncryptedRcloneConfig("[gd]\ntype = drive\ntoken = {}\n"));
    QVERIFY(!isEncryptedRcloneConfig(""));
    QVERIFY(!isEncryptedRcloneConfig("# comment\n[x]\nRCLONE_ENCRYPT_V0:\n"));
}

void PaneroCloudTest::redaction()
{
    const QString token = QStringLiteral("{\"access_token\":\"ya29.SECRETVALUE\",\"token_type\":\"Bearer\",\"refresh_token\":\"1//REFRESHSECRET\"}");
    QString r = redactSecrets(QStringLiteral("token = ") + token);
    QVERIFY(!r.contains(QLatin1String("SECRETVALUE")));
    QVERIFY(!r.contains(QLatin1String("REFRESHSECRET")));
    r = redactSecrets(token);
    QVERIFY(!r.contains(QLatin1String("SECRETVALUE")));
    QVERIFY(r.contains(QLatin1String("\"token_type\":\"Bearer\"")));
    r = redactSecrets(QStringLiteral("Authorization: Bearer abc.def-ghi"));
    QVERIFY(!r.contains(QLatin1String("abc.def")));
    r = redactSecrets(QStringLiteral("GET http://127.0.0.1:53682/?code=4/0AXcode&state=xyz123&hostname=api.pcloud.com"));
    QVERIFY(!r.contains(QLatin1String("4/0AXcode")));
    QVERIFY(!r.contains(QLatin1String("xyz123")));
    QVERIFY(r.contains(QLatin1String("hostname=api.pcloud.com")));
    r = redactSecrets(QStringLiteral("[nc]\ntype = webdav\npass = AbCdObscured\nuser = me\n"));
    QVERIFY(!r.contains(QLatin1String("AbCdObscured")));
    QVERIFY(r.contains(QLatin1String("user = me")));
    r = redactSecrets(QStringLiteral("Error: Auth state doesn't match\nDescription: Expecting \"p4jnGXaA5H3\" got \"\""));
    QVERIFY(!r.contains(QLatin1String("p4jnGXaA5H3")));
    // rclone error summaries are redacted too
    QVERIFY(!PaneroCloudUtils::rcloneErrorSummary(QStringLiteral("2026/11/21 ERROR : failed: token = {\"access_token\":\"LEAK\"}\n")).contains(QLatin1String("LEAK")));
}

void PaneroCloudTest::pcloudHosts()
{
    QVERIFY(isAllowedPcloudHost(QStringLiteral("api.pcloud.com")));
    QVERIFY(isAllowedPcloudHost(QStringLiteral("eapi.pcloud.com")));
    QVERIFY(!isAllowedPcloudHost(QStringLiteral("evil.example")));
    QVERIFY(!isAllowedPcloudHost(QStringLiteral("pcloud.com")));
    QVERIFY(!isAllowedPcloudHost(QStringLiteral("api.pcloud.com.evil.example")));
    QVERIFY(!isAllowedPcloudHost(QStringLiteral("evilpcloud.com")));
    QVERIFY(!isAllowedPcloudHost(QStringLiteral("api.pcloud.com/x")));
}

void PaneroCloudTest::iniAndDump()
{
    QCOMPARE(iniValue(QStringLiteral("[p]\ntype = pcloud\nhostname = eapi.pcloud.com\ntoken = XXX\n"), QStringLiteral("hostname")),
             QStringLiteral("eapi.pcloud.com"));
    QVERIFY(iniValue(QStringLiteral("[p]\ntype = pcloud\n"), QStringLiteral("hostname")).isEmpty());
    QJsonObject remotes;
    QJsonObject gd;
    gd.insert(QStringLiteral("type"), QStringLiteral("drive"));
    gd.insert(QStringLiteral("token"), QStringLiteral("{\"access_token\":\"a\"}"));
    gd.insert(QStringLiteral("scope"), QStringLiteral("drive"));
    remotes.insert(QStringLiteral("gd"), gd);
    QJsonObject bad;
    bad.insert(QStringLiteral("type"), QStringLiteral("x\ninjected = 1"));
    remotes.insert(QStringLiteral("bad]name"), bad);
    const QByteArray ini = rcloneIniFromDump(remotes);
    QVERIFY(ini.startsWith("[gd]\ntype = drive\n"));
    QVERIFY(ini.contains("token = {\"access_token\":\"a\"}\n"));
    QVERIFY(!ini.contains("bad]name"));
    QVERIFY(!ini.contains("injected"));
}

void PaneroCloudTest::oneDriveScopes()
{
    QCOMPARE(PaneroCloudUtils::oneDriveScopes(false, false), QStringLiteral("Files.Read Files.ReadWrite offline_access"));
    QCOMPARE(PaneroCloudUtils::oneDriveScopes(true, false), QStringLiteral("Files.Read offline_access"));
    QVERIFY(PaneroCloudUtils::oneDriveScopes(false, true).contains(QLatin1String("Files.ReadWrite.All")));
    QVERIFY(!PaneroCloudUtils::oneDriveScopes(false, true).contains(QLatin1String("Sites")));
    QCOMPARE(PaneroCloudSetup::oauthOptions(QStringLiteral("drive"), true, false), QStringList{QStringLiteral("scope=drive.readonly")});
    QCOMPARE(PaneroCloudSetup::oauthOptions(QStringLiteral("drive"), false, false), QStringList{QStringLiteral("scope=drive")});
    const QStringList od = PaneroCloudSetup::oauthOptions(QStringLiteral("onedrive"), false, false);
    QVERIFY(od.contains(QStringLiteral("disable_site_permission=true")));
    QVERIFY(od.contains(QStringLiteral("access_scopes=Files.Read Files.ReadWrite offline_access")));
    QVERIFY(PaneroCloudSetup::oauthOptions(QStringLiteral("dropbox"), true, true).isEmpty());
    QVERIFY(PaneroCloudSetup::accessDescription(QStringLiteral("drive"), false, false).contains(QLatin1String("e-mail")));
}

void PaneroCloudTest::passwordsAndAddresses()
{
    QVERIFY(PaneroCloudSetup::passwordProblem(QStringLiteral("good pass")).isEmpty());
    QVERIFY(!PaneroCloudSetup::passwordProblem(QString()).isEmpty());
    QVERIFY(!PaneroCloudSetup::passwordProblem(QStringLiteral(" lead")).isEmpty());
    QVERIFY(!PaneroCloudSetup::passwordProblem(QStringLiteral("trail ")).isEmpty());
    QVERIFY(!PaneroCloudSetup::passwordProblem(QStringLiteral("two\nlines")).isEmpty());
    QCOMPARE(PaneroCloudSetup::webDavAddress(QStringLiteral("cloud.example.com/"), QStringLiteral("nextcloud"), QStringLiteral("anna b")),
             QStringLiteral("https://cloud.example.com/remote.php/dav/files/anna%20b"));
    QCOMPARE(PaneroCloudSetup::webDavAddress(QStringLiteral("https://o.example.com"), QStringLiteral("owncloud"), QStringLiteral("x")),
             QStringLiteral("https://o.example.com/remote.php/webdav"));
    QCOMPARE(PaneroCloudSetup::webDavAddress(QStringLiteral("https://d.example.com/dav"), QStringLiteral("other"), QStringLiteral("x")),
             QStringLiteral("https://d.example.com/dav"));
}

void PaneroCloudTest::offlineCacheFiles()
{
    QCOMPARE(vfsCacheFile(QStringLiteral("/c/gd"), QStringLiteral("gd"), QStringLiteral("Docs/a.txt")), QStringLiteral("/c/gd/vfs/gd/Docs/a.txt"));
    QVERIFY(vfsCacheFile(QStringLiteral("/c/gd"), QStringLiteral("gd"), QStringLiteral("../../etc/passwd")).isEmpty());
    QCOMPARE(aliasCacheSubPath(QStringLiteral("/srv/data/")), QStringLiteral("local/srv/data"));
    QCOMPARE(aliasCacheSubPath(QStringLiteral("gd:Work/Docs")), QStringLiteral("gd/Work/Docs"));
    QCOMPARE(aliasCacheSubPath(QStringLiteral("gd:")), QStringLiteral("gd"));
    // a sparse file (partly downloaded) is not "available"; a full one is
    const QString sparse = m_tmp.path() + QStringLiteral("/sparse.bin");
    QFile f(sparse);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("head");
    QVERIFY(f.resize(4 * 1024 * 1024));
    f.close();
    QVERIFY(!isFullyCached(sparse, 4 * 1024 * 1024));
    const QString full = m_tmp.path() + QStringLiteral("/full.bin");
    QFile g(full);
    QVERIFY(g.open(QIODevice::WriteOnly));
    g.write(QByteArray(300000, 'x'));
    g.close();
    QVERIFY(isFullyCached(full, 300000));
    QVERIFY(!isFullyCached(full, 300001));
    QVERIFY(!isFullyCached(m_tmp.path() + QStringLiteral("/missing"), 1));
    QCOMPARE(safeFileName(QStringLiteral("../x/y")), QStringLiteral("%2E.%2Fx%2Fy"));
    QCOMPARE(safeFileName(QStringLiteral("gd")), QStringLiteral("gd"));
    QCOMPARE(safeFileName(QStringLiteral("..")), QStringLiteral("%2E."));
    QVERIFY(safeFileName(QStringLiteral(".x")) != safeFileName(QStringLiteral("_x")));
    QVERIFY(safeFileName(QStringLiteral("a b")) != safeFileName(QStringLiteral("a_b")));
    QVERIFY(!safeFileName(QStringLiteral("a/b")).contains(QLatin1Char('/')));
}

void PaneroCloudTest::revokeRequests()
{
    using namespace PaneroCloudRevoke;
    QJsonObject gd;
    gd.insert(QStringLiteral("token"), QStringLiteral("{\"access_token\":\"AT\",\"refresh_token\":\"1//RT\"}"));
    Request r;
    QVERIFY(buildRequest(QStringLiteral("drive"), gd, QString(), &r));
    QCOMPARE(r.url, QUrl(QStringLiteral("https://oauth2.googleapis.com/revoke")));
    QCOMPARE(r.method, QByteArray("POST"));
    QCOMPARE(r.body, QByteArray("token=1%2F%2FRT"));

    QVERIFY(buildRequest(QStringLiteral("dropbox"), gd, QString(), &r));
    QCOMPARE(r.url, QUrl(QStringLiteral("https://api.dropboxapi.com/2/auth/token/revoke")));
    QVERIFY(r.headers.contains(qMakePair(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer AT"))));
    QVERIFY(r.headers.contains(qMakePair(QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json"))));
    QCOMPARE(r.body, QByteArray("null"));

    QVERIFY(buildRequest(QStringLiteral("box"), gd, QStringLiteral("BOXSECRET"), &r));
    QCOMPARE(r.url, QUrl(QStringLiteral("https://api.box.com/oauth2/revoke")));
    QVERIFY(r.body.contains("client_id=d0374ba6pgmaguie02ge15sv1mllndho"));
    QVERIFY(r.body.contains("client_secret=BOXSECRET"));
    QVERIFY(r.body.contains("token=1%2F%2FRT"));
    QVERIFY(!buildRequest(QStringLiteral("box"), gd, QString(), &r)); // no secret, no request

    QJsonObject pc = gd;
    QVERIFY(buildRequest(QStringLiteral("pcloud"), pc, QString(), &r));
    QCOMPARE(r.url, QUrl(QStringLiteral("https://api.pcloud.com/logout")));
    QCOMPARE(r.method, QByteArray("GET"));
    pc.insert(QStringLiteral("hostname"), QStringLiteral("eapi.pcloud.com"));
    QVERIFY(buildRequest(QStringLiteral("pcloud"), pc, QString(), &r));
    QCOMPARE(r.url.host(), QStringLiteral("eapi.pcloud.com"));
    pc.insert(QStringLiteral("hostname"), QStringLiteral("evil.example"));
    QString why;
    QVERIFY(!buildRequest(QStringLiteral("pcloud"), pc, QString(), &r, &why)); // never send a token to a foreign host
    QVERIFY(!why.isEmpty());

    QVERIFY(!buildRequest(QStringLiteral("onedrive"), gd, QString(), &r));
    QVERIFY(!buildRequest(QStringLiteral("drive"), QJsonObject(), QString(), &r));

    QVERIFY(responseConfirmsRevocation(QStringLiteral("drive"), 200, QByteArray()));
    QVERIFY(!responseConfirmsRevocation(QStringLiteral("drive"), 400, QByteArray()));
    QVERIFY(responseConfirmsRevocation(QStringLiteral("pcloud"), 200, "{\"result\":0,\"auth_deleted\":true}"));
    QVERIFY(!responseConfirmsRevocation(QStringLiteral("pcloud"), 200, "{\"result\":2000,\"error\":\"Log in failed.\"}"));

    QCOMPARE(manageUrl(QStringLiteral("onedrive"), QJsonObject()), QUrl(QStringLiteral("https://account.live.com/consent/Manage")));
    QJsonObject biz;
    biz.insert(QStringLiteral("drive_type"), QStringLiteral("business"));
    QCOMPARE(manageUrl(QStringLiteral("onedrive"), biz), QUrl(QStringLiteral("https://myapps.microsoft.com")));
    QJsonObject nc;
    nc.insert(QStringLiteral("vendor"), QStringLiteral("nextcloud"));
    nc.insert(QStringLiteral("url"), QStringLiteral("https://cloud.example.com/remote.php/dav/files/anna"));
    QCOMPARE(manageUrl(QStringLiteral("webdav"), nc), QUrl(QStringLiteral("https://cloud.example.com/index.php/settings/user/security")));
}

void PaneroCloudTest::identityRequests()
{
    using namespace PaneroCloudRevoke;
    QJsonObject cfg;
    cfg.insert(QStringLiteral("token"), QStringLiteral("{\"access_token\":\"AT\"}"));
    Request r;
    QVERIFY(buildIdentityRequest(QStringLiteral("onedrive"), cfg, &r));
    QCOMPARE(r.url.host(), QStringLiteral("graph.microsoft.com"));
    QCOMPARE(r.url.scheme(), QStringLiteral("https"));
    QVERIFY(buildIdentityRequest(QStringLiteral("drive"), cfg, &r));
    QCOMPARE(r.url.host(), QStringLiteral("www.googleapis.com"));
    QVERIFY(buildIdentityRequest(QStringLiteral("dropbox"), cfg, &r));
    QCOMPARE(r.body, QByteArray("null"));
    cfg.insert(QStringLiteral("hostname"), QStringLiteral("evil.example"));
    QVERIFY(!buildIdentityRequest(QStringLiteral("pcloud"), cfg, &r));
    QVERIFY(!buildIdentityRequest(QStringLiteral("webdav"), cfg, &r));
    QVERIFY(!buildIdentityRequest(QStringLiteral("drive"), QJsonObject(), &r));
    QCOMPARE(parseIdentity(QStringLiteral("onedrive"), R"({"owner":{"user":{"displayName":"Test User","email":"t@example.com"}}})"),
             QStringLiteral("Test User <t@example.com>"));
    QCOMPARE(parseIdentity(QStringLiteral("onedrive"), R"({"owner":{"user":{"displayName":"Test User","id":"abc"}}})"),
             QStringLiteral("Test User"));
    QCOMPARE(parseIdentity(QStringLiteral("drive"), R"({"user":{"displayName":"A B","emailAddress":"a@gmail.com"}})"),
             QStringLiteral("A B <a@gmail.com>"));
    QCOMPARE(parseIdentity(QStringLiteral("dropbox"), R"({"name":{"display_name":"D"},"email":"d@x.org"})"), QStringLiteral("D <d@x.org>"));
    QCOMPARE(parseIdentity(QStringLiteral("pcloud"), R"({"result":0,"email":"p@x.org"})"), QStringLiteral("p@x.org"));
    QVERIFY(parseIdentity(QStringLiteral("pcloud"), R"({"result":2000})").isEmpty());
    QVERIFY(parseIdentity(QStringLiteral("box"), "not json").isEmpty());
}

void PaneroCloudTest::callbackPort()
{
    QString why;
    QTcpServer squatter;
    if (!squatter.listen(QHostAddress::LocalHost, PaneroCloudSetup::CALLBACK_PORT))
        QSKIP("port 53682 is in use on this machine");
    QVERIFY(!PaneroCloudSetup::callbackPortFree(&why));
    QVERIFY(why.contains(QLatin1String("53682")));
    squatter.close();
    QTcpServer squatter6;
    if (squatter6.listen(QHostAddress::LocalHostIPv6, PaneroCloudSetup::CALLBACK_PORT)) {
        QVERIFY(!PaneroCloudSetup::callbackPortFree(&why));
        squatter6.close();
    }
    QVERIFY2(PaneroCloudSetup::callbackPortFree(&why), qPrintable(why));
}

QTEST_GUILESS_MAIN(PaneroCloudTest)

#include "panerocloudtest.moc"
