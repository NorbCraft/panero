/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerosettings.h"

#include "../Panel/krcolorcache.h"
#include "../Panel/listpanel.h"
#include "../Panel/paneroappearance.h"
#include "../Panel/paneroiconthemes.h"
#include "../Panel/panerotheme.h"
#include "../Panel/PanelView/krselectionmode.h"
#include "../Panel/PanelView/krview.h"
#include "../Panel/PanelView/krviewfactory.h"
#include "../defaults.h"
#include "../kractions.h"
#include "../krglobal.h"
#include "../krusader.h"
#include "../krusaderview.h"
#include "../panelmanager.h"

#include <QAbstractItemView>
#include <QColorDialog>
#include <QComboBox>
#include <QFileInfo>
#include <QIcon>
#include <QPixmap>
#include <QStandardPaths>

#include <KConfigCore/KConfigGroup>
#include <KI18n/KLocalizedString>
#include <KWidgetsAddons/KMessageBox>
#include <KWidgetsAddons/KToggleAction>

namespace PaneroSettings
{
const char *const DETAILED_VIEW_GROUP = "KrInterDetailedView";

namespace
{
const char *CUSTOM = "custom";
constexpr int SpaciousRowHeight = 32;
constexpr int CustomColorRole = Qt::UserRole + 1;

KConfigGroup look()
{
    return KConfigGroup(krConfig, PaneroTheme::GROUP);
}

KConfigGroup lookAndFeel()
{
    return KConfigGroup(krConfig, "Look&Feel");
}

QIcon swatch(const QColor &color)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(color);
    return QIcon(pixmap);
}
} // namespace

const QVector<int> &iconSizes()
{
    static const QVector<int> sizes = {16, 22, 32, 48};
    return sizes;
}

void fillThemeCombo(QComboBox *combo)
{
    combo->addItems({i18n("Follow the desktop"), i18n("Light"), i18n("Dark")});
    combo->setToolTip(i18n("Panero's light or dark appearance. 'Follow the desktop' uses the "
                           "desktop's light/dark setting. Other applications are not affected."));
}

bool fillIconThemeCombo(QComboBox *combo)
{
    combo->addItem(i18n("The desktop's icon theme"), QString());
#ifndef Q_OS_WIN
    // icon theme for files and folders: the desktop's, or one Panero ships (D-020)
    for (const QString &dir : QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("panero/icons"),
                                                         QStandardPaths::LocateDirectory)) {
        if (QFileInfo::exists(dir + QStringLiteral("/Win11/index.theme")) && combo->findData(QStringLiteral("Win11")) < 0)
            combo->addItem(i18n("Windows 11 (Win11 icon theme)"), QStringLiteral("Win11"));
    }
#endif
    combo->setToolTip(i18n("Icons of files, folders and drives. The dark variant is used with the dark look. "
                           "Choosing a theme shows its own folder colour."));
    return combo->count() > 1;
}

void fillFolderColorCombo(QComboBox *combo, QWidget *dialogParent)
{
    combo->addItem(i18n("As in the icon theme"), QString());
    for (const auto &preset : PaneroIconThemes::folderColorPresets())
        combo->addItem(swatch(preset.second), preset.first, preset.second.name());
    combo->addItem(i18n("Custom..."), QString::fromLatin1(CUSTOM));
    QObject::connect(combo, QOverload<int>::of(&QComboBox::activated), combo, [combo, dialogParent](int index) {
        if (combo->itemData(index).toString() != QLatin1String(CUSTOM))
            return;
        const QColor current = combo->itemData(index, CustomColorRole).value<QColor>();
        const QColor picked = QColorDialog::getColor(current.isValid() ? current : QColor(0x35, 0x84, 0xe4), dialogParent,
                                                     i18n("Folder Color"));
        if (!picked.isValid()) {
            if (!current.isValid())
                combo->setCurrentIndex(0);
            return;
        }
        combo->setItemData(index, picked, CustomColorRole);
        combo->setItemIcon(index, swatch(picked));
        emit combo->currentIndexChanged(index); // a new custom colour is a change too
    });
}

void setFolderColor(QComboBox *combo, const QString &value)
{
    int index = combo->findData(value);
    if (index < 0 && QColor(value).isValid()) {
        index = combo->findData(QString::fromLatin1(CUSTOM));
        combo->setItemData(index, QColor(value), CustomColorRole);
        combo->setItemIcon(index, swatch(QColor(value)));
    }
    combo->setCurrentIndex(qMax(0, index));
}

QString folderColor(const QComboBox *combo)
{
    if (combo->currentData().toString() == QLatin1String(CUSTOM)) {
        const QColor custom = combo->currentData(CustomColorRole).value<QColor>();
        return custom.isValid() ? custom.name() : QString();
    }
    return combo->currentData().toString();
}

void fillDensityCombo(QComboBox *combo)
{
    combo->addItems({i18n("Compact"), i18n("Comfortable"), i18n("Spacious"), i18n("Custom")});
    combo->setToolTip(i18n("Row height and icon size of the file list"));
}

int densityIndex(int rowHeight, int iconSize)
{
    if (rowHeight == PaneroTheme::CompactRowHeight && iconSize == 16)
        return 0;
    if (rowHeight == PaneroTheme::ComfortableRowHeight && iconSize == 22)
        return 1;
    if (rowHeight == SpaciousRowHeight && iconSize == 22)
        return 2;
    return 3;
}

bool densityValues(int index, int *rowHeight, int *iconSize)
{
    switch (index) {
    case 0:
        *rowHeight = PaneroTheme::CompactRowHeight;
        *iconSize = 16;
        return true;
    case 1:
        *rowHeight = PaneroTheme::ComfortableRowHeight;
        *iconSize = 22;
        return true;
    case 2:
        *rowHeight = SpaciousRowHeight;
        *iconSize = 22;
        return true;
    default:
        return false;
    }
}

void fillSelectionCombo(QComboBox *combo, const QString &current)
{
    combo->addItem(i18n("Panero: click moves the cursor, right click marks"), QStringLiteral("5"));
    combo->addItem(i18n("Krusader: click selects, Ctrl+click adds"), QStringLiteral("0"));
    combo->setToolTip(i18n("How the mouse selects files. More modes (Total Commander, Konqueror, "
                           "custom) are in Settings > Advanced > Panel > Selection Mode."));
    if (combo->findData(current) < 0) // another mode chosen in the Advanced settings
        combo->addItem(i18n("Other (set in Advanced > Panel)"), current);
    combo->setCurrentIndex(combo->findData(current));
}

void fillHoldFeedbackCombo(QComboBox *combo)
{
    combo->addItem(i18n("Pie next to the pointer"), QStringLiteral("pie"));
    combo->addItem(i18n("Ring around the pointer"), QStringLiteral("ring"));
    combo->addItem(i18n("Bar below the pointer"), QStringLiteral("bar"));
    combo->addItem(i18n("Off"), QStringLiteral("none"));
    combo->setToolTip(i18n("Shown while the right button is held, until the context menu opens."));
}

// ---- current values

int theme()
{
    return qBound(0, look().readEntry("Theme", 0), 2);
}

bool paneroStyle()
{
    return look().readEntry("Panero Style", true);
}

QString iconSet()
{
    return look().readEntry("Icon Set", QString());
}

QString iconTheme()
{
    return look().readEntry("Icon Theme", QString());
}

QString storedFolderColor()
{
    return look().readEntry("Folder Color", QString());
}

int rowHeight()
{
    const int configured = PaneroTheme::configuredRowHeight();
    return configured > 0 ? configured : int(PaneroTheme::ComfortableRowHeight);
}

int iconSize()
{
    return KConfigGroup(krConfig, DETAILED_VIEW_GROUP).readEntry("IconSize", QString("22")).toInt();
}

bool showHidden()
{
    if (KrActions::actToggleHidden)
        return KrActions::actToggleHidden->isChecked();
    return lookAndFeel().readEntry("Show Hidden", _ShowHidden);
}

bool showDriveButtons()
{
    return look().readEntry("Show Drive Buttons", false);
}

bool showCloud()
{
    return look().readEntry("Show Cloud", PANERO_CLOUD_DEFAULT);
}

bool detailsPreviews()
{
    return KConfigGroup(krConfig, DETAILED_VIEW_GROUP).readEntry("ShowPreviews", false);
}

QString mouseSelection()
{
    return lookAndFeel().readEntry("Mouse Selection", QString::number(_MouseSelection));
}

// ---- writing

void setTheme(int value)
{
    look().writeEntry("Theme", value);
}

void setPaneroStyle(bool on)
{
    look().writeEntry("Panero Style", on);
}

void setIconSet(const QString &set)
{
    look().writeEntry("Icon Set", set);
}

void setIconTheme(const QString &value)
{
    KConfigGroup g = look();
    if (value == g.readEntry("Icon Theme", QString()))
        return;
    if (value.isEmpty())
        g.deleteEntry("Icon Theme");
    else
        g.writeEntry("Icon Theme", value);
    // a new icon theme shows its own folders; a colour can be picked afterwards
    g.writeEntry("Folder Color", QString());
}

void setStoredFolderColor(const QString &value)
{
    look().writeEntry("Folder Color", value);
}

void setShowDriveButtons(bool on)
{
    look().writeEntry("Show Drive Buttons", on);
}

void setShowCloud(bool on)
{
    look().writeEntry("Show Cloud", on);
}

void setMouseSelection(const QString &mode)
{
    lookAndFeel().writeEntry("Mouse Selection", mode);
}

void setRowHeightAndIconSize(int rowHeight, int iconSize)
{
    look().writeEntry("Row Height", rowHeight);
    KConfigGroup(krConfig, DETAILED_VIEW_GROUP).writeEntry("IconSize", QString::number(iconSize));
}

// ---- applying

namespace
{
template<typename F>
void forEachView(F f)
{
    if (!MAIN_VIEW)
        return;
    for (PanelManager *manager : {MAIN_VIEW->leftManager(), MAIN_VIEW->rightManager()}) {
        for (int i = 0; i < manager->tabCount(); ++i) {
            ListPanel *panel = manager->tabAt(i);
            if (panel && panel->view)
                f(panel->view);
        }
    }
}
} // namespace

void applyShowHidden(bool on)
{
    if (KrActions::actToggleHidden) {
        if (KrActions::actToggleHidden->isChecked() != on)
            KrActions::actToggleHidden->trigger();
    } else {
        lookAndFeel().writeEntry("Show Hidden", on);
    }
}

void applyDetailsPreviews(bool on)
{
    KConfigGroup(krConfig, DETAILED_VIEW_GROUP).writeEntry("ShowPreviews", on);
    krConfig->sync();
    forEachView([on](KrView *view) {
        if (view->instance()->name() == QLatin1String(DETAILED_VIEW_GROUP) && view->previewsShown() != on)
            view->showPreviews(on);
    });
}

void applyLive(bool look)
{
    krConfig->sync();
    PaneroTheme::invalidate();
    if (look) // (not for the other switches: a new application style sheet re-polishes every widget)
        PaneroAppearance::instance()->apply();
    KrColorCache::getColorCache().refreshColors();
    const int size = iconSize();
    forEachView([size](KrView *view) {
        if (view->instance()->name() == QLatin1String(DETAILED_VIEW_GROUP))
            view->setFileIconSize(size);
        view->refreshColors();
        if (auto *itemView = qobject_cast<QAbstractItemView *>(view->widget()))
            itemView->doItemsLayout();
    });
    emit PaneroAppearance::instance()->settingsChanged();
}

void recreatePanels()
{
    if (!MAIN_VIEW || !ACTIVE_PANEL)
        return;
    KrSelectionMode::resetSelectionHandler();
    const bool leftActive = ACTIVE_PANEL->gui->isLeft();
    MAIN_VIEW->leftManager()->slotRecreatePanels();
    MAIN_VIEW->rightManager()->slotRecreatePanels();
    (leftActive ? LEFT_PANEL : RIGHT_PANEL)->slotFocusOnMe();
}

QString restartSignature()
{
    return QStringList({QString::number(theme()), paneroStyle() ? QStringLiteral("1") : QStringLiteral("0"), iconSet(), iconTheme(),
                        storedFolderColor()})
        .join(QLatin1Char('|'));
}

bool askRestart(QWidget *parent)
{
    const auto answer = KMessageBox::questionTwoActions(
        parent, i18n("Restart Panero now to apply the new appearance?\nYour folders and tabs are kept."), i18n("Restart Panero"),
        KGuiItem(i18n("Restart"), QStringLiteral("view-refresh")), KGuiItem(i18n("Later"), QStringLiteral("chronometer")));
    if (answer != KMessageBox::PrimaryAction)
        return false;
    Krusader::restart();
    return true;
}

} // namespace PaneroSettings
