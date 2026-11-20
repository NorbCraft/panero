/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROWINSHELL_H
#define PANEROWINSHELL_H

#include <QIcon>
#include <QList>
#include <QString>

/**
 * Windows build only: drive names and the icons Explorer shows for drives,
 * folders and files (taken from the Windows shell), so Panero looks at home on
 * Windows while keeping its own layout and toolbar.
 */
namespace PaneroWinShell
{
struct Drive {
    QString rootPath; ///< "C:/"
    QString name;     ///< "Local Disk (C:)", as in Explorer
    QIcon icon;
};

/// All drive letters that exist, in letter order.
QList<Drive> drives();
/// Explorer's display name of the drive that holds @p rootPath.
QString driveName(const QString &rootPath);
/// Explorer's icon of the drive that holds @p rootPath.
QIcon driveIcon(const QString &rootPath);

/// True when the file list should use the Windows shell icons: Quick Settings >
/// Icons is "System" and, for folders, no Panero folder colour is chosen.
bool useShellIcons(bool isDir);
/// Cache key: one entry per extension, per file for files that carry their own
/// icon (.exe, .lnk, .ico, ...) and per folder for special folders and drives.
QString iconKey(const QString &localPath, bool isDir);
/// The shell icon for a local file or folder (cached by iconKey()).
QIcon fileIcon(const QString &localPath, bool isDir);
/// Forget cached settings and icons (after Quick Settings changes).
void reset();

/// Create a symbolic link (needs Developer Mode or administrator rights).
bool createSymlink(const QString &target, const QString &linkPath);
/// Create a hard link (files on the same NTFS volume).
bool createHardLink(const QString &target, const QString &linkPath);
}

#endif
