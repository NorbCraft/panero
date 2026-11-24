/*
    SPDX-FileCopyrightText: 2003 Shie Erlich <erlich@users.sourceforge.net>
    SPDX-FileCopyrightText: 2003 Rafi Yanai <yanai@users.sourceforge.net>
    SPDX-FileCopyrightText: 2004-2022 Krusader Krew <https://krusader.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panelcontextmenu.h"
#include "../FileSystem/paneroslowpaths.h"
#include "../panerowording.h"
#include <QClipboard>
#include <QApplication>

// QtGui
#include <QPixmap>

#include <KCoreAddons/KProcess>
#include <KI18n/KLocalizedString>
#include <KIOWidgets/KRun>
#include <KWidgetsAddons/KMessageBox>
#include <kservice_version.h>
#include <KService/KApplicationTrader>
#include <KXmlGui/KActionCollection>
#include <KIOCore/KFileItem>
#include <KIOCore/KFileItemListProperties>
#include <KIOWidgets/KAbstractFileItemActionPlugin>
#include <KCoreAddons/KPluginMetaData>
#include <kio_version.h>

#include "krpreviewpopup.h"
#include "listpanel.h"
#include "listpanelactions.h"
#include "panelfunc.h"
#include "PanelView/krview.h"
#include "PanelView/krviewitem.h"
#include "../defaults.h"
#include "../icon.h"
#include "PanelView/commandergesture.h"
#include "../krservices.h"
#include "../krslots.h"
#include "../krusader.h"
#include "../krusaderview.h"
#include "../panelmanager.h"
#include "../Archive/krarchandler.h"
#include "../FileSystem/fileitem.h"
#include "../FileSystem/filesystem.h"
#include "../FileSystem/krtrashhandler.h"
#include "../MountMan/kmountman.h"
#include "../krglobal.h"
#ifndef Q_OS_WIN // cloud drives are Linux-only (rclone, FUSE)
#include "../Cloud/panerocloud.h"
#include "../Cloud/panerooffline.h"
#else
#include "../FileSystem/panerocloudfiles.h"
#include "../kractions.h"
#include "panerowinshellmenu.h"
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>
#endif
#include "../UserAction/useractionpopupmenu.h"
#include "../UserAction/useraction.h"

PanelContextMenu* PanelContextMenu::run(const QPoint &pos, KrPanel *panel, bool background)
{
#ifdef Q_OS_WIN
    // Panero (D-019): Explorer's menu; Panero's own one through its "Panero Menu" item
    if (runShellMenu(pos, panel, background))
        return nullptr;
#endif
    auto menu = new PanelContextMenu(panel, background);
    QAction * res = menu->exec(pos);
    int result = res && res->data().canConvert<int>() ?
                     res->data().toInt() :
                     -1;
    menu->performAction(result);
    return menu;
}

#if KIO_VERSION < QT_VERSION_CHECK(5, 79, 0)
/**
 * Copied from dolphin/src/dolphincontextmenu.cpp and modified to add only compress and extract submenus.
 * Only needed before KIO 5.79: since then KFileItemActions::addActionsTo() adds these plugin actions
 * together with all other file item actions.
 */
void PanelContextMenu::addCompressAndExtractPluginActions()
{
    KFileItemListProperties props(_items);

    QVector<KPluginMetaData> jsonPlugins = KPluginLoader::findPlugins("kf5/kfileitemaction",
                                                                      [=](const KPluginMetaData& metaData) {
        return metaData.pluginId() == "compressfileitemaction" || metaData.pluginId() == "extractfileitemaction";
    });

    foreach (const KPluginMetaData &jsonMetadata, jsonPlugins) {
        auto* abstractPlugin = KPluginLoader(jsonMetadata.fileName())
                                                            .factory()->create<KAbstractFileItemActionPlugin>();
        if (abstractPlugin) {
            abstractPlugin->setParent(this);
            addActions(abstractPlugin->actions(props, this));
        }
    }
}
#endif

PanelContextMenu::PanelContextMenu(KrPanel *krPanel, bool background, QWidget *parent)
    : QMenu(parent), panel(krPanel)
{
    // selected file names: the marked files, or the current file if nothing is marked.
    // The background (empty space) menu never targets files, even if some are marked.
    QStringList marked;
    panel->view->getSelectedItems(&marked, false);
    const QStringList fileNames =
        CommanderTargets::contextMenuTargets(background, marked, panel->view->getCurrentItem());
    const bool targetIsMarkedSet = !background && !marked.isEmpty();

    // file items
    QList<FileItem*> files;
    for (const QString& fileName : fileNames) {
        files.append(panel->func->files()->getFileItem(fileName));
    }

    // KFileItems
    bool allFilesAreDirs = true;
    for (FileItem *file : files) {
        _items.append(KFileItem(file->getUrl(), file->getMime(), file->getMode()));
        allFilesAreDirs &= file->isDir();
    }

    if (files.empty()) {
        addCreateNewMenu();
        addSeparator();
        addEmptyMenuEntries();
        return;
    }

    const bool multipleSelections = files.size() > 1;

    // Panero: state the actual target set when it comes from the marks, so a
    // menu opened on an unmarked row never silently acts on other (marked) files.
    if (targetIsMarkedSet) {
        QAction *header = addAction(Icon("checkbox"),
                                    i18np("Marked: %1 item", "Marked: %1 items", files.size()));
        header->setEnabled(false);
        addSeparator();
    }

    QSet<QString> protocols;
    for (FileItem *file : files) {
        protocols.insert(file->getUrl().scheme());
    }
    const bool inTrash = protocols.contains("trash");
    const bool trashOnly = inTrash && protocols.count() == 1;

    FileItem *file = files.first();

    // ------------ the OPEN/BROWSE option - open preferred service

    // open/run - if not only multiple dirs are selected
    if (!(multipleSelections && allFilesAreDirs)) {
        QAction *openAction = new QAction(this);
        openAction->setData(QVariant(OPEN_ID));
        if (multipleSelections) {
            openAction->setText(i18n("Open/Run Files"));
        } else {
            openAction->setText(file->isExecutable() && !file->isDir() ?
                                    i18n("Run") : i18n("Open"));
            const KrViewItemList viewItems = panel->view->getSelectedKrViewItems();
            openAction->setIcon(viewItems.first()->icon());
        }
        addAction(openAction);
    }

    // open in a new tab (if only folder(s) are selected)
    if (allFilesAreDirs) {
        QAction *openTab = addAction(multipleSelections ? i18n("Open in New Tabs") : i18n("Open in New Tab"));
        openTab->setData(QVariant(OPEN_TAB_ID));
        openTab->setIcon(Icon("tab-new"));
    }

    // browse archive - if one file is selected and the file can be browsed as archive...
    if (!multipleSelections && !panel->func->browsableArchivePath(file->getName()).isEmpty()
        // ...but user disabled archive browsing...
        && (!KConfigGroup(krConfig, "Archives").readEntry("ArchivesAsDirectories", _ArchivesAsDirectories)
            // ...or the file is not a standard archive (e.g. odt, docx, etc.)...
            || !KrArcHandler::arcSupported(file->getMime()))) {
        // ...it will not be browsed as a directory by default, but add an option for it
        QAction *browseAct = addAction(i18n("Browse Archive"));
        browseAct->setData(QVariant(BROWSE_ID));
        browseAct->setIcon(Icon("archive-insert-directory"));
    }

    // ------------- Preview - local filesystem only ?
    if (panel->func->files()->isLocal()) {
        // create the preview popup
        KrPreviewPopup *preview = new KrPreviewPopup(this);
        preview->setUrls(panel->func->files()->getUrls(fileNames));
        QAction *previewAction = addMenu(preview);
        previewAction->setData(QVariant(PREVIEW_ID));
        previewAction->setText(i18n("Preview"));
        previewAction->setIcon(Icon("document-print-preview"));
    }

    // -------------- Open with: try to find-out which apps can open the file
    QSet<QString> uniqueMimeTypes;
    for (FileItem *file : files)
        uniqueMimeTypes.insert(file->getMime());
    const QStringList mimeTypes = uniqueMimeTypes.values();

#if KSERVICE_VERSION >= QT_VERSION_CHECK(5, 83, 0)
    offers = mimeTypes.count() == 1 ?
                 KApplicationTrader::queryByMimeType(mimeTypes.first()) :
                 KFileItemActions::associatedApplications(mimeTypes);
#elif KSERVICE_VERSION >= QT_VERSION_CHECK(5, 68, 0)
    offers = mimeTypes.count() == 1 ?
                 KApplicationTrader::queryByMimeType(mimeTypes.first()) :
                 KFileItemActions::associatedApplications(mimeTypes, QString());
#else
    offers = mimeTypes.count() == 1 ?
                 KMimeTypeTrader::self()->query(mimeTypes.first()) :
                 KFileItemActions::associatedApplications(mimeTypes, QString());
#endif

    if (!offers.isEmpty()) {
        auto *openWithMenu = new QMenu(this);
        for (int i = 0; i < offers.count(); ++i) {
            QExplicitlySharedDataPointer<KService> service = offers[i];
            if (service->isValid() && service->isApplication()) {
                openWithMenu->addAction(Icon(service->icon()),
                                   service->name())->setData(QVariant(SERVICE_LIST_ID + i));
            }
        }
        openWithMenu->addSeparator();
        if (!multipleSelections && file->isDir()) {
            openWithMenu->addAction(Icon("utilities-terminal"),
                               i18n("Terminal"))->setData(QVariant(OPEN_TERM_ID));
        }
        openWithMenu->addAction(i18n("Other..."))->setData(QVariant(CHOOSE_ID));
        QAction *openWithAction = addMenu(openWithMenu);
        openWithAction->setText(i18n("Open With"));
        openWithAction->setIcon(Icon("document-open"));
    }

    addSeparator();

    // --------------- user actions
    // (Panero: only once the user has defined user actions, D-022)
    if (UserAction::userDefinedActions()) {
        QAction *userAction = new UserActionPopupMenu(file->getUrl(), this);
        userAction->setText(i18n("User Actions"));
        addAction(userAction);
    }

    // --------------- compress/extract actions
    // --------------- KDE file item actions
    // workaround for Bug 372999: application freezes very long time if many files are selected
    // Panero: not on cloud/network drives - KFileItemListProperties checks every
    // item's permissions with a stat() in the GUI thread, which waits for the
    // drive (a frozen window while rclone is busy, D-018)
    const bool onSlowDrive = file->getUrl().isLocalFile() && PaneroSlowPaths::isSlowPath(file->getUrl().toLocalFile());
    if (_items.length() < 1000 && !onSlowDrive) {
#if KIO_VERSION < QT_VERSION_CHECK(5, 79, 0)
        // add compress and extract plugins (if available); newer KIO adds them in addActionsTo()
        addCompressAndExtractPluginActions();
#endif
        // NOTE: design and usability problem here. Services disabled in kservicemenurc settings won't
        // be added to the menu. But Krusader does not provide a way do change these settings (only
        // Dolphin does).
        auto *fileItemActions = new KFileItemActions(this);
        fileItemActions->setItemListProperties(KFileItemListProperties(_items));
        fileItemActions->setParentWidget(MAIN_VIEW);
        const int before = actions().count();
#if KIO_VERSION >= QT_VERSION_CHECK(5, 79, 0)
        fileItemActions->addActionsTo(this);
#else
        fileItemActions->addServiceActionsTo(this);
#endif
        // Panero: Panero has its own Copy Path; installed service menus doing
        // the same would show it twice (D-019)
        hideCopyPathDuplicates(actions().mid(before));
    }

    addSeparator();

    // ------------- 'create new' submenu
    addCreateNewMenu();
    addSeparator();

    // ---------- COPY
    addAction(panel->gui->actions()->actCopyF5);
    // ------- MOVE
    addAction(panel->gui->actions()->actMoveF6);
    // ------- RENAME - only one file
    if (!multipleSelections && !inTrash) {
        addAction(panel->gui->actions()->actRenameF2);
    }

    // -------- MOVE TO TRASH
    if (KConfigGroup(krConfig, "General").readEntry("Move To Trash", _MoveToTrash)
        && panel->func->files()->canMoveToTrash(fileNames)) {
        addAction(Icon("user-trash"),
                  PANERO_TEXT(i18n("Move to Trash"), i18n("Move to Recycle Bin")))->setData(QVariant(TRASH_ID));
    }
    // -------- DELETE
    addAction(Icon("edit-delete"),
              i18n("Delete"))->setData(QVariant(DELETE_ID));
    // -------- SHRED - only one file
    /*      if ( panel->func->files() ->getType() == filesystem:fileSystemM_NORMAL &&
                !fileitem->isDir() && !multipleSelections )
             addAction( i18n( "Shred" ) )->setData( QVariant( SHRED_ID ) );*/

    // ---------- link handling
    // create new shortcut or redirect links - only on local directories:
    if (panel->func->files()->isLocal()) {
        addSeparator();
        auto *linkMenu = new QMenu(this);
        linkMenu->addAction(i18n("New Symlink..."))->setData(QVariant(NEW_SYMLINK_ID));
        linkMenu->addAction(i18n("New Hardlink..."))->setData(QVariant(NEW_LINK_ID));
        if (file->isSymLink()) {
            linkMenu->addAction(i18n("Redirect Link..."))->setData(QVariant(REDIRECT_LINK_ID));
        }
        QAction *linkAction = addMenu(linkMenu);
        linkAction->setText(i18n("Link Handling"));
        linkAction->setIcon(Icon("insert-link"));
    }
    addSeparator();

    // ---------- calculate space
    if (panel->func->files()->isLocal() && (file->isDir() || multipleSelections))
        addAction(panel->gui->actions()->actCalculate);

    // ---------- mount/umount/eject
    if (panel->func->files()->isLocal() && file->isDir() && !multipleSelections) {
        const QString selectedDirectoryPath = file->getUrl().path();
        if (krMtMan.getStatus(selectedDirectoryPath) == KMountMan::MOUNTED)
            addAction(i18n("Unmount"))->setData(QVariant(UNMOUNT_ID));
        else if (krMtMan.getStatus(selectedDirectoryPath) == KMountMan::NOT_MOUNTED)
            addAction(i18n("Mount"))->setData(QVariant(MOUNT_ID));
        if (krMtMan.ejectable(selectedDirectoryPath))
            addAction(i18n("Eject"))->setData(QVariant(EJECT_ID));
    }

    // --------- send by mail
    if (KrServices::supportedTools().contains("MAIL") && !file->isDir()) {
        addAction(Icon("mail-send"),
                  i18n("Send by Email"))->setData(QVariant(SEND_BY_EMAIL_ID));
    }

    // --------- empty trash
    if (trashOnly) {
        addAction(i18n("Restore"))->setData(QVariant(RESTORE_TRASHED_FILE_ID));
        addAction(PANERO_TEXT(i18n("Empty Trash"), i18n("Empty Recycle Bin")))->setData(QVariant(EMPTY_TRASH_ID));
    }

#ifdef SYNCHRONIZER_ENABLED
    // --------- synchronize
    if (panel->view->numSelected()) {
        addAction(i18n("Synchronize Selected Files..."))->setData(QVariant(SYNC_SELECTED_ID));
    }
#endif

    // --------- copy/paste
    addSeparator();
#ifndef Q_OS_WIN
    // Panero: offline files of cloud drives (R10), for files and folders
    {
        PaneroOffline *offline = PaneroOffline::instance();
        int inDrive = 0, inMirror = 0;
        for (FileItem *f : files) {
            QString remote, rel;
            bool mirror = false;
            if (f->getUrl().isLocalFile() && offline->cloudPath(f->getUrl().toLocalFile(), &remote, &rel, &mirror) && !rel.isEmpty())
                ++(mirror ? inMirror : inDrive);
        }
        if (inDrive > 0 && inDrive == files.size()) {
            addAction(Icon("folder-download"), i18n("Make Available Offline"))->setData(QVariant(CLOUD_AVAILABLE_ID));
            addAction(Icon("edit-clear"), i18n("Free Up Space"))->setData(QVariant(CLOUD_FREE_ID));
        }
        if (!multipleSelections && (inDrive + inMirror) == 1) {
            QString remote, rel;
            bool mirror = false;
            offline->cloudPath(file->getUrl().toLocalFile(), &remote, &rel, &mirror);
            const QString pin = offline->pinFor(remote, rel);
            if (file->isDir() && pin.isNull() && !mirror) {
                addAction(Icon("folder-sync"), i18n("Always Keep on This Device (Synced Copy)"))->setData(QVariant(CLOUD_KEEP_ID));
            } else if (file->isDir() && pin == rel) {
                addAction(Icon("view-refresh"), i18n("Synchronise Now"))->setData(QVariant(CLOUD_SYNC_ID));
                addAction(Icon("edit-clear"), i18n("Stop Keeping on This Device"))->setData(QVariant(CLOUD_UNPIN_ID));
            }
            if (!file->isDir() && PaneroCloud::instance()->mayBeGoogleDocument(file->getUrl().toLocalFile(), qint64(file->getSize()))) {
                addAction(Icon("internet-web-browser"), i18n("Open in Google Docs"))->setData(QVariant(CLOUD_GDOC_OPEN_ID));
                addAction(Icon("document-save-as"), i18n("Save a Copy as Office File in Other Panel"))->setData(QVariant(CLOUD_GDOC_SAVE_ID));
            }
        }
        if (inDrive + inMirror > 0)
            addSeparator();
    }

#else
    // Panero (D-020): OneDrive & co. on Windows, as in Explorer: the PINNED /
    // UNPINNED attributes, which the provider acts on (download / free up)
    {
        int inCloud = 0, pinned = 0;
        for (FileItem *f : files) {
            const QString path = f->getUrl().isLocalFile() ? f->getUrl().toLocalFile() : QString();
            PaneroCloudFiles::SyncRoot root;
            if (!path.isEmpty() && PaneroCloudFiles::rootFor(path, &root) && !root.virtualDrive && PaneroCloudFiles::isInSyncRoot(path)) {
                ++inCloud;
                if (PaneroCloudFiles::attributes(path) & PaneroCloudFiles::AttrPinned)
                    ++pinned;
            }
        }
        if (inCloud > 0 && inCloud == files.size()) {
            QAction *keep = addAction(Icon("folder-download"), i18n("Always Keep on This Device"));
            keep->setData(QVariant(CLOUD_KEEP_ID));
            keep->setCheckable(true);
            keep->setChecked(pinned == inCloud);
            addAction(Icon("edit-clear"), i18n("Free Up Space"))->setData(QVariant(CLOUD_FREE_ID));
            addSeparator();
        }
    }
#endif

    addAction(panel->gui->actions()->actCut);
    addAction(panel->gui->actions()->actCopy);
    addAction(panel->gui->actions()->actPaste);
    // Panero: full path(s) as text, and a terminal right here
    addAction(Icon("edit-copy-path"), multipleSelections ? i18n("Copy Paths") : i18n("Copy Path"))
        ->setData(QVariant(COPY_PATH_ID));
    if (panel->func->files()->isLocal())
        addAction(Icon("utilities-terminal"), i18n("Open Terminal Here"))->setData(QVariant(TERMINAL_HERE_ID));
    addSeparator();

    // --------- properties
    addAction(panel->gui->actions()->actProperties);
}

bool PanelContextMenu::isCopyPathText(const QString &text)
{
    QString t = text;
    t.remove(QLatin1Char('&'));
    t = t.simplified().toLower();
    while (t.endsWith(QLatin1Char('.')) || t.endsWith(QChar(0x2026)))
        t.chop(1);
    static const QStringList same{QStringLiteral("copy path"), QStringLiteral("copy paths"), QStringLiteral("copy location"),
                                  QStringLiteral("copy full path"), QStringLiteral("copy file path"), QStringLiteral("copy file paths"),
                                  QStringLiteral("copy path to clipboard"), QStringLiteral("copy file path to clipboard")};
    return same.contains(t.trimmed());
}

bool PanelContextMenu::hideCopyPathDuplicates(const QList<QAction *> &added)
{
    bool anyVisible = false;
    for (QAction *a : added) {
        if (a->isSeparator()) {
            continue;
        }
        if (QMenu *sub = a->menu()) {
            // KFileItemActions puts most service menus into an "Actions" submenu
            if (!hideCopyPathDuplicates(sub->actions()))
                a->setVisible(false);
        } else if (isCopyPathText(a->text())) {
            a->setVisible(false);
        }
        anyVisible = anyVisible || a->isVisible();
    }
    return anyVisible;
}

void PanelContextMenu::addEmptyMenuEntries()
{
    addAction(panel->gui->actions()->actPaste);
    // Panero: the folder's own path, and a terminal in it
    addAction(Icon("edit-copy-path"), i18n("Copy Path"))->setData(QVariant(COPY_PATH_ID));
    if (panel->func->files()->isLocal())
        addAction(Icon("utilities-terminal"), i18n("Open Terminal Here"))->setData(QVariant(TERMINAL_HERE_ID));
}

void PanelContextMenu::addCreateNewMenu()
{
    auto *createNewMenu = new QMenu(this);
    createNewMenu->addAction(panel->gui->actions()->actNewFolderF7);
    createNewMenu->addAction(panel->gui->actions()->actNewFileShiftF4);

    QAction *newMenuAction = addMenu(createNewMenu);
    newMenuAction->setText(i18n("Create New"));
    newMenuAction->setIcon(Icon("document-new"));
}

void PanelContextMenu::performAction(int id)
{
    const QUrl singleURL = _items.isEmpty() ? QUrl() : _items.first().url();

    switch (id) {
    case - 1 : // the user clicked outside of the menu
        return;
    case OPEN_TAB_ID :
        for (const KFileItem &fileItem : _items) {
            panel->manager()->duplicateTab(fileItem.url(), panel);
        }
        break;
    case OPEN_ID :
            for (const KFileItem &fileItem : _items) {
                // do not open dirs if multiple files are selected
                if (_items.size() == 1 || !fileItem.isDir()) {
                    panel->func->execute(fileItem.name());
                }
            }
        break;
    case BROWSE_ID :
        panel->func->goInside(singleURL.fileName());
        break;
    case TRASH_ID :
        panel->func->deleteFiles(true);
        break;
    case DELETE_ID :
        panel->func->deleteFiles(false);
        break;
    case EJECT_ID :
        krMtMan.eject(singleURL.adjusted(QUrl::StripTrailingSlash).path());
        break;
    case CHOOSE_ID : // open-with dialog
        panel->func->displayOpenWithDialog(_items.urlList());
        break;
    case MOUNT_ID :
        krMtMan.mount(singleURL.adjusted(QUrl::StripTrailingSlash).path());
        break;
    case NEW_LINK_ID :
        panel->func->krlink(false);
        break;
    case NEW_SYMLINK_ID :
        panel->func->krlink(true);
        break;
    case REDIRECT_LINK_ID :
        panel->func->redirectLink();
        break;
    case EMPTY_TRASH_ID :
        KrTrashHandler::emptyTrash();
        break;
    case RESTORE_TRASHED_FILE_ID :
        KrTrashHandler::restoreTrashedFiles(_items.urlList());
    break;
    case UNMOUNT_ID :
        krMtMan.unmount(singleURL.adjusted(QUrl::StripTrailingSlash).path());
        break;
    case SEND_BY_EMAIL_ID : {
        SLOTS->sendFileByEmail(_items.urlList());
        break;
    }
#ifdef SYNCHRONIZER_ENABLED
    case SYNC_SELECTED_ID : {
        QStringList selectedNames;
        for (const KFileItem& item : _items) {
            selectedNames.append(item.name());
        }
        const KrViewItemList otherItems = panel->otherPanel()->view->getSelectedKrViewItems();
        for (KrViewItem *otherItem : otherItems) {
            const QString& name = otherItem->name();
            if (!selectedNames.contains(name)) {
                selectedNames.append(name);
            }
        }
        SLOTS->slotSynchronizeDirs(selectedNames);
    }
    break;
#endif
    case OPEN_TERM_ID :
        SLOTS->runTerminal(singleURL.isLocalFile() ? singleURL.toLocalFile() : singleURL.path());
        break;
    case COPY_PATH_ID : {
        QStringList paths;
        const QList<QUrl> urls = _items.isEmpty() ? QList<QUrl>{panel->virtualPath()} : _items.urlList();
        for (const QUrl &url : urls)
#ifdef Q_OS_WIN // backslashes, as Windows writes paths
            paths << (url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile())
                                        : url.toDisplayString(QUrl::PreferLocalFile | QUrl::StripTrailingSlash));
#else
            paths << url.toDisplayString(QUrl::PreferLocalFile | QUrl::StripTrailingSlash);
#endif
        QApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
        break;
    }
#ifdef Q_OS_WIN
    case CLOUD_KEEP_ID :
    case CLOUD_FREE_ID : {
        QStringList paths;
        for (const KFileItem &item : _items) {
            if (item.url().isLocalFile())
                paths << item.url().toLocalFile();
        }
        setCloudPin(panel, paths, id == CLOUD_FREE_ID);
        break;
    }
#else
    case CLOUD_AVAILABLE_ID : {
        QStringList paths;
        for (const KFileItem &item : _items)
            if (item.url().isLocalFile())
                paths << item.url().toLocalFile();
        // not the menu as parent: it is gone when the download runs
        PaneroOffline::instance()->makeAvailableOffline(krMainWindow, paths);
        break;
    }
    case CLOUD_FREE_ID : {
        QStringList paths;
        for (const KFileItem &item : _items)
            if (item.url().isLocalFile())
                paths << item.url().toLocalFile();
        const QString summary = PaneroOffline::instance()->freeUpCache(paths);
        panel->func->refresh();
        KMessageBox::information(krMainWindow, summary + QLatin1String("\n\n")
                                     + i18n("The files stay in the cloud and are downloaded again when you open them."),
                                 i18n("Free Up Space"));
        break;
    }
    case CLOUD_GDOC_OPEN_ID :
        if (singleURL.isLocalFile() && !PaneroCloud::instance()->openGoogleDocument(krMainWindow, singleURL.toLocalFile(), 0))
            KMessageBox::information(krMainWindow, i18n("This is not a Google Docs, Sheets or Slides file."), i18n("Open in Google Docs"));
        break;
    case CLOUD_GDOC_SAVE_ID :
        if (singleURL.isLocalFile() && panel->otherPanel()->virtualPath().isLocalFile()
            && PaneroCloud::instance()->saveGoogleDocumentCopy(krMainWindow, singleURL.toLocalFile(),
                                                                panel->otherPanel()->virtualPath().toLocalFile()))
            panel->otherPanel()->func->refresh();
        break;
    case CLOUD_KEEP_ID :
    case CLOUD_UNPIN_ID :
    case CLOUD_SYNC_ID : {
        QString remote, rel;
        PaneroOffline *offline = PaneroOffline::instance();
        if (!singleURL.isLocalFile() || !offline->cloudPath(singleURL.toLocalFile(), &remote, &rel))
            break;
        if (id == CLOUD_KEEP_ID) {
            QString error;
            if (!offline->pin(remote, rel, &error))
                KMessageBox::error(krMainWindow, error, i18n("Always Keep on This Device"));
        } else if (id == CLOUD_SYNC_ID) {
            offline->syncNow(remote, rel);
        } else {
            const auto answer = KMessageBox::questionTwoActions(
                krMainWindow, i18n("<p>Stop keeping <b>%1</b> on this device?</p><p>Changes are uploaded first, then the synced copy is "
                                   "deleted. The folder stays in the cloud.</p>", rel.toHtmlEscaped()),
                i18n("Stop Keeping on This Device"), KGuiItem(i18n("Stop Keeping"), QStringLiteral("edit-clear")), KStandardGuiItem::cancel());
            if (answer == KMessageBox::PrimaryAction)
                offline->unpin(krMainWindow, remote, rel);
        }
        panel->func->refresh();
        break;
    }
#endif
    case TERMINAL_HERE_ID : {
        // a folder opens there, a file in the folder it is in
        QString dir = panel->virtualPath().toLocalFile();
        if (_items.count() == 1 && _items.first().isDir() && _items.first().url().isLocalFile())
            dir = _items.first().url().toLocalFile();
        SLOTS->runTerminal(dir);
        break;
    }
    }

    // check if something from the open-with-offered-services was selected
    if (id >= SERVICE_LIST_ID) {
        const QStringList names = panel->gui->getSelectedNames();
        panel->func->runService(*(offers[ id - SERVICE_LIST_ID ]),
                                panel->func->files()->getUrls(names));
    }
}

#ifdef Q_OS_WIN
void PanelContextMenu::setCloudPin(KrPanel *panel, const QStringList &paths, bool freeUp)
{
    // "Always keep" is a check box, as in Explorer: unchecking clears the pin
    bool allPinned = !paths.isEmpty();
    for (const QString &path : paths)
        allPinned &= bool(PaneroCloudFiles::attributes(path) & PaneroCloudFiles::AttrPinned);
    const PaneroCloudFiles::Pin pin = freeUp ? PaneroCloudFiles::Pin::FreeUp
        : allPinned                          ? PaneroCloudFiles::Pin::Clear
                                             : PaneroCloudFiles::Pin::AlwaysKeep;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const int changed = PaneroCloudFiles::setPinState(paths, pin, &error);
    QApplication::restoreOverrideCursor();
    if (changed < paths.size())
        KMessageBox::error(krMainWindow, i18n("Could not change %1.", error),
                           freeUp ? i18n("Free Up Space") : i18n("Always Keep on This Device"));
    panel->func->refresh();
}

namespace
{
enum ShellExtraId : unsigned {
    ExtraCopy = PaneroWinShell::MenuExtraFirst,
    ExtraMove,
    ExtraCopyPath,
    ExtraTerminal,
    ExtraCloudKeep,
    ExtraCloudFree,
    ExtraPaneroMenu,
};

PaneroWinShell::MenuExtra extraFor(unsigned id, QAction *action, const QIcon &icon)
{
    PaneroWinShell::MenuExtra e;
    e.id = id;
    e.text = action->text().remove(QLatin1Char('&'));
    e.shortcut = action->shortcut().toString(QKeySequence::NativeText);
    e.icon = action->icon().isNull() ? icon : action->icon();
    return e;
}

PaneroWinShell::MenuExtra extra(unsigned id, const QString &text, const QIcon &icon)
{
    PaneroWinShell::MenuExtra e;
    e.id = id;
    e.text = text;
    e.icon = icon;
    return e;
}
} // namespace

bool PanelContextMenu::runShellMenu(const QPoint &pos, KrPanel *panel, bool background)
{
    if (!PaneroWinShell::explorerMenuEnabled()) // Quick Settings > Right-click menu
        return false;
    // only items of a local folder (not archives, the Recycle Bin, search results, FTP, ...)
    const QUrl dir = panel->virtualPath();
    if (!dir.isLocalFile() || !panel->func->files()->isLocal())
        return false;
    const QString folder = dir.toLocalFile();
    QStringList marked;
    panel->view->getSelectedItems(&marked, false);
    const QStringList names = CommanderTargets::contextMenuTargets(background, marked, panel->view->getCurrentItem());
    QStringList paths;
    bool allDirs = !names.isEmpty();
    for (const QString &name : names) {
        FileItem *file = panel->func->files()->getFileItem(name);
        if (!file || !file->getUrl().isLocalFile())
            return false;
        const QString path = file->getUrl().toLocalFile();
        if (QDir::cleanPath(QFileInfo(path).absolutePath()).compare(QDir::cleanPath(folder), Qt::CaseInsensitive) != 0)
            return false;
        paths << path;
        allDirs = allDirs && file->isDir();
    }

    QPointer<ListPanel> gui = panel->gui;
    const bool shift = QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier;
    PaneroWinShell::ShellMenu menu(panel->gui, folder, names, shift);
    if (!menu.isValid()) {
        qWarning() << "Explorer menu not available:" << menu.error();
        return false;
    }

    // Panero's few items on top: F5, F6, Copy Path, terminal, the offline actions, its own menu
    QList<PaneroWinShell::MenuExtra> extras;
    ListPanelActions *actions = panel->gui->actions();
    if (!names.isEmpty())
        extras << extraFor(ExtraCopy, actions->actCopyF5, Icon("edit-copy")) << extraFor(ExtraMove, actions->actMoveF6, Icon("go-next"));
    extras << extra(ExtraCopyPath, names.size() > 1 ? i18n("Copy Paths") : i18n("Copy Path"), Icon("edit-copy-path"));
    extras << extra(ExtraTerminal, i18n("Open Terminal Here"), Icon("utilities-terminal"));
    int inCloud = 0, pinned = 0;
    bool knownProvider = false;
    for (const QString &path : qAsConst(paths)) {
        PaneroCloudFiles::SyncRoot root;
        if (PaneroCloudFiles::rootFor(path, &root) && !root.virtualDrive && PaneroCloudFiles::isInSyncRoot(path)) {
            ++inCloud;
            // their shell extensions add the offline actions, in every language
            if (root.provider == QLatin1String("OneDrive") || root.provider == QLatin1String("Dropbox"))
                knownProvider = true;
            if (PaneroCloudFiles::attributes(path) & PaneroCloudFiles::AttrPinned)
                ++pinned;
        }
    }
    // only when the provider's shell extension has no such items of its own: none
    // in the menu, and not a provider known to add them (Panero Menu still has them)
    if (!paths.isEmpty() && inCloud == paths.size() && !knownProvider && !menu.hasProviderOfflineItems()) {
        PaneroWinShell::MenuExtra keep = extra(ExtraCloudKeep, i18n("Always Keep on This Device"), QIcon());
        keep.checkable = true;
        keep.checked = pinned == inCloud;
        extras << keep << extra(ExtraCloudFree, i18n("Free Up Space"), Icon("edit-clear"));
    }
    extras << extra(ExtraPaneroMenu, i18n("Panero Menu"), Icon(Krusader::appIconName()));
    menu.addExtras(extras);

    // opened by a held right button: at the pointer, so that the release lands on
    // the menu's corner and chooses nothing
    const QPoint at = (QGuiApplication::mouseButtons() & Qt::RightButton) ? QCursor::pos() : pos;
    const PaneroWinShell::ShellMenu::Choice choice = menu.exec(at);
    if (!gui) // the panel went away while the menu was open
        return true;
    if (!choice.shell) {
        switch (choice.extra) {
        case 0: // closed without a choice
            return true;
        case ExtraPaneroMenu:
            return false;
        case ExtraCopy:
            actions->actCopyF5->trigger();
            return true;
        case ExtraMove:
            actions->actMoveF6->trigger();
            return true;
        case ExtraCopyPath: {
            QStringList list;
            for (const QString &path : paths.isEmpty() ? QStringList{folder} : paths)
                list << QDir::toNativeSeparators(path);
            QApplication::clipboard()->setText(list.join(QLatin1Char('\n')));
            return true;
        }
        case ExtraTerminal:
            SLOTS->runTerminal(paths.size() == 1 && allDirs ? paths.first() : folder);
            return true;
        case ExtraCloudKeep:
        case ExtraCloudFree:
            setCloudPin(panel, paths, choice.extra == ExtraCloudFree);
            return true;
        default:
            return true;
        }
    }

    // the shell's items: a few are done Panero's way
    if (choice.verb == QLatin1String("rename")) {
        // in place (F2), as Explorer does; several items: Multi-Rename
        if (names.size() == 1) {
            panel->view->setCurrentItem(names.first());
            actions->actRenameF2->trigger();
        } else if (KrActions::actMultiRename) {
            KrActions::actMultiRename->trigger();
        }
        return true;
    }
    if (choice.verb == QLatin1String("open") && names.size() == 1 && allDirs) {
        panel->func->execute(names.first()); // a folder opens in the panel, not in Explorer
        return true;
    }
    QString error;
    if (!menu.invoke(&error))
        qWarning() << "Explorer menu command failed:" << choice.text << choice.verb << error;
    // the command may have changed the folder (Delete, New, Paste, an extension's action, ...)
    panel->func->refresh();
    for (int delay : {700, 2500}) {
        QTimer::singleShot(delay, gui.data(), [gui]() {
            if (gui)
                gui->func->refresh();
        });
    }
    return true;
}
#endif
