/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROOFFLINEDIALOG_H
#define PANEROOFFLINEDIALOG_H

#include <QDialog>
#include <QUrl>

class QCheckBox;
class QSpinBox;
class QTreeWidget;
class QPushButton;

/**
 * "Offline Files" of one cloud drive (dev/CLOUD-SECURITY-AUDIT.md R10):
 * settings (keep opened files, cache limit, delete local copies on sign-out)
 * and the folders kept on this device with their synchronisation state.
 */
class PaneroOfflineDialog : public QDialog
{
    Q_OBJECT
public:
    PaneroOfflineDialog(const QString &remote, const QString &label, QWidget *parent = nullptr);

    /// A folder the user asked to open (valid after the dialog closed with it).
    QUrl urlToOpen() const { return m_open; }

private:
    void reload();
    void save();
    QString selectedPin() const;

    QString m_remote;
    QCheckBox *m_keepOpened;
    QSpinBox *m_limit;
    QCheckBox *m_removeOnSignOut;
    QTreeWidget *m_pins;
    QPushButton *m_sync;
    QPushButton *m_free;
    QPushButton *m_repair;
    QPushButton *m_openPin;
    QPushButton *m_clearCache;
    QUrl m_open;
};

#endif // PANEROOFFLINEDIALOG_H
