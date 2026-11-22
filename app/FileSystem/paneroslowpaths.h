/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROSLOWPATHS_H
#define PANEROSLOWPATHS_H

#include <QString>
#include <QStringList>

#include <functional>

class QObject;

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
/// True for the top folder of one of Panero's own cloud drive mounts: browsing
/// a cloud never goes above it (no "..", Up stops there).
bool isCloudRoot(const QString &path);
/// Lists @p dirs (only names, no file access) in background threads, a few in
/// parallel, so a network file system has them ready. At most @p max folders;
/// a new call replaces folders of an earlier call that are still waiting.
void prefetchListings(const QStringList &dirs, int max = 60);

/// Asks the cloud drive holding @p dir to read the folder again from the
/// provider (in the background); @p done runs in the GUI thread afterwards,
/// unless @p context was deleted. Set by the cloud module (PaneroCloud).
using Revalidator = std::function<bool(const QString &dir, QObject *context, std::function<void()> done)>;
void setRevalidator(const Revalidator &revalidator);
/// False when no cloud drive holds @p dir (@p done is not called then).
bool revalidate(const QString &dir, QObject *context, const std::function<void()> &done);

/// The name shown for a cloud drive mount folder ("OneDrive", or the name the
/// user gave it); empty when unknown. Set by the cloud module.
using NameProvider = std::function<QString(const QString &mountRoot)>;
void setNameProvider(const NameProvider &provider);
QString displayName(const QString &mountRoot);
}

#endif // PANEROSLOWPATHS_H
