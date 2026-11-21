/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANERORCLONE_H
#define PANERORCLONE_H

#include <QByteArray>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

class QProcess;

/**
 * How Panero runs rclone (dev/CLOUD-SECURITY-AUDIT.md R1-R3, R6, R8):
 *
 * - Panero's own rclone binary (shipped in the package, pinned and verified at
 *   build time), never the distribution's rclone. Older than 1.75.2 is refused.
 * - Panero's own configuration ~/.config/panero/cloud/rclone.conf (folder 0700,
 *   file 0600), separate from the user's ~/.config/rclone/rclone.conf, and
 *   always encrypted with rclone's configuration encryption.
 * - The encryption key is 32 random bytes kept in the desktop keyring
 *   (PaneroSecretStore). rclone gets it only from the panero-cloud-key helper
 *   (--password-command), never through arguments, environment or files.
 * - rclone never prompts (--ask-password=false), never colours its output and
 *   gets an environment without any inherited RCLONE_* variable.
 * - Caches live below ~/.cache/panero/cloud/<remote>.
 */
namespace PaneroRclone
{
/// Oldest rclone accepted (fixes CVE-2026-116194, pCloud sign-in CSRF).
QString minimumVersion();

/// Panero's rclone binary, or an empty string when it is not installed.
/// Order: $PANERO_RCLONE (development and tests), <exe dir>/panero-rclone
/// (build tree), the installed copy in Panero's libexec folder.
QString program();

/// "1.76.0" for program(), empty when it cannot be run. Cached per file.
QString version();

/// program() exists and is at least minimumVersion().
bool usable(QString *problem = nullptr);

/// The panero-cloud-key helper (<exe dir>, next to $PANERO_RCLONE, else
/// Panero's libexec folder).
QString keyHelper();

QString configDir();
QString configPath();
/// ~/.cache/panero/cloud (cacheDir(remote) = <base>/<remote>).
QString cacheBase();
QString cacheDir(const QString &remote);

/// The options every rclone call gets (config, key helper, no prompts, no colours).
QStringList globalArgs();
/// The system environment without RCLONE_* variables.
QProcessEnvironment environment();
/// Sets program, arguments (globalArgs() + @p args) and environment on @p process.
void prepare(QProcess &process, const QStringList &args);

/// True when the configuration file exists and is encrypted.
bool configIsEncrypted();
/// True when Panero's configuration file exists (encrypted or not).
bool configExists();

enum class StoreState { Ready, NoRclone, NoKeyring, KeyringLocked, KeyMissing, Failed };

/**
 * Makes sure the key is in the keyring and the configuration is encrypted:
 * creates both on first use, encrypts a plain configuration. Never creates a
 * new key while an encrypted configuration exists (KeyMissing: the old one
 * cannot be read any more; see resetStore()). Blocking (keyring prompts).
 */
StoreState ensureStore(QString *error = nullptr);

/// Deletes the configuration and the key (all clouds are forgotten locally).
bool resetStore(QString *error = nullptr);

/**
 * Runs rclone synchronously with globalArgs(). Returns the exit code, or -1
 * when it could not be started or timed out. @p input goes to stdin (then
 * stdin is closed). Output that may contain secrets must be wiped by the caller.
 */
int run(const QStringList &args, QByteArray *out = nullptr, QByteArray *err = nullptr, int timeoutMs = 30000,
        const QByteArray &input = QByteArray());
}

#endif // PANERORCLONE_H
