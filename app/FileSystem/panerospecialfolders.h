/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROSPECIALFOLDERS_H
#define PANEROSPECIALFOLDERS_H

#include <QString>

/**
 * The user's special folders (Home, Desktop, Documents, Downloads, Music,
 * Pictures, Videos, Templates, Public) and the theme icon names KDE and GTK
 * themes use for them (D-022). Matched by path only - no file access.
 */
namespace PaneroSpecialFolders
{
/// "folder-download" for ~/Downloads, ...; empty for any other path
QString iconName(const QString &localPath);
}

#endif // PANEROSPECIALFOLDERS_H
