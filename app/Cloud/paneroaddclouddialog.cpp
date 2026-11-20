/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroaddclouddialog.h"
#include "panerocloudutils.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <KConfigCore/KConfigGroup>
#include <KConfigCore/KSharedConfig>
#include <KI18n/KLocalizedString>
#include <KWidgetsAddons/KMessageBox>
#include <KWidgetsAddons/KMessageWidget>

namespace
{
const int CANCELLED = -2;

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

/// rclone's own messages, minus the "<5>NOTICE:" prefix and minus anything
/// that is not an error (rclone prints the new config - with tokens - on
/// stdout, which is never read).
QString errorLines(const QString &stderrText)
{
    return PaneroCloudUtils::rcloneErrorSummary(stderrText, 2);
}

QString toHtml(const QString &text)
{
    return text.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br>"));
}

/**
 * Runs rclone with a small modal progress dialog. Returns the exit code,
 * CANCELLED, or -1 when it could not be started / timed out.
 */
int runRclone(QWidget *parent, const QStringList &args, const QString &label, const QByteArray &input, int timeoutMs,
              QString *stdoutText, QString *stderrText)
{
    QProcess process;
    QProgressDialog progress(label, i18n("Cancel"), 0, 0, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(400);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    QEventLoop loop;
    int result = -1;
    bool cancelled = false;
    QObject::connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop,
                     [&](int code, QProcess::ExitStatus status) {
                         result = status == QProcess::NormalExit ? code : -1;
                         loop.quit();
                     });
    QObject::connect(&process, &QProcess::errorOccurred, &loop, [&](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            loop.quit();
    });
    QObject::connect(&progress, &QProgressDialog::canceled, &loop, [&]() {
        cancelled = true;
        loop.quit();
    });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    process.start(PaneroCloud::rcloneProgram(), args);
    if (!input.isNull()) {
        process.write(input);
        process.closeWriteChannel();
    }
    if (timeoutMs > 0)
        timeout.start(timeoutMs);
    loop.exec();
    if (process.state() != QProcess::NotRunning) {
        process.disconnect();
        process.kill();
        process.waitForFinished(2000);
        if (!cancelled && stderrText)
            *stderrText = i18n("rclone did not answer in time.");
        return cancelled ? CANCELLED : -1;
    }
    if (stdoutText)
        *stdoutText = QString::fromUtf8(process.readAllStandardOutput());
    if (stderrText)
        *stderrText = QString::fromUtf8(process.readAllStandardError());
    return result;
}

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
        auto *title = new QLabel(i18n("<b>Sign in to %1 in your browser...</b>", service.toHtmlEscaped()), this);
        layout->addWidget(title);
        auto *text = new QLabel(i18n("Your web browser opens the sign-in page of %1. Sign in and allow rclone to "
                                     "access your files, then come back here: Panero continues by itself.",
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
        setMinimumWidth(460);

        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_open, &QPushButton::clicked, this, [this]() {
            QDesktopServices::openUrl(m_authUrl);
        });

        m_process.setStandardOutputFile(QProcess::nullDevice()); // the new config incl. token: never read
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
        connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this](int code, QProcess::ExitStatus status) {
                    m_finished = true;
                    m_stderr += QString::fromUtf8(m_process.readAllStandardError());
                    if (status == QProcess::NormalExit && code == 0) {
                        accept();
                    } else {
                        m_error = errorLines(m_stderr);
                        if (m_error.isEmpty())
                            m_error = i18n("rclone ended with exit code %1.", code);
                        QDialog::reject();
                    }
                });
        connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart)
                return;
            m_finished = true;
            m_error = i18n("rclone could not be started.");
            QDialog::reject();
        });
        m_process.start(PaneroCloud::rcloneProgram(), args);
    }

    ~SignInDialog() override
    {
        stop();
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
        {QStringLiteral("mega"), i18n("MEGA"), {QStringLiteral("mega"), QStringLiteral("megasync"), QStringLiteral("folder-cloud")}, Kind::Mega},
        {QStringLiteral("webdav"), i18n("Nextcloud / WebDAV"), {QStringLiteral("goa-account-owncloud"), QStringLiteral("nextcloud"), QStringLiteral("folder-cloud")}, Kind::WebDav},
        {QString(), i18n("Other (rclone)..."), {QStringLiteral("utilities-terminal")}, Kind::Other},
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
    m_description->setMinimumWidth(340);
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
    m_form->addRow(i18n("Name:"), m_name);
    m_form->addRow(QString(), m_nameHint);
    m_form->addRow(i18n("Server type:"), m_vendor);
    m_form->addRow(i18n("Address:"), m_url);
    m_form->addRow(i18n("User name:"), m_user);
    m_form->addRow(i18n("Password:"), m_password);
    right->addLayout(m_form);
    right->addStretch();

    m_error = new KMessageWidget(this);
    m_error->setMessageType(KMessageWidget::Error);
    m_error->setWordWrap(true);
    m_error->setCloseButtonVisible(false);
    m_error->hide();
    right->addWidget(m_error);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(i18n("Connect"));
    m_buttons->button(QDialogButtonBox::Ok)->setIcon(QIcon::fromTheme(QStringLiteral("network-connect")));
    outer->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &PaneroAddCloudDialog::connectClicked);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::currentRowChanged, this, &PaneroAddCloudDialog::providerChanged);
    connect(m_name, &QLineEdit::textEdited, this, &PaneroAddCloudDialog::nameEdited);
    for (QLineEdit *e : {m_name, m_url, m_user, m_password})
        connect(e, &QLineEdit::textChanged, this, &PaneroAddCloudDialog::updateState);

    m_list->setCurrentRow(0);
    resize(640, 380);
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
    const bool other = p.kind == Kind::Other;
    const bool needsLogin = p.kind == Kind::Mega || p.kind == Kind::WebDav;
    switch (p.kind) {
    case Kind::OAuth:
        m_description->setText(i18n("<p><b>%1</b></p><p>Click <b>Connect</b> and sign in to your %1 account in your "
                                    "web browser. Panero never sees your password.</p>",
                                    p.title.toHtmlEscaped()));
        break;
    case Kind::Mega:
        m_description->setText(i18n("<p><b>MEGA</b></p><p>Enter the e-mail address and password of your MEGA account. "
                                    "They are stored in rclone's configuration (password in obscured form).</p>"));
        break;
    case Kind::WebDav:
        m_description->setText(i18n("<p><b>Nextcloud / WebDAV</b></p><p>Enter the address of your server, for example "
                                    "<tt>https://cloud.example.com</tt>. For Nextcloud an app password "
                                    "(Settings &gt; Security) is recommended.</p>"));
        break;
    case Kind::Other:
        m_description->setText(i18n("<p><b>Other services</b></p><p>rclone supports about 70 services (Amazon S3, "
                                    "Backblaze B2, Yandex Disk, Jottacloud, SFTP and many more). To set one up, open a "
                                    "terminal and run:</p><p><tt>rclone config</tt></p><p>and follow the questions. "
                                    "Every remote you create there appears in Panero's Cloud section automatically.</p>"));
        break;
    }

    setRowVisible(m_form, m_name, !other);
    setRowVisible(m_form, m_nameHint, !other);
    setRowVisible(m_form, m_vendor, p.kind == Kind::WebDav);
    setRowVisible(m_form, m_url, p.kind == Kind::WebDav);
    setRowVisible(m_form, m_user, needsLogin);
    setRowVisible(m_form, m_password, needsLogin);
    m_form->labelForField(m_user)->setProperty("text", p.kind == Kind::Mega ? i18n("E-mail:") : i18n("User name:"));
    if (!m_nameEdited && !other)
        m_name->setText(suggestedName(p.title));
    m_buttons->button(QDialogButtonBox::Ok)->setVisible(!other);
    m_buttons->button(QDialogButtonBox::Cancel)->setText(other ? i18n("Close") : i18n("Cancel"));
    updateState();
}

void PaneroAddCloudDialog::nameEdited()
{
    m_nameEdited = !m_name->text().isEmpty();
}

void PaneroAddCloudDialog::updateState()
{
    const Provider p = currentProvider();
    const QString remote = PaneroCloudUtils::sanitizeRemoteName(m_name->text());
    const bool exists = PaneroCloud::instance()->remoteNames().contains(remote);
    if (m_name->text().trimmed().isEmpty())
        m_nameHint->setText(QString());
    else if (exists)
        m_nameHint->setText(i18n("A cloud drive named \"%1\" already exists.", remote));
    else
        m_nameHint->setText(i18n("rclone remote: %1", remote));
    bool ok = !m_name->text().trimmed().isEmpty() && !exists;
    if (p.kind == Kind::Mega || p.kind == Kind::WebDav)
        ok = ok && !m_user->text().trimmed().isEmpty() && !m_password->text().isEmpty();
    if (p.kind == Kind::WebDav)
        ok = ok && !m_url->text().trimmed().isEmpty();
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);
}

QString PaneroAddCloudDialog::obscure(const QString &password, QString *error)
{
    // through stdin, so the plain password never appears in a process list
    QString out, err;
    const int rc = runRclone(this, {QStringLiteral("obscure"), QStringLiteral("-")}, i18n("Preparing..."),
                             password.toUtf8() + '\n', 10000, &out, &err);
    if (rc != 0) {
        if (error)
            *error = errorLines(err);
        return QString();
    }
    return out.trimmed();
}

void PaneroAddCloudDialog::deleteRemote(const QString &remote)
{
    QProcess p;
    p.start(PaneroCloud::rcloneProgram(), {QStringLiteral("config"), QStringLiteral("delete"), remote});
    p.waitForFinished(10000);
}

bool PaneroAddCloudDialog::createRemote(const Provider &p, const QString &remote, QString *error)
{
    if (p.kind == Kind::OAuth) {
        // rclone 1.60: config_is_local defaults to true, so rclone starts its
        // helper on 127.0.0.1:53682, opens the browser and waits for the token.
        // Questions after the sign-in take rclone's defaults: for OneDrive that
        // is "OneDrive Personal or Business" and the first drive of the account
        // (the user's own drive), confirmed automatically.
        SignInDialog dlg(p.title, {QStringLiteral("config"), QStringLiteral("create"), remote, p.type}, this);
        const int rc = dlg.exec();
        if (rc == QDialog::Accepted)
            return true;
        if (error)
            *error = dlg.cancelled() ? QString() : dlg.error();
        return false;
    }

    QString passError;
    const QString pass = obscure(m_password->text(), &passError);
    if (pass.isEmpty()) {
        if (error)
            *error = passError.isEmpty() ? i18n("The password could not be prepared.") : passError;
        return false;
    }
    QStringList args{QStringLiteral("config"), QStringLiteral("create"), remote, p.type};
    if (p.kind == Kind::Mega) {
        args << QStringLiteral("user") << m_user->text().trimmed();
    } else {
        const QString vendor = m_vendor->currentData().toString();
        QString address = m_url->text().trimmed();
        if (!address.contains(QLatin1String("://")))
            address.prepend(QLatin1String("https://"));
        while (address.endsWith(QLatin1Char('/')))
            address.chop(1);
        // the usual WebDAV paths, unless the full address was given
        if (vendor == QLatin1String("nextcloud") && !address.contains(QLatin1String("/remote.php/")))
            address += QLatin1String("/remote.php/dav/files/") + QString::fromUtf8(QUrl::toPercentEncoding(m_user->text().trimmed()));
        else if (vendor == QLatin1String("owncloud") && !address.contains(QLatin1String("/remote.php/")))
            address += QLatin1String("/remote.php/webdav");
        args << QStringLiteral("url") << address << QStringLiteral("vendor") << vendor << QStringLiteral("user")
             << m_user->text().trimmed();
    }
    args << QStringLiteral("pass") << pass << QStringLiteral("--no-obscure");
    QString err;
    const int rc = runRclone(this, args, i18n("Saving the connection..."), QByteArray(), 30000, nullptr, &err);
    if (rc != 0) {
        if (error)
            *error = rc == CANCELLED ? QString() : errorLines(err);
        return false;
    }
    return true;
}

bool PaneroAddCloudDialog::verifyRemote(const QString &remote, QString *error)
{
    QString err;
    const int rc = runRclone(this, {QStringLiteral("lsd"), remote + QLatin1Char(':'), QStringLiteral("--contimeout"), QStringLiteral("15s")},
                             i18n("Connecting to %1...", remote), QByteArray(), 45000, nullptr, &err);
    if (rc == 0)
        return true;
    if (error)
        *error = rc == CANCELLED ? i18n("Cancelled.") : PaneroCloudUtils::rcloneErrorSummary(err, 1);
    return false;
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
        if (listed)
            deleteRemote(remote); // half-created by a cancelled or failed sign-in
        cloud->refreshNow();
        if (!error.isEmpty() || created) {
            m_error->setText(i18n("Could not add %1: %2", p.title, error.isEmpty() ? i18n("unknown error") : error));
            m_error->animatedShow();
        }
        return;
    }

    if (p.kind == Kind::Mega || p.kind == Kind::WebDav) {
        QString verifyError;
        if (!verifyRemote(remote, &verifyError)) {
            const auto answer = KMessageBox::warningTwoActions(
                this,
                i18n("<p>Panero could not connect to %1:</p><p>%2</p><p>Check the address, user name and password.</p>",
                     label.toHtmlEscaped(), toHtml(verifyError)),
                i18n("Connection Failed"), KGuiItem(i18n("Change Settings"), QStringLiteral("document-edit")),
                KGuiItem(i18n("Keep Anyway"), QStringLiteral("dialog-ok")));
            if (answer == KMessageBox::PrimaryAction) {
                deleteRemote(remote);
                cloud->refreshNow();
                        return;
            }
        }
    }

    PaneroCloud::Entry e = cloud->entry(QLatin1String("rclone:") + remote);
    if (label != remote)
        cloud->setLabel(e, label);
    m_created = cloud->entry(QLatin1String("rclone:") + remote);
    accept();
}
