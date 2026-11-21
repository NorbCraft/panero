/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerorclone.h"
#include "panerocloudutils.h"
#include "panerosecretstore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>

#ifndef PANERO_LIBEXEC_DIR
#define PANERO_LIBEXEC_DIR "/usr/lib/panero"
#endif

namespace
{
const char KEY_NAME[] = "cloud-config-key";
const int KEY_BYTES = 32;

QString appDirFile(const QString &name)
{
    if (!QCoreApplication::instance())
        return QString();
    const QString path = QCoreApplication::applicationDirPath() + QLatin1Char('/') + name;
    const QFileInfo fi(path);
    return fi.isFile() && fi.isExecutable() ? fi.absoluteFilePath() : QString();
}

QString libexecFile(const QString &name)
{
    const QFileInfo fi(QLatin1String(PANERO_LIBEXEC_DIR) + QLatin1Char('/') + name);
    return fi.isFile() && fi.isExecutable() ? fi.absoluteFilePath() : QString();
}

/// rclone splits --password-command like a shell word list.
QString quotedCommand(const QString &path)
{
    if (!path.contains(QLatin1Char(' ')) && !path.contains(QLatin1Char('"')))
        return path;
    QString q = path;
    q.replace(QLatin1Char('"'), QLatin1String("\"\""));
    return QLatin1Char('"') + q + QLatin1Char('"');
}

void restrictPermissions()
{
    QFile::setPermissions(PaneroRclone::configDir(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    if (QFileInfo::exists(PaneroRclone::configPath()))
        QFile::setPermissions(PaneroRclone::configPath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}
} // namespace

namespace PaneroRclone
{
QString minimumVersion()
{
    return QStringLiteral("1.75.2");
}

QString program()
{
    const QString env = qEnvironmentVariable("PANERO_RCLONE");
    if (!env.isEmpty()) {
        const QFileInfo fi(env);
        return fi.isFile() && fi.isExecutable() ? fi.absoluteFilePath() : QString();
    }
    QString p = appDirFile(QStringLiteral("panero-rclone"));
    if (p.isEmpty())
        p = libexecFile(QStringLiteral("rclone"));
    return p;
}

QString version()
{
    static QHash<QString, QPair<qint64, QString>> cache; // path -> (mtime, version)
    const QString p = program();
    if (p.isEmpty())
        return QString();
    const qint64 mtime = QFileInfo(p).lastModified().toMSecsSinceEpoch();
    const auto it = cache.constFind(p);
    if (it != cache.constEnd() && it->first == mtime)
        return it->second;
    QProcess proc;
    proc.setProcessEnvironment(environment());
    proc.start(p, {QStringLiteral("version")});
    QString v;
    if (proc.waitForFinished(10000) && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0)
        v = PaneroCloudUtils::parseRcloneVersion(QString::fromUtf8(proc.readAllStandardOutput()));
    else
        proc.kill();
    cache.insert(p, qMakePair(mtime, v));
    return v;
}

bool usable(QString *problem)
{
    if (program().isEmpty()) {
        if (problem)
            *problem = QCoreApplication::translate("PaneroRclone", "Panero's rclone component is not installed. Reinstall Panero.");
        return false;
    }
    const QString v = version();
    if (v.isEmpty() || PaneroCloudUtils::compareVersions(v, minimumVersion()) < 0) {
        if (problem)
            *problem = QCoreApplication::translate("PaneroRclone", "Panero's rclone component is too old (version %1, at least %2 is needed).")
                           .arg(v.isEmpty() ? QStringLiteral("?") : v, minimumVersion());
        return false;
    }
    return true;
}

QString keyHelper()
{
    QString p = appDirFile(QStringLiteral("panero-cloud-key"));
    if (p.isEmpty() && !qEnvironmentVariableIsEmpty("PANERO_RCLONE")) {
        // development runs from a staged install: next to the given rclone
        const QFileInfo fi(QFileInfo(qEnvironmentVariable("PANERO_RCLONE")).absolutePath() + QLatin1String("/panero-cloud-key"));
        if (fi.isFile() && fi.isExecutable())
            p = fi.absoluteFilePath();
    }
    if (p.isEmpty())
        p = libexecFile(QStringLiteral("panero-cloud-key"));
    return p;
}

QString configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/panero/cloud");
}

QString configPath()
{
    return configDir() + QLatin1String("/rclone.conf");
}

QString cacheBase()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QLatin1String("/panero/cloud");
}

QString cacheDir(const QString &remote)
{
    return cacheBase() + QLatin1Char('/') + PaneroCloudUtils::safeFileName(remote);
}

QStringList globalArgs()
{
    return {QStringLiteral("--config"), configPath(), QStringLiteral("--password-command"), quotedCommand(keyHelper()),
            QStringLiteral("--ask-password=false"), QStringLiteral("--color"), QStringLiteral("NEVER")};
}

QProcessEnvironment environment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QStringList keys = env.keys();
    for (const QString &k : keys) {
        if (k.startsWith(QLatin1String("RCLONE_")) || k.startsWith(QLatin1String("_RCLONE_")))
            env.remove(k);
    }
    return env;
}

void prepare(QProcess &process, const QStringList &args)
{
    process.setProgram(program());
    process.setArguments(globalArgs() + args);
    process.setProcessEnvironment(environment());
}

bool configExists()
{
    return QFileInfo(configPath()).size() > 0;
}

bool configIsEncrypted()
{
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return PaneroCloudUtils::isEncryptedRcloneConfig(f.read(4096));
}

StoreState ensureStore(QString *error)
{
    QString problem;
    if (!usable(&problem)) {
        if (error)
            *error = problem;
        return StoreState::NoRclone;
    }
    if (keyHelper().isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("PaneroRclone", "Panero's keyring helper (panero-cloud-key) is not installed. Reinstall Panero.");
        return StoreState::NoRclone;
    }
    if (!PaneroSecretStore::isAvailable()) {
        if (error)
            *error = QCoreApplication::translate("PaneroRclone",
                                                 "No keyring is available. Panero keeps the key of your cloud sign-ins in the desktop "
                                                 "keyring (GNOME Keyring or KWallet) and does not store them without it.");
        return StoreState::NoKeyring;
    }
    if (!QDir().mkpath(configDir())) {
        if (error)
            *error = QCoreApplication::translate("PaneroRclone", "Could not create the folder %1.").arg(configDir());
        return StoreState::Failed;
    }
    restrictPermissions();

    QString keyError;
    bool notFound = false;
    QByteArray key = PaneroSecretStore::lookup(QLatin1String(KEY_NAME), &keyError, &notFound);
    if (key.isEmpty()) {
        if (!notFound) {
            if (error)
                *error = QCoreApplication::translate("PaneroRclone", "The keyring could not be opened: %1").arg(keyError);
            return StoreState::KeyringLocked;
        }
        if (configExists() && configIsEncrypted()) {
            if (error)
                *error = QCoreApplication::translate("PaneroRclone",
                                                     "The key of your saved cloud sign-ins is missing from the keyring, so they "
                                                     "cannot be opened any more.");
            return StoreState::KeyMissing;
        }
        QByteArray raw(KEY_BYTES, Qt::Uninitialized);
        QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(raw.data()), KEY_BYTES / int(sizeof(quint32)));
        key = raw.toHex();
        PaneroSecretStore::wipe(raw);
        const bool stored = PaneroSecretStore::store(QLatin1String(KEY_NAME), QStringLiteral("Panero cloud sign-ins (encryption key)"), key,
                                                     &keyError);
        if (!stored) {
            PaneroSecretStore::wipe(key);
            if (error)
                *error = QCoreApplication::translate("PaneroRclone", "The key could not be saved in the keyring: %1").arg(keyError);
            return StoreState::KeyringLocked;
        }
    }
    PaneroSecretStore::wipe(key);

    if (!configIsEncrypted()) {
        if (!QFileInfo::exists(configPath())) {
            // created by us with 0600: rclone keeps the mode of an existing file
            QFile f(configPath());
            if (!f.open(QIODevice::WriteOnly)) {
                if (error)
                    *error = QCoreApplication::translate("PaneroRclone", "Could not create %1.").arg(configPath());
                return StoreState::Failed;
            }
            f.close();
            restrictPermissions();
        }
        QByteArray err;
        const int rc = run({QStringLiteral("config"), QStringLiteral("encryption"), QStringLiteral("set")}, nullptr, &err, 120000);
        restrictPermissions();
        if (rc != 0 || !configIsEncrypted()) {
            if (error)
                *error = QCoreApplication::translate("PaneroRclone", "The cloud configuration could not be encrypted: %1")
                             .arg(PaneroCloudUtils::rcloneErrorSummary(QString::fromUtf8(err)));
            return StoreState::Failed;
        }
    }
    restrictPermissions();
    return StoreState::Ready;
}

bool resetStore(QString *error)
{
    QFile::remove(configPath());
    return PaneroSecretStore::remove(QLatin1String(KEY_NAME), error);
}

int run(const QStringList &args, QByteArray *out, QByteArray *err, int timeoutMs, const QByteArray &input)
{
    QProcess p;
    prepare(p, args);
    p.start();
    if (!p.waitForStarted(10000))
        return -1;
    if (!input.isNull())
        p.write(input);
    p.closeWriteChannel();
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        if (err)
            *err = QByteArrayLiteral("ERROR : rclone did not answer in time.");
        return -1;
    }
    if (out)
        *out = p.readAllStandardOutput();
    if (err)
        *err = p.readAllStandardError();
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}
} // namespace PaneroRclone
