/*
    SPDX-FileCopyrightText: 2009 Csaba Karai <cskarai@freemail.hu>
    SPDX-FileCopyrightText: 2009-2022 Krusader Krew <https://krusader.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KRMOUSEHANDLER_H
#define KRMOUSEHANDLER_H

// QtCore
#include <QPoint>
#include <QTimer>
#include <QTime>
#include <QStringList>

#include "commandergesture.h"

class QMouseEvent;
class QWheelEvent;
class KrView;
class KrViewItem;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;

/**
 * @brief Mouse handler for view
 */
class KrMouseHandler : public QObject
{
    Q_OBJECT

public:
    KrMouseHandler(KrView * view, int contextMenuShift);

    bool mousePressEvent(QMouseEvent *e);
    bool mouseReleaseEvent(QMouseEvent *e);
    bool mouseDoubleClickEvent(QMouseEvent *e);
    bool mouseMoveEvent(QMouseEvent *e);
    bool wheelEvent(QWheelEvent *);
    bool dragEnterEvent(QDragEnterEvent *e);
    bool dragMoveEvent(QDragMoveEvent *e);
    bool dragLeaveEvent(QDragLeaveEvent *e);
    bool dropEvent(QDropEvent *e);
    void handleContextMenu(KrViewItem * it, const QPoint & pos);
    void otherEvent(QEvent * e);
    void cancelTwoClickRename();
    /// Abort a pending (deferred) right-button press without toggling any mark.
    void cancelRightPress();
    /// True while a deferred right press is waiting for release or the hold timeout.
    bool isRightPressPending() const { return _gesture.isPending(); }

public slots:
    void showContextMenu();

signals:
    void renameCurrentItem();

protected:
    KrView     * _view;
    KrViewItem * _rightClickedItem;
    KrViewItem * _clickedItem;
    bool         _rightClickSelects;
    bool         _singleClick;
    QPoint       _contextMenuPoint;
    QTimer       _contextMenuTimer;
    int          _contextMenuShift;
    bool         _singleClicked;
    KrViewItem * _singleClickedItem;
    QTime        _singleClickTime;
    QTimer       _renameTimer;
    QPoint       _dragStartPos;
    bool         _emptyContextMenu;
    QStringList  _selectedItemNames;

    // Panero Commander right-button gesture (see KrSelectionMode::deferRightButtonToggle)
    CommanderGesture _gesture;
    void applyGestureAction(CommanderGesture::Action action);

    bool deferredRightPress(QMouseEvent *e, KrViewItem *item);
    bool deferredRightMove(QMouseEvent *e, KrViewItem *item);
    bool deferredRightRelease(QMouseEvent *e);
};

#endif
