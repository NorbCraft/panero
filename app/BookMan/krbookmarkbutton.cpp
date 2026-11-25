/*
    SPDX-FileCopyrightText: 2002 Shie Erlich <erlich@users.sourceforge.net>
    SPDX-FileCopyrightText: 2002 Rafi Yanai <yanai@users.sourceforge.net>
    SPDX-FileCopyrightText: 2004-2022 Krusader Krew <https://krusader.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "krbookmarkbutton.h"
#include "../paneronavigation.h"
#include "krbookmarkhandler.h"
#include "../krglobal.h"
#include "../icon.h"
#include "../compat.h"

// QtGui
#include <QPixmap>
// QtWidgets
#include <QAction>
#include <QMenu>

#include <KI18n/KLocalizedString>


KrBookmarkButton::KrBookmarkButton(QWidget *parent): QToolButton(parent)
{
    setAutoRaise(true);
    setIcon(Icon("bookmarks"));
    setText(i18n("Pinned Folders"));
    setToolTip(i18n("Pinned folders (Ctrl+D); Ctrl+B pins the current folder"));
    setPopupMode(QToolButton::InstantPopup);
    setAcceptDrops(false);

    acmBookmarks = new KActionMenu(Icon("bookmarks"), i18n("Bookmarks"), this);
    acmBookmarks->KACTIONMENU_SETDELAYED;

    setMenu(acmBookmarks->menu());
    connect(acmBookmarks->menu(), &QMenu::aboutToShow, this, &KrBookmarkButton::populate);
    connect(acmBookmarks->menu(), &QMenu::aboutToShow, this, &KrBookmarkButton::aboutToShow);
}

void KrBookmarkButton::populate()
{
    // Panero: Pinned is the single list of favourite places (D-022); Krusader's
    // bookmarks were imported into it once
    if (PaneroNavigationDock *dock = PaneroNavigationDock::instance()) {
        QMenu *m = menu();
        m->clear();
        dock->fillPinMenu(m, [this](const QUrl &url) { emit openUrl(url); });
        return;
    }
    krBookMan->populate(static_cast<QMenu*>(menu()));
}

void KrBookmarkButton::showMenu()
{
    populate();
    menu()->exec(mapToGlobal(QPoint(0, height())));
}

