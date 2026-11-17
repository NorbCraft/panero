/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROPACKAGEINSTALLER_H
#define PANEROPACKAGEINSTALLER_H

#include <QDBusObjectPath>
#include <QObject>
#include <QString>

class QProgressDialog;
class QTimer;
class QEventLoop;

/**
 * Install a distribution package through PackageKit (the system's normal
 * installation service on Zorin/Ubuntu). Authorisation is requested by
 * PackageKit via PolicyKit and shown by the desktop's own authentication
 * agent - Panero never asks for or sees a password.
 *
 * Usage: PaneroPackageInstaller::install(parent, "kdiff3", "KDiff3")
 */
class PaneroPackageInstaller : public QObject
{
    Q_OBJECT
public:
    enum class Result {
        Installed,           ///< package installed successfully
        AlreadyInstalled,    ///< the package was already installed
        Cancelled,           ///< user cancelled (dialog or authentication)
        NotAvailable,        ///< package unknown to the configured repositories
        NoInstaller,         ///< PackageKit is not available on this system
        Failed,              ///< any other error (network, dpkg lock, ...)
    };

    /// Runs the installation with a modal progress dialog. @p error gets a message on failure.
    static Result install(QWidget *parent, const QString &packageName, const QString &displayName, QString *error);

private slots:
    void onPackage(uint info, const QString &packageId, const QString &summary);
    void onErrorCode(uint code, const QString &details);
    void onFinished(uint exitCode, uint runtime);
    void pollProgress();
    void cancelRequested();

private:
    PaneroPackageInstaller(QWidget *parent, const QString &packageName, const QString &displayName);
    bool createTransaction();
    bool runResolve();
    bool runInstall();
    Result exec(QString *error);

    QWidget *m_parent;
    QString m_packageName;
    QString m_displayName;
    QDBusObjectPath m_transaction;
    QString m_packageId;
    bool m_packageInstalled = false;
    uint m_exitCode = 0;
    QString m_error;
    bool m_cancelled = false;
    QProgressDialog *m_progress = nullptr;
    QTimer *m_poll = nullptr;
    QEventLoop *m_loop = nullptr;
};

#endif // PANEROPACKAGEINSTALLER_H
