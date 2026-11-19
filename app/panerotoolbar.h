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

class QDockWidget;
class QToolButton;

/**
 * The sidebar toggle at the far left of the toolbar. Style "icon" is a plain toolbar
 * toggle; style "header" is as wide as the sidebar and reads as its title
 * ("Places «"), shrinking to "»" while the sidebar is hidden.
 * Panero Appearance / Sidebar Button = icon | header.
 */
class PaneroSidebarButtonAction : public QWidgetAction
{
    Q_OBJECT
public:
    PaneroSidebarButtonAction(QAction *toggle, QDockWidget *dock, QObject *parent);

protected:
    QWidget *createWidget(QWidget *parent) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void sync(QToolButton *button) const;
    QAction *m_toggle;
    QDockWidget *m_dock;
    QList<QToolButton *> m_buttons;
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
