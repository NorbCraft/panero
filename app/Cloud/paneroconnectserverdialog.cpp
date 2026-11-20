/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroconnectserverdialog.h"
#include "panerocloudutils.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <KJobWidgets/KJobWidgets>
#include <KI18n/KLocalizedString>
#include <KIO/StatJob>

namespace
{
const int TEST_TIMEOUT_MS = 60000; // generous: KIO may be showing its password dialog
}

QList<QPair<QString, QString>> PaneroConnectServerDialog::serverTypes()
{
    return {
        {QStringLiteral("webdavs"), i18n("WebDAV (https)")},
        {QStringLiteral("webdav"), i18n("WebDAV")},
        {QStringLiteral("sftp"), i18n("SFTP (SSH)")},
        {QStringLiteral("smb"), i18n("Windows share / NAS (SMB)")},
        {QStringLiteral("ftp"), i18n("FTP")},
    };
}

PaneroConnectServerDialog::PaneroConnectServerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Connect to Server"));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    layout->addLayout(form);

    m_type = new QComboBox(this);
    for (const auto &t : serverTypes())
        m_type->addItem(t.second, t.first);
    m_server = new QLineEdit(this);
    m_server->setPlaceholderText(i18n("server.example.com or 192.168.1.10"));
    m_port = new QSpinBox(this);
    m_port->setRange(0, 65535);
    m_port->setSpecialValueText(i18n("Default"));
    m_folder = new QLineEdit(this);
    m_user = new QLineEdit(this);
    m_user->setPlaceholderText(i18n("Optional"));
    m_label = new QLineEdit(this);
    m_preview = new QLabel(this);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_preview->setEnabled(false);

    form->addRow(i18n("Type:"), m_type);
    form->addRow(i18n("Server:"), m_server);
    form->addRow(i18n("Port:"), m_port);
    form->addRow(i18n("Folder:"), m_folder);
    form->addRow(i18n("User name:"), m_user);
    form->addRow(i18n("Name:"), m_label);
    form->addRow(i18n("Address:"), m_preview);

    auto *note = new QLabel(i18n("If the server needs a password, you are asked for it when you open the "
                                 "connection; it can be kept in your wallet."),
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto *testRow = new QHBoxLayout;
    m_testButton = new QPushButton(QIcon::fromTheme(QStringLiteral("network-connect")), i18n("Test"), this);
    m_testResult = new QLabel(this);
    m_testResult->setWordWrap(true);
    testRow->addWidget(m_testButton);
    testRow->addWidget(m_testResult, 1);
    layout->addLayout(testRow);
    layout->addStretch();

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(i18n("Add"));
    m_buttons->button(QDialogButtonBox::Ok)->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    layout->addWidget(m_buttons);

    m_testTimer = new QTimer(this);
    m_testTimer->setSingleShot(true);
    connect(m_testTimer, &QTimer::timeout, this, &PaneroConnectServerDialog::testTimedOut);

    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        const QString scheme = m_type->currentData().toString();
        m_folder->setPlaceholderText(scheme == QLatin1String("smb") ? i18n("share/folder") : i18n("Optional"));
        updateState();
    });
    for (QLineEdit *e : {m_server, m_folder, m_user, m_label})
        connect(e, &QLineEdit::textChanged, this, &PaneroConnectServerDialog::updateState);
    connect(m_port, QOverload<int>::of(&QSpinBox::valueChanged), this, &PaneroConnectServerDialog::updateState);
    connect(m_testButton, &QPushButton::clicked, this, &PaneroConnectServerDialog::testConnection);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &PaneroConnectServerDialog::addClicked);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_folder->setPlaceholderText(i18n("Optional"));
    updateState();
    setMinimumWidth(480);
}

PaneroConnectServerDialog::~PaneroConnectServerDialog()
{
    if (m_testJob)
        m_testJob->kill(KJob::Quietly);
}

QUrl PaneroConnectServerDialog::url() const
{
    return PaneroCloudUtils::buildServerUrl(m_type->currentData().toString(), m_server->text(), m_port->value(),
                                            m_folder->text(), m_user->text());
}

void PaneroConnectServerDialog::updateState()
{
    const QUrl u = url();
    m_preview->setText(u.isValid() ? u.toDisplayString() : QString());
    m_label->setPlaceholderText(u.isValid() ? PaneroCloudUtils::defaultServerLabel(u) : QString());
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(u.isValid());
    m_testButton->setEnabled(u.isValid() && !m_testJob);
}

void PaneroConnectServerDialog::testConnection()
{
    const QUrl u = url();
    if (!u.isValid() || m_testJob)
        return;
    m_testResult->setText(i18n("Connecting..."));
    KIO::StatJob *job = KIO::statDetails(u, KIO::StatJob::SourceSide, KIO::StatNoDetails, KIO::HideProgressInfo);
    KJobWidgets::setWindow(job, this); // lets KIO ask for the password with this dialog as parent
    m_testJob = job;
    connect(job, &KJob::result, this, &PaneroConnectServerDialog::testFinished);
    m_testTimer->start(TEST_TIMEOUT_MS);
    updateState();
}

void PaneroConnectServerDialog::testFinished(KJob *job)
{
    m_testTimer->stop();
    m_testJob = nullptr;
    if (job->error())
        m_testResult->setText(i18n("Failed: %1", job->errorString()));
    else
        m_testResult->setText(i18n("Connected successfully."));
    updateState();
}

void PaneroConnectServerDialog::testTimedOut()
{
    if (m_testJob)
        m_testJob->kill(KJob::Quietly);
    m_testJob = nullptr;
    m_testResult->setText(i18n("Failed: the server did not answer in time."));
    updateState();
}

void PaneroConnectServerDialog::addClicked()
{
    const QUrl u = url();
    if (!u.isValid())
        return;
    m_created = PaneroCloud::instance()->addServer(u, m_label->text());
    accept();
}
