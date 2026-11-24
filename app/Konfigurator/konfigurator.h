/*
    SPDX-FileCopyrightText: 2000 Shie Erlich <krusader@users.sourceforge.net>
    SPDX-FileCopyrightText: 2000 Rafi Yanai <krusader@users.sourceforge.net>
    SPDX-FileCopyrightText: 2004-2022 Krusader Krew <https://krusader.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/


#ifndef KONFIGURATOR_H
#define KONFIGURATOR_H

#include "konfiguratorpage.h"

// QtCore
#include <QHash>
#include <QTimer>

#include <KWidgetsAddons/KPageDialog>

class QString;
class QResizeEvent;
class QCloseEvent;

class Konfigurator : public KPageDialog
{
    Q_OBJECT

signals:
    void configChanged(bool isGUIRestartNeeded);

public:
    explicit Konfigurator(bool f = false, const QString &startPage = QString()); // true if Konfigurator is run for the first time

    void reject() override;

    /// Panero: shows a page by id ("appearance", "panels", "mouse", "drives", "windows",
    /// "advanced", "startup", "panel", "colors", "general", "fine-tuning", "archives",
    /// "dependencies", "useractions", "protocols"), optionally "<id>-<sub page>".
    void showPage(const QString &id);

protected:
    KPageWidgetItem *newPage(KonfiguratorPage *, const QString &, const QString &, const QIcon &,
                             const QString &id, KPageWidgetItem *parent = nullptr); // adds widget and connects to slot
    void createLayout(const QString &startPage);
    /// after an Apply: unchanged pages show the new values (several pages share keys)
    void reloadOtherPages(KonfiguratorPage *applied);
    void closeDialog();

    void resizeEvent(QResizeEvent *e) override;
    void closeEvent(QCloseEvent *event) override;

protected slots:
    void slotApplyEnable();
    bool slotPageSwitch(KPageWidgetItem *, KPageWidgetItem *);
    void slotRestorePage();
    void slotClose();
    void slotApply();
    void slotReset();
    void slotRestoreDefaults();
    void slotShowHelp();

private:
    QList<KPageWidgetItem*>     kgPages;
    QHash<QString, KPageWidgetItem*> pageIds;
    bool                        firstTime;
    KPageWidgetItem            *lastPage;
    bool                        internalCall;
    QTimer                      restoreTimer;
    int                         sizeX;
    int                         sizeY;
};

#endif

