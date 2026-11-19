/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROTOOLBAR_H
#define PANEROTOOLBAR_H

#include <QWidgetAction>

/**
 * Expanding gap for the main toolbar: everything after it (the appearance button)
 * is pushed to the right edge.
 */
class PaneroToolBarSpacer : public QWidgetAction
{
    Q_OBJECT
public:
    explicit PaneroToolBarSpacer(QObject *parent);

protected:
    QWidget *createWidget(QWidget *parent) override;
};

/**
 * The sidebar toggle at the far left of the toolbar: an icon-only toggle twice as
 * wide as the other buttons, pressed while the sidebar is shown.
 */
class PaneroSidebarButtonAction : public QWidgetAction
{
    Q_OBJECT
public:
    PaneroSidebarButtonAction(QAction *toggle, QObject *parent);

protected:
    QWidget *createWidget(QWidget *parent) override;

private:
    QAction *m_toggle;
};

namespace PaneroToolBar
{
/// Version of the default toolbar layout shipped in paneroui.rc (the <gui version>).
constexpr int LayoutVersion = 32;

/**
 * KXMLGUI keeps the toolbars of a user's local copy of paneroui.rc (written as soon
 * as any toolbar setting is changed) even when a newer Panero ships a different
 * default layout. When the layout version changed, back the local copy up, drop its
 * <ToolBar> elements (custom shortcuts stay) and forget the saved toolbar style, so
 * the new default layout is used. Must run before the main window is created.
 */
void migrateLayout();
}

#endif // PANEROTOOLBAR_H
