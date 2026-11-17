/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneropackageinstaller.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QEventLoop>
#include <QLocale>
#include <QProgressDialog>
#include <QTimer>

#include <KI18n/KLocalizedString>

namespace
{
const char PK_SERVICE[] = "org.freedesktop.PackageKit";
const char PK_PATH[] = "/org/freedesktop/PackageKit";
const char PK_INTERFACE[] = "org.freedesktop.PackageKit";
const char PK_TRANSACTION[] = "org.freedesktop.PackageKit.Transaction";

// PackageKit enums (see pk-enum.h)
const quint64 FILTER_NONE = 1ull << 1;          // PK_FILTER_ENUM_NONE
const quint64 FLAG_ONLY_TRUSTED = 1ull << 1;    // PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED
const uint INFO_INSTALLED = 1;                  // PK_INFO_ENUM_INSTALLED
const uint EXIT_SUCCESS_PK = 1;                 // PK_EXIT_ENUM_SUCCESS
const uint EXIT_CANCELLED = 3;                  // PK_EXIT_ENUM_CANCELLED
const uint ERROR_NOT_AUTHORIZED = 48;           // PK_ERROR_ENUM_NOT_AUTHORIZED
const uint ERROR_TRANSACTION_CANCELLED = 17;    // PK_ERROR_ENUM_TRANSACTION_CANCELLED
const uint ERROR_ALREADY_INSTALLED = 9;         // PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED
const uint ERROR_ALL_ALREADY_INSTALLED = 41;    // PK_ERROR_ENUM_ALL_PACKAGES_ALREADY_INSTALLED
// values verified against PackageKitGlib 1.0 (Ubuntu 24.04) via GObject introspection

QDBusMessage transactionCall(const QDBusObjectPath &path, const QString &method)
{
    return QDBusMessage::createMethodCall(QLatin1String(PK_SERVICE), path.path(), QLatin1String(PK_TRANSACTION), method);
}
} // namespace

PaneroPackageInstaller::PaneroPackageInstaller(QWidget *parent, const QString &packageName, const QString &displayName)
    : m_parent(parent), m_packageName(packageName), m_displayName(displayName)
{
}

PaneroPackageInstaller::Result PaneroPackageInstaller::install(QWidget *parent, const QString &packageName,
                                                               const QString &displayName, QString *error)
{
    PaneroPackageInstaller installer(parent, packageName, displayName);
    return installer.exec(error);
}

bool PaneroPackageInstaller::createTransaction()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(PK_SERVICE), QLatin1String(PK_PATH),
                                                      QLatin1String(PK_INTERFACE), QStringLiteral("CreateTransaction"));
    const QDBusReply<QDBusObjectPath> reply = QDBusConnection::systemBus().call(msg, QDBus::Block, 10000);
    if (!reply.isValid()) {
        m_error = reply.error().message();
        return false;
    }
    m_transaction = reply.value();
    QDBusConnection bus = QDBusConnection::systemBus();
    const QString path = m_transaction.path();
    bus.connect(QLatin1String(PK_SERVICE), path, QLatin1String(PK_TRANSACTION), QStringLiteral("Package"), this,
                SLOT(onPackage(uint,QString,QString)));
    bus.connect(QLatin1String(PK_SERVICE), path, QLatin1String(PK_TRANSACTION), QStringLiteral("ErrorCode"), this,
                SLOT(onErrorCode(uint,QString)));
    bus.connect(QLatin1String(PK_SERVICE), path, QLatin1String(PK_TRANSACTION), QStringLiteral("Finished"), this,
                SLOT(onFinished(uint,uint)));

    // allow PolicyKit to ask the user (the desktop's authentication agent shows the prompt)
    QDBusMessage hints = transactionCall(m_transaction, QStringLiteral("SetHints"));
    hints << QStringList{QStringLiteral("interactive=true"), QStringLiteral("background=false"),
                         QStringLiteral("locale=") + QLocale().name() + QStringLiteral(".UTF-8")};
    bus.call(hints, QDBus::Block, 5000);
    return true;
}

PaneroPackageInstaller::Result PaneroPackageInstaller::exec(QString *error)
{
    QDBusConnectionInterface *iface = QDBusConnection::systemBus().interface();
    const bool available = iface && (iface->isServiceRegistered(QLatin1String(PK_SERVICE)).value()
                                     || iface->activatableServiceNames().value().contains(QLatin1String(PK_SERVICE)));
    if (!available) {
        if (error)
            *error = i18n("The PackageKit installation service is not available on this system.");
        return Result::NoInstaller;
    }

    QProgressDialog progress(i18n("Looking up %1...", m_displayName), i18n("Cancel"), 0, 0, m_parent);
    progress.setWindowTitle(i18n("Install %1", m_displayName));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    m_progress = &progress;
    connect(&progress, &QProgressDialog::canceled, this, &PaneroPackageInstaller::cancelRequested);
    QTimer poll;
    m_poll = &poll;
    connect(&poll, &QTimer::timeout, this, &PaneroPackageInstaller::pollProgress);
    QEventLoop loop;
    m_loop = &loop;
    progress.show();

    // 1. resolve the package name to a package id
    if (!createTransaction() || !runResolve()) {
        if (error)
            *error = m_error.isEmpty() ? i18n("Could not contact the PackageKit service.") : m_error;
        return Result::NoInstaller;
    }
    loop.exec();
    if (m_cancelled) {
        return Result::Cancelled;
    }
    if (m_exitCode != EXIT_SUCCESS_PK) {
        if (error)
            *error = m_error;
        return Result::Failed;
    }
    if (m_packageInstalled && m_packageId.isEmpty())
        return Result::AlreadyInstalled;
    if (m_packageId.isEmpty()) {
        if (error)
            *error = i18n("The package \"%1\" was not found in the configured software sources.", m_packageName);
        return Result::NotAvailable;
    }

    // 2. install it (PolicyKit asks for authorisation)
    progress.setLabelText(i18n("Installing %1...\nYou may be asked to authenticate.", m_displayName));
    m_exitCode = 0;
    m_error.clear();
    if (!createTransaction() || !runInstall()) {
        if (error)
            *error = m_error;
        return Result::Failed;
    }
    poll.start(400);
    loop.exec();
    poll.stop();
    if (m_cancelled || m_exitCode == EXIT_CANCELLED)
        return Result::Cancelled;
    if (m_exitCode != EXIT_SUCCESS_PK && m_packageInstalled)
        return Result::AlreadyInstalled;
    if (m_exitCode != EXIT_SUCCESS_PK) {
        if (error)
            *error = m_error.isEmpty() ? i18n("The installation failed.") : m_error;
        return Result::Failed;
    }
    return Result::Installed;
}

bool PaneroPackageInstaller::runResolve()
{
    QDBusMessage msg = transactionCall(m_transaction, QStringLiteral("Resolve"));
    msg << QVariant::fromValue<quint64>(FILTER_NONE) << QStringList{m_packageName};
    const QDBusMessage reply = QDBusConnection::systemBus().call(msg, QDBus::Block, 10000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_error = reply.errorMessage();
        return false;
    }
    return true;
}

bool PaneroPackageInstaller::runInstall()
{
    QDBusMessage msg = transactionCall(m_transaction, QStringLiteral("InstallPackages"));
    msg << QVariant::fromValue<quint64>(FLAG_ONLY_TRUSTED) << QStringList{m_packageId};
    const QDBusMessage reply = QDBusConnection::systemBus().call(msg, QDBus::Block, 10000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_error = reply.errorMessage();
        return false;
    }
    return true;
}

void PaneroPackageInstaller::onPackage(uint info, const QString &packageId, const QString &)
{
    // package id: "name;version;arch;data"
    const QStringList parts = packageId.split(QLatin1Char(';'));
    if (parts.value(0) != m_packageName)
        return;
    const QString arch = parts.value(2);
    if (!arch.isEmpty() && arch != QLatin1String("amd64") && arch != QLatin1String("all") && arch != QLatin1String("x86_64"))
        return;
    if (info == INFO_INSTALLED) {
        m_packageInstalled = true;
        return;
    }
    if (m_packageId.isEmpty())
        m_packageId = packageId;
}

void PaneroPackageInstaller::onErrorCode(uint code, const QString &details)
{
    if (code == ERROR_NOT_AUTHORIZED || code == ERROR_TRANSACTION_CANCELLED)
        m_cancelled = true; // authentication dialog cancelled or transaction cancelled
    if (code == ERROR_ALREADY_INSTALLED || code == ERROR_ALL_ALREADY_INSTALLED)
        m_packageInstalled = true;
    m_error = details;
}

void PaneroPackageInstaller::onFinished(uint exitCode, uint)
{
    m_exitCode = exitCode;
    if (m_loop)
        m_loop->quit();
}

void PaneroPackageInstaller::pollProgress()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(PK_SERVICE), m_transaction.path(),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << QLatin1String(PK_TRANSACTION) << QStringLiteral("Percentage");
    const QDBusReply<QDBusVariant> reply = QDBusConnection::systemBus().call(msg, QDBus::Block, 1000);
    if (!reply.isValid() || !m_progress)
        return;
    const uint percentage = reply.value().variant().toUInt();
    if (percentage <= 100) {
        m_progress->setMaximum(100);
        m_progress->setValue(int(percentage));
    }
}

void PaneroPackageInstaller::cancelRequested()
{
    m_cancelled = true;
    if (!m_transaction.path().isEmpty())
        QDBusConnection::systemBus().call(transactionCall(m_transaction, QStringLiteral("Cancel")), QDBus::NoBlock);
    if (m_loop)
        m_loop->quit();
}
