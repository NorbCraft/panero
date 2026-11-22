/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANERODRIVEFILTER_H
#define PANERODRIVEFILTER_H

#include <QString>

/**
 * Which mounts get a drive button (D-019). Pure functions on the mount table
 * entries (no file access), so they are cheap and testable.
 *
 * Linux mounts many system internals: snap images (squashfs on loop
 * devices), /boot, memory file systems, container layers, AppImage and
 * portal FUSE mounts. Only places people keep files on get a button.
 */
namespace PaneroDriveFilter
{
enum class Kind {
    Hidden,  ///< system internal, never a button
    Root,    ///< "/" (the System button)
    Local,   ///< a disk, partition or removable medium
    Network, ///< SMB, NFS, SSH, WebDAV ...
};

/// @p home: the user's home folder; @p paneroMount: one of Panero's own cloud
/// mounts (those get their cloud button instead).
Kind classify(const QString &mountPoint, const QString &fsType, const QString &device, const QString &home, bool paneroMount);

/// File system types that never hold user files (memory, kernel, images).
bool isSystemFsType(const QString &fsType);

/// Short label for a network mount: "share on server" when it can be told,
/// else the mount folder's name.
QString networkLabel(const QString &mountPoint, const QString &fsType, const QString &device);
}

#endif // PANERODRIVEFILTER_H
