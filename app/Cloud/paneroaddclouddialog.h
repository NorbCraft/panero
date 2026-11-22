/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROADDCLOUDDIALOG_H
#define PANEROADDCLOUDDIALOG_H

#include "panerocloud.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QFormLayout;
class QPushButton;
class KMessageWidget;

/**
 * "Add Cloud..." - creates a remote in Panero's encrypted rclone configuration
 * (dev/CLOUD-SECURITY-AUDIT.md).
 *
 * - OAuth services (Google Drive, OneDrive, Dropbox, pCloud, Box): the user
 *   signs in on the provider's page in the web browser; Panero explains the
 *   access first and offers read-only access where the provider has it.
 * - Nextcloud/WebDAV: address, user and (app) password; the password goes to
 *   rclone through stdin only.
 * - Import from the user's own rclone configuration.
 * - Other services: rclone's interactive setup in a terminal, on Panero's
 *   encrypted configuration.
 */
class PaneroAddCloudDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PaneroAddCloudDialog(QWidget *parent = nullptr);

    /// The new entry (valid after Accepted).
    PaneroCloud::Entry createdEntry() const { return m_created; }
    QString createdRemote() const { return m_created.rcloneRemote; }

    enum class Kind { OAuth, WebDav, Import, Other };
    struct Provider {
        QString type;   ///< rclone backend
        QString title;  ///< shown in the list and as default name
        QStringList icons;
        Kind kind;
    };
    static QList<Provider> providers();

private slots:
    void providerChanged();
    void nameEdited();
    void updateState();
    void connectClicked();

private:
    Provider currentProvider() const;
    QString suggestedName(const QString &title) const;
    bool createRemote(const Provider &p, const QString &remote, QString *error);
    void loadImportList();
    bool runImport();
    void openInteractiveSetup();

    QList<Provider> m_providers;
    QListWidget *m_list;
    QLabel *m_description;
    QFormLayout *m_form;
    QLineEdit *m_name;
    QLabel *m_nameHint;
    QComboBox *m_vendor;
    QLineEdit *m_url;
    QLineEdit *m_user;
    QLineEdit *m_password;
    QCheckBox *m_readOnly;
    QCheckBox *m_shared;
    QListWidget *m_importList;
    QCheckBox *m_importDelete;
    QPushButton *m_terminal;
    KMessageWidget *m_error;
    QDialogButtonBox *m_buttons;
    bool m_nameEdited = false;
    bool m_driveRecovery = false; ///< OneDrive signed in, rclone's drive choice failed
    QString m_setupError;
    PaneroCloud::Entry m_created;
};

#endif // PANEROADDCLOUDDIALOG_H
