/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROPATHS_H
#define PANEROPATHS_H

#include <QUrl>

namespace PaneroPaths
{
/**
 * Root of the folder tree in "Start at Home" mode: Home while the folder is inside
 * Home, otherwise the mount point of the drive it is on (or "/"); for remote
 * locations the root of that location (scheme://host/).
 */
QUrl homeOrDriveRoot(const QUrl &url);
}

#endif // PANEROPATHS_H
