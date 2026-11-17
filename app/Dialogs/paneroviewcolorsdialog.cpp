/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroviewcolorsdialog.h"

#include "../Panel/krcolorcache.h"
#include "../Panel/listpanel.h"
#include "../Panel/paneroappearance.h"
#include "../Panel/panerotheme.h"
#include "../Panel/PanelView/krview.h"
#include "../Panel/PanelView/krviewfactory.h"
#include "../kractions.h"
#include "../krglobal.h"
#include "../krslots.h"
#include "../krusaderview.h"
#include "../panelmanager.h"
#include "../paneltabbar.h"

#include <QAbstractItemView>
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
    setWindowTitle(i18n("View & Colors"));
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
            grid->addWidget(button, row / 2, (row % 2) * 2 + 1);
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
    m_markIndicator->setChecked(g.readEntry("Mark Indicator", true));
    m_classicColors->setChecked(!g.readEntry("Use Panero Colors", true));
    m_showHidden->setChecked(KrActions::actToggleHidden && KrActions::actToggleHidden->isChecked());

    const int iconSize = KConfigGroup(krConfig, DETAILED_VIEW_GROUP).readEntry("IconSize", QString("22")).toInt();
    const int iconIndex = m_iconSize->findData(iconSize);
    m_updatingDensity = true;
    m_iconSize->setCurrentIndex(iconIndex >= 0 ? iconIndex : 1);
    const int configured = PaneroTheme::configuredRowHeight();
    m_rowHeight->setValue(configured > 0 ? configured : PaneroTheme::ComfortableRowHeight);
    m_updatingDensity = false;
    rowHeightEdited();

    for (auto it = m_colorButtons.constBegin(); it != m_colorButtons.constEnd(); ++it) {
        const QColor c = g.readEntry(it.key(), QColor());
        it.value()->setColor(c.isValid() ? c : it.value()->defaultColor());
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
    m_markIndicator->setChecked(true);
    m_classicColors->setChecked(false);
    m_density->setCurrentIndex(1); // comfortable: sets row height and icon size
    for (KColorButton *button : qAsConst(m_colorButtons))
        button->setColor(button->defaultColor());
}

void PaneroViewColorsDialog::apply()
{
    KConfigGroup g(krConfig, PaneroTheme::GROUP);
    g.writeEntry("Theme", m_theme->currentIndex());
    g.writeEntry("Panero Style", m_paneroStyle->isChecked());
    g.writeEntry("Alternating Rows", m_alternating->isChecked());
    g.writeEntry("Mark Indicator", m_markIndicator->isChecked());
    g.writeEntry("Use Panero Colors", !m_classicColors->isChecked());
    g.writeEntry("Row Height", m_rowHeight->value());
    for (auto it = m_colorButtons.constBegin(); it != m_colorButtons.constEnd(); ++it) {
        // store only real customisations so later default improvements still apply
        if (it.value()->color() == it.value()->defaultColor())
            g.deleteEntry(it.key());
        else
            g.writeEntry(it.key(), it.value()->color());
    }
    const int iconSize = m_iconSize->currentData().toInt();
    KConfigGroup(krConfig, DETAILED_VIEW_GROUP).writeEntry("IconSize", QString::number(iconSize));
    krConfig->sync();

    PaneroAppearance::instance()->apply();
    KrColorCache::getColorCache().refreshColors();
    applyToPanels(iconSize);

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
