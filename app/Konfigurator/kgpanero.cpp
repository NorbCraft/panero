/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kgpanero.h"

#include "../Dialogs/panerosettings.h"
#include "../Panel/panerotheme.h"
#include "../defaults.h"
#include "../krglobal.h"
#include "../krusader.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <KConfigCore/KConfigGroup>
#include <KConfigWidgets/KStandardAction>
#include <KI18n/KLocalizedString>
#include <KWidgetsAddons/KColorButton>
#include <KXmlGui/KActionCollection>

namespace
{
QString colorLabel(const QString &key)
{
    if (key == QLatin1String("Background"))
        return i18n("Row background");
    if (key == QLatin1String("Alternate Background"))
        return i18n("Alternate row background");
    if (key == QLatin1String("Text"))
        return i18n("Text");
    if (key == QLatin1String("Mark Foreground"))
        return i18n("Marked text");
    if (key == QLatin1String("Mark Background"))
        return i18n("Marked background");
    if (key == QLatin1String("Current Background"))
        return i18n("Cursor row fill");
    if (key == QLatin1String("Current Frame"))
        return i18n("Cursor frame (active panel)");
    if (key == QLatin1String("Inactive Current Frame"))
        return i18n("Cursor frame (inactive panel)");
    if (key == QLatin1String("Accent"))
        return i18n("Active panel and selection");
    return key;
}

QAction *menubarAction()
{
    return krApp ? krApp->actionCollection()->action(KStandardAction::name(KStandardAction::ShowMenubar)) : nullptr;
}
} // namespace

// ============================================================================ base

KgPaneroPage::KgPaneroPage(bool first, QWidget *parent)
    : KonfiguratorPage(first, parent)
{
    auto *inner = new QWidget(this);
    setWidget(inner);
    setWidgetResizable(true);
    m_layout = new QVBoxLayout(inner);
}

QGroupBox *KgPaneroPage::addGroup(const QString &title, QFormLayout **form)
{
    auto *box = new QGroupBox(title, widget());
    *form = new QFormLayout(box);
    (*form)->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    m_layout->addWidget(box);
    return box;
}

QLabel *KgPaneroPage::addNote(const QString &html, QWidget *parent)
{
    QLabel *label = createSettingsLink(html, parent ? parent : widget());
    if (!parent)
        m_layout->addWidget(label);
    return label;
}

void KgPaneroPage::finishSetup()
{
    m_layout->addStretch(1);
    loadInitialValues();
    // every own input marks the page as changed (Konfigurator items do this themselves)
    for (QComboBox *combo : widget()->findChildren<QComboBox *>())
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &KgPaneroPage::markChanged);
    for (QSpinBox *spin : widget()->findChildren<QSpinBox *>())
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &KgPaneroPage::markChanged);
    for (QAbstractButton *button : widget()->findChildren<QAbstractButton *>()) {
        if (button->isCheckable())
            connect(button, &QAbstractButton::toggled, this, &KgPaneroPage::markChanged);
    }
    for (KColorButton *button : widget()->findChildren<KColorButton *>())
        connect(button, &KColorButton::changed, this, &KgPaneroPage::markChanged);
}

void KgPaneroPage::markChanged()
{
    if (m_loading)
        return;
    m_changed = true;
    emit sigChanged();
}

bool KgPaneroPage::isChanged()
{
    return m_changed || KonfiguratorPage::isChanged();
}

bool KgPaneroPage::apply()
{
    if (!isChanged())
        return false;
    const QString restartBefore = PaneroSettings::restartSignature();
    bool recreate = KonfiguratorPage::apply(); // Konfigurator items (restart = recreate the panels)
    if (m_changed)
        recreate = save() || recreate;
    m_changed = false;
    krConfig->sync();
    PaneroSettings::applyLive(changesLook());
    emit sigChanged();
    if (PaneroSettings::restartSignature() != restartBefore && PaneroSettings::askRestart(this))
        return false;
    return recreate;
}

void KgPaneroPage::setDefaults()
{
    KonfiguratorPage::setDefaults();
    defaults();
}

void KgPaneroPage::loadInitialValues()
{
    KonfiguratorPage::loadInitialValues();
    m_loading = true;
    load();
    m_loading = false;
    m_changed = false;
    emit sigChanged();
}

// ====================================================================== Appearance

KgPaneroAppearance::KgPaneroAppearance(bool first, QWidget *parent)
    : KgPaneroPage(first, parent)
{
    QFormLayout *form;
    QGroupBox *lookBox = addGroup(i18n("Look"), &form);
    m_theme = new QComboBox(lookBox);
    PaneroSettings::fillThemeCombo(m_theme);
    form->addRow(i18n("Appearance:"), m_theme);
    m_paneroStyle = new QCheckBox(i18n("Use Panero's look (Fusion style with Panero colors)"), lookBox);
    m_paneroStyle->setToolTip(i18n("If unchecked, Panero uses the system Qt style and palette."));
    form->addRow(QString(), m_paneroStyle);
    m_iconSet = new QComboBox(lookBox);
    m_iconSet->addItem(i18n("System icons"), QString());
    m_iconSet->addItem(i18n("Panero Line"), QStringLiteral("line"));
    m_iconSet->setToolTip(i18n("Panero Line: thin line icons for the toolbar and sidebar. File icons come from the icon theme."));
    form->addRow(i18n("Toolbar icons:"), m_iconSet);
    m_iconTheme = new QComboBox(lookBox);
    if (PaneroSettings::fillIconThemeCombo(m_iconTheme))
        form->addRow(i18n("Icon theme:"), m_iconTheme);
    else
        m_iconTheme->hide();
    m_folderColor = new QComboBox(lookBox);
    PaneroSettings::fillFolderColorCombo(m_folderColor, this);
    form->addRow(i18n("Folder color:"), m_folderColor);
    // a new icon theme shows its own folder colour (D-022); a colour can be chosen again
    connect(m_iconTheme, QOverload<int>::of(&QComboBox::activated), this, [this]() {
        m_folderColor->setCurrentIndex(0);
    });
    m_menuMode = new QComboBox(lookBox);
    m_menuMode->addItem(i18n("Menu button (at the end of the toolbar)"), false);
    m_menuMode->addItem(i18n("Menu bar (Alt+letter works)"), true);
    form->addRow(i18n("Menus:"), m_menuMode);

    // ---- colours of the file list (light and dark sets)
    auto *colorBox = new QGroupBox(i18n("File list colors"), widget());
    auto *colorLayout = new QVBoxLayout(colorBox);
    auto *tabs = new QTabWidget(colorBox);
    for (bool dark : {false, true}) {
        auto *page = new QWidget(tabs);
        auto *grid = new QGridLayout(page);
        int row = 0;
        for (const PaneroTheme::ColorKey &k : PaneroTheme::colorKeys()) {
            const QString key = QString::fromLatin1(k.key);
            const QString configKey = PaneroTheme::keyPrefix(dark) + key;
            auto *button = new KColorButton(page);
            button->setDefaultColor(PaneroTheme::suggestedColor(dark, k.member));
            auto *label = new QLabel(colorLabel(key) + QLatin1Char(':'), page);
            label->setBuddy(button);
            grid->addWidget(label, row / 2, (row % 2) * 2);
            if (PaneroTheme::canBeTransparent(k.member)) {
                // "Transparent": the row keeps its background (cursor: only the frame shows)
                auto *cell = new QWidget(page);
                auto *cellLayout = new QHBoxLayout(cell);
                cellLayout->setContentsMargins(0, 0, 0, 0);
                cellLayout->addWidget(button, 1);
                auto *transparent = new QCheckBox(i18n("Transparent"), cell);
                transparent->setToolTip(k.member == &PaneroTheme::Scheme::currentBackground
                                            ? i18n("No fill: only the frame shows the cursor row")
                                            : i18n("No fill: marked files are shown by their text color only"));
                connect(transparent, &QCheckBox::toggled, button, &QWidget::setDisabled);
                cellLayout->addWidget(transparent);
                m_transparent.insert(configKey, transparent);
                grid->addWidget(cell, row / 2, (row % 2) * 2 + 1);
            } else {
                grid->addWidget(button, row / 2, (row % 2) * 2 + 1);
            }
            m_colorButtons.insert(configKey, button);
            ++row;
        }
        tabs->addTab(page, dark ? i18n("Dark appearance") : i18n("Light appearance"));
    }
    tabs->setCurrentIndex(PaneroTheme::isDark() ? 1 : 0);
    colorLayout->addWidget(tabs);
    m_classicColors = new QCheckBox(i18n("Use the classic Krusader color settings instead"), colorBox);
    m_classicColors->setToolTip(i18n("Uses the colors from Settings > Advanced > Colors."));
    colorLayout->addWidget(m_classicColors);
    colorLayout->addWidget(addNote(i18n("The classic colors are in <a href=\"settings:colors\">Advanced › Colors</a>."), colorBox));
    connect(m_classicColors, &QCheckBox::toggled, tabs, &QWidget::setDisabled);
    m_layout->addWidget(colorBox);

    finishSetup();
}

void KgPaneroAppearance::load()
{
    const KConfigGroup g(krConfig, PaneroTheme::GROUP);
    m_theme->setCurrentIndex(PaneroSettings::theme());
    m_paneroStyle->setChecked(PaneroSettings::paneroStyle());
    m_iconSet->setCurrentIndex(qMax(0, m_iconSet->findData(PaneroSettings::iconSet())));
    m_iconTheme->setCurrentIndex(qMax(0, m_iconTheme->findData(PaneroSettings::iconTheme())));
    PaneroSettings::setFolderColor(m_folderColor, PaneroSettings::storedFolderColor());
    bool menubar = KConfigGroup(krConfig, "Startup").readEntry("Show menubar", false);
    if (QAction *action = menubarAction())
        menubar = action->isChecked();
    m_menuMode->setCurrentIndex(menubar ? 1 : 0);
    m_classicColors->setChecked(!g.readEntry("Use Panero Colors", true));
    for (const PaneroTheme::ColorKey &k : PaneroTheme::colorKeys()) {
        for (bool dark : {false, true}) {
            const QString configKey = PaneroTheme::keyPrefix(dark) + QLatin1String(k.key);
            QColor c = PaneroTheme::readColor(g.readEntry(configKey, QString()));
            if (!c.isValid())
                c = PaneroTheme::defaultScheme(dark).*(k.member);
            const bool transparent = c.alpha() == 0;
            KColorButton *button = m_colorButtons.value(configKey);
            button->setColor(transparent ? button->defaultColor() : c);
            if (QCheckBox *box = m_transparent.value(configKey))
                box->setChecked(transparent);
        }
    }
}

bool KgPaneroAppearance::save()
{
    KConfigGroup g(krConfig, PaneroTheme::GROUP);
    PaneroSettings::setTheme(m_theme->currentIndex());
    PaneroSettings::setPaneroStyle(m_paneroStyle->isChecked());
    PaneroSettings::setIconSet(m_iconSet->currentData().toString());
    if (m_iconTheme->count() > 1)
        PaneroSettings::setIconTheme(m_iconTheme->currentData().toString());
    PaneroSettings::setStoredFolderColor(PaneroSettings::folderColor(m_folderColor));
    if (QAction *action = menubarAction()) {
        if (action->isChecked() != m_menuMode->currentData().toBool())
            action->trigger();
    }
    g.writeEntry("Use Panero Colors", !m_classicColors->isChecked());
    for (const PaneroTheme::ColorKey &k : PaneroTheme::colorKeys()) {
        for (bool dark : {false, true}) {
            const QString configKey = PaneroTheme::keyPrefix(dark) + QLatin1String(k.key);
            const QColor def = PaneroTheme::defaultScheme(dark).*(k.member);
            QCheckBox *transparent = m_transparent.value(configKey);
            KColorButton *button = m_colorButtons.value(configKey);
            // store only real customisations so later default improvements still apply
            if (transparent && transparent->isChecked()) {
                if (def.alpha() == 0)
                    g.deleteEntry(configKey);
                else
                    g.writeEntry(configKey, QStringLiteral("transparent"));
            } else if (def.alpha() > 0 && button->color() == def) {
                g.deleteEntry(configKey);
            } else {
                g.writeEntry(configKey, button->color().name());
            }
        }
    }
    return false;
}

void KgPaneroAppearance::defaults()
{
    m_theme->setCurrentIndex(0);
    m_paneroStyle->setChecked(true);
    m_iconSet->setCurrentIndex(0);
    m_iconTheme->setCurrentIndex(0);
    m_folderColor->setCurrentIndex(0);
    m_menuMode->setCurrentIndex(0);
    m_classicColors->setChecked(false);
    for (auto it = m_colorButtons.constBegin(); it != m_colorButtons.constEnd(); ++it)
        it.value()->setColor(it.value()->defaultColor());
    for (const PaneroTheme::ColorKey &k : PaneroTheme::colorKeys()) {
        for (bool dark : {false, true}) {
            if (QCheckBox *box = m_transparent.value(PaneroTheme::keyPrefix(dark) + QLatin1String(k.key)))
                box->setChecked((PaneroTheme::defaultScheme(dark).*(k.member)).alpha() == 0);
        }
    }
}

// ========================================================================== Panels

KgPaneroPanels::KgPaneroPanels(bool first, QWidget *parent)
    : KgPaneroPage(first, parent)
{
    QFormLayout *form;
    QGroupBox *listBox = addGroup(i18n("File list"), &form);
    m_density = new QComboBox(listBox);
    PaneroSettings::fillDensityCombo(m_density);
    form->addRow(i18n("Density:"), m_density);
    m_rowHeight = new QSpinBox(listBox);
    m_rowHeight->setRange(PaneroTheme::MinRowHeight, PaneroTheme::MaxRowHeight);
    m_rowHeight->setSuffix(i18nc("pixels", " px"));
    m_rowHeight->setToolTip(i18n("Row height in the details view. Rows never get smaller than the "
                                 "font and icon need, so nothing is clipped."));
    form->addRow(i18n("Row height:"), m_rowHeight);
    m_iconSize = new QComboBox(listBox);
    for (int size : PaneroSettings::iconSizes())
        m_iconSize->addItem(i18nc("icon size in pixels", "%1 px", size), size);
    form->addRow(i18n("Icon size:"), m_iconSize);
    form->addRow(QString(), createCheckBox(QString::fromLatin1(PaneroTheme::GROUP), QStringLiteral("Alternating Rows"), true,
                                           i18n("Alternating row backgrounds"), listBox));
    form->addRow(QString(), createCheckBox(QString::fromLatin1(PaneroTheme::GROUP), QStringLiteral("Mark Indicator"), false,
                                           i18n("Show a check badge on marked files"), listBox));
    m_showHidden = new QCheckBox(i18n("Show hidden files"), listBox);
    m_showHidden->setToolTip(i18n("Also View > Show Hidden Files (Alt+.)"));
    form->addRow(QString(), m_showHidden);
    m_previews = new QCheckBox(i18n("Thumbnails instead of icons in the details view"), listBox);
    m_previews->setToolTip(i18n("Pictures and documents show a small preview. The Thumbnails view "
                                "(toolbar: Details / Thumbnails) always shows previews."));
    form->addRow(QString(), m_previews);
    addNote(i18n("Fonts, sorting, columns, tabs and the path bar: <a href=\"settings:panel\">Advanced › Panel</a>."));

    connect(m_density, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &KgPaneroPanels::densityChanged);
    connect(m_rowHeight, QOverload<int>::of(&QSpinBox::valueChanged), this, &KgPaneroPanels::sizesEdited);
    connect(m_iconSize, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &KgPaneroPanels::sizesEdited);
    finishSetup();
}

void KgPaneroPanels::densityChanged(int index)
{
    if (m_updatingDensity)
        return;
    int height, size;
    if (!PaneroSettings::densityValues(index, &height, &size))
        return;
    m_updatingDensity = true;
    m_rowHeight->setValue(height);
    m_iconSize->setCurrentIndex(qMax(0, m_iconSize->findData(size)));
    m_updatingDensity = false;
}

void KgPaneroPanels::sizesEdited()
{
    if (m_updatingDensity)
        return;
    m_updatingDensity = true;
    m_density->setCurrentIndex(PaneroSettings::densityIndex(m_rowHeight->value(), m_iconSize->currentData().toInt()));
    m_updatingDensity = false;
}

void KgPaneroPanels::load()
{
    const int size = PaneroSettings::iconSize();
    if (m_iconSize->findData(size) < 0 && size > 0) // a size set elsewhere (Advanced > Panel, zoom)
        m_iconSize->addItem(i18nc("icon size in pixels", "%1 px", size), size);
    m_updatingDensity = true;
    m_iconSize->setCurrentIndex(qMax(0, m_iconSize->findData(size)));
    m_rowHeight->setValue(PaneroSettings::rowHeight());
    m_updatingDensity = false;
    sizesEdited();
    m_showHidden->setChecked(PaneroSettings::showHidden());
    m_previews->setChecked(PaneroSettings::detailsPreviews());
}

bool KgPaneroPanels::save()
{
    PaneroSettings::setRowHeightAndIconSize(m_rowHeight->value(), m_iconSize->currentData().toInt());
    PaneroSettings::applyShowHidden(m_showHidden->isChecked());
    if (m_previews->isChecked() != PaneroSettings::detailsPreviews())
        PaneroSettings::applyDetailsPreviews(m_previews->isChecked());
    return false;
}

void KgPaneroPanels::defaults()
{
    m_density->setCurrentIndex(1); // comfortable: sets row height and icon size
    m_showHidden->setChecked(_ShowHidden);
    m_previews->setChecked(false);
}

// ==================================================================== Mouse & keys

KgPaneroMouse::KgPaneroMouse(bool first, QWidget *parent)
    : KgPaneroPage(first, parent)
{
    QFormLayout *form;
    QGroupBox *mouseBox = addGroup(i18n("Mouse"), &form);
    m_selection = new QComboBox(mouseBox);
    PaneroSettings::fillSelectionCombo(m_selection, PaneroSettings::mouseSelection());
    form->addRow(i18n("Selection:"), m_selection);
    m_holdFeedback = new QComboBox(mouseBox);
    PaneroSettings::fillHoldFeedbackCombo(m_holdFeedback);
    form->addRow(i18n("Hold feedback:"), m_holdFeedback);
    form->addRow(QString(), addNote(i18n("Total Commander, Konqueror and custom modes, and the hold time: "
                                         "<a href=\"settings:panel-mouse\">Advanced › Panel › Selection Mode</a>."),
                                    mouseBox));

    QGroupBox *keysBox = addGroup(i18n("Keyboard"), &form);
    auto *shortcuts = new QPushButton(QIcon::fromTheme(QStringLiteral("configure-shortcuts")), i18n("Keyboard Shortcuts..."), keysBox);
    connect(shortcuts, &QPushButton::clicked, this, []() {
        if (QAction *keys = krApp->actionCollection()->action(KStandardAction::name(KStandardAction::KeyBindings)))
            keys->trigger();
    });
    form->addRow(i18n("Shortcuts:"), shortcuts);
    form->addRow(QString(), addNote(i18n("The function key bar and the command line: "
                                         "<a href=\"settings:startup\">Advanced › Startup</a>."),
                                    keysBox));
    finishSetup();
}

void KgPaneroMouse::load()
{
    const QString mode = PaneroSettings::mouseSelection();
    if (m_selection->findData(mode) < 0)
        m_selection->addItem(i18n("Other (set in Advanced > Panel)"), mode);
    m_selection->setCurrentIndex(m_selection->findData(mode));
    m_holdFeedback->setCurrentIndex(qMax(0, m_holdFeedback->findData(
        KConfigGroup(krConfig, "Look&Feel").readEntry("Hold Indicator", QStringLiteral("pie")))));
}

bool KgPaneroMouse::save()
{
    KConfigGroup(krConfig, "Look&Feel").writeEntry("Hold Indicator", m_holdFeedback->currentData().toString());
    const QString mode = m_selection->currentData().toString();
    if (mode == PaneroSettings::mouseSelection())
        return false;
    PaneroSettings::setMouseSelection(mode);
    PaneroSettings::recreatePanels();
    return false;
}

void KgPaneroMouse::defaults()
{
    m_selection->setCurrentIndex(m_selection->findData(QString::number(_MouseSelection)));
    m_holdFeedback->setCurrentIndex(0);
}

// =================================================================== Drives & cloud

KgPaneroDrives::KgPaneroDrives(bool first, QWidget *parent)
    : KgPaneroPage(first, parent)
{
    QFormLayout *form;
    QGroupBox *drivesBox = addGroup(i18n("Drives"), &form);
    m_showDrives = new QCheckBox(i18n("Show drive buttons above each panel"), drivesBox);
    m_showDrives->setToolTip(i18n("System, Home, USB drives, network shares and cloud drives as buttons; "
                                  "a click opens the drive in that panel. Alt+F1 / Alt+F2 open the drive list."));
    form->addRow(QString(), m_showDrives);
    m_driveLabels = new QComboBox(drivesBox);
    m_driveLabels->addItem(i18n("Icon and name"), QString());
#ifdef Q_OS_WIN
    m_driveLabels->addItem(i18n("Letter only"), QStringLiteral("short"));
#else
    m_driveLabels->addItem(i18n("Icon only"), QStringLiteral("short"));
#endif
    m_driveLabels->setToolTip(i18n("The name is always in the button's tooltip."));
    connect(m_showDrives, &QCheckBox::toggled, m_driveLabels, &QWidget::setEnabled);
    form->addRow(i18n("Drive buttons:"), m_driveLabels);
    form->addRow(QString(), createCheckBox(QStringLiteral("Look&Feel"), QStringLiteral("Media Button Visible"), true,
                                           i18n("Show the drive list button next to the path"), drivesBox, true,
                                           i18n("A button left of the path with all drives (Alt+F1 / Alt+F2)")));

    QGroupBox *cloudBox = addGroup(i18n("Cloud"), &form);
    m_showCloud = new QCheckBox(i18n("Show the Cloud section in the sidebar"), cloudBox);
#ifdef Q_OS_WIN
    m_showCloud->setToolTip(i18n("The cloud apps' folders found on this PC (OneDrive, Google Drive, Dropbox...)."));
#else
    m_showCloud->setToolTip(i18n("Cloud drives and servers you added (Add Cloud..., Connect to Server...)."));
#endif
    form->addRow(QString(), m_showCloud);
#ifndef Q_OS_WIN
    addNote(i18n("Which drives the drive list shows (sizes, file systems, hidden mount points): "
                 "<a href=\"settings:panel-media\">Advanced › Panel › Media Menu</a>."));
#endif
    finishSetup();
}

void KgPaneroDrives::load()
{
    const KConfigGroup g(krConfig, PaneroTheme::GROUP);
    m_showDrives->setChecked(PaneroSettings::showDriveButtons());
    m_driveLabels->setCurrentIndex(qMax(0, m_driveLabels->findData(g.readEntry("Drive Button Labels", QString()))));
    m_driveLabels->setEnabled(m_showDrives->isChecked());
    m_showCloud->setChecked(PaneroSettings::showCloud());
}

bool KgPaneroDrives::save()
{
    PaneroSettings::setShowDriveButtons(m_showDrives->isChecked());
    KConfigGroup(krConfig, PaneroTheme::GROUP).writeEntry("Drive Button Labels", m_driveLabels->currentData().toString());
    PaneroSettings::setShowCloud(m_showCloud->isChecked());
    return false;
}

void KgPaneroDrives::defaults()
{
    m_showDrives->setChecked(false);
    m_driveLabels->setCurrentIndex(0);
    m_showCloud->setChecked(PANERO_CLOUD_DEFAULT);
}

// ============================================================== Windows integration

#ifdef Q_OS_WIN
KgPaneroWindows::KgPaneroWindows(bool first, QWidget *parent)
    : KgPaneroPage(first, parent)
{
    QFormLayout *form;
    // (the right-click menu choice Explorer / Panero goes into this group)
    QGroupBox *menuBox = addGroup(i18n("Right-click menu"), &form);
    form->addRow(QString(), addNote(i18n("The right-click menu shows Explorer's items (Open with, Send to, "
                                         "the cloud apps...) with Panero's own items on top."),
                                    menuBox));

    QGroupBox *appsBox = addGroup(i18n("Programs"), &form);
    auto *terminalLabel = new QLabel(i18n("Terminal:"), appsBox);
    form->addRow(terminalLabel, createURLRequester(QStringLiteral("General"), QStringLiteral("Terminal"), QStringLiteral(_Terminal),
                                                   terminalLabel, appsBox, false, i18n("%d is replaced by the folder.")));
    auto *editorLabel = new QLabel(i18n("Editor (F4):"), appsBox);
    form->addRow(editorLabel, createURLRequester(QStringLiteral("General"), QStringLiteral("Editor"), QStringLiteral(_Editor),
                                                 editorLabel, appsBox, false,
                                                 i18n("'internal editor' uses Panero's built-in editor.")));

    QGroupBox *filesBox = addGroup(i18n("Files"), &form);
    form->addRow(QString(), createCheckBox(QStringLiteral("Look&Feel"), QStringLiteral("Rename Selects Extension"),
                                           _RenameSelectsExtension, i18n("Rename (F2) selects the extension too"), filesBox,
                                           false, i18n("Off: only the name is selected, as in Explorer.")));
    finishSetup();
}
#endif

// ======================================================================= Advanced

KgPaneroAdvancedIntro::KgPaneroAdvancedIntro(bool first, QWidget *parent)
    : KonfiguratorPage(first, parent)
{
    auto *inner = new QWidget(this);
    setWidget(inner);
    setWidgetResizable(true);
    auto *layout = new QVBoxLayout(inner);
    auto *text = createSettingsLink(
        i18n("<p>Krusader's original settings, for everything Panero's own pages do not cover: "
             "<a href=\"settings:startup\">Startup</a>, <a href=\"settings:panel\">Panel</a>, "
             "<a href=\"settings:colors\">Colors</a>, <a href=\"settings:general\">General</a>, "
             "<a href=\"settings:fine-tuning\">Fine-Tuning</a>, <a href=\"settings:archives\">Archives</a>, "
             "<a href=\"settings:dependencies\">Dependencies</a>, <a href=\"settings:useractions\">User Actions</a> "
             "and <a href=\"settings:protocols\">Protocols</a>.</p>"
             "<p>Options that Panero sets on its own pages (look, colors, drives) are hidden there "
             "or point to the Panero page.</p>"),
        inner);
    layout->addWidget(text);
    layout->addStretch(1);
}
