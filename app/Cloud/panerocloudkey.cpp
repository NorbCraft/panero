/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
 * panero-cloud-key: rclone's --password-command for Panero's encrypted cloud
 * configuration (dev/CLOUD-SECURITY-AUDIT.md R2). Reads the key from the
 * desktop keyring and writes it to stdout, which is a pipe to rclone. Never
 * creates keys, never writes anything to disk, and refuses to print the key
 * to a terminal so it does not end up on screen or in screenshots.
 */

#include "panerosecretstore.h"

#include <QCoreApplication>

#include <cstdio>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (isatty(STDOUT_FILENO)) {
        std::fputs("panero-cloud-key: this helper gives the key of Panero's cloud configuration to Panero's rclone; "
                   "it does not print it to a terminal.\n",
                   stderr);
        return 2;
    }
    QCoreApplication app(argc, argv);
    QString error;
    bool notFound = false;
    QByteArray key = PaneroSecretStore::lookup(QStringLiteral("cloud-config-key"), &error, &notFound);
    if (key.isEmpty()) {
        const QByteArray msg = notFound ? QByteArrayLiteral("the key is not in the keyring") : error.toUtf8();
        std::fprintf(stderr, "panero-cloud-key: %s\n", msg.constData());
        return 1;
    }
    key.append('\n');
    const bool ok = std::fwrite(key.constData(), 1, size_t(key.size()), stdout) == size_t(key.size()) && std::fflush(stdout) == 0;
    PaneroSecretStore::wipe(key);
    return ok ? 0 : 1;
}
