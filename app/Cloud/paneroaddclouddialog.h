/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROADDCLOUDDIALOG_H
#define PANEROADDCLOUDDIALOG_H

#include "panerocloud.h"

#include <QDialog>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QFormLayout;
class KMessageWidget;

/**
 * "Add Cloud..." - creates an rclone remote for a cloud service.
 *
 * OAuth services (Google Drive, OneDrive, Dropbox, pCloud, Box) sign in in the
 * web browser (rclone's local helper on 127.0.0.1:53682); MEGA and
 * Nextcloud/WebDAV take a user name and password, which go to rclone in
 * obscured form only.
 *
 * Usage:
 *     PaneroAddCloudDialog dlg(parent);
 *     if (dlg.exec() == QDialog::Accepted)
 *         PaneroCloud::instance()->mount(dlg.createdEntry());
 */
class PaneroAddCloudDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PaneroAddCloudDialog(QWidget *parent = nullptr);

    /// The new entry (valid after Accepted).
    PaneroCloud::Entry createdEntry() const { return m_created; }
    QString createdRemote() const { return m_created.rcloneRemote; }

    enum class Kind { OAuth, Mega, WebDav, Other };
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
    bool verifyRemote(const QString &remote, QString *error);
    void deleteRemote(const QString &remote);
    QString obscure(const QString &password, QString *error);

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
    KMessageWidget *m_error;
    QDialogButtonBox *m_buttons;
    bool m_nameEdited = false;
    PaneroCloud::Entry m_created;
};

#endif // PANEROADDCLOUDDIALOG_H
