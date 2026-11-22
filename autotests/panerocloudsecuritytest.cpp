/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
 * Security and integration tests of the cloud feature
 * (dev/CLOUD-SECURITY-AUDIT.md). They need Panero's rclone next to the test
 * (build with -DPANERO_RCLONE_BINARY) and a *private* Secret Service: run them
 * through dev/scripts/cloud-security-test.sh, which starts its own D-Bus
 * session and gnome-keyring in a temporary HOME and sets
 * PANERO_TEST_PRIVATE_KEYRING=1. Without that variable everything is skipped,
 * so a normal `ctest` never touches the desktop keyring of the person running it.
 *
 * Throughout the run a background thread reads the command line of every
 * process and the environment of every own process, looking for the test
 * secrets; at the end, no file below the temporary root may contain them.
 */

#include "paneroaddclouddialog.h"
#include "panerocloud.h"
#include "panerocloudrevoke.h"
#include "panerocloudsetup.h"
#include "panerocloudutils.h"
#include "panerooffline.h"
#include "panerorclone.h"
#include "panerosecretstore.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <KConfigCore/KConfigGroup>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <atomic>
#include <mutex>
#include <thread>

#include <sys/stat.h>
#include <unistd.h>

namespace
{
const QByteArray WEBDAV_PASSWORD = "Pan3ro-Test pass#1";
const QByteArray IMPORT_TOKEN = "FAKE-IMPORT-TOKEN-7f3a9c";

/// Reads /proc/<pid>/cmdline (all processes) and environ (own processes).
class ProcessScanner
{
public:
    void start()
    {
        m_running = true;
        m_thread = std::thread([this]() {
            while (m_running) {
                scanOnce();
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
        });
    }
    void stop()
    {
        m_running = false;
        if (m_thread.joinable())
            m_thread.join();
    }
    void addSecret(const QByteArray &s)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!s.isEmpty() && !m_secrets.contains(s))
            m_secrets << s;
    }
    QStringList hits()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_hits;
    }
    int scans() const { return m_scans; }
    /// Clears the hits once two complete scans have run (a scan in progress
    /// may still report a process that has just ended).
    void settleAndClearHits()
    {
        const int target = m_scans + 2;
        while (m_scans < target)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        std::lock_guard<std::mutex> lock(m_mutex);
        m_hits.clear();
    }

private:
    void scanOnce()
    {
        QList<QByteArray> secrets;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            secrets = m_secrets;
        }
        const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &pid : pids) {
            bool isPid = false;
            pid.toInt(&isPid);
            if (!isPid)
                continue;
            for (const char *what : {"cmdline", "environ"}) {
                QFile f(QStringLiteral("/proc/%1/%2").arg(pid, QLatin1String(what)));
                if (!f.open(QIODevice::ReadOnly))
                    continue;
                const QByteArray data = f.readAll();
                // the local WebDAV test server gets its password on the command line by design
                if (qstrcmp(what, "cmdline") == 0 && data.contains("serve\0webdav"))
                    continue;
                for (const QByteArray &s : qAsConst(secrets)) {
                    if (data.contains(s)) {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        const QString hit = QStringLiteral("pid %1 %2: %3").arg(pid, QLatin1String(what), QString::fromUtf8(data).replace(QChar(0), QLatin1Char(' ')).left(200));
                        if (!m_hits.contains(hit))
                            m_hits << hit;
                    }
                }
            }
        }
        ++m_scans;
    }

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<int> m_scans{0};
    std::mutex m_mutex;
    QList<QByteArray> m_secrets;
    QStringList m_hits;
};
} // namespace

class PaneroCloudSecurityTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void scannerSeesLeaks();
    void environmentIsClean();
    void keyringRoundTrip();
    void encryptedStore();
    void keyHelper();
    void webDavPasswordNeverExposed();
    void pcloudCallbackCsrfRejected();
    void oauthSetupFinishesOrStops();
    void mountAndOfflineFiles();
    void removeDeletesCache();
    void importFromPlainConfig();
    void missingKeyIsDetected();
    void addCloudDialog();
    void noSecretsAnywhere();

private:
    QString root() const { return m_tmp.path(); }
    QString runtimeDir() const { return root() + QStringLiteral("/run"); }
    QByteArray dumpValue(const QString &remote, const QString &key);
    QStringList filesContaining(const QByteArray &secret) const;
    int freePort();

    QTemporaryDir m_tmp;
    ProcessScanner m_scanner;
    QProcess m_server;
    int m_serverPort = 0;
};

void PaneroCloudSecurityTest::initTestCase()
{
    if (qgetenv("PANERO_TEST_PRIVATE_KEYRING") != "1")
        QSKIP("needs the private keyring session of dev/scripts/cloud-security-test.sh");
    QVERIFY(m_tmp.isValid());
    for (const char *d : {"home", "config", "cache", "data", "run", "srv", "bin"})
        QDir().mkpath(root() + QLatin1Char('/') + QLatin1String(d));
    QFile::setPermissions(runtimeDir(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("HOME", QFile::encodeName(root() + QStringLiteral("/home")));
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(root() + QStringLiteral("/config")));
    qputenv("XDG_CACHE_HOME", QFile::encodeName(root() + QStringLiteral("/cache")));
    qputenv("XDG_DATA_HOME", QFile::encodeName(root() + QStringLiteral("/data")));
    qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtimeDir()));
    qputenv("RCLONE_CONFIG", QFile::encodeName(root() + QStringLiteral("/config/rclone/rclone.conf"))); // the "user's own" config
    QString problem;
    QVERIFY2(PaneroRclone::usable(&problem), qPrintable(problem));
    QVERIFY2(!PaneroRclone::keyHelper().isEmpty(), "panero-cloud-key not built");
    QVERIFY2(PaneroSecretStore::isAvailable(), "no Secret Service in the test session");
    PaneroCloud::instance()->setConfig(KSharedConfig::openConfig(root() + QStringLiteral("/config/panerorc")));
    PaneroOffline::instance()->setConfig(KSharedConfig::openConfig(root() + QStringLiteral("/config/panerorc")));
    m_scanner.addSecret(WEBDAV_PASSWORD);
    m_scanner.addSecret(IMPORT_TOKEN);
    m_scanner.start();
}

void PaneroCloudSecurityTest::cleanupTestCase()
{
    PaneroCloud::instance()->unmountAll();
    m_scanner.stop();
    if (m_server.state() != QProcess::NotRunning) {
        m_server.terminate();
        m_server.waitForFinished(3000);
    }
}

int PaneroCloudSecurityTest::freePort()
{
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    return s.serverPort();
}

QByteArray PaneroCloudSecurityTest::dumpValue(const QString &remote, const QString &key)
{
    QByteArray dump;
    PaneroRclone::run({QStringLiteral("config"), QStringLiteral("dump")}, &dump);
    return QJsonDocument::fromJson(dump).object().value(remote).toObject().value(key).toString().toUtf8();
}

QStringList PaneroCloudSecurityTest::filesContaining(const QByteArray &secret) const
{
    QStringList found;
    QDirIterator it(root(), QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.startsWith(root() + QStringLiteral("/srv")) || path.startsWith(root() + QStringLiteral("/bin/"))
            || path.contains(QLatin1String("/run/panero-cloud/")))
            continue; // test server data and helper scripts (fake tokens by design), live mounts
        QFile f(path);
        if (f.size() > 64 * 1024 * 1024 || !f.open(QIODevice::ReadOnly))
            continue;
        if (f.readAll().contains(secret))
            found << path;
    }
    return found;
}

void PaneroCloudSecurityTest::scannerSeesLeaks()
{
    // positive control: a secret on a command line (what preview 4 did with the
    // obscured WebDAV/MEGA password) and in an environment is found
    const QByteArray canary = "SCANNER-CANARY-5d1e";
    m_scanner.addSecret(canary);
    QProcess leak;
    leak.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("sleep 1"), QString::fromLatin1(canary)});
    QVERIFY(leak.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(m_scanner.hits().filter(QStringLiteral(" cmdline: ")).size() > 0, 3000);
    leak.waitForFinished();
    m_scanner.settleAndClearHits();
    QProcess envLeak;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("SOME_VAR"), QString::fromLatin1(canary));
    envLeak.setProcessEnvironment(env);
    envLeak.start(QStringLiteral("/bin/sleep"), {QStringLiteral("1")});
    QVERIFY(envLeak.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(m_scanner.hits().filter(QStringLiteral(" environ: ")).size() > 0, 3000);
    envLeak.waitForFinished();
    m_scanner.settleAndClearHits();
}

void PaneroCloudSecurityTest::environmentIsClean()
{
    qputenv("RCLONE_CONFIG_PASS", "should-not-reach-rclone");
    qputenv("RCLONE_PASSWORD_COMMAND", "/bin/false");
    const QProcessEnvironment env = PaneroRclone::environment();
    QVERIFY(!env.contains(QStringLiteral("RCLONE_CONFIG_PASS")));
    QVERIFY(!env.contains(QStringLiteral("RCLONE_PASSWORD_COMMAND")));
    QVERIFY(!env.contains(QStringLiteral("RCLONE_CONFIG")));
    qunsetenv("RCLONE_CONFIG_PASS");
    qunsetenv("RCLONE_PASSWORD_COMMAND");
    const QStringList args = PaneroRclone::globalArgs();
    QVERIFY(args.contains(QStringLiteral("--ask-password=false")));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("--config")) + 1), PaneroRclone::configPath());
    QVERIFY(PaneroRclone::configPath().startsWith(root() + QStringLiteral("/config/panero/cloud/")));
    QVERIFY(PaneroCloudUtils::compareVersions(PaneroRclone::version(), PaneroRclone::minimumVersion()) >= 0);
}

void PaneroCloudSecurityTest::keyringRoundTrip()
{
    QString error;
    QVERIFY2(PaneroSecretStore::store(QStringLiteral("panero-test"), QStringLiteral("Panero test"), "s3cret-value", &error), qPrintable(error));
    QCOMPARE(PaneroSecretStore::lookup(QStringLiteral("panero-test"), &error), QByteArray("s3cret-value"));
    QVERIFY(PaneroSecretStore::store(QStringLiteral("panero-test"), QStringLiteral("Panero test"), "replaced", &error));
    QCOMPARE(PaneroSecretStore::lookup(QStringLiteral("panero-test"), &error), QByteArray("replaced"));
    QVERIFY(PaneroSecretStore::remove(QStringLiteral("panero-test"), &error));
    bool notFound = false;
    QVERIFY(PaneroSecretStore::lookup(QStringLiteral("panero-test"), &error, &notFound).isEmpty());
    QVERIFY(notFound);
}

void PaneroCloudSecurityTest::encryptedStore()
{
    QVERIFY(!PaneroRclone::configExists());
    QString error;
    QCOMPARE(int(PaneroRclone::ensureStore(&error)), int(PaneroRclone::StoreState::Ready));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(PaneroRclone::configExists());
    QVERIFY(PaneroRclone::configIsEncrypted());
    struct stat st;
    QCOMPARE(::stat(QFile::encodeName(PaneroRclone::configPath()).constData(), &st), 0);
    QCOMPARE(int(st.st_mode & 0777), 0600);
    QCOMPARE(::stat(QFile::encodeName(PaneroRclone::configDir()).constData(), &st), 0);
    QCOMPARE(int(st.st_mode & 0777), 0700);
    const QByteArray key = PaneroSecretStore::lookup(QStringLiteral("cloud-config-key"));
    QCOMPARE(key.size(), 64); // 32 random bytes, hex
    QVERIFY(QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(QString::fromLatin1(key)).hasMatch());
    m_scanner.addSecret(key);
    // a second call keeps the key and the file
    QCOMPARE(int(PaneroRclone::ensureStore(&error)), int(PaneroRclone::StoreState::Ready));
    QCOMPARE(PaneroSecretStore::lookup(QStringLiteral("cloud-config-key")), key);
    // rclone can read it (through the helper), and nothing else can
    QByteArray out, err;
    QCOMPARE(PaneroRclone::run({QStringLiteral("listremotes")}, &out, &err), 0);
    QProcess plain;
    plain.setProcessEnvironment(PaneroRclone::environment());
    plain.start(PaneroRclone::program(), {QStringLiteral("--config"), PaneroRclone::configPath(), QStringLiteral("--ask-password=false"),
                                          QStringLiteral("listremotes")});
    QVERIFY(plain.waitForFinished(20000));
    QVERIFY(plain.exitCode() != 0);
}

void PaneroCloudSecurityTest::keyHelper()
{
    QProcess helper;
    helper.start(PaneroRclone::keyHelper(), QStringList());
    QVERIFY(helper.waitForFinished(20000));
    QCOMPARE(helper.exitCode(), 0);
    QCOMPARE(helper.readAllStandardOutput(), PaneroSecretStore::lookup(QStringLiteral("cloud-config-key")) + '\n');
}

void PaneroCloudSecurityTest::webDavPasswordNeverExposed()
{
    // a local WebDAV server with a password
    m_serverPort = freePort();
    m_server.setProcessEnvironment(PaneroRclone::environment());
    m_server.setProcessChannelMode(QProcess::MergedChannels);
    m_server.start(PaneroRclone::program(), {QStringLiteral("serve"), QStringLiteral("webdav"), root() + QStringLiteral("/srv"),
                                             QStringLiteral("--addr"), QStringLiteral("127.0.0.1:%1").arg(m_serverPort),
                                             QStringLiteral("--user"), QStringLiteral("tester"), QStringLiteral("--pass"),
                                             QString::fromUtf8(WEBDAV_PASSWORD), QStringLiteral("--config"), QStringLiteral("/dev/null")});
    QVERIFY(m_server.waitForStarted(10000));
    const auto serverUp = [this]() {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, quint16(m_serverPort));
        return s.waitForConnected(200);
    };
    QTRY_VERIFY_WITH_TIMEOUT(serverUp(), 15000);
    QFile f(root() + QStringLiteral("/srv/hello.txt"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello\n");
    f.close();

    const QString url = QStringLiteral("http://127.0.0.1:%1").arg(m_serverPort);
    QString error;
    QVERIFY2(PaneroCloudSetup::createWebDavRemote(QStringLiteral("nc"), url, QStringLiteral("other"), QStringLiteral("tester"),
                                                  QString::fromUtf8(WEBDAV_PASSWORD), &error),
             qPrintable(error));
    const QByteArray obscured = dumpValue(QStringLiteral("nc"), QStringLiteral("pass"));
    QVERIFY(!obscured.isEmpty());
    QVERIFY(obscured != WEBDAV_PASSWORD);
    m_scanner.addSecret(obscured);
    QVERIFY2(PaneroCloudSetup::verifyRemote(QStringLiteral("nc"), &error), qPrintable(error));

    // a wrong password fails verification and leaves nothing behind
    QVERIFY(PaneroCloudSetup::createWebDavRemote(QStringLiteral("ncbad"), url, QStringLiteral("other"), QStringLiteral("tester"),
                                                 QStringLiteral("wrong password"), &error));
    QVERIFY(!PaneroCloudSetup::verifyRemote(QStringLiteral("ncbad"), &error));
    QVERIFY(!error.contains(QLatin1String("wrong password")));
    PaneroCloudSetup::deleteRemote(QStringLiteral("ncbad"));

    // refused passwords never reach rclone
    QVERIFY(!PaneroCloudSetup::createWebDavRemote(QStringLiteral("ncsp"), url, QStringLiteral("other"), QStringLiteral("tester"),
                                                  QStringLiteral(" spaced "), &error));
    QVERIFY(PaneroCloud::instance()->refreshNow());
    QVERIFY(PaneroCloud::instance()->remoteNames().contains(QStringLiteral("nc")));
    QVERIFY(!PaneroCloud::instance()->remoteNames().contains(QStringLiteral("ncsp")));

    QCOMPARE(filesContaining(WEBDAV_PASSWORD), QStringList());
    QCOMPARE(filesContaining(obscured), QStringList());
}

void PaneroCloudSecurityTest::pcloudCallbackCsrfRejected()
{
    // CVE-2026-116194: a stateless callback, or a foreign API host, must not be accepted
    QString why;
    if (!PaneroCloudSetup::callbackPortFree(&why))
        QSKIP("port 53682 is in use");
    // a fake browser opener records what rclone tries to open
    const QString opener = root() + QStringLiteral("/bin/xdg-open");
    QFile o(opener);
    QVERIFY(o.open(QIODevice::WriteOnly));
    o.write(QByteArray("#!/bin/sh\necho \"$@\" >> ") + QFile::encodeName(root() + QStringLiteral("/bin/opened.txt")) + "\n");
    o.close();
    o.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    for (const bool withState : {false, true}) {
        QProcess p;
        PaneroRclone::prepare(p, {QStringLiteral("config"), QStringLiteral("create"), QStringLiteral("xp"), QStringLiteral("pcloud")});
        QProcessEnvironment env = p.processEnvironment();
        env.insert(QStringLiteral("PATH"), root() + QStringLiteral("/bin:") + env.value(QStringLiteral("PATH")));
        p.setProcessEnvironment(env);
        p.start();
        QString err;
        QTRY_VERIFY_WITH_TIMEOUT((err += QString::fromUtf8(p.readAllStandardError())).contains(QLatin1String("Waiting for code")), 20000);
        const QUrl auth = PaneroCloudUtils::parseAuthUrl(err);
        QVERIFY(auth.isValid());
        const QString state = QUrlQuery(auth).queryItemValue(QStringLiteral("state"));
        QVERIFY(state.length() >= 16);

        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, PaneroCloudSetup::CALLBACK_PORT);
        QVERIFY(s.waitForConnected(3000));
        QByteArray path = "/?code=attackercode&hostname=evil.example";
        if (withState)
            path += "&state=" + state.toUtf8();
        s.write("GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
        QVERIFY(s.waitForBytesWritten(3000));
        QByteArray reply;
        while (s.waitForReadyRead(3000))
            reply += s.readAll();
        QVERIFY(p.waitForFinished(15000));
        err += QString::fromUtf8(p.readAllStandardError());
        QVERIFY(p.exitCode() != 0);
        if (withState)
            QVERIFY2(err.contains(QLatin1String("invalid hostname")), qPrintable(err));
        else {
            QVERIFY(reply.startsWith("HTTP/1.1 400"));
            QVERIFY2(err.contains(QLatin1String("Auth state doesn't match")), qPrintable(err));
        }
        QVERIFY(PaneroCloud::instance()->refreshNow());
        QVERIFY(!PaneroCloud::instance()->remoteNames().contains(QStringLiteral("xp")));
    }
    // the browser was only ever sent to rclone's local helper
    QFile opened(root() + QStringLiteral("/bin/opened.txt"));
    if (opened.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = opened.readAll().split('\n');
        for (const QByteArray &l : lines) {
            if (!l.isEmpty())
                QVERIFY2(l.startsWith("http://127.0.0.1:53682/auth?state="), l.constData());
        }
    }
}

void PaneroCloudSecurityTest::oauthSetupFinishesOrStops()
{
    // A fake OAuth server and a fake browser complete rclone's browser sign-in
    // locally. Google Drive must finish; for OneDrive the drive listing then
    // fails (fake token) and rclone retries forever - Panero must notice.
    QString why;
    if (!PaneroCloudSetup::callbackPortFree(&why))
        QSKIP("port 53682 is in use");
    if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty() || QStandardPaths::findExecutable(QStringLiteral("curl")).isEmpty())
        QSKIP("needs python3 and curl");
    const QByteArray fakeToken = "fake-refresh-token-91c2";
    m_scanner.addSecret(fakeToken);
    const QString script = root() + QStringLiteral("/bin/fakeoauth.py");
    QFile py(script);
    QVERIFY(py.open(QIODevice::WriteOnly));
    py.write("import http.server, json, sys, urllib.parse\n"
             "class H(http.server.BaseHTTPRequestHandler):\n"
             "    def log_message(self, *a): pass\n"
             "    def do_GET(self):\n"
             "        u = urllib.parse.urlparse(self.path); q = urllib.parse.parse_qs(u.query)\n"
             "        loc = q['redirect_uri'][0] + '?code=fakecode&state=' + urllib.parse.quote(q['state'][0])\n"
             "        self.send_response(302); self.send_header('Location', loc); self.end_headers()\n"
             "    def do_POST(self):\n"
             "        self.rfile.read(int(self.headers.get('Content-Length', 0)))\n"
             "        b = json.dumps({'access_token': 'fake-access', 'refresh_token': '" + fakeToken + "', 'token_type': 'Bearer', 'expires_in': 3600}).encode()\n"
             "        self.send_response(200); self.send_header('Content-Type', 'application/json')\n"
             "        self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)\n"
             "s = http.server.HTTPServer(('127.0.0.1', 0), H)\n"
             "print(s.server_address[1], flush=True)\n"
             "s.serve_forever()\n");
    py.close();
    QProcess server;
    server.start(QStringLiteral("python3"), {QStringLiteral("-I"), script});
    QVERIFY(server.waitForReadyRead(10000));
    const QString port = QString::fromLatin1(server.readLine()).trimmed();
    QVERIFY(port.toInt() > 0);
    const QString base = QStringLiteral("http://127.0.0.1:") + port;
    QFile opener(root() + QStringLiteral("/bin/xdg-open"));
    QVERIFY(opener.open(QIODevice::WriteOnly | QIODevice::Truncate));
    opener.write("#!/bin/sh\ncurl -s -L -o /dev/null \"$1\"\n");
    opener.close();
    opener.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    const auto runSetup = [&](const QStringList &args, PaneroCloudSetup::SetupOutputWatcher &watcher, int timeoutMs) {
        QProcess p;
        PaneroRclone::prepare(p, args);
        QProcessEnvironment env = p.processEnvironment();
        env.insert(QStringLiteral("PATH"), root() + QStringLiteral("/bin:") + env.value(QStringLiteral("PATH")));
        p.setProcessEnvironment(env);
        p.start();
        p.closeWriteChannel();
        QElapsedTimer t;
        t.start();
        while (p.state() != QProcess::NotRunning && !watcher.stuck() && t.elapsed() < timeoutMs) {
            p.waitForReadyRead(200);
            watcher.feed(p.readAllStandardOutput());
        }
        const bool finished = p.state() == QProcess::NotRunning;
        if (!finished) {
            p.kill();
            p.waitForFinished(3000);
        }
        return finished ? p.exitCode() : -1;
    };

    // Google Drive: completes, token stored encrypted only
    PaneroCloudSetup::SetupOutputWatcher drive;
    const int rc = runSetup({QStringLiteral("config"), QStringLiteral("create"), QStringLiteral("gfake"), QStringLiteral("drive"),
                             QStringLiteral("auth_url=") + base + QStringLiteral("/auth"), QStringLiteral("token_url=") + base + QStringLiteral("/token"),
                             QStringLiteral("scope=drive")},
                            drive, 60000);
    QCOMPARE(rc, 0);
    QVERIFY(!drive.stuck());
    QVERIFY(PaneroCloud::instance()->refreshNow());
    QVERIFY(PaneroCloud::instance()->remoteNames().contains(QStringLiteral("gfake")));
    const QStringList withToken = filesContaining(fakeToken);
    QVERIFY2(withToken.isEmpty(), qPrintable(withToken.join(QLatin1Char(' '))));
    PaneroCloudSetup::deleteRemote(QStringLiteral("gfake"));

    // OneDrive: the failing drive listing loops; the watcher stops it
    PaneroCloudSetup::SetupOutputWatcher onedrive;
    runSetup({QStringLiteral("config"), QStringLiteral("create"), QStringLiteral("odfake"), QStringLiteral("onedrive"),
              QStringLiteral("auth_url=") + base + QStringLiteral("/auth"), QStringLiteral("token_url=") + base + QStringLiteral("/token"),
              QStringLiteral("access_scopes=") + PaneroCloudUtils::oneDriveScopes(false, false), QStringLiteral("--contimeout"), QStringLiteral("3s"),
              QStringLiteral("--timeout"), QStringLiteral("5s"), QStringLiteral("--low-level-retries"), QStringLiteral("1")},
             onedrive, 90000);
    QVERIFY2(onedrive.stuck(), "rclone's endless retry was not detected");
    QVERIFY(onedrive.error().startsWith(QLatin1String("Failed to query available drives")));
    QVERIFY(!onedrive.error().contains(QString::fromLatin1(fakeToken)));
    // the sign-in is stored at that point, and the drive can be set without
    // running rclone's drive wizard again (which would loop)
    QVERIFY(dumpValue(QStringLiteral("odfake"), QStringLiteral("token")).contains(fakeToken));
    QElapsedTimer setTimer;
    setTimer.start();
    QString setError;
    QVERIFY2(PaneroCloudRevoke::setOneDriveDrive(QStringLiteral("odfake"), QStringLiteral("abc123"), QStringLiteral("personal"), &setError),
             qPrintable(setError));
    QVERIFY(setTimer.elapsed() < 20000);
    QCOMPARE(dumpValue(QStringLiteral("odfake"), QStringLiteral("drive_id")), QByteArray("abc123"));
    QCOMPARE(dumpValue(QStringLiteral("odfake"), QStringLiteral("drive_type")), QByteArray("personal"));
    QCOMPARE(filesContaining(fakeToken), QStringList());
    PaneroCloudSetup::deleteRemote(QStringLiteral("odfake"));
    server.kill();
    server.waitForFinished(3000);
}

void PaneroCloudSecurityTest::mountAndOfflineFiles()
{
    if (!PaneroCloud::fuseAvailable() || !QFileInfo(QStringLiteral("/dev/fuse")).isWritable())
        QSKIP("FUSE not usable here");
    // an "alias" remote pointing at a local folder stands in for a cloud drive
    const QString data = root() + QStringLiteral("/clouddata");
    QDir().mkpath(data + QStringLiteral("/docs/sub"));
    QDir().mkpath(data + QStringLiteral("/photos"));
    for (const QString &name : {QStringLiteral("docs/a.txt"), QStringLiteral("docs/sub/b.txt"), QStringLiteral("photos/p.jpg")}) {
        QFile f(data + QLatin1Char('/') + name);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray("content of ") + name.toUtf8() + '\n');
    }
    QCOMPARE(PaneroRclone::run({QStringLiteral("config"), QStringLiteral("create"), QStringLiteral("lc"), QStringLiteral("alias"),
                                QStringLiteral("remote=") + data}),
             0);
    PaneroCloud *cloud = PaneroCloud::instance();
    PaneroOffline *offline = PaneroOffline::instance();
    QVERIFY(cloud->refreshNow());
    const PaneroCloud::Entry e = cloud->entry(QStringLiteral("rclone:lc"));
    QCOMPARE(e.rcloneType, QStringLiteral("alias"));

    QSignalSpy mounted(cloud, &PaneroCloud::mounted);
    QSignalSpy failed(cloud, &PaneroCloud::mountFailed);
    cloud->mount(e);
    QTRY_VERIFY_WITH_TIMEOUT(mounted.count() == 1 || failed.count() == 1, 20000);
    if (!failed.isEmpty())
        QFAIL(qPrintable(failed.first().at(1).toString()));
    const QString dir = mounted.first().at(1).toUrl().toLocalFile();
    QCOMPARE(dir, runtimeDir() + QStringLiteral("/panero-cloud/lc"));

    // online-only until opened, then available on this device
    const QString photo = dir + QStringLiteral("/photos/p.jpg");
    const qint64 size = QFileInfo(photo).size();
    QCOMPARE(int(offline->state(photo, size, false)), int(PaneroOffline::State::OnlineOnly));
    QCOMPARE(int(offline->state(dir + QStringLiteral("/photos"), 0, true)), int(PaneroOffline::State::OnlineOnly));
    QCOMPARE(int(offline->state(root(), 0, true)), int(PaneroOffline::State::NotCloud));
    QFile through(photo);
    QVERIFY(through.open(QIODevice::ReadOnly));
    QVERIFY(through.readAll().startsWith("content of photos/p.jpg"));
    through.close();
    QTRY_COMPARE_WITH_TIMEOUT(int(offline->state(photo, size, false)), int(PaneroOffline::State::Available), 10000);
    QVERIFY(QDir(PaneroRclone::cacheDir(QStringLiteral("lc")) + QStringLiteral("/vfs")).exists()); // Panero's own cache folder

    // keep "docs" on this device
    QString error;
    QVERIFY2(offline->pin(QStringLiteral("lc"), QStringLiteral("docs"), &error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!offline->isBusy(QStringLiteral("lc")), 60000);
    PaneroOffline::PinStatus st = offline->status(QStringLiteral("lc"), QStringLiteral("docs"));
    QVERIFY2(st.error.isEmpty(), qPrintable(st.error));
    const QString mirror = PaneroOffline::mirrorPath(QStringLiteral("lc"), QStringLiteral("docs"));
    QCOMPARE(mirror, root() + QStringLiteral("/home/Cloud Offline/lc/docs"));
    QVERIFY(QFileInfo::exists(mirror + QStringLiteral("/a.txt")));
    QVERIFY(QFileInfo::exists(mirror + QStringLiteral("/sub/b.txt")));
    QCOMPARE(int(offline->state(dir + QStringLiteral("/docs/a.txt"), 0, false)), int(PaneroOffline::State::Pinned));
    QCOMPARE(int(offline->state(mirror + QStringLiteral("/a.txt"), 0, false)), int(PaneroOffline::State::Pinned));
    // opening the pinned folder in the drive opens the local copy
    QCOMPARE(offline->redirect(dir + QStringLiteral("/docs/sub")), mirror + QStringLiteral("/sub"));
    QCOMPARE(offline->redirect(dir + QStringLiteral("/photos")), dir + QStringLiteral("/photos"));
    QTRY_VERIFY_WITH_TIMEOUT(!offline->isBusy(QStringLiteral("lc")), 60000);

    // changes go both ways
    QFile local(mirror + QStringLiteral("/made-offline.txt"));
    QVERIFY(local.open(QIODevice::WriteOnly));
    local.write("offline edit\n");
    local.close();
    QFile remote(data + QStringLiteral("/docs/made-in-cloud.txt"));
    QVERIFY(remote.open(QIODevice::WriteOnly));
    remote.write("cloud edit\n");
    remote.close();
    offline->syncNow(QStringLiteral("lc"), QStringLiteral("docs"));
    QTRY_VERIFY_WITH_TIMEOUT(!offline->isBusy(QStringLiteral("lc")), 60000);
    st = offline->status(QStringLiteral("lc"), QStringLiteral("docs"));
    QVERIFY2(st.error.isEmpty(), qPrintable(st.error));
    QVERIFY(QFileInfo::exists(data + QStringLiteral("/docs/made-offline.txt")));
    QVERIFY(QFileInfo::exists(mirror + QStringLiteral("/made-in-cloud.txt")));

    // review finding 1: when bisync loses its state, nothing is resynced
    // automatically (a resync would let the cloud overwrite local edits)
    QFile edit(mirror + QStringLiteral("/a.txt"));
    QVERIFY(edit.open(QIODevice::WriteOnly | QIODevice::Truncate));
    edit.write("local edit that must survive\n");
    edit.close();
    {
        QDirIterator lst(PaneroRclone::cacheDir(QStringLiteral("lc")) + QStringLiteral("/bisync"), {QStringLiteral("*.lst*")}, QDir::Files,
                         QDirIterator::Subdirectories);
        int removed = 0;
        while (lst.hasNext())
            removed += QFile::remove(lst.next()) ? 1 : 0;
        QVERIFY(removed > 0);
    }
    offline->syncNow(QStringLiteral("lc"), QStringLiteral("docs"));
    QTRY_VERIFY_WITH_TIMEOUT(!offline->isBusy(QStringLiteral("lc")), 60000);
    st = offline->status(QStringLiteral("lc"), QStringLiteral("docs"));
    QVERIFY2(st.needsRepair, qPrintable(st.error));
    QVERIFY(edit.open(QIODevice::ReadOnly));
    QCOMPARE(edit.readAll(), QByteArray("local edit that must survive\n"));
    edit.close();
    QString blockingError;
    QVERIFY(!offline->syncAllBlocking(QStringLiteral("lc"), &blockingError)); // sign-out/unpin see the problem
    // a repair keeps the newer local edit and uploads it
    offline->repair(QStringLiteral("lc"), QStringLiteral("docs"));
    QTRY_VERIFY_WITH_TIMEOUT(!offline->isBusy(QStringLiteral("lc")), 60000);
    st = offline->status(QStringLiteral("lc"), QStringLiteral("docs"));
    QVERIFY2(st.error.isEmpty() && !st.needsRepair, qPrintable(st.error));
    QFile up(data + QStringLiteral("/docs/a.txt"));
    QVERIFY(up.open(QIODevice::ReadOnly));
    QCOMPARE(up.readAll(), QByteArray("local edit that must survive\n"));
    up.close();

    // a missing local copy is never synchronised as "delete everything"
    QVERIFY(QDir().rename(mirror, mirror + QStringLiteral(".moved")));
    offline->syncNow(QStringLiteral("lc"), QStringLiteral("docs"));
    QTRY_VERIFY_WITH_TIMEOUT(!offline->isBusy(QStringLiteral("lc")), 60000);
    QVERIFY(!offline->status(QStringLiteral("lc"), QStringLiteral("docs")).error.isEmpty());
    QVERIFY(QFileInfo::exists(data + QStringLiteral("/docs/a.txt")));
    QVERIFY(QDir().rename(mirror + QStringLiteral(".moved"), mirror));

    // free up space: final sync, local copy deleted, cloud untouched
    QVERIFY(offline->unpin(nullptr, QStringLiteral("lc"), QStringLiteral("docs")));
    QVERIFY(!QFileInfo::exists(mirror));
    QVERIFY(!QFileInfo::exists(PaneroOffline::mirrorRoot(QStringLiteral("lc"))));
    QVERIFY(QFileInfo::exists(data + QStringLiteral("/docs/a.txt")));
    QVERIFY(QFileInfo::exists(data + QStringLiteral("/docs/made-offline.txt")));
    QVERIFY(offline->pins(QStringLiteral("lc")).isEmpty());

    // review finding 8: pin paths from the configuration cannot leave the folder
    {
        KConfigGroup g(KSharedConfig::openConfig(root() + QStringLiteral("/config/panerorc")), QStringLiteral("Panero Cloud Offline lc"));
        g.writeEntry("Pins", QStringList{QStringLiteral("../.."), QStringLiteral("docs/../../x"), QStringLiteral("/etc")});
        g.sync();
        QVERIFY(offline->pins(QStringLiteral("lc")).isEmpty());
        g.writeEntry("Pins", QStringList());
        g.sync();
        QString why;
        QVERIFY(!offline->pin(QStringLiteral("lc"), QStringLiteral("../outside"), &why));
    }

    // review finding 3: a folder left from an earlier sign-in is never used
    // for a new remote of the same name
    offline->forgetRemote(QStringLiteral("lc"), false);
    QDir().mkpath(PaneroOffline::mirrorRoot(QStringLiteral("lc")) + QStringLiteral("/photos"));
    QFile old(PaneroOffline::mirrorRoot(QStringLiteral("lc")) + QStringLiteral("/photos/old-account.txt"));
    QVERIFY(old.open(QIODevice::WriteOnly));
    old.write("from the previous account\n");
    old.close();
    QString claimError;
    QVERIFY(!offline->pin(QStringLiteral("lc"), QStringLiteral("photos"), &claimError));
    QVERIFY(claimError.contains(QLatin1String("earlier sign-in")));
    QVERIFY(!QFileInfo::exists(data + QStringLiteral("/photos/old-account.txt")));
    QCOMPARE(int(offline->state(PaneroOffline::mirrorRoot(QStringLiteral("lc")) + QStringLiteral("/photos/old-account.txt"), 0, false)),
             int(PaneroOffline::State::NotCloud));
    QVERIFY(QDir(root() + QStringLiteral("/home/Cloud Offline")).removeRecursively());

    // review finding 2: files not uploaded yet are detected in the cache
    const QString meta = PaneroRclone::cacheDir(QStringLiteral("lc")) + QStringLiteral("/vfsMeta/local/x");
    QDir().mkpath(meta);
    QFile dirtyMeta(meta + QStringLiteral("/pending.txt"));
    QVERIFY(dirtyMeta.open(QIODevice::WriteOnly));
    dirtyMeta.write("{\"Size\": 3, \"Dirty\": true}");
    dirtyMeta.close();
    QCOMPARE(PaneroCloudUtils::dirtyCacheEntries(PaneroRclone::cacheDir(QStringLiteral("lc"))), 1);
    QVERIFY(QFile::remove(meta + QStringLiteral("/pending.txt")));
    QCOMPARE(PaneroCloudUtils::dirtyCacheEntries(PaneroRclone::cacheDir(QStringLiteral("lc"))), 0);

    // without "keep opened files", the cache goes away with the connection
    PaneroOffline::Settings s = offline->settings(QStringLiteral("lc"));
    s.keepOpened = false;
    offline->setSettings(QStringLiteral("lc"), s);
    cloud->unmount(e.id);
    QVERIFY(!QDir(PaneroRclone::cacheDir(QStringLiteral("lc")) + QStringLiteral("/vfs")).exists());
}

void PaneroCloudSecurityTest::removeDeletesCache()
{
    PaneroCloud *cloud = PaneroCloud::instance();
    QVERIFY(cloud->refreshNow());
    const PaneroCloud::Entry e = cloud->entry(QStringLiteral("rclone:nc"));
    QVERIFY(!e.id.isEmpty());
    QDir().mkpath(PaneroRclone::cacheDir(QStringLiteral("nc")) + QStringLiteral("/vfs/nc"));
    // nothing to revoke for WebDAV; the result never contains secrets
    const PaneroCloudRevoke::Result r = PaneroCloudRevoke::revoke(QStringLiteral("nc"), QStringLiteral("webdav"));
    QVERIFY(r.outcome == PaneroCloudRevoke::Outcome::NotNeeded || r.outcome == PaneroCloudRevoke::Outcome::Manual);
    QString error;
    QVERIFY2(cloud->removeEntry(e, &error), qPrintable(error));
    QVERIFY(!QFileInfo::exists(PaneroRclone::cacheDir(QStringLiteral("nc"))));
    QVERIFY(cloud->refreshNow());
    QVERIFY(!cloud->remoteNames().contains(QStringLiteral("nc")));
}

void PaneroCloudSecurityTest::importFromPlainConfig()
{
    const QString userConfig = PaneroCloudUtils::userRcloneConfigPath();
    QDir().mkpath(QFileInfo(userConfig).absolutePath());
    QFile f(userConfig);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("[imp]\ntype = alias\nremote = " + QFile::encodeName(root() + QStringLiteral("/clouddata")) + "\ntoken = {\"access_token\":\""
            + IMPORT_TOKEN + "\"}\n\n[keep]\ntype = memory\n\n[evilpc]\ntype = pcloud\nhostname = evil.example\ntoken = {\"access_token\":\"x\"}\n");
    f.close();
    QString error;
    const auto remotes = PaneroCloudSetup::listUserRemotes(&error);
    QCOMPARE(remotes.size(), 3);
    QStringList imported;
    QVERIFY2(PaneroCloudSetup::importRemotes({QStringLiteral("imp"), QStringLiteral("lc"), QStringLiteral("evilpc")}, true, &imported, &error),
             qPrintable(error));
    QVERIFY(!PaneroCloud::instance()->remoteNames().contains(QStringLiteral("evilpc"))); // review finding 7
    QCOMPARE(imported, QStringList{QStringLiteral("imp")}); // "lc" is not in the user's file
    QVERIFY(PaneroRclone::configIsEncrypted());
    QVERIFY(PaneroCloud::instance()->refreshNow());
    QVERIFY(PaneroCloud::instance()->remoteNames().contains(QStringLiteral("imp")));
    QVERIFY(PaneroCloud::instance()->remoteNames().contains(QStringLiteral("lc"))); // existing ones kept
    QCOMPARE(dumpValue(QStringLiteral("imp"), QStringLiteral("token")), QByteArray("{\"access_token\":\"") + IMPORT_TOKEN + "\"}");
    // removed from the old plain file, the other remote kept there
    QFile again(userConfig);
    QVERIFY(again.open(QIODevice::ReadOnly));
    const QByteArray left = again.readAll();
    QVERIFY(!left.contains("[imp]"));
    QVERIFY(left.contains("[keep]"));
    QCOMPARE(filesContaining(IMPORT_TOKEN), QStringList());
    QVERIFY(QDir(runtimeDir()).entryList({QStringLiteral("panero-cloud-import-*")}, QDir::AllEntries).isEmpty());
}

void PaneroCloudSecurityTest::missingKeyIsDetected()
{
    const QByteArray key = PaneroSecretStore::lookup(QStringLiteral("cloud-config-key"));
    QVERIFY(PaneroSecretStore::remove(QStringLiteral("cloud-config-key")));
    QString error;
    // never a new key over an existing encrypted configuration
    QCOMPARE(int(PaneroRclone::ensureStore(&error)), int(PaneroRclone::StoreState::KeyMissing));
    bool notFound = false;
    QVERIFY(PaneroSecretStore::lookup(QStringLiteral("cloud-config-key"), nullptr, &notFound).isEmpty());
    QVERIFY(notFound);
    // rclone fails cleanly without the key
    QByteArray err;
    QVERIFY(PaneroRclone::run({QStringLiteral("listremotes")}, nullptr, &err) != 0);
    // put it back, then start over
    QVERIFY(PaneroSecretStore::store(QStringLiteral("cloud-config-key"), QStringLiteral("Panero cloud sign-ins (encryption key)"), key));
    QCOMPARE(int(PaneroRclone::ensureStore(&error)), int(PaneroRclone::StoreState::Ready));
    QVERIFY(PaneroRclone::resetStore(&error));
    QVERIFY(!PaneroRclone::configExists());
    QCOMPARE(int(PaneroRclone::ensureStore(&error)), int(PaneroRclone::StoreState::Ready));
    const QByteArray newKey = PaneroSecretStore::lookup(QStringLiteral("cloud-config-key"));
    QVERIFY(newKey != key);
    m_scanner.addSecret(newKey);
}

void PaneroCloudSecurityTest::addCloudDialog()
{
    PaneroAddCloudDialog dialog;
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    auto *list = dialog.findChild<QListWidget *>();
    QVERIFY(list);
    QStringList titles;
    for (int i = 0; i < list->count(); ++i)
        titles << list->item(i)->text();
    QVERIFY(!titles.join(QLatin1Char(' ')).contains(QLatin1String("MEGA"))); // R7
    const auto visibleCheck = [&dialog](const QString &text) {
        for (QCheckBox *c : dialog.findChildren<QCheckBox *>()) {
            if (c->text().startsWith(text))
                return c->isVisible();
        }
        return false;
    };
    const auto description = [&dialog]() {
        QString all;
        for (QLabel *l : dialog.findChildren<QLabel *>())
            all += l->text();
        return all;
    };
    const QString shots = qEnvironmentVariable("PANERO_CLOUD_DIALOG_SHOTS");
    for (int row = 0; row < list->count(); ++row) {
        list->setCurrentRow(row);
        QTest::qWait(50);
        const QString title = list->item(row)->text();
        const bool scoped = title == QLatin1String("Google Drive") || title == QLatin1String("OneDrive");
        QCOMPARE(visibleCheck(QStringLiteral("Read-only access")), scoped);
        QCOMPARE(visibleCheck(QStringLiteral("Also files others shared")), title == QLatin1String("OneDrive"));
        if (row < 5) // OAuth providers: access is explained before signing in
            QVERIFY2(description().contains(QLatin1String("Panero never sees your password")), qPrintable(title));
        if (title == QLatin1String("OneDrive"))
            QVERIFY(description().contains(QLatin1String("not files others shared with you")));
        if (title.startsWith(QLatin1String("Nextcloud")))
            QVERIFY(description().contains(QLatin1String("app password")));
        if (!shots.isEmpty())
            dialog.grab().save(shots + QStringLiteral("/add-cloud-%1.png").arg(row));
    }
}

void PaneroCloudSecurityTest::noSecretsAnywhere()
{
    QTest::qWait(50);
    m_scanner.stop();
    QVERIFY2(m_scanner.scans() > 50, "the process scanner did not run");
    const QStringList hits = m_scanner.hits();
    QVERIFY2(hits.isEmpty(), qPrintable(hits.join(QLatin1Char('\n'))));
    QCOMPARE(filesContaining(WEBDAV_PASSWORD), QStringList());
    QCOMPARE(filesContaining(IMPORT_TOKEN), QStringList());
    QCOMPARE(filesContaining(PaneroSecretStore::lookup(QStringLiteral("cloud-config-key"))), QStringList());
}

QTEST_MAIN(PaneroCloudSecurityTest)

#include "panerocloudsecuritytest.moc"
