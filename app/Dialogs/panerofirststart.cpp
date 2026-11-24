/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerofirststart.h"

#include "panerosettings.h"
#include "../Panel/paneroappearance.h"
#include "../krglobal.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

#include <KConfigCore/KConfigGroup>
#include <KConfigCore/KSharedConfig>
#include <KI18n/KLocalizedString>

namespace
{
QWizardPage *makePage(const QString &title, const QString &subTitle)
{
    auto *p = new QWizardPage;
    p->setTitle(title);
    p->setSubTitle(subTitle);
    return p;
}

QLabel *hint(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setEnabled(false); // secondary text colour
    return label;
}
} // namespace

void PaneroFirstStart::runOnFirstStart()
{
    if (!krConfig)
        krConfig = KSharedConfig::openConfig().data();
    const KConfigGroup top(krConfig, QString());
    if (!top.readEntry("First Time", true))
        return; // an existing user (or imported Krusader settings)
    if (qEnvironmentVariableIsSet("PANERO_NO_FIRST_START_ASSISTANT"))
        return; // automated runs that start with an empty profile
    PaneroFirstStart assistant;
    if (assistant.exec() == QDialog::Accepted) {
        assistant.save();
        krConfig->sync();
        PaneroAppearance::instance()->apply(); // the main window is created with the new look
    }
    KConfigGroup(krConfig, "Panero").writeEntry("First Start Assistant Shown", true);
    krConfig->sync();
}

void PaneroFirstStart::showFromHelp(QWidget *parent)
{
    PaneroFirstStart assistant(parent);
    if (assistant.exec() != QDialog::Accepted)
        return;
    const QString restartBefore = PaneroSettings::restartSignature();
    const QString selectionBefore = PaneroSettings::mouseSelection();
    assistant.save();
    PaneroSettings::applyLive();
    if (PaneroSettings::mouseSelection() != selectionBefore)
        PaneroSettings::recreatePanels();
    if (PaneroSettings::restartSignature() != restartBefore)
        PaneroSettings::askRestart(parent);
}

PaneroFirstStart::PaneroFirstStart(QWidget *parent)
    : QWizard(parent)
{
    setWindowTitle(i18n("Welcome to Panero"));
    setObjectName(QStringLiteral("PaneroFirstStart"));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage);
    setButtonText(QWizard::FinishButton, i18n("Start Panero"));
    if (parent)
        setButtonText(QWizard::FinishButton, i18n("Apply"));

    // ---- 1. look
    QWizardPage *look = makePage(i18n("Choose a look"), i18n("Everything here can be changed later in Settings (Ctrl+,) "
                                                         "or with the gear button on the toolbar."));
    auto *lookForm = new QFormLayout(look);
    auto *themeBox = new QWidget(look);
    auto *themeLayout = new QVBoxLayout(themeBox);
    themeLayout->setContentsMargins(0, 0, 0, 0);
    m_theme = new QButtonGroup(this);
    const QPair<int, QString> themes[] = {{1, i18n("Light")}, {2, i18n("Dark")}, {0, i18n("System (follow the desktop)")}};
    for (const auto &t : themes) {
        auto *radio = new QRadioButton(t.second, themeBox);
        radio->setChecked(PaneroSettings::theme() == t.first);
        m_theme->addButton(radio, t.first);
        themeLayout->addWidget(radio);
    }
    lookForm->addRow(i18n("Appearance:"), themeBox);
    m_paneroStyle = new QCheckBox(i18n("Panero style (modern look with Panero's colors)"), look);
    m_paneroStyle->setChecked(PaneroSettings::paneroStyle());
    m_paneroStyle->setToolTip(i18n("Off: the system's Qt style and colors"));
    lookForm->addRow(QString(), m_paneroStyle);
    auto *iconTheme = new QComboBox(look);
    if (PaneroSettings::fillIconThemeCombo(iconTheme)) {
        m_iconTheme = iconTheme;
        m_iconTheme->setCurrentIndex(qMax(0, m_iconTheme->findData(PaneroSettings::iconTheme())));
        lookForm->addRow(i18n("Icons:"), m_iconTheme);
    } else {
        delete iconTheme;
    }
    addPage(look);

    // ---- 2. drives and cloud
    QWizardPage *drives = makePage(i18n("Drives and cloud"), i18n("How Panero shows your drives and cloud folders."));
    auto *drivesLayout = new QVBoxLayout(drives);
    m_driveButtons = new QCheckBox(i18n("Drive buttons above each panel"), drives);
    m_driveButtons->setChecked(PaneroSettings::showDriveButtons());
    drivesLayout->addWidget(m_driveButtons);
    drivesLayout->addWidget(hint(i18n("System, Home, USB drives and network shares as buttons, as in Total Commander. "
                                      "Without them, the drive list is next to the path (Alt+F1 / Alt+F2)."),
                                 drives));
    m_cloud = new QCheckBox(i18n("Cloud section in the sidebar"), drives);
    m_cloud->setChecked(PaneroSettings::showCloud());
    drivesLayout->addSpacing(12);
    drivesLayout->addWidget(m_cloud);
#ifdef Q_OS_WIN
    drivesLayout->addWidget(hint(i18n("OneDrive, Google Drive, Dropbox and other cloud apps' folders on this PC."), drives));
#else
    drivesLayout->addWidget(hint(i18n("Google Drive, OneDrive, Dropbox and servers you add (rclone, SFTP, WebDAV...)."), drives));
#endif
    drivesLayout->addStretch(1);
    addPage(drives);

    // ---- 3. mouse
    QWizardPage *mouse = makePage(i18n("Mouse selection"), i18n("How a click works in the file lists."));
    auto *mouseLayout = new QVBoxLayout(mouse);
    m_mouse = new QButtonGroup(this);
    auto *panero = new QRadioButton(i18n("Panero (like Total Commander)"), mouse);
    m_mouse->addButton(panero, 5);
    mouseLayout->addWidget(panero);
    mouseLayout->addWidget(hint(i18n("A click moves the cursor and never loses your marks. A short right click, "
                                     "Insert or Space marks files; holding the right button opens the menu."),
                                mouse));
    auto *classic = new QRadioButton(i18n("Classic (like Windows Explorer and Krusader)"), mouse);
    m_mouse->addButton(classic, 0);
    mouseLayout->addSpacing(12);
    mouseLayout->addWidget(classic);
    mouseLayout->addWidget(hint(i18n("A click selects the file, Ctrl+click and Shift+click add more; "
                                     "a right click opens the menu."),
                                mouse));
    const QString mode = PaneroSettings::mouseSelection();
    (mode == QLatin1String("0") ? classic : panero)->setChecked(true);
    mouseLayout->addStretch(1);
    addPage(mouse);

    // ---- 4. done
    QWizardPage *done = makePage(i18n("Ready"), QString());
    auto *doneLayout = new QVBoxLayout(done);
    auto *doneText = new QLabel(i18n("<p>Panero is ready.</p>"
                                     "<p>The gear button on the toolbar has the most used switches; "
                                     "<b>Settings</b> (Ctrl+,) has everything. "
                                     "This assistant is in <b>Help › First-Start Assistant</b>.</p>"),
                                done);
    doneText->setWordWrap(true);
    doneLayout->addWidget(doneText);
    doneLayout->addStretch(1);
    addPage(done);

    resize(560, 420);
}

void PaneroFirstStart::save()
{
    PaneroSettings::setTheme(m_theme->checkedId());
    PaneroSettings::setPaneroStyle(m_paneroStyle->isChecked());
    if (m_iconTheme)
        PaneroSettings::setIconTheme(m_iconTheme->currentData().toString());
    PaneroSettings::setShowDriveButtons(m_driveButtons->isChecked());
    PaneroSettings::setShowCloud(m_cloud->isChecked());
    PaneroSettings::setMouseSelection(QString::number(m_mouse->checkedId()));
    krConfig->sync();
}
