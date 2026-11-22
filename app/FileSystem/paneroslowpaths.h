/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROSLOWPATHS_H
#define PANEROSLOWPATHS_H

#include <QString>
#include <QStringList>

/**
 * Local paths that are really network storage: Panero's cloud drive mounts
 * (rclone, below $XDG_RUNTIME_DIR/panero-cloud or ~/.cache/panero/mounts) and
 * GNOME's gvfs network mounts. On such paths every file open or statfs is a
 * network request, so Panero avoids them while listing: file types come from
 * the extension only, free space is cached, and the next folder level is
 * listed ahead in the background.
 */
namespace PaneroSlowPaths
{
/// True for paths inside a cloud drive mount or a gvfs network mount.
bool isSlowPath(const QString &path);
/// The mount folder of a cloud drive (or gvfs mount) containing @p path, else empty.
QString mountRoot(const QString &path);
/// Lists the subfolders of @p dirs (only names, no file access) in a background
/// thread, so a network file system has them ready. At most @p max folders,
/// one batch at a time; does nothing while a batch is running.
void prefetchListings(const QStringList &dirs, int max = 40);
}

#endif // PANEROSLOWPATHS_H
