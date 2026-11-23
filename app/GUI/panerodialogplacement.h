/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANERODIALOGPLACEMENT_H
#define PANERODIALOGPLACEMENT_H

#include <QObject>

/**
 * Every dialog opens centred on the Panero window (D-022 follow-up).
 *
 * Many dialogs inherited from Krusader are created without a parent (New
 * Folder, error messages from KIO, ...). The window system then does not know
 * they belong to Panero: on Wayland the compositor places them like separate
 * applications, on X11 and Windows they appear at the screen's corner or
 * centre. This application-wide filter makes the main window the parent of
 * such dialogs before they are first shown: Qt then centres them on it, and
 * the window system knows they belong to it (Wayland compositors centre them
 * themselves). Dialogs with their own parent are left alone.
 */
class PaneroDialogPlacement : public QObject
{
    Q_OBJECT
public:
    explicit PaneroDialogPlacement(QObject *parent = nullptr);
    /// install the filter on the application
    static void install();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif // PANERODIALOGPLACEMENT_H
