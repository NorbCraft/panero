/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROICONTHEMES_H
#define PANEROICONTHEMES_H

#include <QColor>
#include <QString>

/**
 * Builds the icon theme Panero uses from layers (D-014):
 *   [recoloured folders] -> [Panero Line icons] -> desktop icon theme
 * Each layer is a small generated theme in ~/.cache/panero/icon-themes that
 * inherits the next one, so everything not overridden comes from the desktop.
 * Only Panero sees these themes (private search path).
 */
namespace PaneroIconThemes
{
/// @param base     the desktop (or chosen) icon theme, already light/dark matched
/// @param line     use the bundled "Panero Line" icons for toolbar/sidebar actions
/// @param dark     dark look (light icons)
/// @param folders  folder colour, invalid = keep the theme's folders
/// @return the theme name to set with QIcon::setThemeName()
QString compose(const QString &base, bool line, bool dark, const QColor &folders);

/// Folder colour presets (name, colour) for the settings dialog.
QList<QPair<QString, QColor>> folderColorPresets();

/// Recolours one colour of a folder icon to @p target's hue (keeps lightness and
/// alpha; greys stay grey). Exposed for tests.
QColor recolor(const QColor &pixel, const QColor &target);
}

#endif // PANEROICONTHEMES_H
