/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANERODRIVEACTIONS_H
#define PANERODRIVEACTIONS_H

#include <QString>

class QMenu;
class QWidget;

/**
 * The system part of a drive's right-click menu on Linux (D-022): Properties
 * (with the usage), Mount / Unmount, Safely Remove, Eject, and "Format or Edit
 * in Disks..." (GNOME Disks, or KDE Partition Manager when that is the one
 * installed). Used by the drive buttons, the drive list (Alt+F1/F2, the path
 * bar's drive button) and drives in Pinned. Windows shows Explorer's own drive
 * menu instead. Nothing here touches the drive until an action is chosen.
 */
namespace PaneroDriveActions
{
/// @p udi: the Solid volume, or empty; @p mountPoint: where it is mounted
/// (System "/", Home, a network share), or empty. Returns the number of
/// actions added.
int addTo(QMenu *menu, const QString &udi, const QString &mountPoint, QWidget *dialogParent);
}

#endif // PANERODRIVEACTIONS_H
