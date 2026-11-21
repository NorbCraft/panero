/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroaddclouddialog.h"
#include "panerocloudrevoke.h"
#include "panerocloudsetup.h"
#include "panerocloudutils.h"
#include "panerooffline.h"
#include "panerorclone.h"
#include "panerosecretstore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVBoxLayout>

#include <KI18n/KLocalizedString>
#include <KWidgetsAddons/KMessageBox>
#include <KWidgetsAddons/KMessageWidget>

namespace
{
void setRowVisible(QFormLayout *form, QWidget *field, bool visible)
{
    // QFormLayout::setRowVisible() is Qt 6.4+
    if (QWidget *label = form->labelForField(field))
        label->setVisible(visible);
    field->setVisible(visible);
}

QIcon iconFor(const QStringList &names)
{
    for (const QString &n : names) {
        if (QIcon::hasThemeIcon(n))
            return QIcon::fromTheme(n);
    }
    return QIcon::fromTheme(QStringLiteral("folder-cloud"), QIcon::fromTheme(QStringLiteral("folder-remote")));
}

QString toHtml(const QString &text)
{
    return text.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br>"));
}

/**
 * Holds [::1]:53682 during a sign-in and forwards every request to rclone's
 * helper on 127.0.0.1:53682. Providers that redirect to "localhost" (pCloud)
 * may make the browser try ::1 first; while Panero holds that address no other
 * program can listen there and catch the sign-in code.
 */
class LoopbackRedirector : public QObject
{
public:
    explicit LoopbackRedirector(QObject *parent)
        : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *s = m_server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [s]() {
                    if (!s->canReadLine())
                        return;
                    const QByteArray line = s->readLine(8192).trimmed(); // "GET /path?query HTTP/1.1"
                    const QList<QByteArray> parts = line.split(' ');
                    QByteArray target = parts.size() >= 2 ? parts.at(1) : QByteArray("/");
                    if (!target.startsWith('/') || target.contains('\r') || target.contains('\n'))
                        target = "/";
                    s->write("HTTP/1.1 307 Temporary Redirect\r\nLocation: http://127.0.0.1:"
                             + QByteArray::number(PaneroCloudSetup::CALLBACK_PORT) + target
                             + "\r\nContent-Length: 0\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        m_server.listen(QHostAddress::LocalHostIPv6, PaneroCloudSetup::CALLBACK_PORT); // fails quietly without IPv6
    }

private:
    QTcpServer m_server;
};

/// "Sign in in your browser" step of OAuth services: runs `rclone config create`.
class SignInDialog : public QDialog
{
public:
    SignInDialog(const QString &service, const QStringList &args, QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(i18n("Sign in to %1", service));
        setModal(true);
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel(i18n("<b>Sign in to %1 in your browser...</b>", service.toHtmlEscaped()), this));
        auto *text = new QLabel(i18n("Your web browser opens the sign-in page of %1. Check that the address in the browser belongs to %1, "
                                     "sign in there and allow access, then come back here: Panero continues by itself. Panero never sees "
                                     "your password.",
                                     service.toHtmlEscaped()),
                                this);
        text->setWordWrap(true);
        layout->addWidget(text);
        m_open = new QPushButton(QIcon::fromTheme(QStringLiteral("internet-web-browser")), i18n("Open the sign-in page"), this);
        m_open->setEnabled(false);
        m_open->setToolTip(i18n("Use this if your browser did not open by itself."));
        auto *row = new QHBoxLayout;
        row->addWidget(m_open);
        row->addStretch();
        layout->addLayout(row);
        m_status = new QLabel(i18n("Starting..."), this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        layout->addWidget(buttons);
        setMinimumWidth(480);

        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_open, &QPushButton::clicked, this, [this]() {
            QDesktopServices::openUrl(m_authUrl);
        });

        new LoopbackRedirector(this);
        PaneroRclone::prepare(m_process, args);
        m_process.setStandardOutputFile(QProcess::nullDevice()); // never read: may contain the new config
        connect(&m_process, &QProcess::readyReadStandardError, this, [this]() {
            m_stderr += QString::fromUtf8(m_process.readAllStandardError());
            if (m_authUrl.isEmpty()) {
                m_authUrl = PaneroCloudUtils::parseAuthUrl(m_stderr);
                if (m_authUrl.isValid()) {
                    m_open->setEnabled(true);
                    m_status->setText(i18n("Waiting for the sign-in to finish..."));
                }
            }
        });
        connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
            m_finished = true;
            m_stderr += QString::fromUtf8(m_process.readAllStandardError());
            if (status == QProcess::NormalExit && code == 0) {
                accept();
                return;
            }
            if (m_stderr.contains(QLatin1String("Auth state doesn't match")))
                m_error = i18n("The sign-in was stopped because Panero's sign-in helper received a request that did not come from the "
                               "provider. A web page may have tried to interfere. Close unknown browser tabs and try again.");
            else if (m_stderr.contains(QLatin1String("invalid hostname")))
                m_error = i18n("The sign-in was stopped because it named an unexpected server. Try again; if it happens again, do "
                               "not continue.");
            else
                m_error = PaneroCloudUtils::rcloneErrorSummary(m_stderr, 2);
            if (m_error.isEmpty())
                m_error = i18n("rclone ended with exit code %1.", code);
            QDialog::reject();
        });
        connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart)
                return;
            m_finished = true;
            m_error = i18n("rclone could not be started.");
            QDialog::reject();
        });
        m_process.start();
    }

    ~SignInDialog() override
    {
        stop();
        PaneroSecretStore::wipe(m_stderr);
    }

    QString error() const { return m_error; }
    bool cancelled() const { return m_cancelled; }

    void reject() override
    {
        if (!m_finished) {
            m_cancelled = true;
            stop();
        }
        QDialog::reject();
    }

private:
    void stop()
    {
        if (m_process.state() == QProcess::NotRunning)
            return;
        m_finished = true;
        m_process.disconnect(this);
        m_process.terminate();
        if (!m_process.waitForFinished(2000)) {
            m_process.kill();
            m_process.waitForFinished(1000);
        }
    }

    QProcess m_process;
    QPushButton *m_open;
    QLabel *m_status;
    QUrl m_authUrl;
    QString m_stderr;
    QString m_error;
    bool m_finished = false;
    bool m_cancelled = false;
};
} // namespace

QList<PaneroAddCloudDialog::Provider> PaneroAddCloudDialog::providers()
{
    return {
        {QStringLiteral("drive"), i18n("Google Drive"), {QStringLiteral("goa-account-google"), QStringLiteral("folder-gdrive"), QStringLiteral("google-drive")}, Kind::OAuth},
        {QStringLiteral("onedrive"), i18n("OneDrive"), {QStringLiteral("goa-account-ms365"), QStringLiteral("goa-account-microsoft"), QStringLiteral("folder-onedrive"), QStringLiteral("onedrive")}, Kind::OAuth},
        {QStringLiteral("dropbox"), i18n("Dropbox"), {QStringLiteral("dropbox"), QStringLiteral("folder-dropbox")}, Kind::OAuth},
        {QStringLiteral("pcloud"), i18n("pCloud"), {QStringLiteral("pcloud"), QStringLiteral("folder-cloud")}, Kind::OAuth},
        {QStringLiteral("box"), i18n("Box"), {QStringLiteral("box"), QStringLiteral("folder-cloud")}, Kind::OAuth},
        {QStringLiteral("webdav"), i18n("Nextcloud / WebDAV"), {QStringLiteral("goa-account-owncloud"), QStringLiteral("nextcloud"), QStringLiteral("folder-cloud")}, Kind::WebDav},
        {QString(), i18n("Import from rclone..."), {QStringLiteral("document-import")}, Kind::Import},
        {QString(), i18n("Other services..."), {QStringLiteral("utilities-terminal")}, Kind::Other},
    };
}

PaneroAddCloudDialog::PaneroAddCloudDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Add Cloud"));
    auto *outer = new QVBoxLayout(this);
    auto *main = new QHBoxLayout;
    outer->addLayout(main, 1);

    m_list = new QListWidget(this);
    m_list->setIconSize(QSize(32, 32));
    m_list->setMinimumWidth(210);
    m_list->setSpacing(2);
    m_providers = providers();
    for (const Provider &p : qAsConst(m_providers)) {
        auto *item = new QListWidgetItem(iconFor(p.icons), p.title, m_list);
        item->setData(Qt::UserRole, p.type);
    }
    main->addWidget(m_list);

    auto *right = new QVBoxLayout;
    main->addLayout(right, 1);
    m_description = new QLabel(this);
    m_description->setWordWrap(true);
    m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_description->setMinimumWidth(420);
    m_description->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    right->addWidget(m_description);

    m_form = new QFormLayout;
    m_name = new QLineEdit(this);
    m_nameHint = new QLabel(this);
    QFont small = m_nameHint->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    m_nameHint->setFont(small);
    m_nameHint->setEnabled(false);
    m_vendor = new QComboBox(this);
    m_vendor->addItem(i18n("Nextcloud"), QStringLiteral("nextcloud"));
    m_vendor->addItem(i18n("ownCloud"), QStringLiteral("owncloud"));
    m_vendor->addItem(i18n("Other WebDAV server"), QStringLiteral("other"));
    m_url = new QLineEdit(this);
    m_url->setPlaceholderText(QStringLiteral("https://cloud.example.com"));
    m_user = new QLineEdit(this);
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_readOnly = new QCheckBox(i18n("Read-only access (browse and download only)"), this);
    m_shared = new QCheckBox(i18n("Also files others shared with me"), this);
    m_form->addRow(i18n("Name:"), m_name);
    m_form->addRow(QString(), m_nameHint);
    m_form->addRow(i18n("Server type:"), m_vendor);
    m_form->addRow(i18n("Address:"), m_url);
    m_form->addRow(i18n("User name:"), m_user);
    m_form->addRow(i18n("App password:"), m_password);
    m_form->addRow(QString(), m_readOnly);
    m_form->addRow(QString(), m_shared);
    right->addLayout(m_form);

    m_importList = new QListWidget(this);
    m_importList->setMinimumHeight(90);
    right->addWidget(m_importList);
    m_importDelete = new QCheckBox(i18n("Delete the imported entries from the old, unencrypted file"), this);
    m_importDelete->setChecked(true);
    right->addWidget(m_importDelete);
    m_terminal = new QPushButton(QIcon::fromTheme(QStringLiteral("utilities-terminal")), i18n("Open rclone Setup in a Terminal"), this);
    right->addWidget(m_terminal, 0, Qt::AlignLeft);
    right->addStretch();

    m_error = new KMessageWidget(this);
    m_error->setMessageType(KMessageWidget::Error);
    m_error->setWordWrap(true);
    m_error->setCloseButtonVisible(false);
    m_error->hide();
    right->addWidget(m_error);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setIcon(QIcon::fromTheme(QStringLiteral("network-connect")));
    outer->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &PaneroAddCloudDialog::connectClicked);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::currentRowChanged, this, &PaneroAddCloudDialog::providerChanged);
    connect(m_name, &QLineEdit::textEdited, this, &PaneroAddCloudDialog::nameEdited);
    for (QLineEdit *e : {m_name, m_url, m_user, m_password})
        connect(e, &QLineEdit::textChanged, this, &PaneroAddCloudDialog::updateState);
    connect(m_readOnly, &QCheckBox::toggled, this, &PaneroAddCloudDialog::providerChanged);
    connect(m_shared, &QCheckBox::toggled, this, &PaneroAddCloudDialog::providerChanged);
    connect(m_importList, &QListWidget::itemChanged, this, &PaneroAddCloudDialog::updateState);
    connect(m_terminal, &QPushButton::clicked, this, &PaneroAddCloudDialog::openInteractiveSetup);

    m_list->setCurrentRow(0);
    resize(780, 540);
}

PaneroAddCloudDialog::Provider PaneroAddCloudDialog::currentProvider() const
{
    const int row = m_list->currentRow();
    return m_providers.value(row < 0 ? 0 : row);
}

QString PaneroAddCloudDialog::suggestedName(const QString &title) const
{
    const QStringList existing = PaneroCloud::instance()->remoteNames();
    QString name = title;
    for (int i = 2; existing.contains(PaneroCloudUtils::sanitizeRemoteName(name)); ++i)
        name = title + QLatin1Char(' ') + QString::number(i);
    return name;
}

void PaneroAddCloudDialog::providerChanged()
{
    const Provider p = currentProvider();
    m_error->animatedHide();
    const bool named = p.kind == Kind::OAuth || p.kind == Kind::WebDav;
    const bool scoped = p.type == QLatin1String("drive") || p.type == QLatin1String("onedrive");
    switch (p.kind) {
    case Kind::OAuth:
        m_description->setText(i18n("<p><b>%1</b></p><p>Click <b>Sign In</b> and sign in to your %1 account in your web browser. "
                                    "Panero never sees your password; it receives a sign-in key that is stored encrypted, unlocked by "
                                    "your desktop keyring.</p><p>%2</p>",
                                    p.title.toHtmlEscaped(),
                                    PaneroCloudSetup::accessDescription(p.type, scoped && m_readOnly->isChecked(),
                                                                        p.type == QLatin1String("onedrive") && m_shared->isChecked())));
        break;
    case Kind::WebDav:
        m_description->setText(i18n("<p><b>Nextcloud / WebDAV</b></p><p>Enter the address of your server, for example "
                                    "<tt>https://cloud.example.com</tt>. Use an <b>app password</b> (Nextcloud: Settings &gt; Security "
                                    "&gt; Devices &amp; sessions), not your account password: it can be revoked for this computer alone. "
                                    "It is stored encrypted, unlocked by your desktop keyring.</p>"));
        break;
    case Kind::Import:
        m_description->setText(i18n("<p><b>Import from rclone</b></p><p>Moves remotes from your own rclone configuration (%1) into "
                                    "Panero's encrypted storage. Disconnect your cloud drives first.</p>",
                                    PaneroCloudUtils::userRcloneConfigPath().toHtmlEscaped()));
        loadImportList();
        break;
    case Kind::Other:
        m_description->setText(i18n("<p><b>Other services</b></p><p>rclone supports about 70 services (Amazon S3, Backblaze B2, "
                                    "Jottacloud, SFTP and many more). The button opens rclone's interactive setup in a terminal; what "
                                    "you set up there is stored in Panero's encrypted storage and appears in the Cloud section.</p>"
                                    "<p>Avoid options that ask for your main account password where the service offers app passwords "
                                    "or tokens.</p>"));
        break;
    }

    // word-wrapped rich text: reserve its full height so the form never covers it
    const int textWidth = qMax(m_description->width(), m_description->minimumWidth());
    m_description->setFixedHeight(m_description->heightForWidth(textWidth));

    setRowVisible(m_form, m_name, named);
    setRowVisible(m_form, m_nameHint, named);
    setRowVisible(m_form, m_vendor, p.kind == Kind::WebDav);
    setRowVisible(m_form, m_url, p.kind == Kind::WebDav);
    setRowVisible(m_form, m_user, p.kind == Kind::WebDav);
    setRowVisible(m_form, m_password, p.kind == Kind::WebDav);
    setRowVisible(m_form, m_readOnly, p.kind == Kind::OAuth && scoped);
    setRowVisible(m_form, m_shared, p.type == QLatin1String("onedrive"));
    m_importList->setVisible(p.kind == Kind::Import);
    m_importDelete->setVisible(p.kind == Kind::Import);
    m_terminal->setVisible(p.kind == Kind::Other);
    if (!m_nameEdited && named)
        m_name->setText(suggestedName(p.title));
    QPushButton *ok = m_buttons->button(QDialogButtonBox::Ok);
    ok->setVisible(p.kind != Kind::Other);
    ok->setText(p.kind == Kind::OAuth ? i18n("Sign In...") : p.kind == Kind::Import ? i18n("Import") : i18n("Connect"));
    m_buttons->button(QDialogButtonBox::Cancel)->setText(p.kind == Kind::Other ? i18n("Close") : i18n("Cancel"));
    updateState();
}

void PaneroAddCloudDialog::nameEdited()
{
    m_nameEdited = !m_name->text().isEmpty();
}

void PaneroAddCloudDialog::updateState()
{
    const Provider p = currentProvider();
    bool ok = true;
    if (p.kind == Kind::Import) {
        int checked = 0;
        for (int i = 0; i < m_importList->count(); ++i)
            checked += m_importList->item(i)->checkState() == Qt::Checked ? 1 : 0;
        ok = checked > 0;
    } else {
        const QString remote = PaneroCloudUtils::sanitizeRemoteName(m_name->text());
        const bool exists = PaneroCloud::instance()->remoteNames().contains(remote);
        if (m_name->text().trimmed().isEmpty())
            m_nameHint->setText(QString());
        else if (exists)
            m_nameHint->setText(i18n("A cloud drive named \"%1\" already exists.", remote));
        else
            m_nameHint->setText(i18n("Internal name: %1", remote));
        ok = !m_name->text().trimmed().isEmpty() && !exists;
        if (p.kind == Kind::WebDav)
            ok = ok && !m_url->text().trimmed().isEmpty() && !m_user->text().trimmed().isEmpty() && !m_password->text().isEmpty();
    }
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);
}

void PaneroAddCloudDialog::loadImportList()
{
    m_importList->clear();
    QString problem;
    if (!PaneroRclone::usable(&problem)) {
        m_importList->addItem(problem);
        return;
    }
    QString error;
    const auto remotes = PaneroCloudSetup::listUserRemotes(&error);
    if (remotes.isEmpty()) {
        auto *item = new QListWidgetItem(error.isEmpty() ? i18n("Your rclone configuration has no remotes.") : error, m_importList);
        item->setFlags(Qt::NoItemFlags);
        return;
    }
    for (const auto &r : remotes) {
        auto *item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(r.name, r.type), m_importList);
        item->setData(Qt::UserRole, r.name);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
}

bool PaneroAddCloudDialog::createRemote(const Provider &p, const QString &remote, QString *error)
{
    if (p.kind == Kind::OAuth) {
        QString why;
        if (!PaneroCloudSetup::callbackPortFree(&why)) {
            if (error)
                *error = why;
            return false;
        }
        const bool scoped = p.type == QLatin1String("drive") || p.type == QLatin1String("onedrive");
        QStringList args{QStringLiteral("config"), QStringLiteral("create"), remote, p.type};
        args << PaneroCloudSetup::oauthOptions(p.type, scoped && m_readOnly->isChecked(), m_shared->isChecked());
        SignInDialog dlg(p.title, args, this);
        if (dlg.exec() == QDialog::Accepted)
            return true;
        if (error)
            *error = dlg.cancelled() ? QString() : dlg.error();
        return false;
    }
    const QString vendor = m_vendor->currentData().toString();
    const QString address = PaneroCloudSetup::webDavAddress(m_url->text(), vendor, m_user->text());
    const QUrl url(address);
    if (url.scheme() != QLatin1String("https") && url.host() != QLatin1String("localhost") && url.host() != QLatin1String("127.0.0.1")) {
        const auto answer = KMessageBox::warningTwoActions(
            this, i18n("<p>The address <b>%1</b> is not encrypted (http). Your password would travel over the network readably.</p>"
                       "<p>Use an https address if your server has one.</p>", address.toHtmlEscaped()),
            i18n("Unencrypted Connection"), KGuiItem(i18n("Connect Anyway"), QStringLiteral("security-low")), KStandardGuiItem::cancel());
        if (answer != KMessageBox::PrimaryAction) {
            if (error)
                error->clear();
            return false;
        }
    }
    QString password = m_password->text();
    const bool ok = PaneroCloudSetup::createWebDavRemote(remote, address, vendor, m_user->text(), password, error);
    PaneroSecretStore::wipe(password);
    if (ok)
        m_password->clear();
    return ok;
}

bool PaneroAddCloudDialog::runImport()
{
    QStringList names;
    for (int i = 0; i < m_importList->count(); ++i) {
        if (m_importList->item(i)->checkState() == Qt::Checked)
            names << m_importList->item(i)->data(Qt::UserRole).toString();
    }
    PaneroCloud *cloud = PaneroCloud::instance();
    if (!cloud->mountDirs().isEmpty()) {
        const auto answer = KMessageBox::questionTwoActions(this, i18n("Importing disconnects your cloud drives first. Continue?"), i18n("Import"),
                                                            KGuiItem(i18n("Disconnect and Import")), KStandardGuiItem::cancel());
        if (answer != KMessageBox::PrimaryAction)
            return false;
        cloud->unmountAll();
    }
    for (const QString &r : cloud->remoteNames()) {
        if (PaneroOffline::instance()->isBusy(r)) {
            m_error->setText(i18n("Offline files are being synchronised. Try again in a moment."));
            m_error->animatedShow();
            return false;
        }
    }
    QStringList imported;
    QString error;
    if (!PaneroCloudSetup::importRemotes(names, m_importDelete->isChecked(), &imported, &error)) {
        m_error->setText(i18n("Could not import: %1", error));
        m_error->animatedShow();
        return false;
    }
    cloud->refreshNow();
    m_created = cloud->entry(QLatin1String("rclone:") + imported.value(0));
    KMessageBox::information(this, i18n("Imported: %1", imported.join(QLatin1String(", "))), i18n("Import"));
    return true;
}

void PaneroAddCloudDialog::openInteractiveSetup()
{
    if (!PaneroCloud::instance()->ensureRclone(this))
        return;
    const QStringList cmd = PaneroCloudSetup::interactiveConfigCommand();
    QProcess p;
    p.setProgram(cmd.first());
    p.setArguments(cmd.mid(1));
    p.setProcessEnvironment(PaneroRclone::environment());
    if (!p.startDetached()) {
        KMessageBox::error(this, i18n("No terminal could be started (x-terminal-emulator)."), i18n("Other Services"));
        return;
    }
    PaneroCloud::instance()->refresh();
}

void PaneroAddCloudDialog::connectClicked()
{
    const Provider p = currentProvider();
    if (p.kind == Kind::Other)
        return;
    m_error->hide();
    PaneroCloud *cloud = PaneroCloud::instance();
    if (!cloud->ensureRclone(this))
        return;
    if (p.kind == Kind::Import) {
        if (runImport())
            accept();
        return;
    }

    cloud->refreshNow();
    const QString label = m_name->text().trimmed();
    const QString remote = PaneroCloudUtils::sanitizeRemoteName(label);
    if (cloud->remoteNames().contains(remote)) {
        updateState();
        return;
    }

    QString error;
    const bool created = createRemote(p, remote, &error);
    cloud->refreshNow();
    const bool listed = cloud->remoteNames().contains(remote);
    if (!created || !listed) {
        if (listed) {
            // half-created by a cancelled or failed sign-in: a token may already exist
            if (p.kind == Kind::OAuth)
                PaneroCloudRevoke::revoke(remote, p.type);
            PaneroCloudSetup::deleteRemote(remote);
        }
        cloud->refreshNow();
        if (!error.isEmpty() || created) {
            m_error->setText(i18n("Could not add %1: %2", p.title, error.isEmpty() ? i18n("unknown error") : error));
            m_error->animatedShow();
        }
        return;
    }

    if (p.type == QLatin1String("pcloud") && !PaneroCloudSetup::checkPcloudHost(remote, &error)) {
        PaneroCloudSetup::deleteRemote(remote); // never send the token to a foreign host, not even to revoke it
        cloud->refreshNow();
        m_error->setText(error);
        m_error->animatedShow();
        return;
    }

    QString verifyError;
    if (!PaneroCloudSetup::verifyRemote(remote, &verifyError)) {
        const auto answer = KMessageBox::warningTwoActions(
            this,
            i18n("<p>Panero could not open %1:</p><p>%2</p>", label.toHtmlEscaped(), toHtml(verifyError))
                + (p.kind == Kind::WebDav ? i18n("<p>Check the address, user name and password.</p>") : QString()),
            i18n("Connection Failed"), KGuiItem(i18n("Remove It"), QStringLiteral("edit-delete")), KGuiItem(i18n("Keep Anyway"), QStringLiteral("dialog-ok")));
        if (answer == KMessageBox::PrimaryAction) {
            if (p.kind == Kind::OAuth)
                PaneroCloudRevoke::revoke(remote, p.type);
            PaneroCloudSetup::deleteRemote(remote);
            cloud->refreshNow();
            return;
        }
    }

    PaneroCloud::Entry e = cloud->entry(QLatin1String("rclone:") + remote);
    if (label != remote)
        cloud->setLabel(e, label);
    m_created = cloud->entry(QLatin1String("rclone:") + remote);
    accept();
}
