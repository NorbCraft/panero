/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerodialogplacement.h"

#include <QApplication>
#include <QEvent>
#include <QMainWindow>
#include <QWidget>

namespace
{
QWidget *mainWindow()
{
    QWidget *fallback = nullptr;
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (!w->isVisible() || !qobject_cast<QMainWindow *>(w))
            continue;
        if (w->isActiveWindow())
            return w;
        if (!fallback)
            fallback = w;
    }
    return fallback;
}
} // namespace

PaneroDialogPlacement::PaneroDialogPlacement(QObject *parent)
    : QObject(parent)
{
}

void PaneroDialogPlacement::install()
{
    qApp->installEventFilter(new PaneroDialogPlacement(qApp));
}

bool PaneroDialogPlacement::eventFilter(QObject *watched, QEvent *event)
{
    // Polish comes once, before the dialog is first shown or sized. Giving it
    // the main window as parent there makes Qt centre it on that window
    // (QDialog::adjustPosition) and tell the window system it belongs to it
    // (transient parent: Wayland compositors centre it themselves).
    if (event->type() != QEvent::Polish || !watched->isWidgetType())
        return false;
    auto *dialog = static_cast<QWidget *>(watched);
    if (!dialog->isWindow() || dialog->windowType() != Qt::Dialog || dialog->parentWidget() || dialog->isVisible())
        return false;
    QWidget *main = mainWindow();
    if (!main || main == dialog)
        return false;
    dialog->setParent(main, dialog->windowFlags());
    return false;
}
