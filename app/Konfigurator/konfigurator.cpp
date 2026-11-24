/*
    SPDX-FileCopyrightText: 2000 Shie Erlich <krusader@users.sourceforge.net>
    SPDX-FileCopyrightText: 2000 Rafi Yanai <krusader@users.sourceforge.net>
    SPDX-FileCopyrightText: 2004-2022 Krusader Krew <https://krusader.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/



#include "konfigurator.h"
#include "../panerowording.h"
#include "../krglobal.h"
#include "../icon.h"
#include "../Dialogs/krdialogs.h"

// QtGui
#include <QPixmap>
#include <QResizeEvent>

#include <KConfigCore/KSharedConfig>
#include <KConfigWidgets/KHelpClient>
#include <KI18n/KLocalizedString>
#include <KWidgetsAddons/KMessageBox>

#include "../defaults.h"
#include "../krusaderview.h"
#include "../GUI/kfnkeys.h"

// the frames
#include "kgstartup.h"
#include "kgpanel.h"
#include "kggeneral.h"
#include "kgadvanced.h"
#include "kgarchives.h"
#include "kgdependencies.h"
#include "kgcolors.h"
#include "kguseractions.h"
#include "kgprotocols.h"
#include "kgpanero.h"

Konfigurator::Konfigurator(bool f, const QString &startPage) : KPageDialog((QWidget *)nullptr),
        firstTime(f), internalCall(false), sizeX(-1), sizeY(-1)
{
    // Panero: one Settings window - Panero's pages first, Krusader's under "Advanced" (D-022)
    setWindowTitle(i18n("Settings"));
    setWindowModality(Qt::ApplicationModal);
    setFaceType(KPageDialog::Tree);

    setStandardButtons(QDialogButtonBox::Help|QDialogButtonBox::RestoreDefaults|QDialogButtonBox::Close|
                       QDialogButtonBox::Apply|QDialogButtonBox::Reset);
    button(QDialogButtonBox::Apply)->setDefault(true);

    connect(button(QDialogButtonBox::Close), &QPushButton::clicked, this, &Konfigurator::slotClose);
    connect(button(QDialogButtonBox::Help), &QPushButton::clicked, this, &Konfigurator::slotShowHelp);
    connect(button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, &Konfigurator::slotRestoreDefaults);
    connect(button(QDialogButtonBox::Reset), &QPushButton::clicked, this, &Konfigurator::slotReset);
    connect(button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &Konfigurator::slotApply);
    connect(this, &Konfigurator::currentPageChanged, this, &Konfigurator::slotPageSwitch);
    connect(&restoreTimer, &QTimer::timeout, this, &Konfigurator::slotRestorePage);

    createLayout(startPage);

    KConfigGroup group(krConfig, "Konfigurator");
    int sx = group.readEntry("Window Width", -1);
    int sy = group.readEntry("Window Height", -1);

    if (sx != -1 && sy != -1)
        resize(sx, sy);
    else
        resize(900, 680);

    if (group.readEntry("Window Maximized",  false))
        showMaximized();
    else
        show();
}

void Konfigurator::resizeEvent(QResizeEvent *e)
{
    if (!isMaximized()) {
        sizeX = e->size().width();
        sizeY = e->size().height();
    }
    QDialog::resizeEvent(e);
}

void Konfigurator::closeDialog()
{
    KConfigGroup group(krConfig, "Konfigurator");

    group.writeEntry("Window Width", sizeX);
    group.writeEntry("Window Height", sizeY);
    group.writeEntry("Window Maximized", isMaximized());
}

void Konfigurator::reject()
{
    closeDialog();
    QDialog::reject();
}

KPageWidgetItem *Konfigurator::newPage(KonfiguratorPage *page, const QString &name, const QString &desc, const QIcon &icon,
                                       const QString &id, KPageWidgetItem *parent)
{
    auto *item = new KPageWidgetItem(page, name);
    item->setIcon(icon);
    item->setHeader(desc);
    if (parent)
        addSubPage(parent, item);
    else
        addPage(item);

    kgPages.append(item);
    pageIds.insert(id, item);
    connect(page, &KonfiguratorPage::sigChanged, this, &Konfigurator::slotApplyEnable);
    return item;
}

void Konfigurator::createLayout(const QString &startPage)
{
    // Panero's pages (the everyday settings, D-022)
    newPage(new KgPaneroAppearance(firstTime, this), i18n("Appearance"), i18n("Light or dark, style, icons and colors"),
            Icon("preferences-desktop-theme"), QStringLiteral("appearance"));
    newPage(new KgPaneroPanels(firstTime, this), i18n("Panels"), i18n("File list density, hidden files and thumbnails"),
            Icon("view-choose"), QStringLiteral("panels"));
    newPage(new KgPaneroMouse(firstTime, this), i18n("Mouse & Keys"), i18n("How the mouse selects files, keyboard shortcuts"),
            Icon("input-mouse"), QStringLiteral("mouse"));
    newPage(new KgPaneroDrives(firstTime, this), i18n("Drives & Cloud"), i18n("Drive buttons and the Cloud section"),
            Icon("drive-harddisk"), QStringLiteral("drives"));
#ifdef Q_OS_WIN
    newPage(new KgPaneroWindows(firstTime, this), i18n("Windows Integration"), i18n("Right-click menu and Windows programs"),
            Icon("preferences-system-windows"), QStringLiteral("windows"));
#endif

    // Krusader's original pages
    KPageWidgetItem *advanced = newPage(new KgPaneroAdvancedIntro(firstTime, this), i18n("Advanced"),
                                        i18n("Krusader's original settings"), Icon("configure"), QStringLiteral("advanced"));
    // startup
    newPage(new KgStartup(firstTime, this), i18n("Startup"), i18n("Panero's settings upon startup"), Icon("go-home"),
            QStringLiteral("startup"), advanced);
    // panel
    newPage(new KgPanel(firstTime, this), i18n("Panel"), i18n("Panel"), Icon("view-choose"), QStringLiteral("panel"), advanced);
    // colors
    newPage(new KgColors(firstTime, this), i18n("Colors"), i18n("Colors"), Icon("color-picker"), QStringLiteral("colors"), advanced);
    // general
    newPage(new KgGeneral(firstTime, this), i18n("General"), i18n("Basic Operations"), Icon("system-run"), QStringLiteral("general"),
            advanced);
    // Krusader's "Advanced" page (the node above is called Advanced now)
    newPage(new KgAdvanced(firstTime, this), i18n("Fine-Tuning"), i18n("Be sure you know what you are doing."),
            Icon("dialog-messages"), QStringLiteral("fine-tuning"), advanced);
    // archives
    newPage(new KgArchives(firstTime, this), i18n("Archives"), i18n("Customize the way Panero deals with archives"),
            Icon("archive-extract"), QStringLiteral("archives"), advanced);
    // dependencies
    newPage(new KgDependencies(firstTime, this), i18n("Dependencies"), i18n("Set the full path of the external applications"),
            Icon("debug-run"), QStringLiteral("dependencies"), advanced);
    // useractions
    newPage(new KgUserActions(firstTime, this), i18n("User Actions"), i18n("Configure your personal actions"),
            Icon("user-properties"), QStringLiteral("useractions"), advanced);
    // protocols
    newPage(new KgProtocols(firstTime, this), i18n("Protocols"), i18n("Link MIMEs to protocols"),
            Icon("document-preview"), QStringLiteral("protocols"), advanced);

    setCurrentPage(kgPages.at(0));
    if (!startPage.isEmpty())
        showPage(startPage);
    slotApplyEnable();
}

void Konfigurator::showPage(const QString &id)
{
    const int dash = id.indexOf(QLatin1Char('-'), id.startsWith(QLatin1String("fine-")) ? 5 : 0);
    const QString pageId = dash > 0 ? id.left(dash) : id;
    KPageWidgetItem *item = pageIds.value(pageId);
    if (!item)
        return;
    setCurrentPage(item);
    if (currentPage() == item && dash > 0) // (the switch can be refused for unsaved changes)
        qobject_cast<KonfiguratorPage *>(item->widget())->showSubPage(id.mid(dash + 1));
}

void Konfigurator::reloadOtherPages(KonfiguratorPage *applied)
{
    for (KPageWidgetItem *item : qAsConst(kgPages)) {
        auto *page = qobject_cast<KonfiguratorPage *>(item->widget());
        if (page && page != applied && !page->isChanged())
            page->loadInitialValues();
    }
}

void Konfigurator::closeEvent(QCloseEvent *event)
{
    lastPage = currentPage();
    if(slotPageSwitch(lastPage, lastPage)) {
        hide();
        KPageDialog::closeEvent(event);
    } else
        event->ignore();
}

void Konfigurator::slotApplyEnable()
{
    lastPage = currentPage();
    bool isChanged = (qobject_cast<KonfiguratorPage *>(lastPage->widget()))->isChanged();
    button(QDialogButtonBox::Apply)->setEnabled(isChanged);
    button(QDialogButtonBox::Reset)->setEnabled(isChanged);
}

bool Konfigurator::slotPageSwitch(KPageWidgetItem *current, KPageWidgetItem *before)
{
    if (before == nullptr)
        return true;

    auto *currentPg = qobject_cast<KonfiguratorPage *>(before->widget());

    if (internalCall) {
        internalCall = false;
        return true;
    }

    if (currentPg->isChanged()) {
        int result = KMessageBox::questionYesNoCancel(nullptr, i18n("The current page has been changed. Do you want to apply changes?"), {}, KStandardGuiItem::apply(), KStandardGuiItem::discard());

        switch (result) {
        case KMessageBox::No:
            currentPg->loadInitialValues();
            currentPg->apply();
            break;
        case KMessageBox::Yes:
            emit configChanged(currentPg->apply());
            reloadOtherPages(currentPg);
            break;
        default:
            restoreTimer.setSingleShot(true);
            restoreTimer.start(0);
            return false;
        }
    }

    button(QDialogButtonBox::Apply)->setEnabled(currentPg->isChanged());
    lastPage = current;
    return true;
}

void Konfigurator::slotRestorePage()
{
    if (lastPage != currentPage()) {
        internalCall = true;
        setCurrentPage(lastPage);
    }
}

void Konfigurator::slotClose()
{
    lastPage = currentPage();
    if (slotPageSwitch(lastPage, lastPage)) {
        reject();
    }
}

void Konfigurator::slotApply()
{
    auto *page = qobject_cast<KonfiguratorPage*>(currentPage()->widget());
    emit configChanged(page->apply());
    reloadOtherPages(page);
}

void Konfigurator::slotReset()
{
    (qobject_cast<KonfiguratorPage *>(currentPage()->widget()))->loadInitialValues();
}

void Konfigurator::slotRestoreDefaults()
{
    (qobject_cast<KonfiguratorPage *>(currentPage()->widget()))->setDefaults();
}

void Konfigurator::slotShowHelp()
{
    KHelpClient::invokeHelp(QStringLiteral("konfigurator"));
}

