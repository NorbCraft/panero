/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroviewcolorsdialog.h"

#include "../Panel/krcolorcache.h"
#include "../Panel/listpanel.h"
#include "../Panel/paneroappearance.h"
#include "../Panel/panerotheme.h"
#include "../Panel/paneroiconthemes.h"
#include "../krusader.h"
#include "../Panel/PanelView/krview.h"
#include "../Panel/PanelView/krviewfactory.h"
#include "../Panel/PanelView/krselectionmode.h"
#include "../defaults.h"
#include "../kractions.h"
#include "../krglobal.h"
#include "../krslots.h"
#include "../krusaderview.h"
#include "../panelmanager.h"
#include "../paneltabbar.h"

#include <QAbstractItemView>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QPixmap>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <KConfigCore/KConfig>
#include <KConfigCore/KConfigGroup>
#include <KI18n/KLocalizedString>
#include <KWidgetsAddons/KColorButton>
#include <KWidgetsAddons/KToggleAction>
#include <KWidgetsAddons/KMessageBox>
#include <KConfigWidgets/KStandardAction>
#include <KXmlGui/KActionCollection>

namespace
{
const char *DETAILED_VIEW_GROUP = "KrInterDetailedView";
const int ICON_SIZES[] = {16, 22, 32, 48};

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
} // namespace

void PaneroViewColorsDialog::showDialog(QWidget *parent)
{
    PaneroViewColorsDialog dialog(parent);
    dialog.exec();
}

PaneroViewColorsDialog::PaneroViewColorsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Quick Settings"));
    auto *layout = new QVBoxLayout(this);

    // ---- look
    auto *lookBox = new QGroupBox(i18n("Look"), this);
    auto *lookForm = new QFormLayout(lookBox);
    m_theme = new QComboBox(lookBox);
    m_theme->addItems({i18n("Follow the desktop"), i18n("Light"), i18n("Dark")});
    m_theme->setToolTip(i18n("Panero's light or dark appearance. 'Follow the desktop' uses the "
                             "Zorin/GNOME light/dark setting. Other applications are not affected."));
    lookForm->addRow(i18n("Appearance:"), m_theme);
    m_paneroStyle = new QCheckBox(i18n("Use Panero's look (Fusion style with Panero colors)"), lookBox);
    m_paneroStyle->setToolTip(i18n("If unchecked, Panero uses the system Qt style and palette."));
    lookForm->addRow(QString(), m_paneroStyle);
    m_iconSet = new QComboBox(lookBox);
    m_iconSet->addItem(i18n("System icons"), QString());
    m_iconSet->addItem(i18n("Panero Line"), QStringLiteral("line"));
    m_iconSet->setToolTip(i18n("Panero Line: thin line icons for the toolbar and sidebar. File icons always come from the system."));
    lookForm->addRow(i18n("Icons:"), m_iconSet);
    m_folderColor = new QComboBox(lookBox);
    m_folderColor->addItem(i18n("As in the icon theme"), QString());
    for (const auto &preset : PaneroIconThemes::folderColorPresets()) {
        QPixmap swatch(16, 16);
        swatch.fill(preset.second);
        m_folderColor->addItem(QIcon(swatch), preset.first, preset.second.name());
    }
    m_folderColor->addItem(i18n("Custom..."), QStringLiteral("custom"));
    lookForm->addRow(i18n("Folder color:"), m_folderColor);
    connect(m_folderColor, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (m_folderColor->itemData(index).toString() != QLatin1String("custom"))
            return;
        const QColor picked = QColorDialog::getColor(m_customFolderColor.isValid() ? m_customFolderColor : QColor(0x35, 0x84, 0xe4), this,
                                                     i18n("Folder Color"));
        if (!picked.isValid()) {
            m_folderColor->setCurrentIndex(0);
            return;
        }
        m_customFolderColor = picked;
        QPixmap swatch(16, 16);
        swatch.fill(picked);
        m_folderColor->setItemIcon(index, QIcon(swatch));
    });
    m_menuMode = new QComboBox(lookBox);
    m_menuMode->addItem(i18n("Menu button (at the end of the toolbar)"), false);
    m_menuMode->addItem(i18n("Menu bar (Alt+letter works)"), true);
    lookForm->addRow(i18n("Menus:"), m_menuMode);
    m_showCloud = new QCheckBox(i18n("Show the Cloud section in the sidebar"), lookBox);
    m_showCloud->setToolTip(i18n("Cloud drives and servers you added (Add Cloud..., Connect to Server...)."));
#ifdef Q_OS_WIN
    m_showCloud->hide(); // no cloud drives on Windows yet (rclone mounts need FUSE)
#else
    lookForm->addRow(QString(), m_showCloud);
#endif
    m_showDrives = new QCheckBox(i18n("Show drive buttons above each panel"), lookBox);
    m_showDrives->setToolTip(i18n("System, Home, USB drives, network shares and cloud drives as buttons; "
                                  "a click opens the drive in that panel. Alt+F1 / Alt+F2 open the drive list."));
    lookForm->addRow(QString(), m_showDrives);
    layout->addWidget(lookBox);

    // ---- file list
    auto *listBox = new QGroupBox(i18n("File list"), this);
    auto *listForm = new QFormLayout(listBox);
    m_density = new QComboBox(listBox);
    m_density->addItems({i18n("Compact"), i18n("Comfortable"), i18n("Spacious"), i18n("Custom")});
    listForm->addRow(i18n("Density:"), m_density);
    m_rowHeight = new QSpinBox(listBox);
    m_rowHeight->setRange(PaneroTheme::MinRowHeight, PaneroTheme::MaxRowHeight);
    m_rowHeight->setSuffix(i18nc("pixels", " px"));
    m_rowHeight->setToolTip(i18n("Row height in the detailed view. Rows never get smaller than the "
                                 "font and icon need, so nothing is clipped."));
    listForm->addRow(i18n("Row height:"), m_rowHeight);
    m_iconSize = new QComboBox(listBox);
    for (int size : ICON_SIZES)
        m_iconSize->addItem(i18nc("icon size in pixels", "%1 px", size), size);
    listForm->addRow(i18n("Icon size:"), m_iconSize);
    m_alternating = new QCheckBox(i18n("Alternating row backgrounds"), listBox);
    listForm->addRow(QString(), m_alternating);
    m_markIndicator = new QCheckBox(i18n("Show a check badge on marked files"), listBox);
    listForm->addRow(QString(), m_markIndicator);
    m_showHidden = new QCheckBox(i18n("Show hidden files"), listBox);
    listForm->addRow(QString(), m_showHidden);
    layout->addWidget(listBox);

    // ---- mouse selection: Panero (default) or the original Krusader way
    auto *mouseBox = new QGroupBox(i18n("Mouse"), this);
    auto *mouseForm = new QFormLayout(mouseBox);
    m_selection = new QComboBox(mouseBox);
    m_selection->addItem(i18n("Panero: click moves the cursor, right click marks"), QStringLiteral("5"));
    m_selection->addItem(i18n("Krusader: click selects, Ctrl+click adds"), QStringLiteral("0"));
    m_selection->setToolTip(i18n("How the mouse selects files. More modes (Total Commander, Konqueror, "
                                 "custom) are in All Settings > Panel > Selection Mode."));
    mouseForm->addRow(i18n("Selection:"), m_selection);
    m_holdFeedback = new QComboBox(mouseBox);
    m_holdFeedback->addItem(i18n("Pie next to the pointer"), QStringLiteral("pie"));
    m_holdFeedback->addItem(i18n("Ring around the pointer"), QStringLiteral("ring"));
    m_holdFeedback->addItem(i18n("Bar below the pointer"), QStringLiteral("bar"));
    m_holdFeedback->addItem(i18n("Off"), QStringLiteral("none"));
    m_holdFeedback->setToolTip(i18n("Shown while the right button is held, until the context menu opens."));
    mouseForm->addRow(i18n("Hold feedback:"), m_holdFeedback);
    layout->addWidget(mouseBox);

    // ---- colours (light and dark sets)
    auto *colorBox = new QGroupBox(i18n("Colors"), this);
    auto *colorLayout = new QVBoxLayout(colorBox);
    auto *tabs = new QTabWidget(colorBox);
    for (bool dark : {false, true}) {
        auto *page = new QWidget(tabs);
        auto *grid = new QGridLayout(page);
        int row = 0;
        for (const PaneroTheme::ColorKey &k : PaneroTheme::colorKeys()) {
            const QString key = QString::fromLatin1(k.key);
            auto *button = new KColorButton(page);
            button->setDefaultColor(PaneroTheme::defaultScheme(dark).*(k.member));
            auto *label = new QLabel(colorLabel(key) + QLatin1Char(':'), page);
            label->setBuddy(button);
            grid->addWidget(label, row / 2, (row % 2) * 2);
            if (key == QLatin1String("Current Background")) {
                // "Transparent": only the cursor frame is drawn
                auto *cell = new QWidget(page);
                auto *cellLayout = new QHBoxLayout(cell);
                cellLayout->setContentsMargins(0, 0, 0, 0);
                cellLayout->addWidget(button, 1);
                auto *transparent = new QCheckBox(i18n("Transparent"), cell);
                transparent->setToolTip(i18n("No fill: only the frame shows the cursor row"));
                connect(transparent, &QCheckBox::toggled, button, &QWidget::setDisabled);
                cellLayout->addWidget(transparent);
                m_transparentFill.insert(PaneroTheme::keyPrefix(dark) + key, transparent);
                grid->addWidget(cell, row / 2, (row % 2) * 2 + 1);
            } else {
                grid->addWidget(button, row / 2, (row % 2) * 2 + 1);
            }
            m_colorButtons.insert(PaneroTheme::keyPrefix(dark) + key, button);
            ++row;
        }
        tabs->addTab(page, dark ? i18n("Dark appearance") : i18n("Light appearance"));
    }
    tabs->setCurrentIndex(PaneroTheme::isDark() ? 1 : 0);
    colorLayout->addWidget(tabs);
    m_classicColors = new QCheckBox(i18n("Use the classic Krusader color settings instead"), colorBox);
    m_classicColors->setToolTip(i18n("Uses the colors from Configure Panero > Colors."));
    colorLayout->addWidget(m_classicColors);
    layout->addWidget(colorBox);

    // ---- buttons
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel |
                                         QDialogButtonBox::RestoreDefaults, this);
    QPushButton *more = buttons->addButton(i18n("All Settings..."), QDialogButtonBox::HelpRole);
    more->setToolTip(i18n("Open Configure Panero with all original settings"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &PaneroViewColorsDialog::apply);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            &PaneroViewColorsDialog::restoreDefaults);
    connect(more, &QPushButton::clicked, this, [this]() {
        reject();
        SLOTS->runKonfigurator(false);
    });
    connect(m_density, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PaneroViewColorsDialog::densityChanged);
    connect(m_rowHeight, QOverload<int>::of(&QSpinBox::valueChanged), this, &PaneroViewColorsDialog::rowHeightEdited);
    connect(m_iconSize, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PaneroViewColorsDialog::rowHeightEdited);

    load();
}

void PaneroViewColorsDialog::load()
{
    const KConfigGroup g(krConfig, PaneroTheme::GROUP);
    m_theme->setCurrentIndex(qBound(0, g.readEntry("Theme", 0), 2));
    m_paneroStyle->setChecked(g.readEntry("Panero Style", true));
    m_alternating->setChecked(g.readEntry("Alternating Rows", true));
    m_markIndicator->setChecked(g.readEntry("Mark Indicator", false));
    m_iconSet->setCurrentIndex(qMax(0, m_iconSet->findData(g.readEntry("Icon Set", QString()))));
    const QString folder = g.readEntry("Folder Color", QString());
    int folderIndex = m_folderColor->findData(folder);
    if (folderIndex < 0 && QColor(folder).isValid()) {
        folderIndex = m_folderColor->findData(QStringLiteral("custom"));
        m_customFolderColor = QColor(folder);
        QPixmap swatch(16, 16);
        swatch.fill(m_customFolderColor);
        m_folderColor->setItemIcon(folderIndex, QIcon(swatch));
    }
    m_folderColor->setCurrentIndex(qMax(0, folderIndex));
    m_menuMode->setCurrentIndex(KConfigGroup(krConfig, "Startup").readEntry("Show menubar", false) ? 1 : 0);
    if (QAction *menubar = krApp->actionCollection()->action(KStandardAction::name(KStandardAction::ShowMenubar)))
        m_menuMode->setCurrentIndex(menubar->isChecked() ? 1 : 0);
    m_showCloud->setChecked(g.readEntry("Show Cloud", PANERO_CLOUD_DEFAULT));
    m_showDrives->setChecked(g.readEntry("Show Drive Buttons", false));
    m_holdFeedback->setCurrentIndex(qMax(0, m_holdFeedback->findData(
        KConfigGroup(krConfig, "Look&Feel").readEntry("Hold Indicator", QStringLiteral("pie")))));
    m_classicColors->setChecked(!g.readEntry("Use Panero Colors", true));
    m_showHidden->setChecked(KrActions::actToggleHidden && KrActions::actToggleHidden->isChecked());
    const QString mode = KConfigGroup(krConfig, "Look&Feel").readEntry("Mouse Selection", QString::number(_MouseSelection));
    if (m_selection->findData(mode) < 0) // another mode chosen in All Settings
        m_selection->addItem(i18n("Other (set in All Settings)"), mode);
    m_selection->setCurrentIndex(m_selection->findData(mode));

    const int iconSize = KConfigGroup(krConfig, DETAILED_VIEW_GROUP).readEntry("IconSize", QString("22")).toInt();
    const int iconIndex = m_iconSize->findData(iconSize);
    m_updatingDensity = true;
    m_iconSize->setCurrentIndex(iconIndex >= 0 ? iconIndex : 1);
    const int configured = PaneroTheme::configuredRowHeight();
    m_rowHeight->setValue(configured > 0 ? configured : PaneroTheme::ComfortableRowHeight);
    m_updatingDensity = false;
    rowHeightEdited();

    for (auto it = m_colorButtons.constBegin(); it != m_colorButtons.constEnd(); ++it) {
        const QColor c = PaneroTheme::readColor(g.readEntry(it.key(), QString()));
        const bool transparent = c.isValid() && c.alpha() == 0;
        it.value()->setColor(c.isValid() && !transparent ? c : it.value()->defaultColor());
        if (QCheckBox *box = m_transparentFill.value(it.key()))
            box->setChecked(transparent);
    }
}

void PaneroViewColorsDialog::densityChanged(int index)
{
    if (m_updatingDensity)
        return;
    m_updatingDensity = true;
    switch (index) {
    case 0: // compact
        m_rowHeight->setValue(PaneroTheme::CompactRowHeight);
        m_iconSize->setCurrentIndex(m_iconSize->findData(16));
        break;
    case 1: // comfortable
        m_rowHeight->setValue(PaneroTheme::ComfortableRowHeight);
        m_iconSize->setCurrentIndex(m_iconSize->findData(22));
        break;
    case 2: // spacious
        m_rowHeight->setValue(32);
        m_iconSize->setCurrentIndex(m_iconSize->findData(22));
        break;
    default:
        break;
    }
    m_updatingDensity = false;
}

void PaneroViewColorsDialog::rowHeightEdited()
{
    if (m_updatingDensity)
        return;
    const int h = m_rowHeight->value();
    const int icon = m_iconSize->currentData().toInt();
    int preset = 3;
    if (h == PaneroTheme::CompactRowHeight && icon == 16)
        preset = 0;
    else if (h == PaneroTheme::ComfortableRowHeight && icon == 22)
        preset = 1;
    else if (h == 32 && icon == 22)
        preset = 2;
    m_updatingDensity = true;
    m_density->setCurrentIndex(preset);
    m_updatingDensity = false;
}

void PaneroViewColorsDialog::restoreDefaults()
{
    m_theme->setCurrentIndex(0);
    m_paneroStyle->setChecked(true);
    m_alternating->setChecked(true);
    m_markIndicator->setChecked(false);
    m_iconSet->setCurrentIndex(0);
    m_folderColor->setCurrentIndex(0);
    m_menuMode->setCurrentIndex(0);
    m_showCloud->setChecked(true);
    m_showDrives->setChecked(false);
    m_holdFeedback->setCurrentIndex(0);
    for (QCheckBox *box : qAsConst(m_transparentFill))
        box->setChecked(false);
    m_classicColors->setChecked(false);
    m_selection->setCurrentIndex(m_selection->findData(QString::number(_MouseSelection)));
    m_density->setCurrentIndex(1); // comfortable: sets row height and icon size
    for (KColorButton *button : qAsConst(m_colorButtons))
        button->setColor(button->defaultColor());
}

void PaneroViewColorsDialog::apply()
{
    KConfigGroup g(krConfig, PaneroTheme::GROUP);
    // these need a restart: a live switch leaves old icons and colours behind
    const QString folderValue = m_folderColor->currentData().toString() == QLatin1String("custom")
        ? (m_customFolderColor.isValid() ? m_customFolderColor.name() : QString())
        : m_folderColor->currentData().toString();
    const bool needsRestart = g.readEntry("Theme", 0) != m_theme->currentIndex()
        || g.readEntry("Panero Style", true) != m_paneroStyle->isChecked()
        || g.readEntry("Icon Set", QString()) != m_iconSet->currentData().toString()
        || g.readEntry("Folder Color", QString()) != folderValue;
    g.writeEntry("Icon Set", m_iconSet->currentData().toString());
    g.writeEntry("Folder Color", folderValue);
    g.writeEntry("Show Cloud", m_showCloud->isChecked());
    g.writeEntry("Show Drive Buttons", m_showDrives->isChecked());
    KConfigGroup(krConfig, "Look&Feel").writeEntry("Hold Indicator", m_holdFeedback->currentData().toString());
    if (QAction *menubar = krApp->actionCollection()->action(KStandardAction::name(KStandardAction::ShowMenubar))) {
        if (menubar->isChecked() != m_menuMode->currentData().toBool())
            menubar->trigger();
    }
    g.writeEntry("Theme", m_theme->currentIndex());
    g.writeEntry("Panero Style", m_paneroStyle->isChecked());
    g.writeEntry("Alternating Rows", m_alternating->isChecked());
    g.writeEntry("Mark Indicator", m_markIndicator->isChecked());
    g.writeEntry("Use Panero Colors", !m_classicColors->isChecked());
    g.writeEntry("Row Height", m_rowHeight->value());
    for (auto it = m_colorButtons.constBegin(); it != m_colorButtons.constEnd(); ++it) {
        QCheckBox *transparent = m_transparentFill.value(it.key());
        if (transparent && transparent->isChecked()) {
            g.writeEntry(it.key(), QStringLiteral("transparent"));
            continue;
        }
        // store only real customisations so later default improvements still apply
        if (it.value()->color() == it.value()->defaultColor())
            g.deleteEntry(it.key());
        else
            g.writeEntry(it.key(), it.value()->color().name());
    }
    KConfigGroup lookAndFeel(krConfig, "Look&Feel");
    const QString selectionMode = m_selection->currentData().toString();
    const bool selectionChanged = lookAndFeel.readEntry("Mouse Selection", QString::number(_MouseSelection)) != selectionMode;
    lookAndFeel.writeEntry("Mouse Selection", selectionMode);
    const int iconSize = m_iconSize->currentData().toInt();
    KConfigGroup(krConfig, DETAILED_VIEW_GROUP).writeEntry("IconSize", QString::number(iconSize));
    krConfig->sync();

    PaneroAppearance::instance()->apply();
    KrColorCache::getColorCache().refreshColors();
    applyToPanels(iconSize);

    if (selectionChanged) {
        // as Configure Panero does: the views pick the selection mode up when created
        KrSelectionMode::resetSelectionHandler();
        const bool leftActive = ACTIVE_PANEL->gui->isLeft();
        MAIN_VIEW->leftManager()->slotRecreatePanels();
        MAIN_VIEW->rightManager()->slotRecreatePanels();
        (leftActive ? LEFT_PANEL : RIGHT_PANEL)->slotFocusOnMe();
    }

    emit PaneroAppearance::instance()->settingsChanged();

    if (needsRestart) {
        const auto answer = KMessageBox::questionTwoActions(
            this, i18n("Restart Panero now to apply the new appearance?\nYour folders and tabs are kept."),
            i18n("Restart Panero"), KGuiItem(i18n("Restart"), QStringLiteral("view-refresh")),
            KGuiItem(i18n("Later"), QStringLiteral("chronometer")));
        if (answer == KMessageBox::PrimaryAction) {
            Krusader::restart();
            return;
        }
    }

    if (KrActions::actToggleHidden && KrActions::actToggleHidden->isChecked() != m_showHidden->isChecked())
        KrActions::actToggleHidden->trigger();
}

void PaneroViewColorsDialog::applyToPanels(int iconSize)
{
    for (PanelManager *manager : {MAIN_VIEW->leftManager(), MAIN_VIEW->rightManager()}) {
        for (int i = 0; i < manager->tabCount(); ++i) {
            ListPanel *panel = manager->tabAt(i);
            if (!panel || !panel->view)
                continue;
            if (panel->view->instance()->name() == QLatin1String(DETAILED_VIEW_GROUP))
                panel->view->setFileIconSize(iconSize);
            panel->view->refreshColors();
            if (auto *itemView = qobject_cast<QAbstractItemView *>(panel->view->widget()))
                itemView->doItemsLayout();
        }
    }
}
