/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerocloudsetup.h"
#include "panerorclone.h"
#include "panerosecretstore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpServer>

#include <KI18n/KLocalizedString>

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

namespace
{
/// Runs Panero's rclone binary on another configuration file (the user's own,
/// or the import file) without Panero's global options.
int runOnConfig(const QString &config, const QStringList &args, bool withKey, QByteArray *out, QByteArray *err, int timeoutMs = 30000)
{
    QProcess p;
    QStringList all{QStringLiteral("--config"), config, QStringLiteral("--ask-password=false"), QStringLiteral("--color"), QStringLiteral("NEVER")};
    if (withKey) {
        // same helper as PaneroRclone::globalArgs()
        const QStringList global = PaneroRclone::globalArgs();
        const int i = global.indexOf(QStringLiteral("--password-command"));
        all << global.mid(i, 2);
    }
    p.setProgram(PaneroRclone::program());
    p.setArguments(all + args);
    p.setProcessEnvironment(PaneroRclone::environment());
    p.start();
    p.closeWriteChannel();
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        return -1;
    }
    if (out)
        *out = p.readAllStandardOutput();
    if (err)
        *err = p.readAllStandardError();
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

/// Overwrites a (small) file with zeros before deleting it.
void shredFile(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::ReadWrite)) {
        const qint64 size = f.size();
        const QByteArray zeros(4096, '\0');
        for (qint64 done = 0; done < size; done += zeros.size())
            f.write(zeros.constData(), qMin<qint64>(zeros.size(), size - done));
        f.flush();
        ::fsync(f.handle());
        f.close();
    }
    QFile::remove(path);
}

bool writePrivateFile(const QString &path, const QByteArray &data)
{
    const QByteArray p = QFile::encodeName(path);
    const int fd = ::open(p.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    qint64 written = 0;
    while (written < data.size()) {
        const ssize_t n = ::write(fd, data.constData() + written, size_t(data.size() - written));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            ::close(fd);
            return false;
        }
        written += n;
    }
    ::close(fd);
    return true;
}
} // namespace

namespace PaneroCloudSetup
{
bool callbackPortFree(QString *why)
{
    QTcpServer v4;
    if (!v4.listen(QHostAddress::LocalHost, CALLBACK_PORT)) {
        if (why)
            *why = i18n("Another program is using the sign-in port %1 on this computer (127.0.0.1). Close it and try again.", QString::number(CALLBACK_PORT));
        return false;
    }
    v4.close();
    QTcpServer v6;
    if (!v6.listen(QHostAddress::LocalHostIPv6, CALLBACK_PORT) && v6.serverError() == QAbstractSocket::AddressInUseError) {
        if (why)
            *why = i18n("Another program is using the sign-in port %1 on this computer (::1). Close it and try again.", QString::number(CALLBACK_PORT));
        return false;
    }
    v6.close();
    return true;
}

QStringList oauthOptions(const QString &type, bool readOnly, bool includeShared)
{
    if (type == QLatin1String("drive"))
        return {QStringLiteral("scope=") + (readOnly ? QStringLiteral("drive.readonly") : QStringLiteral("drive"))};
    if (type == QLatin1String("onedrive"))
        // not disable_site_permission: rclone then replaces access_scopes by
        // its broad Files.*.All list; without Sites.* SharePoint stays out anyway
        return {QStringLiteral("access_scopes=") + PaneroCloudUtils::oneDriveScopes(readOnly, includeShared)};
    return {};
}

bool SetupOutputWatcher::feed(const QByteArray &chunk)
{
    m_buffer += chunk;
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(nl).trimmed();
        // overwrite the consumed part: rclone's stdout ends with the new configuration
        for (int i = 0; i <= nl; ++i)
            m_buffer[i] = '\0';
        m_buffer.remove(0, nl + 1);
        if (line.startsWith("Failed to ")) {
            ++m_failures;
            if (m_error.isEmpty())
                m_error = PaneroCloudUtils::redactSecrets(QString::fromUtf8(line)).left(400);
        }
        PaneroSecretStore::wipe(line);
    }
    return stuck();
}

void SetupOutputWatcher::clear()
{
    PaneroSecretStore::wipe(m_buffer);
    m_failures = 0;
    m_error.clear();
}

QString accessDescription(const QString &type, bool readOnly, bool includeShared)
{
    const QString notMail = i18n("It cannot read your e-mail, contacts or calendar and cannot change your password. "
                                 "You can withdraw the access at any time in your account settings or with Remove in Panero.");
    if (type == QLatin1String("drive"))
        return (readOnly ? i18n("Panero (through rclone) will be able to <b>see and download</b> all files in your Google Drive.")
                         : i18n("Panero (through rclone) will be able to <b>see, upload, change and delete</b> all files in your Google Drive."))
            + QLatin1Char(' ') + notMail;
    if (type == QLatin1String("onedrive")) {
        QString what = readOnly ? i18n("Panero (through rclone) will be able to <b>see and download</b> the files in your OneDrive")
                                : i18n("Panero (through rclone) will be able to <b>see, upload, change and delete</b> the files in your OneDrive");
        what += includeShared ? i18n(", including files others shared with you.") : i18n(" (not files others shared with you, not SharePoint).");
        return what + QLatin1Char(' ') + i18n("It cannot read your Outlook e-mail, contacts or calendar and cannot change your password. "
                                              "You can withdraw the access at any time in your Microsoft account or with Remove in Panero.");
    }
    if (type == QLatin1String("dropbox"))
        return i18n("Panero (through rclone) will be able to <b>see, upload, change, delete and share</b> all files in your Dropbox "
                    "(Dropbox offers no narrower access for rclone).")
            + QLatin1Char(' ') + notMail;
    if (type == QLatin1String("pcloud"))
        return i18n("Panero (through rclone) will have <b>full access</b> to the files of your pCloud account (pCloud offers no narrower "
                    "access). pCloud sign-ins do not expire: use Remove in Panero to cancel it.")
            + QLatin1Char(' ') + notMail;
    if (type == QLatin1String("box"))
        return i18n("Panero (through rclone) will be able to <b>see, upload, change and delete</b> all files in your Box account.")
            + QLatin1Char(' ') + notMail;
    return QString();
}

QString passwordProblem(const QString &password)
{
    if (password.isEmpty())
        return i18n("Enter the password.");
    if (password.contains(QLatin1Char('\n')) || password.contains(QLatin1Char('\r')))
        return i18n("The password cannot contain line breaks.");
    if (password != password.trimmed())
        return i18n("The password cannot start or end with a space.");
    return QString();
}

QString webDavAddress(const QString &address, const QString &vendor, const QString &user)
{
    QString a = address.trimmed();
    if (!a.contains(QLatin1String("://")))
        a.prepend(QLatin1String("https://"));
    while (a.endsWith(QLatin1Char('/')))
        a.chop(1);
    if (vendor == QLatin1String("nextcloud") && !a.contains(QLatin1String("/remote.php/")))
        a += QLatin1String("/remote.php/dav/files/") + QString::fromUtf8(QUrl::toPercentEncoding(user.trimmed()));
    else if (vendor == QLatin1String("owncloud") && !a.contains(QLatin1String("/remote.php/")))
        a += QLatin1String("/remote.php/webdav");
    return a;
}

bool createWebDavRemote(const QString &remote, const QString &url, const QString &vendor, const QString &user, const QString &password,
                        QString *error)
{
    const QString problem = passwordProblem(password);
    if (!problem.isEmpty()) {
        if (error)
            *error = problem;
        return false;
    }
    // rclone asks (with url, vendor and user given): pass (y = type it, twice),
    // bearer_token (empty), "Edit advanced config?" (n). The answers go to
    // stdin, so the password never appears in a process list.
    QByteArray input = "y\n" + password.toUtf8() + '\n' + password.toUtf8() + "\n\nn\n";
    QByteArray err;
    const int rc = PaneroRclone::run({QStringLiteral("config"), QStringLiteral("create"), remote, QStringLiteral("webdav"),
                                      QStringLiteral("url=") + url, QStringLiteral("vendor=") + vendor, QStringLiteral("user=") + user.trimmed(),
                                      QStringLiteral("--all")},
                                     nullptr, &err, 30000, input);
    PaneroSecretStore::wipe(input);
    if (rc != 0) {
        deleteRemote(remote);
        if (error)
            *error = PaneroCloudUtils::rcloneErrorSummary(QString::fromUtf8(err));
        return false;
    }
    // check what rclone stored (catches a changed question order in a new rclone)
    QByteArray dump;
    PaneroRclone::run({QStringLiteral("config"), QStringLiteral("dump")}, &dump, nullptr, 20000);
    const QJsonObject o = QJsonDocument::fromJson(dump).object().value(remote).toObject();
    PaneroSecretStore::wipe(dump);
    const bool ok = o.value(QStringLiteral("type")).toString() == QLatin1String("webdav") && o.value(QStringLiteral("url")).toString() == url
        && o.value(QStringLiteral("user")).toString() == user.trimmed() && !o.value(QStringLiteral("pass")).toString().isEmpty()
        && o.value(QStringLiteral("bearer_token")).toString().isEmpty();
    if (!ok) {
        deleteRemote(remote);
        if (error)
            *error = i18n("rclone did not store the connection as expected.");
        return false;
    }
    return true;
}

bool checkPcloudHost(const QString &remote, QString *error)
{
    QByteArray out;
    if (PaneroRclone::run({QStringLiteral("config"), QStringLiteral("redacted"), remote}, &out, nullptr, 20000) != 0) {
        if (error)
            *error = i18n("The stored pCloud settings could not be read.");
        return false;
    }
    QString host = PaneroCloudUtils::iniValue(QString::fromUtf8(out), QStringLiteral("hostname"));
    if (host.isEmpty())
        host = QStringLiteral("api.pcloud.com");
    if (!PaneroCloudUtils::isAllowedPcloudHost(host)) {
        if (error)
            *error = i18n("The sign-in named an unexpected server (%1) instead of pCloud. It was not saved.", host.toHtmlEscaped());
        return false;
    }
    return true;
}

bool verifyRemote(const QString &remote, QString *error)
{
    QByteArray err;
    const int rc = PaneroRclone::run({QStringLiteral("lsd"), remote + QLatin1Char(':'), QStringLiteral("--contimeout"), QStringLiteral("15s")},
                                     nullptr, &err, 60000);
    if (rc == 0)
        return true;
    if (error)
        *error = PaneroCloudUtils::rcloneErrorSummary(QString::fromUtf8(err), 1);
    return false;
}

void deleteRemote(const QString &remote)
{
    PaneroRclone::run({QStringLiteral("config"), QStringLiteral("delete"), remote}, nullptr, nullptr, 20000);
}

QList<PaneroCloudUtils::Remote> listUserRemotes(QString *error)
{
    const QString path = PaneroCloudUtils::userRcloneConfigPath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = i18n("No rclone configuration was found (%1).", path);
        return {};
    }
    if (PaneroCloudUtils::isEncryptedRcloneConfig(f.read(4096))) {
        if (error)
            *error = i18n("Your rclone configuration (%1) is encrypted with your own password, so Panero cannot import from it. "
                          "Set the clouds up again in Panero instead.",
                          path);
        return {};
    }
    QByteArray out, err;
    if (runOnConfig(path, {QStringLiteral("listremotes"), QStringLiteral("--long")}, false, &out, &err) != 0) {
        if (error)
            *error = PaneroCloudUtils::rcloneErrorSummary(QString::fromUtf8(err));
        return {};
    }
    return PaneroCloudUtils::parseListRemotes(QString::fromUtf8(out));
}

bool importRemotes(const QStringList &names, bool deleteFromSource, QStringList *imported, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (names.isEmpty())
        return fail(i18n("Nothing selected."));
    if (qEnvironmentVariableIsEmpty("XDG_RUNTIME_DIR"))
        return fail(i18n("Importing needs a private runtime folder ($XDG_RUNTIME_DIR), which this session does not have."));
    const QString userPath = PaneroCloudUtils::userRcloneConfigPath();

    QByteArray userDump, err;
    if (runOnConfig(userPath, {QStringLiteral("config"), QStringLiteral("dump")}, false, &userDump, &err) != 0)
        return fail(PaneroCloudUtils::rcloneErrorSummary(QString::fromUtf8(err)));
    QJsonObject source = QJsonDocument::fromJson(userDump).object();
    PaneroSecretStore::wipe(userDump);

    QJsonObject target;
    if (PaneroRclone::configExists()) {
        QByteArray dump;
        if (PaneroRclone::run({QStringLiteral("config"), QStringLiteral("dump")}, &dump, &err, 20000) != 0) {
            PaneroSecretStore::wipe(dump);
            return fail(PaneroCloudUtils::rcloneErrorSummary(QString::fromUtf8(err)));
        }
        target = QJsonDocument::fromJson(dump).object();
        PaneroSecretStore::wipe(dump);
    }
    QStringList done;
    for (const QString &name : names) {
        if (!source.contains(name))
            continue;
        const QJsonObject options = source.value(name).toObject();
        if (options.value(QStringLiteral("type")).toString() == QLatin1String("pcloud")) {
            const QString host = options.value(QStringLiteral("hostname")).toString();
            if (!host.isEmpty() && !PaneroCloudUtils::isAllowedPcloudHost(host))
                continue; // would send the token to a foreign server
        }
        const QString newName = PaneroCloudUtils::uniqueRemoteName(name, target.keys());
        target.insert(newName, source.value(name));
        done << newName;
    }
    source = QJsonObject();
    if (done.isEmpty())
        return fail(i18n("The selected remotes were not found."));

    // assemble and encrypt in memory-backed private storage
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString tmpDir = runtime + QLatin1String("/panero-cloud-import-") + QString::number(QRandomGenerator::system()->generate64(), 16);
    if (!QDir().mkdir(tmpDir))
        return fail(i18n("Could not create a private folder for the import."));
    QFile::setPermissions(tmpDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString tmpFile = tmpDir + QLatin1String("/rclone.conf");
    QByteArray ini = PaneroCloudUtils::rcloneIniFromDump(target);
    target = QJsonObject();
    const bool written = writePrivateFile(tmpFile, ini);
    PaneroSecretStore::wipe(ini);
    const auto cleanup = [&]() {
        shredFile(tmpFile);
        QDir(tmpDir).removeRecursively();
    };
    if (!written) {
        cleanup();
        return fail(i18n("Could not write the import file."));
    }
    if (runOnConfig(tmpFile, {QStringLiteral("config"), QStringLiteral("encryption"), QStringLiteral("set")}, true, nullptr, &err, 120000) != 0) {
        cleanup();
        return fail(PaneroCloudUtils::rcloneErrorSummary(QString::fromUtf8(err)));
    }
    QFile check(tmpFile);
    const bool encrypted = check.open(QIODevice::ReadOnly) && PaneroCloudUtils::isEncryptedRcloneConfig(check.read(4096));
    check.close();
    QByteArray listed;
    if (!encrypted || runOnConfig(tmpFile, {QStringLiteral("listremotes")}, true, &listed, &err) != 0) {
        cleanup();
        return fail(i18n("The imported configuration could not be encrypted."));
    }

    // move the encrypted file into place (same file system for the final rename)
    QFile in(tmpFile);
    QByteArray encryptedData;
    if (in.open(QIODevice::ReadOnly))
        encryptedData = in.readAll();
    in.close();
    cleanup();
    const QString staging = PaneroRclone::configPath() + QLatin1String(".import");
    QFile::remove(staging);
    if (encryptedData.isEmpty() || !PaneroCloudUtils::isEncryptedRcloneConfig(encryptedData.left(4096)) || !writePrivateFile(staging, encryptedData)
        || std::rename(QFile::encodeName(staging).constData(), QFile::encodeName(PaneroRclone::configPath()).constData()) != 0) {
        QFile::remove(staging);
        return fail(i18n("The imported configuration could not be saved."));
    }

    if (deleteFromSource) {
        for (const QString &name : names)
            runOnConfig(userPath, {QStringLiteral("config"), QStringLiteral("delete"), name}, false, nullptr, nullptr);
    }
    if (imported)
        *imported = done;
    return true;
}

QStringList interactiveConfigCommand()
{
    return QStringList{QStringLiteral("x-terminal-emulator"), QStringLiteral("-e"), PaneroRclone::program()} + PaneroRclone::globalArgs()
        + QStringList{QStringLiteral("config")};
}
} // namespace PaneroCloudSetup
