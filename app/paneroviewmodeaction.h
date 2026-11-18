/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROVIEWMODEACTION_H
#define PANEROVIEWMODEACTION_H

#include <QPointer>
#include <QWidgetAction>

class QAction;

/**
 * Toolbar control "Details | Thumbnails": one segmented button showing both view
 * modes, the active one highlighted. A click anywhere switches the active panel to
 * the other mode. It drives the existing view-type actions ("view0" detailed,
 * "view2" thumbnails), so shortcuts and the View menu stay in sync.
 */
class PaneroViewModeAction : public QWidgetAction
{
    Q_OBJECT
public:
    PaneroViewModeAction(QAction *details, QAction *thumbnails, QObject *parent);

protected:
    QWidget *createWidget(QWidget *parent) override;

private:
    void toggle();
    QPointer<QAction> m_details;
    QPointer<QAction> m_thumbnails;
};

#endif // PANEROVIEWMODEACTION_H
