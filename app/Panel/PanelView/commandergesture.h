/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef COMMANDERGESTURE_H
#define COMMANDERGESTURE_H

#include <QPoint>
#include <QString>
#include <QStringList>

/**
 * Panero's "Commander" input model, kept free of widgets so it can be unit tested.
 *
 * Right mouse button gesture:
 *  - press:            the cursor moves to the pressed item; nothing is toggled yet and
 *                      the hold timer starts (wantsTimer() == true)
 *  - release (short):  the pressed item's mark is toggled (Toggle)
 *  - hold (timeout):   the native context menu opens (ItemMenu, or BackgroundMenu when
 *                      the press was on empty space / ".."); the following release is ignored
 *  - move beyond the drag distance: becomes a sweep that marks/unmarks the range
 *                      (SweepStart, SweepTo); no menu, no toggle on release
 *  - cancel (focus lost, window switch, drag): nothing happens
 */
class CommanderGesture
{
public:
    enum class Action {
        None,
        Toggle,          ///< toggle the mark of item()
        ItemMenu,        ///< open the context menu for the item / marked set
        BackgroundMenu,  ///< open the folder (background) context menu
        SweepStart,      ///< begin sweep: set mark of item() to sweepMarks()
        SweepTo,         ///< extend sweep from item() to target() with sweepMarks()
    };

    /// @param pressedItem name of the item under the pointer, empty for background or ".."
    /// @param pressedItemMarked current mark state of that item
    void press(const QPoint &pos, const QString &pressedItem, bool pressedItemMarked);
    /// @param dragDistance QApplication::startDragDistance()
    /// @param itemUnderPointer item under the pointer now (empty if none)
    Action move(const QPoint &pos, int dragDistance, const QString &itemUnderPointer);
    Action release();
    Action holdTimeout();
    void cancel();

    bool isPending() const { return m_pending; }
    bool wantsTimer() const { return m_pending && !m_sweeping && !m_menuShown; }
    QString item() const { return m_item; }
    QString target() const { return m_target; }
    bool sweepMarks() const { return m_sweepMarks; }

private:
    bool m_pending = false;
    bool m_menuShown = false;
    bool m_sweeping = false;
    bool m_sweepMarks = true;
    bool m_itemMarked = false;
    QPoint m_pressPos;
    QString m_item;
    QString m_target;
};

namespace CommanderTargets
{
/**
 * Files an operation (copy, delete, ...) or a context menu acts on:
 * the marked files if any, otherwise the current file (never "..").
 */
QStringList operationTargets(const QStringList &marked, const QString &current);

/**
 * Files the context menu targets. A background menu never targets files,
 * even when files are marked.
 */
QStringList contextMenuTargets(bool background, const QStringList &marked, const QString &current);
}

#endif // COMMANDERGESTURE_H
