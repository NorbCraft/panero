/*
    SPDX-FileCopyrightText: 2000 Shie Erlich <krusader@users.sourceforge.net>
    SPDX-FileCopyrightText: 2000 Rafi Yanai <krusader@users.sourceforge.net>
    SPDX-FileCopyrightText: 2004-2022 Krusader Krew <https://krusader.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "krusader.h"
#include "panerowording.h"
#include "paneronavigation.h"
#include "paneroviewmodeaction.h"
#include "panerotoolbar.h"
#include "Panel/paneroappearance.h"

#include <QFile>
#include <QProcess>
#include <QToolButton>
#include <KConfigWidgets/KHamburgerMenu>
#include <KConfigWidgets/KStandardAction>
#include <KWidgetsAddons/KToggleAction>
#include "icon.h"
#include "Panel/krpanel.h"
#include "Panel/listpanel.h"
#include "Panel/panelfunc.h"

#include <QGuiApplication>
#include <QScreen>

// QtCore
#include <QDir>
#include <QDateTime>
#include <QStringList>
#include <QStandardPaths>
// QtGui
#include <QMoveEvent>
#include <QResizeEvent>
// QtWidgets
#include <QApplication>
#include <QMenuBar>
#include <QDesktopWidget>
// QtDBus
#include <QDBusInterface>

#include <KConfigCore/KSharedConfig>
#include <KConfigGui/KWindowConfig>
#include <KI18n/KLocalizedString>
#include <KXmlGui/KActionCollection>
#include <KXmlGui/KXMLGUIFactory>
#include <KXmlGui/KToolBar>
#include <KWidgetsAddons/KAcceleratorManager>
#include <KWidgetsAddons/KCursor>
#include <KWidgetsAddons/KMessageBox>
#include <KWidgetsAddons/KToggleAction>
#include <KWidgetsAddons/KToolBarPopupAction>
#include <KWindowSystem/KStartupInfo>
#include <KWindowSystem/KWindowSystem>
#include <utility>

#include "defaults.h"
#include "kractions.h"
#include "krarchandler.h"
#include "krglobal.h"
#include "krservices.h"
#include "krslots.h"
#include "krtrashhandler.h"
#include "krusaderversion.h"
#include "krusaderview.h"
#include "panelmanager.h"
#include "tabactions.h"

#include "BookMan/krbookmarkhandler.h"
#include "Dialogs/checksumdlg.h"
#include "Dialogs/krpleasewait.h"
#include "Dialogs/popularurls.h"
#include "FileSystem/fileitem.h"
#include "FileSystem/krpermhandler.h"
#include "GUI/kcmdline.h"
#include "GUI/kfnkeys.h"
#include "GUI/krremoteencodingmenu.h"
#include "GUI/krusaderstatus.h"
#include "GUI/terminaldock.h"
#include "JobMan/jobman.h"
#include "KViewer/krviewer.h"
#include "Konfigurator/kgprotocols.h"
#include "MountMan/kmountman.h"
#include "Panel/PanelView/krview.h"
#include "Panel/PanelView/krviewfactory.h"
#include "Panel/krcolorcache.h"
#include "Panel/krpanel.h"
#include "Panel/listpanelactions.h"
#include "Panel/viewactions.h"
#include "UserAction/expander.h"
// This makes gcc-4.1 happy. Warning about possible problem with KrAction's dtor not called
#include "UserAction/kraction.h"
#include "UserAction/useraction.h"

// define the static members
Krusader *Krusader::App = nullptr;
QString   Krusader::AppName;

// construct the views, statusbar and menu bars and prepare Krusader to start
// (Windows: plain Qt::Window. Explicit title/help hints make Qt draw a dialog
// frame there - only "?" and a disabled close button, no minimize/maximize;
// X11/Wayland window managers ignore these hints.)
Krusader::Krusader(const QCommandLineParser &parser) : KParts::MainWindow(nullptr,
#ifdef Q_OS_WIN
                Qt::Window),
#else
                Qt::Window | Qt::WindowTitleHint | Qt::WindowContextHelpButtonHint),
#endif
        _listPanelActions(nullptr), isStarting(true), _quit(false)
{
    // create the "krusader"
    App = this;
    krMainWindow = this;
    SLOTS = new KrSlots(this);
    setXMLFile(QStringLiteral("paneroui.rc"));   // kpart-related xml file

    plzWait = new KrPleaseWaitHandler(this);

    const bool runKonfig = versionControl();

    QString message;
    switch (krConfig->accessMode()) {
    case KConfigBase::NoAccess :
        message = "Panero's configuration file can't be found. Default values will be used.";
        break;
    case KConfigBase::ReadOnly :
        message = "Panero's configuration file is in READ ONLY mode. Changed values will not be saved.";
        break;
    case KConfigBase::ReadWrite :
        message = "";
        break;
    }
    if (!message.isEmpty()) {
        KMessageBox::error(krApp, message);
    }

    // create an object that manages archives in several parts of the source code
    KrGlobal::arcMan = new KrArcHandler(this);

    // create MountMan
    KrGlobal::mountMan = new KMountMan(this);
    connect(KrGlobal::mountMan, &KMountMan::refreshPanel, SLOTS, &KrSlots::refresh);

    // create popular URLs container
    _popularUrls = new PopularUrls(this);

    // create bookman
    krBookMan = new KrBookmarkHandler(this);

    // create job manager
    krJobMan = new JobMan(this);

    // create the main view
    MAIN_VIEW = new KrusaderView(this);

    // setup all the krusader's actions
    setupActions();

    // init the permission handler class
    KrPermHandler::init();

    // init the protocol handler
    KgProtocols::init();

    const KConfigGroup lookFeelGroup(krConfig, "Look&Feel");
    FileItem::loadUserDefinedFolderIcons(lookFeelGroup.readEntry("Load User Defined Folder Icons",
                                                                 _UserDefinedFolderIcons));

    const KConfigGroup startupGroup(krConfig, "Startup");
    QString startProfile = startupGroup.readEntry("Starter Profile Name", QString());

    QList<QUrl> leftTabs;
    QList<QUrl> rightTabs;

    // get command-line arguments
    if (parser.isSet("left")) {
        leftTabs = KrServices::toUrlList(parser.value("left").split(','));
        startProfile.clear();
    }
    if (parser.isSet("right")) {
        rightTabs = KrServices::toUrlList(parser.value("right").split(','));
        startProfile.clear();
    }
    if (parser.isSet("profile"))
        startProfile = parser.value("profile");

    if (!startProfile.isEmpty()) {
        leftTabs.clear();
        rightTabs.clear();
    }
    // starting the panels
    MAIN_VIEW->start(startupGroup, startProfile.isEmpty(), leftTabs, rightTabs);

    // create a status bar
    auto *status = new KrusaderStatus(this);
    setStatusBar(status);
    status->setWhatsThis(i18n("Statusbar will show basic information "
                              "about file below mouse pointer."));

    // create tray icon (if needed)
    const bool startToTray = startupGroup.readEntry("Start To Tray", _StartToTray);
    setTray(startToTray);

    setCentralWidget(MAIN_VIEW);

    // Panero: single navigation sidebar (pins + folder tree) following the active panel
    _navigationDock = new PaneroNavigationDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, _navigationDock);
    connect(MAIN_VIEW, &KrusaderView::activeLocationChanged, _navigationDock, &PaneroNavigationDock::activeLocationChanged);
    // window title "<folder> — Panero" (KMainWindow appends the application name)
    setCaption(QString());
    connect(MAIN_VIEW, &KrusaderView::activeLocationChanged, this, [this](const QUrl &url) {
        QString folder = url.adjusted(QUrl::StripTrailingSlash).fileName();
        if (folder.isEmpty())
            folder = url.toDisplayString(QUrl::PreferLocalFile);
        setCaption(folder);
    });
    {
        QAction *toggleNavigation = _navigationDock->toggleViewAction();
        toggleNavigation->setIcon(Icon(QStringLiteral("view-list-tree")));
        toggleNavigation->setText(i18n("Folder &Tree and Pins"));
        toggleNavigation->setToolTip(i18n("Show or hide the folder tree and pinned folders"));
        actionCollection()->addAction(QStringLiteral("panero toggle navigation"), toggleNavigation);
        actionCollection()->addAction(QStringLiteral("panero sidebar button"),
                                      new PaneroSidebarButtonAction(toggleNavigation, this));
    }
    setupPaneroToolbarLabels();
    {
        // Settings > Show Menubar; with the menu bar hidden, the menu button at the end
        // of the toolbar (KHamburgerMenu) offers the same menus
        KToggleAction *showMenubar = KStandardAction::showMenubar(nullptr, nullptr, actionCollection());
        // Ctrl+M is Multi-Rename (D-019), not Show Menubar
        actionCollection()->setDefaultShortcuts(showMenubar, {});
        connect(showMenubar, &QAction::toggled, this, [this](bool on) { menuBar()->setVisible(on); });
        KHamburgerMenu *menuButton = KStandardAction::hamburgerMenu(nullptr, nullptr, actionCollection());
        menuButton->setMenuBar(menuBar());
        menuButton->setText(i18n("Menu"));
        menuButton->setIconText(i18n("Menu"));
        menuButton->setShowMenuBarAction(showMenubar);
        // the "three lines" icon: the desktop's symbolic open-menu icon in the toolbar's
        // text colour (Zorin draws application-menu as a gear), else Breeze's
        // with the Panero Line icons the theme itself has the line icons
        auto lineIcons = []() {
            return KConfigGroup(krConfig, "Panero Appearance").readEntry("Icon Set", QString()) == QLatin1String("line");
        };
        auto menuIcon = [menuButton, lineIcons]() {
            if (lineIcons()) {
                menuButton->setIcon(QIcon::fromTheme(QStringLiteral("application-menu")));
                return;
            }
            const QIcon symbolic = PaneroAppearance::symbolicIcon(QStringLiteral("open-menu-symbolic"),
                                                                  QApplication::palette().color(QPalette::ButtonText));
            menuButton->setIcon(!symbolic.isNull() ? symbolic : Icon(QStringLiteral("application-menu")));
        };
        // the View & Colors button: the classic cogwheel, in the same way
        auto gearIcon = [this, lineIcons]() {
            QAction *settings = actionCollection()->action(QStringLiteral("panero view colors"));
            if (lineIcons()) {
                if (settings)
                    settings->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
                return;
            }
            const QIcon symbolic = PaneroAppearance::symbolicIcon(QStringLiteral("emblem-system-symbolic"),
                                                                  QApplication::palette().color(QPalette::ButtonText));
            if (settings)
                settings->setIcon(!symbolic.isNull() ? symbolic : Icon(QStringLiteral("configure")));
        };
        gearIcon();
        connect(PaneroAppearance::instance(), &PaneroAppearance::appearanceChanged, this, gearIcon);
        menuIcon();
        connect(PaneroAppearance::instance(), &PaneroAppearance::appearanceChanged, menuButton, menuIcon);
    }

    // manage our keyboard short-cuts
    //KAcceleratorManager::manage(this,true);

    setCursor(Qt::ArrowCursor);

    if (! startProfile.isEmpty())
        MAIN_VIEW->profiles(startProfile);

    // restore gui settings
    {
        // now, check if we need to create a konsole_part
        // call the XML GUI function to draw the UI
        createGUI(MAIN_VIEW->terminalDock()->part());

        // this needs to be called AFTER createGUI() !!!
        updateUserActions();
        _listPanelActions->guiUpdated();

        // not using this. See savePosition()
        //applyMainWindowSettings();

        const KConfigGroup cfgToolbar(krConfig, "Main Toolbar");
        toolBar()->applySettings(cfgToolbar);

        const KConfigGroup cfgJobBar(krConfig, "Job Toolbar");
        toolBar("jobToolBar")->applySettings(cfgJobBar);
        // Panero: the job toolbar starts hidden (copy/move show their own progress);
        // Settings > Toolbars > Show Job Toolbar brings it back and is remembered
        if (!cfgJobBar.hasKey("Hidden"))
            toolBar("jobToolBar")->hide();

        const KConfigGroup cfgActionsBar(krConfig, "Actions Toolbar");
        toolBar("actionsToolBar")->applySettings(cfgActionsBar);
        // Panero: the actions toolbar (user actions) is an empty strip unless the user
        // put actions on it; Settings > Toolbars > Show Actions Toolbar still works
        if (toolBar("actionsToolBar")->actions().isEmpty())
            toolBar("actionsToolBar")->hide();

        // the main toolbar owns its whole row (its spacer pushes the appearance button to
        // the right edge); the optional actions/job toolbars go below it when shown
        insertToolBarBreak(toolBar("actionsToolBar"));
        insertToolBarBreak(toolBar("jobToolBar"));

        _navigationDock->restoreSettings();
        emit MAIN_VIEW->activeLocationChanged(ACTIVE_PANEL->virtualPath(), ACTIVE_PANEL->gui->isLeft()); // sidebar + title

        // restore toolbars position and visibility
        restoreState(startupGroup.readEntry("State", QByteArray()));

        statusBar()->setVisible(startupGroup.readEntry("Show status bar", _ShowStatusBar));
        if (QAction *showMenubar = actionCollection()->action(KStandardAction::name(KStandardAction::ShowMenubar)))
            showMenubar->setChecked(startupGroup.readEntry("Show menubar", false));
        // Panero: the menu button at the end of the toolbar replaces the menu bar (D-013)
        menuBar()->setVisible(startupGroup.readEntry("Show menubar", false));

        MAIN_VIEW->updateGUI(startupGroup);

        // popular urls
        _popularUrls->load();
    }

    // (first run: the first-start assistant ran before the window was created,
    // main.cpp; no Konfigurator, D-022)

    KConfigGroup viewerModuleGrp(krConfig, "ViewerModule");
    if (viewerModuleGrp.readEntry("FirstRun", true)) {
        KrViewer::configureDeps();
        viewerModuleGrp.writeEntry("FirstRun", false);
    }

#ifdef Q_OS_WIN
    // Windows: also on the first run (upstream skipped it and the window opened small)
    {
#else
    if (!runKonfig) {
#endif
        KConfigGroup cfg(krConfig, "Private");
        move(cfg.readEntry("Start Position", _StartPosition));
        resize(cfg.readEntry("Start Size", _StartSize));
    }

    // view initialized; show window or only tray
    if (!startToTray) {
        show();
    }

    KrTrashHandler::startWatcher();
    isStarting = false;

    //HACK - used by [ListerTextArea|KrSearchDialog|LocateDlg]:keyPressEvent()
    KrGlobal::copyShortcut = _listPanelActions->actCopy->shortcut();

    //HACK: make sure the active view becomes focused
    // for some reason sometimes the active view cannot be focused immediately at this point,
    // so queue it for the main loop
    QTimer::singleShot(0, ACTIVE_PANEL->view->widget(), QOverload<>::of(&QWidget::setFocus));

    _openUrlTimer.setSingleShot(true);
    connect(&_openUrlTimer, &QTimer::timeout, this, &Krusader::doOpenUrl);

    auto *startupInfo = new KStartupInfo(0, this);
    connect(startupInfo, &KStartupInfo::gotNewStartup,
            this, &Krusader::slotGotNewStartup);
    connect(startupInfo, &KStartupInfo::gotRemoveStartup,
            this, &Krusader::slotGotRemoveStartup);
}

Krusader::~Krusader()
{
    KrTrashHandler::stopWatcher();

    delete MAIN_VIEW;
    MAIN_VIEW = nullptr;
    App = nullptr;
}

void Krusader::setTray(bool forceCreation)
{
    const bool trayIsNeeded = forceCreation || KConfigGroup(krConfig, "Look&Feel")
                                               .readEntry("Minimize To Tray", _ShowTrayIcon);
    if (!sysTray && trayIsNeeded) {
        sysTray = new KStatusNotifierItem(this);
        sysTray->setIconByName(appIconName());
        // we have our own "quit" method, re-connect
        QAction *quitAction = sysTray->action(QStringLiteral("quit"));
        if (quitAction) {
            disconnect(quitAction, &QAction::triggered, nullptr, nullptr);
            connect(quitAction, &QAction::triggered, this, &Krusader::quit);
        }
    } else if (sysTray && !trayIsNeeded) {
        // user does not want tray anymore :(
        sysTray->deleteLater();
    }
}

bool Krusader::versionControl()
{
    // create config file
    krConfig = KSharedConfig::openConfig().data();
    KConfigGroup nogroup(krConfig, QString());
    const bool firstRun = nogroup.readEntry("First Time", true);
    KrGlobal::sCurrentConfigVersion = nogroup.readEntry("Config Version", -1);

    // first installation: Panero's first-start assistant replaces Krusader's welcome
    // box and first-run Konfigurator (main.cpp, PaneroFirstStart, D-022)
    nogroup.writeEntry("Version", VERSION);
    nogroup.writeEntry("First Time", false);
    krConfig->sync();

    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                  QStringLiteral("/panero/"));

    return firstRun;
}

void Krusader::statusBarUpdate(const QString& mess)
{
    // change the message on the statusbar for 5 seconds
    if (statusBar()->isVisible())
        statusBar()->showMessage(mess, 5000);
}

bool Krusader::event(QEvent *e) {
    if(e->type() == QEvent::ApplicationPaletteChange) {
        KrColorCache::getColorCache().refreshColors();
    }
    return KParts::MainWindow::event(e);
}

// <patch> Moving from Pixmap actions to generic filenames - thanks to Carsten Pfeiffer
void Krusader::setupActions() {
    QAction *bringToTopAct = new QAction(i18n("Bring Main Window to Top"), this);
    actionCollection()->addAction("bring_main_window_to_top", bringToTopAct);
    connect(bringToTopAct, &QAction::triggered, this, &Krusader::moveToTop);

    KrActions::setupActions(this);
    _krActions = new KrActions(this);
    _viewActions = new ViewActions(this, this);
    _listPanelActions = new ListPanelActions(this, this);
    _tabActions = new TabActions(this, this);
}

///////////////////////////////////////////////////////////////////////////
//////////////////// implementation of slots //////////////////////////////
///////////////////////////////////////////////////////////////////////////

void Krusader::savePosition() {
    KConfigGroup cfg(krConfig, "Private");
    cfg.writeEntry("Start Position", pos());
    cfg.writeEntry("Start Size", size());

    cfg = krConfig->group("Startup");
    MAIN_VIEW->saveSettings(cfg);

    // NOTE: this would save current window state/size, statusbar and settings for each toolbar.
    // We are not using this and saving everything manually because
    // - it does not save window position
    // - window size save/restore does sometimes not work (multi-monitor setup)
    // - saving the statusbar visibility should be independent from window position and restoring it
    //   does not work properly.
    //KConfigGroup cfg = KConfigGroup(&cfg, "MainWindowSettings");
    //saveMainWindowSettings(cfg);
    //statusBar()->setVisible(cfg.readEntry("StatusBar", "Enabled") != "Disabled");

    krConfig->sync();
}

void Krusader::saveSettings() {
    // avoid that information about closed tabs gets saved
    ACTIVE_MNG->delAllClosedTabs();

    // workaround: revert terminal fullscreen mode before saving widget and toolbar visibility
    if (MAIN_VIEW->isTerminalEmulatorFullscreen()) {
        MAIN_VIEW->setTerminalEmulator(false, true);
    }

    KConfigGroup noGroup(krConfig, QString());
    noGroup.writeEntry("Config Version", KrGlobal::sConfigVersion);

    // save toolbar settings
    KConfigGroup cfg(krConfig, "Main Toolbar");
    toolBar()->saveSettings(cfg);

    cfg = krConfig->group("Job Toolbar");
    toolBar("jobToolBar")->saveSettings(cfg);

    cfg = krConfig->group("Actions Toolbar");
    toolBar("actionsToolBar")->saveSettings(cfg);


    if (_navigationDock)
        _navigationDock->saveSettings();

    cfg = krConfig->group("Startup");
    // save toolbar visibility and position
    cfg.writeEntry("State", saveState());
    cfg.writeEntry("Show status bar", statusBar()->isVisible());
    cfg.writeEntry("Show menubar", !menuBar()->isHidden());

    // save panel and window settings
    if (cfg.readEntry("Remember Position", _RememberPos))
        savePosition();

    // save the gui components visibility
    if (cfg.readEntry("UI Save Settings", _UiSave)) {
        cfg.writeEntry("Show FN Keys", KrActions::actToggleFnkeys->isChecked());
        cfg.writeEntry("Show Cmd Line", KrActions::actToggleCmdline->isChecked());
        cfg.writeEntry("Show Terminal Emulator", KrActions::actToggleTerminal->isChecked());
    }

    // save popular links
    _popularUrls->save();

    krConfig->sync();
}

void Krusader::closeEvent(QCloseEvent *event)
{
    if (sysTray && !_quit && !qApp->isSavingSession()) {
        // close to tray instead
        event->ignore();
        hide();
        return;
    }

    _quit = false; // in case quit will be aborted
    KParts::MainWindow::closeEvent(event); // (may) quit, continues with queryClose()...
}

void Krusader::showEvent(QShowEvent *event)
{
    const KConfigGroup lookFeelGroup(krConfig, "Look&Feel");
    if (sysTray && !lookFeelGroup.readEntry("Minimize To Tray", _ShowTrayIcon)) {
        // restoring from "start to tray", tray icon is not needed anymore
        sysTray->deleteLater();
    }

    KParts::MainWindow::showEvent(event);
}

bool Krusader::queryClose() {
    if (isStarting)
        return false;

    if (qApp->isSavingSession()) { // KDE is logging out, accept the close
        acceptClose();
        return true;
    }

    const KConfigGroup cfg = krConfig->group("Look&Feel");
    const bool confirmExit = cfg.readEntry("Warn On Exit", _WarnOnExit);

    // ask user and wait until all KIO::job operations are terminated. Krusader won't exit before
    // that anyway
    if (!krJobMan->waitForJobs(confirmExit))
        return false;

    /* First try to close the child windows, because it's the safer
       way to avoid crashes, then close the main window.
       If closing a child is not successful, then we cannot let the
       main window close. */

    for (;;) {
        QWidgetList list = QApplication::topLevelWidgets();
        QWidget *activeModal = QApplication::activeModalWidget();
        QWidget *w = list.at(0);

        if (activeModal &&
                activeModal != this &&
                activeModal != menuBar() &&
                list.contains(activeModal) &&
                !activeModal->isHidden()) {
            w = activeModal;
        } else {
            int i = 1;
            for (; i < list.count(); ++i) {
                w = list.at(i);
                if (!(w && (w == this || w->isHidden() || w == menuBar())))
                    break;
            }

            if (i == list.count())
                w = nullptr;
        }

        if (!w) break;

        if (!w->close()) {
            if (w->inherits("QDialog")) {
                fprintf(stderr, "Failed to close: %s\n", w->metaObject()->className());
            }
            return false;
        }
    }

    acceptClose();
    return true;
}

void Krusader::acceptClose() {
    saveSettings();

    emit shutdown();

    // Removes the DBUS registration of the application. Single instance mode requires unique appid.
    // As Krusader is exiting, we release that unique appid, so new Krusader instances
    // can be started.

    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.unregisterObject("/Instances/" + Krusader::AppName);
}

// the please wait dialog functions
void Krusader::startWaiting(QString msg, int count , bool cancel) {
    plzWait->startWaiting(std::move(msg) , count, cancel);
}

bool Krusader::wasWaitingCancelled() const {
    return plzWait->wasCancelled();
}

void Krusader::stopWait() {
    plzWait->stopWait();
}

void Krusader::updateUserActions() {
    auto *userActionMenu = qobject_cast<KActionMenu *>( KrActions::actUserMenu);
    if (userActionMenu) {
        userActionMenu->menu()->clear();

        userActionMenu->addAction(KrActions::actManageUseractions);
        userActionMenu->addSeparator();
        krUserAction->populateMenu(userActionMenu, nullptr);
        // Panero: no User Actions menu until the user defines one (Tools > Manage
        // User Actions, Settings > Advanced > User Actions) - D-022
        userActionMenu->setVisible(UserAction::userDefinedActions());
    }
}

const char* Krusader::appIconName() {
    if (geteuid())
        return PANERO_APP_ID;
    else
        return PANERO_APP_ID;
}

void Krusader::quit()
{
    _quit = true; // remember that we want to quit and not close to tray
    close(); // continues with closeEvent()...
}

void Krusader::moveToTop() {
    if (isHidden())
        show();

    KWindowSystem::forceActiveWindow(winId());
}

bool Krusader::isRunning() {
    moveToTop(); //FIXME - doesn't belong here
    return true;
}

bool Krusader::isLeftActive()  {
    return MAIN_VIEW->isLeftActive();
}

bool Krusader::openUrl(QString url)
{
    _urlToOpen = std::move(url);
    _openUrlTimer.start(0);
    return true;
}

void Krusader::doOpenUrl()
{
    QUrl url = QUrl::fromUserInput(_urlToOpen, QDir::currentPath(), QUrl::AssumeLocalFile);
    _urlToOpen.clear();
    int tab = ACTIVE_MNG->findTab(url);
    if(tab >= 0)
        ACTIVE_MNG->setActiveTab(tab);
    else if((tab = OTHER_MNG->findTab(url)) >= 0) {
        OTHER_MNG->setActiveTab(tab);
        OTHER_MNG->currentPanel()->view->widget()->setFocus();
    } else
        ACTIVE_MNG->slotNewTab(url);
}

void Krusader::slotGotNewStartup(const KStartupInfoId &id, const KStartupInfoData &data)
{
    Q_UNUSED(id)
    Q_UNUSED(data)
    // This is here to show busy mouse cursor when _other_ applications are launched, not for krusader itself.
    qApp->setOverrideCursor(Qt::BusyCursor);
}

void Krusader::slotGotRemoveStartup(const KStartupInfoId &id, const KStartupInfoData &data)
{
    Q_UNUSED(id)
    Q_UNUSED(data)
    qApp->restoreOverrideCursor();
}

KrView *Krusader::activeView()
{
    return ACTIVE_PANEL->view;
}

AbstractPanelManager *Krusader::activeManager()
{
    return MAIN_VIEW->activeManager();
}

AbstractPanelManager *Krusader::leftManager()
{
    return MAIN_VIEW->leftManager();
}

AbstractPanelManager *Krusader::rightManager()
{
    return MAIN_VIEW->rightManager();
}


void Krusader::setupPaneroToolbarLabels()
{
    // short labels for the text-beside-icon toolbars (menus keep the full texts)
    const QList<QPair<const char *, QString>> labels = {
        {"go_up", i18nc("toolbar", "Up")},
        {"go_home", i18nc("toolbar", "Home")},
        {"std_redisplay", i18nc("toolbar", "Refresh")},
        {"compare", i18nc("toolbar", "Compare")},
        {"pack", i18nc("toolbar", "Pack")},
        {"unpack", i18nc("toolbar", "Unpack")},
        {"find", i18nc("toolbar", "Search")},
        {"panero open trash", PANERO_TEXT(i18nc("toolbar", "Open Trash"), i18nc("toolbar", "Open Recycle Bin"))},
        {"panero trash menu", PANERO_TEXT(i18nc("toolbar", "Trash"), i18nc("toolbar", "Recycle Bin"))},
        {"panero toggle navigation", i18nc("toolbar", "Folder Tree")},
        {"toggle hidden files", i18nc("toolbar", "Hidden Files")},
        {"view0", i18nc("toolbar", "Details")},
        {"view2", i18nc("toolbar", "Thumbnails")},
        {"panero view colors", i18nc("toolbar", "Quick Settings")},
    };
    for (const auto &label : labels) {
        if (QAction *a = actionCollection()->action(QLatin1String(label.first)))
            a->setIconText(label.second);
    }
    // one segmented Details | Thumbnails control instead of two buttons
    auto *viewMode = new PaneroViewModeAction(actionCollection()->action(QStringLiteral("view0")),
                                              actionCollection()->action(QStringLiteral("view2")), this);
    actionCollection()->addAction(QStringLiteral("panero view mode"), viewMode);
    actionCollection()->addAction(QStringLiteral("panero toolbar spacer"), new PaneroToolBarSpacer(this));
    if (QAction *hidden = actionCollection()->action(QStringLiteral("toggle hidden files"))) {
        if (hidden->icon().isNull())
            hidden->setIcon(Icon(QStringLiteral("view-hidden")));
    }
}

void Krusader::restart()
{
    if (!App)
        return;
    // the new process waits until this one is gone (single instance mode would
    // otherwise hand the start over to this closing window); settings and tabs are
    // saved on quit and restored by the new process
    const QString exe = QCoreApplication::applicationFilePath();
    const QString pid = QString::number(QCoreApplication::applicationPid());
    connect(qApp, &QCoreApplication::aboutToQuit, qApp, [exe, pid]() {
        QProcess::startDetached(QStringLiteral("/bin/sh"),
                                {QStringLiteral("-c"),
                                 QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; exec \"$2\""),
                                 QStringLiteral("panero-restart"), pid, exe});
    });
    App->quit();
}

void Krusader::emergencySaveSettings()
{
    // Panero: skip while the main window is still starting up or already
    // being destroyed - saveSettings() needs the panels and toolbars
    if (App && !App->isStarting && MAIN_VIEW) {
        fprintf(stderr, "Emergency saving of settings...\n");
        App->saveSettings();
        fprintf(stderr, "...settings saved.\n");
    }
}
