/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "commandergesture.h"

void CommanderGesture::press(const QPoint &pos, const QString &pressedItem, bool pressedItemMarked)
{
    m_pending = true;
    m_menuShown = false;
    m_sweeping = false;
    m_pressPos = pos;
    m_item = pressedItem;
    m_itemMarked = pressedItemMarked;
    m_target.clear();
}

CommanderGesture::Action CommanderGesture::move(const QPoint &pos, int dragDistance, const QString &itemUnderPointer)
{
    if (!m_pending || m_menuShown)
        return Action::None;
    if (!m_sweeping) {
        if ((pos - m_pressPos).manhattanLength() < dragDistance)
            return Action::None; // still a click / hold
        m_sweeping = true;
        if (m_item.isEmpty())
            return Action::None; // started on empty space: nothing to sweep
        m_sweepMarks = !m_itemMarked;
        return Action::SweepStart;
    }
    if (m_item.isEmpty() || itemUnderPointer.isEmpty() || itemUnderPointer == m_target)
        return Action::None;
    m_target = itemUnderPointer;
    return Action::SweepTo;
}

CommanderGesture::Action CommanderGesture::release()
{
    if (!m_pending)
        return Action::None;
    const bool click = !m_menuShown && !m_sweeping;
    m_pending = false;
    m_sweeping = false;
    m_menuShown = false;
    if (click && !m_item.isEmpty())
        return Action::Toggle;
    return Action::None;
}

CommanderGesture::Action CommanderGesture::holdTimeout()
{
    if (!m_pending || m_sweeping || m_menuShown)
        return Action::None;
    m_menuShown = true;
    return m_item.isEmpty() ? Action::BackgroundMenu : Action::ItemMenu;
}

void CommanderGesture::cancel()
{
    m_pending = false;
    m_menuShown = false;
    m_sweeping = false;
    m_item.clear();
    m_target.clear();
}

namespace CommanderTargets
{
QStringList operationTargets(const QStringList &marked, const QString &current)
{
    QStringList result;
    for (const QString &name : marked) {
        if (name != QLatin1String(".."))
            result << name;
    }
    if (result.isEmpty() && !current.isEmpty() && current != QLatin1String(".."))
        result << current;
    return result;
}

QStringList contextMenuTargets(bool background, const QStringList &marked, const QString &current)
{
    if (background)
        return QStringList();
    return operationTargets(marked, current);
}
}
