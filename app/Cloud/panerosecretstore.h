/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROSECRETSTORE_H
#define PANEROSECRETSTORE_H

#include <QByteArray>
#include <QString>

/**
 * Minimal client of the desktop keyring through the freedesktop Secret Service
 * D-Bus API (org.freedesktop.secrets): gnome-keyring on GNOME/Zorin, KWallet
 * (kwalletd with its Secret Service interface) on KDE, KeePassXC, ...
 *
 * Items are identified by the attributes {"application": "panero",
 * "panero-secret": <key>}. Secrets travel over the user's private session bus
 * ("plain" session) and are never written anywhere by Panero.
 *
 * Only used for the key of Panero's encrypted rclone configuration (see
 * dev/CLOUD-SECURITY-AUDIT.md, R2). Blocking calls; a locked keyring makes the
 * keyring show its own unlock prompt.
 */
namespace PaneroSecretStore
{
/// True when a Secret Service is running or can be D-Bus activated.
bool isAvailable();

/// Reads the secret stored under @p key. Returns an empty array when it is
/// missing (then *notFound is set) or on error (*error is set).
QByteArray lookup(const QString &key, QString *error = nullptr, bool *notFound = nullptr);

/// Stores (or replaces) the secret under @p key in the default collection.
bool store(const QString &key, const QString &label, const QByteArray &secret, QString *error = nullptr);

/// Deletes the secret stored under @p key (true when nothing was stored).
bool remove(const QString &key, QString *error = nullptr);

/// Overwrites the bytes of @p data before it is released.
void wipe(QByteArray &data);
void wipe(QString &data);
}

#endif // PANEROSECRETSTORE_H
