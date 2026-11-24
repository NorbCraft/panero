/*
    SPDX-FileCopyrightText: 2000 Shie Erlich <krusader@users.sourceforge.net>
    SPDX-FileCopyrightText: 2000 Rafi Yanai <krusader@users.sourceforge.net>
    SPDX-FileCopyrightText: 2004-2022 Krusader Krew <https://krusader.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kractions.h"
#include "panerowording.h"

// QtWidgets
#include <QMenu>
#include <QAction>

#include <KI18n/KLocalizedString>
#include <KXmlGui/KActionCollection>
#include <KWidgetsAddons/KToggleAction>
#include <KWidgetsAddons/KToolBarPopupAction>
#include <KXmlGui/KToggleToolBarAction>
#include <KXmlGui/KToolBar>
#include <KXmlGui/KXMLGUIFactory>

#include "defaults.h"
#include "krusader.h"
#include <KWidgetsAddons/KActionMenu>
#include <QToolButton>
#include "paneroimport.h"
#include "Panel/krpanel.h"
#include "Panel/panelfunc.h"
#include "Dialogs/paneroquicksettings.h"
#include "Dialogs/panerofirststart.h"
#include "Panel/listpanel.h"
#include "icon.h"
#include "krusaderview.h"
#include "krslots.h"
#include "krtrashhandler.h"
#include "Dialogs/popularurls.h"
#include "GUI/krremoteencodingmenu.h"
#include "JobMan/jobman.h"
#include "JobMan/paneroundo.h"
#include "MountMan/kmountman.h"
#include "Panel/PanelView/krviewfactory.h"
#include "UserAction/useraction.h"
#include "QuickView/paneroquickviewcontroller.h"

QAction *KrActions::actCompare = nullptr;
QAction *KrActions::actDiskUsage = nullptr;
QAction *KrActions::actHomeTerminal = nullptr;
QAction *KrActions::actRemoteEncoding = nullptr;
RecentlyClosedTabsMenu *KrActions::actClosedTabsMenu = nullptr;
QAction *KrActions::actProfiles = nullptr;
QAction *KrActions::actMultiRename = nullptr;
QAction *KrActions::actMountMan = nullptr;
QAction *KrActions::actNewTool = nullptr;
QAction *KrActions::actKonfigurator = nullptr;
QAction *KrActions::actToolsSetup = nullptr;
QAction *KrActions::actSwapPanels = nullptr;
QAction *KrActions::actSwapSides = nullptr;
QAction *KrActions::actFind = nullptr;
QAction *KrActions::actLocate = nullptr;
QAction *KrActions::actSwitchFullScreenTE = nullptr;
QAction *KrActions::actAddBookmark = nullptr;
QAction *KrActions::actSavePosition = nullptr;
QAction *KrActions::actImportKrusader = nullptr;
QAction *KrActions::actViewColors = nullptr;
QAction *KrActions::actOpenTrash = nullptr;
QAction *KrActions::actSelectColorMask = nullptr;
QAction *KrActions::actOpenLeftBm = nullptr;
QAction *KrActions::actOpenRightBm = nullptr;
QAction *KrActions::actCmdlinePopup = nullptr;
QAction *KrActions::actSplit = nullptr;
QAction *KrActions::actCombine = nullptr;
QAction *KrActions::actUserMenu = nullptr;
QAction *KrActions::actManageUseractions = nullptr;
#ifdef SYNCHRONIZER_ENABLED
QAction *KrActions::actSyncDirs = nullptr;
#endif
QAction *KrActions::actF10Quit = nullptr;
QAction *KrActions::actEmptyTrash = nullptr;
QAction *KrActions::actTrashBin = nullptr;
QAction *KrActions::actPopularUrls = nullptr;

KToggleAction *KrActions::actToggleTerminal = nullptr;
QAction  *KrActions::actVerticalMode = nullptr;
QAction  *KrActions::actSelectNewerAndSingle = nullptr;
QAction  *KrActions::actSelectSingle = nullptr;
QAction  *KrActions::actSelectNewer = nullptr;
QAction  *KrActions::actSelectDifferentAndSingle = nullptr;
QAction  *KrActions::actSelectDifferent = nullptr;
QAction  **KrActions::compareArray[] = {&actSelectNewerAndSingle, &actSelectNewer, &actSelectSingle,
                                       &actSelectDifferentAndSingle, &actSelectDifferent, nullptr
                                      };
QAction *KrActions::actExecStartAndForget = nullptr;
QAction *KrActions::actExecCollectSeparate = nullptr;
QAction *KrActions::actExecCollectTogether = nullptr;
QAction *KrActions::actExecTerminalExternal = nullptr;
QAction *KrActions::actExecTerminalEmbedded = nullptr;
QAction **KrActions::execTypeArray[] = {&actExecStartAndForget, &actExecCollectSeparate, &actExecCollectTogether,
                                       &actExecTerminalExternal, &actExecTerminalEmbedded, nullptr
                                      };
KToggleAction *KrActions::actToggleFnkeys = nullptr;
KToggleAction *KrActions::actToggleCmdline = nullptr;
KToggleAction *KrActions::actShowStatusBar = nullptr;
KToggleAction *KrActions::actToggleHidden = nullptr;
KToggleAction *KrActions::actCompareDirs = nullptr;

QAction *KrActions::actJobProgress = nullptr;
QAction *KrActions::actJobControl = nullptr;
QAction *KrActions::actJobMode = nullptr;
QAction *KrActions::actJobUndo = nullptr;

QAction *createAction(const QString& text, const QString& iconName, const QKeySequence& shortcut,
                      QObject *recv, const char *slot, const QString& name, Krusader *krusaderApp)
{
    QAction *a;
    if (iconName.isEmpty())
        a = new QAction(text, krusaderApp);
    else
        a = new QAction(Icon(iconName), text, krusaderApp);
    krusaderApp->connect(a, SIGNAL(triggered(bool)), recv, slot);
    krusaderApp->actionCollection()->addAction(name, a);
    krusaderApp->actionCollection()->setDefaultShortcut(a, shortcut);
    return a;
}

QAction *createAction(const QString& text, const QString& iconName, const QList<QKeySequence>& shortcuts,
                      QObject *recv, const char *slot, const QString& name, Krusader *krusaderApp)
{
    QAction *a;
    if (iconName.isEmpty())
        a = new QAction(text, krusaderApp);
    else
        a = new QAction(Icon(iconName), text, krusaderApp);
    krusaderApp->connect(a, SIGNAL(triggered(bool)), recv, slot);
    krusaderApp->actionCollection()->addAction(name, a);
    krusaderApp->actionCollection()->setDefaultShortcuts(a, shortcuts);
    return a;
}


KToggleAction *createToggleAction(const QString& text, const QString& iconName, const QKeySequence& shortcut,
                                 QObject *recv, const char *slot, const QString& name, Krusader *krusaderApp)
{
    KToggleAction *a;
    if (iconName == nullptr)
        a = new KToggleAction(text, krusaderApp);
    else
        a = new KToggleAction(Icon(iconName), text, krusaderApp);
    krusaderApp->connect(a, SIGNAL(triggered(bool)), recv, slot);
    krusaderApp->actionCollection()->addAction(name, a);
    krusaderApp->actionCollection()->setDefaultShortcut(a, shortcut);
    return a;
}


void KrActions::setupActions(Krusader *krusaderApp)
{

#define NEW_KACTION(VAR, TEXT, ICON_NAME, SHORTCUT, RECV_OBJ, SLOT_NAME, NAME) \
    VAR = createAction(TEXT, ICON_NAME, SHORTCUT, RECV_OBJ, SLOT_NAME, NAME, krusaderApp);

#define NEW_KTOGGLEACTION(VAR, TEXT, ICON_NAME, SHORTCUT, RECV_OBJ, SLOT_NAME, NAME) \
    VAR = createToggleAction(TEXT, ICON_NAME, SHORTCUT, RECV_OBJ, SLOT_NAME, NAME, krusaderApp);


    // first come the TODO actions
    //actSync =       0;//new QAction(i18n("S&ynchronize Dirs"),                         0, krusaderApp, 0, actionCollection(), "sync dirs");
    //actNewTool =    0;//new QAction(i18n("&Add a new tool"),                          0, krusaderApp, 0, actionCollection(), "add tool");
    //actToolsSetup = 0;//new QAction(i18n("&Tools Menu Setup"),                        0, 0, krusaderApp, 0, actionCollection(), "tools setup");
    //KStandardAction::print(SLOTS, 0,actionCollection(),"std_print");
    //KStandardAction::showMenubar( SLOTS, SLOT(showMenubar()), actionCollection(), "std_menubar" );

    /* Shortcut disabled because of the Terminal Emulator bug. */
    KConfigGroup group(krConfig, "Private");
    int compareMode = group.readEntry("Compare Mode", 0);
    int cmdExecMode =  group.readEntry("Command Execution Mode", 0);
    QAction *tmp;
    Q_UNUSED(tmp);

    NEW_KACTION(tmp, i18n("Tab-Switch panel"), nullptr, Qt::Key_Tab, MAIN_VIEW, SLOT(panelSwitch()), "tab");

    KToggleToolBarAction *actShowToolBar = new KToggleToolBarAction(krusaderApp->toolBar(), i18n("Show Main Toolbar"), krusaderApp);
    krusaderApp->actionCollection()->addAction(KStandardAction::name(KStandardAction::ShowToolbar), actShowToolBar);

    KToggleToolBarAction *actShowJobToolBar = new KToggleToolBarAction(krusaderApp->toolBar("jobToolBar"), i18n("Show Job Toolbar"), krusaderApp);
    krusaderApp->actionCollection()->addAction("toggle show jobbar", actShowJobToolBar);

    KToggleToolBarAction *actShowActionsToolBar = new KToggleToolBarAction(krusaderApp->toolBar("actionsToolBar"), i18n("Show Actions Toolbar"), krusaderApp);
    krusaderApp->actionCollection()->addAction("toggle actions toolbar", actShowActionsToolBar);

    actShowStatusBar = KStandardAction::showStatusbar(SLOTS, SLOT(updateStatusbarVisibility()), krusaderApp->actionCollection());
    // Panero: Ctrl+Q is Quick View (Total Commander, D-022); F10 and Alt+F4 quit
    krusaderApp->actionCollection()->setDefaultShortcuts(KStandardAction::quit(krusaderApp, SLOT(quit()), krusaderApp->actionCollection()), {});
    PaneroQuickViewController::setupActions(krusaderApp->actionCollection(), krusaderApp);
    KStandardAction::configureToolbars(krusaderApp, SLOT(configureToolbars()), krusaderApp->actionCollection());
    KStandardAction::keyBindings(krusaderApp->guiFactory(), SLOT(configureShortcuts()), krusaderApp->actionCollection());

    // the toggle actions
    NEW_KTOGGLEACTION(actToggleFnkeys, i18n("Show &FN Keys Bar"), nullptr, 0, SLOTS,  SLOT(toggleFnkeys()), "toggle fn bar");

    NEW_KTOGGLEACTION(actToggleCmdline, i18n("Show &Command Line"), nullptr, 0, SLOTS, SLOT(toggleCmdline()), "toggle command line");

    NEW_KTOGGLEACTION(actToggleTerminal, i18n("Show &Embedded Terminal"), nullptr, Qt::CTRL + Qt::ALT + Qt::Key_E, SLOTS, SLOT(toggleTerminal()), "toggle terminal emulator");


    NEW_KTOGGLEACTION(actToggleHidden, i18n("Show &Hidden Files"), nullptr, Qt::ALT + Qt::Key_Period, SLOTS, SLOT(showHiddenFiles(bool)), "toggle hidden files");

    NEW_KACTION(actSwapPanels, i18n("S&wap Panels"), nullptr, Qt::CTRL + Qt::Key_U, SLOTS, SLOT(swapPanels()), "swap panels");

    NEW_KACTION(actEmptyTrash, PANERO_TEXT(i18n("Empty Trash"), i18n("Empty Recycle Bin")), "trash-empty", 0, SLOTS, SLOT(emptyTrash()), "emptytrash");

    // Panero: open the Trash (trash:/) in the active panel; deleting stays on F8/Delete
    actOpenTrash = new QAction(Icon(KrTrashHandler::trashIconName()), PANERO_TEXT(i18n("Open &Trash"), i18n("Open &Recycle Bin")), krusaderApp);
    actOpenTrash->setToolTip(PANERO_TEXT(i18n("Show the Trash in the active panel"), i18n("Show the Recycle Bin in the active panel")));
    krusaderApp->actionCollection()->addAction("panero open trash", actOpenTrash);
    QObject::connect(actOpenTrash, &QAction::triggered, krusaderApp, []() {
        ACTIVE_FUNC->openUrl(QUrl(QStringLiteral("trash:/")));
    });

    // Toolbar Trash button: a menu with Open Trash and Empty Trash (empty asks first)
    auto *trashMenu = new KActionMenu(Icon(KrTrashHandler::trashIconName()), PANERO_TEXT(i18n("Trash"), i18n("Recycle Bin")), krusaderApp);
    trashMenu->setPopupMode(QToolButton::InstantPopup);
    trashMenu->setToolTip(PANERO_TEXT(i18n("Open or empty the Trash"), i18n("Open or empty the Recycle Bin")));
    krusaderApp->actionCollection()->addAction("panero trash menu", trashMenu);

    NEW_KACTION(actTrashBin, PANERO_TEXT(i18n("Trash Popup Menu"), i18n("Recycle Bin Popup Menu")), KrTrashHandler::trashIconName(), 0, SLOTS, SLOT(trashPopupMenu()), "trashbin");
    trashMenu->addAction(actOpenTrash);
    trashMenu->addAction(actEmptyTrash);

    NEW_KACTION(actSwapSides, i18n("Sw&ap Sides"), nullptr, Qt::CTRL + Qt::SHIFT + Qt::Key_U, SLOTS, SLOT(toggleSwapSides()), "toggle swap sides");
    actToggleHidden->setChecked(KConfigGroup(krConfig, "Look&Feel").readEntry("Show Hidden", _ShowHidden));

    // and then the DONE actions
    NEW_KACTION(actCmdlinePopup, i18n("popup cmdline"), nullptr, Qt::CTRL + Qt::Key_Slash, SLOTS, SLOT(cmdlinePopup()), "cmdline popup");


    NEW_KACTION(tmp, i18n("Start &Root Mode Panero"), "krusader_root", Qt::ALT + Qt::SHIFT + Qt::Key_K, SLOTS, SLOT(rootKrusader()), "root krusader");
    NEW_KACTION(actProfiles, i18n("Pro&files"), "user-identity", Qt::ALT + Qt::SHIFT + Qt::Key_L, MAIN_VIEW, SLOT(profiles()), "profile");
    NEW_KACTION(actSplit, i18n("Sp&lit File..."), "split", Qt::CTRL + Qt::Key_P, SLOTS, SLOT(slotSplit()), "split");
    NEW_KACTION(actCombine, i18n("Com&bine Files..."), "kr_combine", 0, SLOTS, SLOT(slotCombine()), "combine");
    NEW_KACTION(actSelectNewerAndSingle, i18n("&Select Newer and Single"), nullptr, 0, SLOTS, SLOT(compareSetup()), "select_newer_and_single");
    NEW_KACTION(actSelectNewer, i18n("Select &Newer"), nullptr, 0, SLOTS, SLOT(compareSetup()), "select_newer");
    NEW_KACTION(actSelectSingle, i18n("Select &Single"), nullptr, 0, SLOTS, SLOT(compareSetup()), "select_single");
    NEW_KACTION(actSelectDifferentAndSingle, i18n("Select Different &and Single"), nullptr, 0, SLOTS, SLOT(compareSetup()), "select_different_and_single");
    NEW_KACTION(actSelectDifferent,  i18n("Select &Different"), nullptr, 0, SLOTS, SLOT(compareSetup()), "select_different");
    actSelectNewerAndSingle->setCheckable(true);
    actSelectNewer->setCheckable(true);
    actSelectSingle->setCheckable(true);
    actSelectDifferentAndSingle->setCheckable(true);
    actSelectDifferent->setCheckable(true);
    auto *selectGroup = new QActionGroup(krusaderApp);
    selectGroup->setExclusive(true);
    selectGroup->addAction(actSelectNewerAndSingle);
    selectGroup->addAction(actSelectNewer);
    selectGroup->addAction(actSelectSingle);
    selectGroup->addAction(actSelectDifferentAndSingle);
    selectGroup->addAction(actSelectDifferent);
    if (compareMode < (int)(sizeof(compareArray) / sizeof(QAction **)) - 1)
        (*compareArray[ compareMode ])->setChecked(true);
    NEW_KACTION(actExecStartAndForget, i18n("Start and &Forget"), nullptr, 0, SLOTS, SLOT(execTypeSetup()), "exec_start_and_forget");
    NEW_KACTION(actExecCollectSeparate, i18n("Display &Separated Standard and Error Output"), nullptr, 0, SLOTS, SLOT(execTypeSetup()), "exec_collect_separate");
    NEW_KACTION(actExecCollectTogether, i18n("Display &Mixed Standard and Error Output"), nullptr, 0, SLOTS, SLOT(execTypeSetup()), "exec_collect_together");
    NEW_KACTION(actExecTerminalExternal, i18n("Start in &New Terminal"), nullptr, 0, SLOTS, SLOT(execTypeSetup()), "exec_terminal_external");
    NEW_KACTION(actExecTerminalEmbedded, i18n("Send to &Embedded Terminal Emulator"), nullptr, 0, SLOTS, SLOT(execTypeSetup()), "exec_terminal_embedded");
    actExecStartAndForget->setCheckable(true);
    actExecCollectSeparate->setCheckable(true);
    actExecCollectTogether->setCheckable(true);
    actExecTerminalExternal->setCheckable(true);
    actExecTerminalEmbedded->setCheckable(true);
    auto *actionGroup = new QActionGroup(krusaderApp);
    actionGroup->setExclusive(true);
    actionGroup->addAction(actExecStartAndForget);
    actionGroup->addAction(actExecCollectSeparate);
    actionGroup->addAction(actExecCollectTogether);
    actionGroup->addAction(actExecTerminalExternal);
    actionGroup->addAction(actExecTerminalEmbedded);
    if (cmdExecMode < (int)(sizeof(execTypeArray) / sizeof(QAction **)) - 1)
        (*execTypeArray[ cmdExecMode ])->setChecked(true);


    NEW_KACTION(actHomeTerminal, i18n("Start &Terminal"), "utilities-terminal", 0, SLOTS, SLOT(homeTerminal()), "terminal@home");

    actMountMan = krMtMan.action();
    krusaderApp->actionCollection()->addAction("mountman", actMountMan);
    krusaderApp->actionCollection()->setDefaultShortcut(actMountMan, Qt::ALT + Qt::Key_Slash);

    NEW_KACTION(actFind, i18n("&Search..."), "system-search", Qt::CTRL + Qt::Key_S, SLOTS, SLOT(search()), "find");
    NEW_KACTION(actLocate, i18n("&Locate..."), "edit-find", Qt::SHIFT + Qt::CTRL + Qt::Key_L, SLOTS, SLOT(locate()), "locate");
#ifdef SYNCHRONIZER_ENABLED
    NEW_KACTION(actSyncDirs, i18n("Synchronize Fol&ders..."), "folder-sync", Qt::CTRL + Qt::Key_Y, SLOTS, SLOT(slotSynchronizeDirs()), "sync dirs");
#endif
    NEW_KACTION(actDiskUsage, i18n("D&isk Usage..."), "kr_diskusage", Qt::ALT + Qt::SHIFT + Qt::Key_S, SLOTS, SLOT(slotDiskUsage()), "disk usage");
    NEW_KACTION(actKonfigurator, i18n("&Settings..."), "configure", KStandardShortcut::shortcut(KStandardShortcut::Preferences), SLOTS, SLOT(startKonfigurator()), "konfigurator");
    // Panero: one Settings window (D-022), Ctrl+, as in most applications; KDE's Ctrl+Shift+, stays
    {
        QList<QKeySequence> settingsKeys = {QKeySequence(Qt::CTRL + Qt::Key_Comma)};
        for (const QKeySequence &key : KStandardShortcut::shortcut(KStandardShortcut::Preferences))
            if (!settingsKeys.contains(key))
                settingsKeys << key;
        krusaderApp->actionCollection()->setDefaultShortcuts(actKonfigurator, settingsKeys);
    }
    auto *firstStart = new QAction(Icon("tools-wizard"), i18n("&First-Start Assistant..."), krusaderApp);
    firstStart->setToolTip(i18n("Choose the look, drive buttons and mouse selection again"));
    krusaderApp->actionCollection()->addAction("panero first start", firstStart);
    QObject::connect(firstStart, &QAction::triggered, krusaderApp, [krusaderApp]() {
        PaneroFirstStart::showFromHelp(krusaderApp);
    });
    actImportKrusader = new QAction(Icon("document-import"), i18n("Import &Krusader Settings..."), krusaderApp);
    krusaderApp->actionCollection()->addAction("panero import krusader settings", actImportKrusader);
    QObject::connect(actImportKrusader, &QAction::triggered, krusaderApp, [krusaderApp]() {
        PaneroImport::requestImportFromMenu(krusaderApp);
    });
    actViewColors = new QAction(Icon("configure"), i18n("&Quick Settings"), krusaderApp);
    actViewColors->setToolTip(i18n("Quick Settings: light or dark, icons, density, hidden files, drives, cloud"));
    krusaderApp->actionCollection()->addAction("panero view colors", actViewColors);
    QObject::connect(actViewColors, &QAction::triggered, krusaderApp, [krusaderApp]() {
        // a popup under the toolbar's gear (centred on the window from the menu)
        PaneroQuickSettings::popup(krusaderApp, krusaderApp->toolBar()->widgetForAction(actViewColors));
    });
    NEW_KACTION(actSavePosition, i18n("Save &Position"), nullptr, 0, krusaderApp, SLOT(savePosition()), "save position");
    NEW_KACTION(actCompare, i18n("Compare b&y Content..."), "kr_comparedirs", 0, SLOTS, SLOT(compareContent()), "compare");
    NEW_KACTION(actMultiRename, i18n("Multi &Rename..."), "edit-rename", Qt::SHIFT + Qt::Key_F2, SLOTS, SLOT(multiRename()), "multirename");
    // Panero: Ctrl+M as in Total Commander (D-019); Shift+F2 stays
    krusaderApp->actionCollection()->setDefaultShortcuts(actMultiRename, {QKeySequence(Qt::CTRL + Qt::Key_M), QKeySequence(Qt::SHIFT + Qt::Key_F2)});

    NEW_KACTION(actAddBookmark, i18n("Pin Current Folder"), "bookmark-new", KStandardShortcut::addBookmark(), SLOTS, SLOT(addBookmark()), "add bookmark");
    actAddBookmark->setToolTip(i18n("Add the active panel's folder to the Pinned list (Krusader: Add Bookmark)"));
    NEW_KACTION(actVerticalMode, i18n("Vertical Mode"), "view-split-top-bottom", Qt::ALT + Qt::CTRL + Qt::Key_R, MAIN_VIEW, SLOT(toggleVerticalMode()), "toggle vertical mode");
    actUserMenu = new KActionMenu(i18n("User&actions"), krusaderApp);
    krusaderApp->actionCollection()->addAction("useractionmenu", actUserMenu);
    NEW_KACTION(actManageUseractions, i18n("Manage User Actions..."), nullptr, 0, SLOTS, SLOT(manageUseractions()), "manage useractions");

    actRemoteEncoding = new KrRemoteEncodingMenu(i18n("Select Remote Charset"), "character-set", krusaderApp->actionCollection());

    actClosedTabsMenu = new RecentlyClosedTabsMenu(i18n("Recently Closed Tabs"), "edit-undo", krusaderApp->actionCollection());
    actClosedTabsMenu->setEnabled(false);

    NEW_KACTION(actF10Quit, i18n("Quit"), nullptr, Qt::Key_F10, krusaderApp, SLOT(quit()) , "F10_Quit");
    actF10Quit->setToolTip(i18n("Quit Panero."));

    // Panero: Ctrl+Z is Undo (D-022); Popular URLs moved to Ctrl+Alt+Z
    NEW_KACTION(actPopularUrls, i18n("Popular URLs..."), nullptr, Qt::CTRL + Qt::ALT + Qt::Key_Z, krusaderApp->popularUrls(), SLOT(showDialog()), "Popular_Urls");
    NEW_KACTION(actSwitchFullScreenTE, i18n("Toggle Fullscreen Embedded Terminal"), nullptr,
                Qt::CTRL + Qt::ALT + Qt::Key_F, MAIN_VIEW, SLOT(toggleFullScreenTerminalEmulator()),
                "switch_fullscreen_te");

    NEW_KACTION(tmp, i18n("Move Focus Up"), nullptr, Qt::CTRL + Qt::SHIFT + Qt::Key_Up, MAIN_VIEW, SLOT(focusUp()), "move_focus_up");
    NEW_KACTION(tmp, i18n("Move Focus Down"), nullptr, Qt::CTRL + Qt::SHIFT + Qt::Key_Down, MAIN_VIEW, SLOT(focusDown()), "move_focus_down");


    // job manager actions
    actJobControl = krJobMan->controlAction();
    krusaderApp->actionCollection()->addAction("job control", actJobControl);
    krusaderApp->actionCollection()->setDefaultShortcut(actJobControl, Qt::CTRL + Qt::ALT + Qt::Key_P);
    actJobProgress = krJobMan->progressAction();
    krusaderApp->actionCollection()->addAction("job progress", actJobProgress);
    actJobMode = krJobMan->modeAction();
    krusaderApp->actionCollection()->addAction("job mode", actJobMode);
    actJobUndo = krJobMan->undoAction();
    krusaderApp->actionCollection()->addAction("job undo", actJobUndo);
    krusaderApp->actionCollection()->setDefaultShortcut(actJobUndo, Qt::CTRL + Qt::Key_Z);
    // Panero (D-022): the jobs button with its progress ring (main toolbar, shown while jobs run)
    krusaderApp->actionCollection()->addAction("panero jobs", krJobMan->jobsAction());
    // after an undo the panels show the result also where no folder watcher runs (cloud folders)
    QObject::connect(PaneroUndo::self(), &PaneroUndo::undone, krusaderApp, []() {
        if (!MAIN_VIEW)
            return;
        LEFT_FUNC->refresh();
        RIGHT_FUNC->refresh();
    });

    // and at last we can set the tool-tips
    actKonfigurator->setToolTip(i18n("Setup Panero the way you like it"));
    actFind->setToolTip(i18n("Search for files"));

    // setup all UserActions
    krUserAction = new UserAction();
}
