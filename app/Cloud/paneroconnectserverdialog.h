/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROCONNECTSERVERDIALOG_H
#define PANEROCONNECTSERVERDIALOG_H

#include "panerocloud.h"

#include <QDialog>
#include <QPointer>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class KJob;

/**
 * "Connect to Server..." - adds a WebDAV / SFTP / SMB / FTP connection (a KIO
 * URL) to the Cloud section. No password is asked or stored here: KIO asks
 * when the server needs one and can keep it in KWallet.
 *
 * Usage:
 *     PaneroConnectServerDialog dlg(parent);
 *     if (dlg.exec() == QDialog::Accepted)
 *         openUrl(dlg.createdEntry().url);
 */
class PaneroConnectServerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PaneroConnectServerDialog(QWidget *parent = nullptr);
    ~PaneroConnectServerDialog() override;

    /// The URL built from the fields (invalid while incomplete).
    QUrl url() const;
    /// The added entry (valid after Accepted).
    PaneroCloud::Entry createdEntry() const { return m_created; }

    /// Protocols offered in the Type list: {scheme, title}.
    static QList<QPair<QString, QString>> serverTypes();

private slots:
    void updateState();
    void testConnection();
    void testFinished(KJob *job);
    void testTimedOut();
    void addClicked();

private:
    QComboBox *m_type;
    QLineEdit *m_server;
    QSpinBox *m_port;
    QLineEdit *m_folder;
    QLineEdit *m_user;
    QLineEdit *m_label;
    QLabel *m_preview;
    QLabel *m_testResult;
    QPushButton *m_testButton;
    QDialogButtonBox *m_buttons;
    QPointer<KJob> m_testJob;
    QTimer *m_testTimer;
    PaneroCloud::Entry m_created;
};

#endif // PANEROCONNECTSERVERDIALOG_H
