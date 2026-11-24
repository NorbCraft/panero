/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROSETTINGS_H
#define PANEROSETTINGS_H

#include <QString>
#include <QVector>

class QComboBox;
class QWidget;

/**
 * Shared pieces of Panero's settings: the Settings window pages
 * (Konfigurator/kgpanero), the Quick Settings popup and the first-start
 * assistant offer the same options with the same config keys, so the choice
 * lists, reading, applying and the "restart needed" logic live here once.
 *
 * Keys: group "Panero Appearance" (PaneroTheme::GROUP) for the look, drives and
 * cloud; "Look&Feel" for hidden files, mouse selection and hold feedback;
 * "KrInterDetailedView" for icon size and previews of the details view.
 */
namespace PaneroSettings
{
extern const char *const DETAILED_VIEW_GROUP;

/// Icon sizes offered for the file list.
const QVector<int> &iconSizes();

// ---- choice lists (the item data is the stored value)
/// "Follow the desktop", "Light", "Dark": the index is the stored "Theme" value.
void fillThemeCombo(QComboBox *combo);
/// The desktop's icon theme plus the themes Panero ships (Linux: Win11).
/// Returns false when there is nothing to choose (Windows, or no theme installed).
bool fillIconThemeCombo(QComboBox *combo);
/// "As in the icon theme", the presets and "Custom..." (asks for a colour).
void fillFolderColorCombo(QComboBox *combo, QWidget *dialogParent);
void setFolderColor(QComboBox *combo, const QString &value);
QString folderColor(const QComboBox *combo);
/// Compact, Comfortable, Spacious, Custom.
void fillDensityCombo(QComboBox *combo);
/// Index of the density preset for a row height / icon size pair (3 = custom).
int densityIndex(int rowHeight, int iconSize);
/// Row height and icon size of a preset (index 0..2); false for "custom".
bool densityValues(int index, int *rowHeight, int *iconSize);
/// Panero / Krusader mouse selection, plus "Other" when another mode is set.
void fillSelectionCombo(QComboBox *combo, const QString &current);
void fillHoldFeedbackCombo(QComboBox *combo);

// ---- current values
int theme();
bool paneroStyle();
QString iconSet();
QString iconTheme();
QString storedFolderColor();
int rowHeight();      ///< configured row height, or the comfortable default
int iconSize();       ///< details view icon size
bool showHidden();
bool showDriveButtons();
bool showCloud();
bool detailsPreviews(); ///< thumbnails instead of icons in the details view
QString mouseSelection();

// ---- writing (each writes its config key; call refresh()/applyLive() afterwards)
void setTheme(int theme);
void setPaneroStyle(bool on);
void setIconSet(const QString &set);
/// Choosing an icon theme resets the folder colour to the theme's own (D-022).
void setIconTheme(const QString &theme);
void setStoredFolderColor(const QString &value);
void setShowDriveButtons(bool on);
void setShowCloud(bool on);
void setMouseSelection(const QString &mode);
/// Writes the row height and icon size (density).
void setRowHeightAndIconSize(int rowHeight, int iconSize);

// ---- applying to the running application
/// Show hidden files: toggles the View action (writes "Show Hidden").
void applyShowHidden(bool on);
/// Previews in the details view: default for new tabs + all open details views.
void applyDetailsPreviews(bool on);
/// Palette, colours, icon size and row height of all panels, sidebar sections,
/// drive buttons (emits PaneroAppearance::settingsChanged).
void applyLive();
/// Views pick the mouse selection mode up when they are created.
void recreatePanels();

/// Snapshot of the settings that need a restart (theme, style, icons, folder colour).
QString restartSignature();
/// Asks "Restart Panero now?" and restarts on yes. Returns true if restarting.
bool askRestart(QWidget *parent);
}

#endif // PANEROSETTINGS_H
