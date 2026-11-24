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
constexpr int LayoutVersion = 33;

/**
 * KXMLGUI keeps the toolbars of a user's local copy of paneroui.rc (written as soon
 * as any toolbar setting is changed) even when a newer Panero ships a different
 * default layout. When the layout version changed, back the local copy up (.bak),
 * rebuild it from the installed file with the user's shortcuts (ActionProperties)
 * carried over, and forget the saved toolbar style and window state, so the new
 * default layout is used. Must run before the main window is created.
 */
void migrateLayout();

/// One-time adjustments of saved settings when Panero's defaults change
/// (Panero / Settings Version). Must run before the main window is created.
void migrateSettings();
}

#endif // PANEROTOOLBAR_H
