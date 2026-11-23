/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneronavigation.h"
#ifndef Q_OS_WIN // cloud drives are Linux-only (rclone, FUSE); Windows: preview 12
#include "Cloud/panerocloud.h"
#include "Cloud/paneroaddclouddialog.h"
#include "Cloud/panerooffline.h"
#include "Cloud/paneroofflinedialog.h"
#include "Cloud/paneroconnectserverdialog.h"
#endif
#include "Panel/paneroappearance.h"

#include <QSignalBlocker>

#include "icon.h"
#include "krglobal.h"
#include "krusaderview.h"
#include "Panel/krfiletreeview.h"
#include "Panel/krpanel.h"
#include "Panel/listpanel.h"
#include "Panel/panelfunc.h"
#ifdef Q_OS_WIN
#include "FileSystem/panerocloudfiles.h"
#include "Panel/panerowinshell.h"
#endif
#include "MountMan/kmountman.h"

#include <memory>

#include <QDir>
#include <QFileInfo>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QActionGroup>
#include <QDropEvent>
#include <QMimeData>
#include <QSplitter>
#include <QTimer>

#include <KCoreAddons/KUrlMimeData>
#include <QLabel>
#include <QListWidget>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

#include <KConfigCore/KConfigGroup>
#include <KI18n/KLocalizedString>
#include <KWidgetsAddons/KMessageBox>
#include <Solid/Device>
#include <Solid/StorageAccess>
#include <Solid/StorageVolume>

namespace
{
const char *NAV_GROUP = "Panero Navigation";
const char *PIN_GROUP = "Panero Pins";
const int UrlRole = Qt::UserRole + 1;
const int UdiRole = Qt::UserRole + 2;
const int DriveLabelRole = Qt::UserRole + 3;

QString iconForUrl(const QUrl &url)
{
    if (!url.isLocalFile())
        return url.scheme() == QLatin1String("trash") ? QStringLiteral("user-trash") : QStringLiteral("folder-network");
    const QString path = url.toLocalFile();
    if (QDir::cleanPath(path) == QDir::homePath())
        return QStringLiteral("user-home");
    if (path == QLatin1String("/"))
        return QStringLiteral("drive-harddisk-root");
    return QStringLiteral("folder");
}

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    return label;
}
} // namespace

namespace
{
QPointer<PaneroNavigationDock> s_navigationDock;
}

PaneroNavigationDock *PaneroNavigationDock::instance()
{
    return s_navigationDock.data();
}

PaneroNavigationDock::PaneroNavigationDock(QWidget *parent)
    : QDockWidget(i18n("Navigation"), parent)
{
    setObjectName(QStringLiteral("panero_navigation_dock"));
    s_navigationDock = this;
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(4, 2, 2, 2);
    layout->setSpacing(3);

    // pinned folders and folder tree share a vertical splitter (drag the handle to resize)
    m_splitter = new QSplitter(Qt::Vertical, content);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);
    auto *pinSection = new QWidget(m_splitter);
    auto *pinLayout = new QVBoxLayout(pinSection);
    pinLayout->setContentsMargins(0, 0, 0, 0);
    pinLayout->setSpacing(2);
    auto *pinHeader = new QHBoxLayout;
    pinHeader->setSpacing(2);
    pinHeader->addWidget(sectionLabel(i18n("Pinned"), pinSection));
    pinHeader->addStretch();
    m_pinButton = new QToolButton(pinSection);
    m_pinButton->setIcon(Icon(QStringLiteral("list-add")));
    m_pinButton->setAutoRaise(true);
    m_pinButton->setToolTip(i18n("Pin the active panel's folder (or drop folders on the list)"));
    m_pinButton->setAccessibleName(i18n("Pin current folder"));
    connect(m_pinButton, &QToolButton::clicked, this, &PaneroNavigationDock::pinCurrentFolder);
    pinHeader->addWidget(m_pinButton);
    pinLayout->addLayout(pinHeader);

    m_pins = new QListWidget(pinSection);
    m_pins->setAccessibleName(i18n("Pinned folders"));
    m_pins->setContextMenuPolicy(Qt::CustomContextMenu);
    m_pins->setUniformItemSizes(true);
    m_pins->setIconSize(QSize(KrFileTreeView::IconSize, KrFileTreeView::IconSize));
    m_pins->setMinimumHeight(40);
    m_pins->setFrameShape(QFrame::NoFrame);
    // folders dragged from the panels (or another file manager) are pinned
    m_pins->setAcceptDrops(true);
    m_pins->viewport()->setAcceptDrops(true);
    m_pins->setDropIndicatorShown(true);
    m_pins->viewport()->installEventFilter(this);
    connect(m_pins, &QListWidget::itemActivated, this, &PaneroNavigationDock::openPin);
    connect(m_pins, &QListWidget::itemClicked, this, &PaneroNavigationDock::openPin);
    connect(m_pins, &QListWidget::customContextMenuRequested, this, &PaneroNavigationDock::pinContextMenu);
    pinLayout->addWidget(m_pins, 1);

#ifndef Q_OS_WIN
    // cloud drives and servers (D-014: rclone remotes, Connect to Server), switchable
    m_cloudSection = new QWidget(m_splitter);
    auto *cloudLayout = new QVBoxLayout(m_cloudSection);
    cloudLayout->setContentsMargins(0, 0, 0, 0);
    cloudLayout->setSpacing(2);
    auto *cloudHeader = new QHBoxLayout;
    cloudHeader->setSpacing(2);
    cloudHeader->addWidget(sectionLabel(i18n("Cloud"), m_cloudSection));
    cloudHeader->addStretch();
    auto *cloudAdd = new QToolButton(m_cloudSection);
    cloudAdd->setIcon(Icon(QStringLiteral("list-add")));
    cloudAdd->setAutoRaise(true);
    cloudAdd->setPopupMode(QToolButton::InstantPopup);
    cloudAdd->setToolTip(i18n("Add a cloud drive or a server"));
    cloudAdd->setAccessibleName(i18n("Add cloud or server"));
    auto *cloudMenu = new QMenu(cloudAdd);
    cloudMenu->addAction(Icon(QStringLiteral("folder-cloud")), i18n("Add Cloud..."), this, [this]() {
        PaneroAddCloudDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
            openCloudEntry(dialog.createdEntry());
    });
    cloudMenu->addAction(Icon(QStringLiteral("network-server")), i18n("Connect to Server..."), this, [this]() {
        PaneroConnectServerDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
            openCloudEntry(dialog.createdEntry());
    });
    cloudAdd->setMenu(cloudMenu);
    cloudHeader->addWidget(cloudAdd);
    cloudLayout->addLayout(cloudHeader);
    m_cloud = new QListWidget(m_cloudSection);
    m_cloud->setAccessibleName(i18n("Cloud drives and servers"));
    m_cloud->setContextMenuPolicy(Qt::CustomContextMenu);
    m_cloud->setUniformItemSizes(true);
    m_cloud->setMinimumHeight(30);
    m_cloud->setFrameShape(QFrame::NoFrame);
    m_cloud->setIconSize(QSize(KrFileTreeView::IconSize, KrFileTreeView::IconSize));
    connect(m_cloud, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        openCloudEntry(PaneroCloud::instance()->entry(item->data(UrlRole).toString()));
    });
    connect(m_cloud, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        openCloudEntry(PaneroCloud::instance()->entry(item->data(UrlRole).toString()));
    });
    connect(m_cloud, &QListWidget::customContextMenuRequested, this, &PaneroNavigationDock::cloudContextMenu);
    cloudLayout->addWidget(m_cloud, 1);
    auto *cloud = PaneroCloud::instance();
    connect(cloud, &PaneroCloud::entriesChanged, this, &PaneroNavigationDock::loadCloud);
    connect(cloud, &PaneroCloud::mounted, this, [this](const QString &id, const QUrl &url) {
        if (m_connecting && id == m_pendingCloud) {
            m_connecting->disconnect(this);
            m_connecting->deleteLater();
        }
        loadCloud();
        if (id == m_pendingCloud) {
            m_pendingCloud.clear();
            openUrlInActivePanel(url);
        }
    });
    connect(cloud, &PaneroCloud::mountFailed, this, [this](const QString &id, const QString &message) {
        if (m_connecting && id == m_pendingCloud) {
            m_connecting->disconnect(this);
            m_connecting->deleteLater();
        }
        loadCloud();
        const bool wasPending = id == m_pendingCloud;
        if (wasPending) {
            m_pendingCloud.clear();
            updateCloudHighlight();
        }
        const QString remote = id.startsWith(QLatin1String("rclone:")) ? id.mid(7) : QString();
        if (wasPending && !remote.isEmpty() && !PaneroOffline::instance()->pins(remote).isEmpty()
            && QFileInfo(PaneroOffline::mirrorRoot(remote)).isDir()) {
            const auto answer = KMessageBox::questionTwoActions(
                this, message + QLatin1String("\n\n") + i18n("Open the folders kept on this device instead?"), i18n("Cloud Drive"),
                KGuiItem(i18n("Open Offline Files"), QStringLiteral("folder-download")), KStandardGuiItem::close());
            if (answer == KMessageBox::PrimaryAction)
                openUrlInActivePanel(QUrl::fromLocalFile(PaneroOffline::mirrorRoot(remote)));
            return;
        }
        KMessageBox::error(this, message, i18n("Cloud Drive"));
    });
    connect(PaneroOffline::instance(), &PaneroOffline::changed, this, &PaneroNavigationDock::loadCloud);
    connect(cloud, &PaneroCloud::unmounted, this, &PaneroNavigationDock::loadCloud);

#else
    // Windows (D-020): the sync folders of OneDrive, Google Drive, Dropbox, ...
    // found by Panero itself (registered Cloud Files sync roots and the apps' settings)
    m_cloudSection = new QWidget(m_splitter);
    auto *cloudLayout = new QVBoxLayout(m_cloudSection);
    cloudLayout->setContentsMargins(0, 0, 0, 0);
    cloudLayout->setSpacing(2);
    cloudLayout->addWidget(sectionLabel(i18n("Cloud"), m_cloudSection));
    m_cloud = new QListWidget(m_cloudSection);
    m_cloud->setAccessibleName(i18n("Cloud folders"));
    m_cloud->setContextMenuPolicy(Qt::CustomContextMenu);
    m_cloud->setUniformItemSizes(true);
    m_cloud->setMinimumHeight(30);
    m_cloud->setFrameShape(QFrame::NoFrame);
    m_cloud->setIconSize(QSize(KrFileTreeView::IconSize, KrFileTreeView::IconSize));
    connect(m_cloud, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        openUrlInActivePanel(item->data(UrlRole).toUrl());
    });
    connect(m_cloud, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        openUrlInActivePanel(item->data(UrlRole).toUrl());
    });
    connect(m_cloud, &QListWidget::customContextMenuRequested, this, &PaneroNavigationDock::cloudContextMenu);
    cloudLayout->addWidget(m_cloud, 1);
    // an app signed in or out: look again now and then (registry and settings only)
    auto *cloudTimer = new QTimer(this);
    cloudTimer->setInterval(15000);
    connect(cloudTimer, &QTimer::timeout, this, &PaneroNavigationDock::loadCloud);
    cloudTimer->start();
#endif

    // folder tree
    auto *treeSection = new QWidget(m_splitter);
    auto *treeLayout = new QVBoxLayout(treeSection);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    treeLayout->setSpacing(2);
    auto *treeHeader = new QHBoxLayout;
    treeHeader->setSpacing(2);
    treeHeader->addWidget(sectionLabel(i18n("Folders"), treeSection));
    treeHeader->addStretch();
    auto *treeOptions = new QToolButton(treeSection);
    treeOptions->setIcon(Icon(QStringLiteral("configure")));
    treeOptions->setAutoRaise(true);
    treeOptions->setPopupMode(QToolButton::InstantPopup);
    treeOptions->setToolTip(i18n("Folder tree options"));
    treeOptions->setAccessibleName(i18n("Folder tree options"));
    treeOptions->setMenu(createTreeOptionsMenu(treeOptions));
    treeHeader->addWidget(treeOptions);
    treeLayout->addLayout(treeHeader);
    m_tree = new KrFileTreeView(treeSection);
    m_tree->setAccessibleName(i18n("Folder tree"));
    connect(m_tree, &KrFileTreeView::urlActivated, this, &PaneroNavigationDock::treeUrlActivated);
    treeLayout->addWidget(m_tree, 1);

    m_splitter->addWidget(pinSection);
    m_splitter->addWidget(m_cloudSection);
    m_splitter->addWidget(treeSection);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setStretchFactor(2, 1);
    m_splitter->setSizes({150, 90, 400});
    layout->addWidget(m_splitter, 1);

    setWidget(content);
    setMinimumWidth(170);
    // compact: no separate title bar; the toolbar's sidebar button shows/hides it
    setTitleBarWidget(new QWidget(this));
    loadPins();
    loadCloud();
    connect(PaneroAppearance::instance(), &PaneroAppearance::settingsChanged, this, &PaneroNavigationDock::loadCloud);
}

#ifdef Q_OS_WIN
void PaneroNavigationDock::loadCloud()
{
    const bool show = KConfigGroup(krConfig, "Panero Appearance").readEntry("Show Cloud", PANERO_CLOUD_DEFAULT);
    const QList<PaneroCloudFiles::SyncRoot> roots = PaneroCloudFiles::syncRoots();
    // the section is there only while a cloud app is connected
    m_cloudSection->setVisible(show && !roots.isEmpty());
    QStringList signature;
    for (const PaneroCloudFiles::SyncRoot &r : roots)
        signature << r.path + QLatin1Char('|') + r.name;
    if (signature == m_cloudSignature)
        return;
    m_cloudSignature = signature;
    m_cloud->clear();
    for (const PaneroCloudFiles::SyncRoot &r : roots) {
        // the folder's own shell icon (OneDrive's cloud folder, Google Drive's drive)
        const QIcon icon = r.virtualDrive ? PaneroWinShell::driveIcon(r.path) : PaneroWinShell::fileIcon(r.path, true);
        auto *item = new QListWidgetItem(QIcon(icon.pixmap(KrFileTreeView::IconSize)), r.name, m_cloud);
        item->setData(UrlRole, QUrl::fromLocalFile(r.path));
        item->setSizeHint(QSize(0, KrFileTreeView::RowHeight));
        item->setToolTip(QDir::toNativeSeparators(r.path));
    }
    updateCloudHighlight();
}

void PaneroNavigationDock::cloudContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_cloud->itemAt(pos);
    if (!item)
        return;
    const QUrl url = item->data(UrlRole).toUrl();
    QMenu menu(this);
    QAction *open = menu.addAction(Icon(QStringLiteral("document-open-folder")), i18n("Open in Active Panel"));
    QAction *other = menu.addAction(Icon(QStringLiteral("view-split-left-right")), i18n("Open in Other Panel"));
    menu.addSeparator();
    QAction *pin = menu.addAction(Icon(QStringLiteral("window-pin")), i18n("Pin"));
    for (int i = 0; i < m_pins->count(); ++i) {
        if (m_pins->item(i)->data(UrlRole).toUrl().adjusted(QUrl::StripTrailingSlash) == url.adjusted(QUrl::StripTrailingSlash))
            pin->setEnabled(false); // pinned already
    }
    QAction *chosen = menu.exec(m_cloud->viewport()->mapToGlobal(pos));
    if (chosen == open) {
        openUrlInActivePanel(url);
    } else if (chosen == other && ACTIVE_PANEL) {
        KrPanel *target = ACTIVE_PANEL->otherPanel();
        target->func->openUrl(url);
        target->gui->slotFocusOnMe();
    } else if (chosen == pin) {
        pinUrl(url, item->text());
    }
}
#else

void PaneroNavigationDock::loadCloud()
{
    const bool show = KConfigGroup(krConfig, "Panero Appearance").readEntry("Show Cloud", PANERO_CLOUD_DEFAULT);
    m_cloudSection->setVisible(show);
    if (!show)
        return;
    auto *cloud = PaneroCloud::instance();
    m_cloud->clear();
    for (const PaneroCloud::Entry &e : cloud->entries()) {
        // Google Drive and OneDrive show their own icon, everything else as before
        const bool ownIcon = e.rcloneType == QLatin1String("drive") || e.rcloneType == QLatin1String("onedrive");
        const QIcon icon = e.isServer() ? Icon(QStringLiteral("network-server"))
                                        : ownIcon ? PaneroCloud::iconForType(e.rcloneType) : Icon(QStringLiteral("folder-cloud"));
        auto *item = new QListWidgetItem(icon, e.label, m_cloud);
        item->setData(UrlRole, e.id);
        item->setSizeHint(QSize(0, KrFileTreeView::RowHeight));
        QString tip = e.isServer() ? e.url.toDisplayString() : i18n("Cloud drive (rclone: %1)", e.rcloneRemote);
        if (e.isRclone()) {
            PaneroOffline *offline = PaneroOffline::instance();
            const QStringList pins = offline->pins(e.rcloneRemote);
            if (!pins.isEmpty()) {
                int problems = 0;
                for (const QString &p : pins)
                    problems += offline->status(e.rcloneRemote, p).error.isEmpty() ? 0 : 1;
                tip += QLatin1Char('\n') + i18np("1 folder kept on this device", "%1 folders kept on this device", pins.count());
                if (offline->isBusy(e.rcloneRemote))
                    tip += QLatin1Char('\n') + i18n("Synchronising...");
                else if (problems)
                    tip += QLatin1Char('\n') + i18np("1 folder could not be synchronised", "%1 folders could not be synchronised", problems);
            }
        }
        if (cloud->isMounted(e.id))
            tip += QLatin1Char('\n') + i18n("Connected");
        else if (cloud->isMounting(e.id))
            tip += QLatin1Char('\n') + i18n("Connecting...");
        item->setToolTip(tip);
        if (cloud->isMounting(e.id))
            item->setText(i18nc("cloud entry while connecting", "%1 (connecting...)", e.label));
    }
    if (m_cloud->count() == 0) {
        auto *hint = new QListWidgetItem(i18n("Add a cloud drive or server with +"), m_cloud);
        hint->setFlags(Qt::NoItemFlags);
    }
    updateCloudHighlight();
}

void PaneroNavigationDock::openCloudEntry(const PaneroCloud::Entry &entry)
{
    auto *cloud = PaneroCloud::instance();
    if (entry.id.isEmpty())
        return;
    if (entry.isServer()) {
        openUrlInActivePanel(cloud->urlFor(entry, this));
        return;
    }
    if (cloud->isMounted(entry.id)) {
        openUrlInActivePanel(cloud->urlFor(entry, this));
        return;
    }
    if (!cloud->ensureRclone(this))
        return;
    m_pendingCloud = entry.id;
    cloud->mount(entry);
    loadCloud();
    // connecting takes a few seconds: a small window instead of a frozen look
    if (cloud->isMounting(entry.id)) {
        delete m_connecting;
        m_connecting = new QProgressDialog(i18n("Connecting to %1...", entry.label), i18n("Cancel"), 0, 0, this);
        m_connecting->setWindowTitle(i18n("Cloud Drive"));
        m_connecting->setMinimumDuration(400);
        m_connecting->setAutoClose(false);
        m_connecting->setAutoReset(false);
        const QString id = entry.id;
        connect(m_connecting.data(), &QProgressDialog::canceled, this, [this, id]() {
            if (m_pendingCloud == id)
                m_pendingCloud.clear();
            PaneroCloud::instance()->unmount(id);
        });
    }
}

void PaneroNavigationDock::cloudContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_cloud->itemAt(pos);
    auto *cloud = PaneroCloud::instance();
    const PaneroCloud::Entry entry = item ? cloud->entry(item->data(UrlRole).toString()) : PaneroCloud::Entry();
    QMenu menu(this);
    QAction *open = nullptr, *disconnect = nullptr, *remove = nullptr, *offlineFiles = nullptr, *openOffline = nullptr, *rename = nullptr,
            *ownClient = nullptr;
    if (!entry.id.isEmpty()) {
        open = menu.addAction(Icon(QStringLiteral("document-open-folder")), i18n("Open in Active Panel"));
        if (entry.isRclone()) {
            if (!PaneroOffline::instance()->pins(entry.rcloneRemote).isEmpty())
                openOffline = menu.addAction(Icon(QStringLiteral("folder-download")), i18n("Open Offline Files"));
            offlineFiles = menu.addAction(Icon(QStringLiteral("configure")), i18n("Offline Files..."));
        }
        if (entry.isRclone() && cloud->isMounted(entry.id))
            disconnect = menu.addAction(Icon(QStringLiteral("media-eject")), i18n("Disconnect"));
        rename = menu.addAction(Icon(QStringLiteral("edit-rename")), i18n("Rename..."));
        if (entry.isRclone() && entry.rcloneType == QLatin1String("drive"))
            ownClient = menu.addAction(Icon(QStringLiteral("speedometer")), i18n("Use Own Google Client ID (Faster)..."));
        menu.addSeparator();
        remove = menu.addAction(Icon(QStringLiteral("list-remove")), entry.isRclone() ? i18n("Sign Out and Remove...") : i18n("Remove..."));
        menu.addSeparator();
    }
    QAction *addCloud = menu.addAction(Icon(QStringLiteral("folder-cloud")), i18n("Add Cloud..."));
    QAction *addServer = menu.addAction(Icon(QStringLiteral("network-server")), i18n("Connect to Server..."));
    QAction *signOutAll = nullptr;
    for (const PaneroCloud::Entry &e : cloud->entries()) {
        if (e.isRclone()) {
            menu.addSeparator();
            signOutAll = menu.addAction(Icon(QStringLiteral("system-log-out")), i18n("Sign Out of All Clouds..."));
            break;
        }
    }
    QAction *chosen = menu.exec(m_cloud->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == open) {
        openCloudEntry(entry);
    } else if (chosen == disconnect) {
        cloud->unmount(entry.id);
    } else if (chosen && chosen == ownClient) {
        if (PaneroAddCloudDialog::useOwnGoogleClient(this, entry))
            KMessageBox::information(this, i18n("%1 now uses your own Google client ID.", entry.label));
    } else if (chosen && chosen == rename) {
        // only the name shown in Panero: the account, its files and the
        // offline copies are not touched
        bool ok = false;
        const QString name = QInputDialog::getText(this, i18n("Rename"), i18n("Name shown for this cloud:"), QLineEdit::Normal,
                                                   entry.label, &ok).trimmed();
        if (ok && !name.isEmpty() && name != entry.label) {
            cloud->setLabel(entry, name);
            // panels showing the drive pick up the new name (drive button)
            if (ACTIVE_PANEL) {
                ACTIVE_FUNC->refresh();
                if (OTHER_PANEL)
                    OTHER_FUNC->refresh();
            }
        }
    } else if (chosen && chosen == openOffline) {
        openUrlInActivePanel(QUrl::fromLocalFile(PaneroOffline::mirrorRoot(entry.rcloneRemote)));
    } else if (chosen && chosen == offlineFiles) {
        PaneroOfflineDialog dialog(entry.rcloneRemote, entry.label, this);
        dialog.exec();
        if (dialog.urlToOpen().isValid())
            openUrlInActivePanel(dialog.urlToOpen());
    } else if (chosen && chosen == signOutAll) {
        cloud->signOutAll(this);
    } else if (chosen == remove) {
        cloud->confirmAndRemove(this, entry);
    } else if (chosen == addCloud) {
        PaneroAddCloudDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
            openCloudEntry(dialog.createdEntry());
    } else if (chosen == addServer) {
        PaneroConnectServerDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
            openCloudEntry(dialog.createdEntry());
    }
}

#endif // !Q_OS_WIN

void PaneroNavigationDock::activeLocationChanged(const QUrl &url, bool leftPanel)
{
    Q_UNUSED(leftPanel)
    const bool changed = url != m_currentUrl;
    m_currentUrl = url;
    updatePinHighlight();
    updateCloudHighlight();
    if (changed && isVisible())
        m_tree->setCurrentUrl(url);
}

void PaneroNavigationDock::updatePinHighlight()
{
    // a pin is highlighted only while exactly its folder is shown (not a subfolder)
    const QUrl current = m_currentUrl.adjusted(QUrl::StripTrailingSlash);
    QListWidgetItem *match = nullptr;
    for (int i = 0; i < m_pins->count() && !match; ++i) {
        if (m_pins->item(i)->data(UrlRole).toUrl().adjusted(QUrl::StripTrailingSlash) == current)
            match = m_pins->item(i);
    }
    const QSignalBlocker blocker(m_pins);
    if (match) {
        m_pins->setCurrentItem(match);
    } else {
        m_pins->clearSelection();
        m_pins->setCurrentRow(-1);
    }
}

void PaneroNavigationDock::updateCloudHighlight()
{
    // stays selected while the active panel is anywhere inside the drive (or
    // in its offline copies), deselected as soon as it is somewhere else
#ifdef Q_OS_WIN
    if (!m_cloud)
        return;
    QListWidgetItem *match = nullptr;
    if (m_currentUrl.isLocalFile()) {
        const QString path = m_currentUrl.toLocalFile();
        int best = -1;
        for (int i = 0; i < m_cloud->count(); ++i) {
            const QString root = m_cloud->item(i)->data(UrlRole).toUrl().toLocalFile();
            if (PaneroCloudFiles::isInside(path, root) && root.length() > best) {
                best = root.length();
                match = m_cloud->item(i);
            }
        }
    }
    const QSignalBlocker blocker(m_cloud);
    if (match) {
        m_cloud->setCurrentItem(match);
    } else {
        m_cloud->clearSelection();
        m_cloud->setCurrentRow(-1);
    }
#else
    if (!m_cloud)
        return;
    QString remote;
    if (m_currentUrl.isLocalFile()) {
        QString rel;
        bool mirror = false;
        if (!PaneroOffline::instance()->cloudPath(m_currentUrl.toLocalFile(), &remote, &rel, &mirror))
            remote.clear();
    }
    QListWidgetItem *match = nullptr;
    auto *cloud = PaneroCloud::instance();
    for (const PaneroCloud::Entry &e : cloud->entries()) {
        bool inside = false;
        if (e.isRclone()) {
            inside = !remote.isEmpty() && remote == e.rcloneRemote;
        } else if (e.isServer() && e.url.isValid() && !m_currentUrl.isLocalFile()) {
            const QString base = e.url.adjusted(QUrl::StripTrailingSlash).path();
            const QString path = m_currentUrl.adjusted(QUrl::StripTrailingSlash).path();
            inside = m_currentUrl.scheme() == e.url.scheme() && m_currentUrl.host() == e.url.host()
                && m_currentUrl.port() == e.url.port() && (base.isEmpty() || path == base || path.startsWith(base + QLatin1Char('/')));
        }
        if (!inside)
            continue;
        for (int i = 0; i < m_cloud->count() && !match; ++i) {
            if (m_cloud->item(i)->data(UrlRole).toString() == e.id)
                match = m_cloud->item(i);
        }
        break;
    }
    // a drive that is still connecting after a click stays selected meanwhile
    for (int i = 0; i < m_cloud->count() && !match && !m_pendingCloud.isEmpty(); ++i) {
        if (m_cloud->item(i)->data(UrlRole).toString() == m_pendingCloud)
            match = m_cloud->item(i);
    }
    const QSignalBlocker blocker(m_cloud);
    if (match) {
        m_cloud->setCurrentItem(match);
    } else {
        m_cloud->clearSelection();
        m_cloud->setCurrentRow(-1);
    }
#endif
}

void PaneroNavigationDock::showEvent(QShowEvent *event)
{
    QDockWidget::showEvent(event);
    // the tree is only updated while visible: catch up with the active panel
    if (m_currentUrl.isValid())
        m_tree->setCurrentUrl(m_currentUrl);
}

void PaneroNavigationDock::treeUrlActivated(const QUrl &url)
{
    openUrlInActivePanel(url);
}

void PaneroNavigationDock::openUrlInActivePanel(const QUrl &url)
{
    if (!url.isValid() || !ACTIVE_PANEL)
        return;
    ACTIVE_FUNC->openUrl(url);
    ACTIVE_PANEL->gui->slotFocusOnMe();
}

void PaneroNavigationDock::pinCurrentFolder()
{
    if (!ACTIVE_PANEL)
        return;
    pinUrl(ACTIVE_PANEL->virtualPath());
}

bool PaneroNavigationDock::pinUrl(const QUrl &folderUrl, const QString &label)
{
    Pin pin;
    pin.url = folderUrl.adjusted(QUrl::StripTrailingSlash);
    if (!pin.url.isValid())
        return false;
    for (int i = 0; i < m_pins->count(); ++i) {
        if (m_pins->item(i)->data(UrlRole).toUrl() == pin.url)
            return false; // already pinned
    }
    pin.label = pin.url.isLocalFile() ? QFileInfo(pin.url.toLocalFile()).fileName() : pin.url.fileName();
    if (!label.isEmpty())
        pin.label = label;
    if (pin.label.isEmpty())
        pin.label = pin.url.toDisplayString(QUrl::PreferLocalFile);
    if (pin.url.isLocalFile()) {
        const QString udi = krMtMan.findUdiForPath(pin.url.toLocalFile(), Solid::DeviceInterface::StorageAccess);
        if (!udi.isEmpty()) {
            Solid::Device device(udi);
            pin.driveUdi = udi;
            pin.driveLabel = device.description();
        }
    }
    addPin(pin);
    savePins();
    updatePinHighlight();
    return true;
}

QMenu *PaneroNavigationDock::createTreeOptionsMenu(QWidget *parent)
{
    // Panero offers two clear starting points instead of upstream's three modes:
    //  Home: the tree starts at Home, or at the drive / "/" when outside Home
    //        (upstream "start from place"), and Whole file system: always "/".
    auto *menu = new QMenu(parent);
    auto *group = new QActionGroup(menu);
    QAction *home = menu->addAction(Icon(QStringLiteral("user-home")), i18n("Start at Home"));
    home->setToolTip(i18n("Outside Home the tree starts at that drive or at /"));
    QAction *root = menu->addAction(Icon(QStringLiteral("drive-harddisk-root")), i18n("Start at the Whole File System (/)"));
    for (QAction *a : {home, root}) {
        a->setCheckable(true);
        group->addAction(a);
    }
    menu->addSeparator();
    QAction *hidden = menu->addAction(i18n("Show Hidden Folders"));
    hidden->setCheckable(true);
    menu->setToolTipsVisible(true);

    connect(menu, &QMenu::aboutToShow, this, [this, home, root, hidden]() {
        const KConfigGroup g = treeConfig();
        const bool fromRoot = !g.readEntry("StartFromPlace", true) && !g.readEntry("StartFromCurrent", false);
        root->setChecked(fromRoot);
        home->setChecked(!fromRoot);
        hidden->setChecked(g.readEntry("ShowHiddenFolders", false));
    });
    auto apply = [this, root, hidden]() {
        KConfigGroup g = treeConfig();
        g.writeEntry("StartFromPlace", !root->isChecked());
        g.writeEntry("StartFromCurrent", false);
        g.writeEntry("ShowHiddenFolders", hidden->isChecked());
        g.writeEntry("BriefMode", true);
        m_tree->restoreSettings(KConfigGroup(krConfig, "Panero Navigation Tree"));
        if (m_currentUrl.isValid())
            m_tree->setCurrentUrl(m_currentUrl);
    };
    connect(group, &QActionGroup::triggered, this, apply);
    connect(hidden, &QAction::triggered, this, apply);
    return menu;
}

KConfigGroup PaneroNavigationDock::treeConfig() const
{
    KConfigGroup navTree(krConfig, "Panero Navigation Tree");
    return KConfigGroup(&navTree, "TreeView");
}

bool PaneroNavigationDock::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_pins->viewport())
        return QDockWidget::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto *e = static_cast<QDropEvent *>(event);
        if (e->mimeData()->hasUrls()) {
            e->setDropAction(Qt::LinkAction);
            e->accept();
            return true;
        }
        return false;
    }
    case QEvent::Drop: {
        auto *e = static_cast<QDropEvent *>(event);
        int pinned = 0;
        for (const QUrl &url : KUrlMimeData::urlsFromMimeData(e->mimeData())) {
            // local: only folders; remote locations are pinned as given
            if (url.isLocalFile() && !QFileInfo(url.toLocalFile()).isDir())
                continue;
            if (pinUrl(url))
                ++pinned;
        }
        e->setDropAction(Qt::LinkAction);
        e->accept();
        return true;
    }
    default:
        return false;
    }
}

void PaneroNavigationDock::addPin(const Pin &pin)
{
    auto *item = new QListWidgetItem(Icon(iconForUrl(pin.url)), pin.label, m_pins);
#ifdef Q_OS_WIN
    // Windows: the shell icons the panels and the folder tree show (Explorer's
    // drive icons, Desktop/Documents/Downloads with their own icons, folders)
    if (pin.url.isLocalFile()) {
        const QString path = pin.url.toLocalFile();
        if (QDir(path).isRoot())
            item->setIcon(PaneroWinShell::driveIcon(path));
        else if (PaneroWinShell::useShellIcons(true) && QFileInfo(path).isDir())
            item->setIcon(QIcon(PaneroWinShell::fileIcon(path, true).pixmap(KrFileTreeView::IconSize)));
    }
#endif
    item->setData(UrlRole, pin.url);
    item->setSizeHint(QSize(0, KrFileTreeView::RowHeight));
    item->setData(UdiRole, pin.driveUdi);
    item->setData(DriveLabelRole, pin.driveLabel);
    QString tip = pin.url.toDisplayString(QUrl::PreferLocalFile);
    if (!pin.driveLabel.isEmpty())
        tip += QLatin1Char('\n') + i18n("Drive: %1", pin.driveLabel);
    item->setToolTip(tip);
}

PaneroNavigationDock::Pin PaneroNavigationDock::pinFromItem(const QListWidgetItem *item) const
{
    Pin pin;
    pin.url = item->data(UrlRole).toUrl();
    pin.label = item->text();
    pin.driveUdi = item->data(UdiRole).toString();
    pin.driveLabel = item->data(DriveLabelRole).toString();
    return pin;
}

bool PaneroNavigationDock::tryMountAndOpen(const Pin &pin)
{
    if (pin.driveUdi.isEmpty())
        return false;
    Solid::Device device(pin.driveUdi);
    auto *access = device.is<Solid::StorageAccess>() ? device.as<Solid::StorageAccess>() : nullptr;
    if (!device.isValid() || !access || access->isAccessible())
        return false;
    // normal desktop mount (udisks2, with the system's own authorisation if needed)
    const QUrl url = pin.url;
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(access, &Solid::StorageAccess::setupDone, this,
            [this, url, connection](Solid::ErrorType error, const QVariant &errorData, const QString &) {
                QObject::disconnect(*connection); // one-shot (Qt 5 has no SingleShotConnection)
                if (error == Solid::NoError)
                    openUrlInActivePanel(url);
                else
                    KMessageBox::error(this, i18n("The drive could not be mounted: %1", errorData.toString()));
            });
    access->setup();
    return true;
}

void PaneroNavigationDock::openPin(QListWidgetItem *item)
{
    if (!item)
        return;
    const Pin pin = pinFromItem(item);
    if (!pin.url.isLocalFile() || QFileInfo(pin.url.toLocalFile()).isDir()) {
        openUrlInActivePanel(pin.url); // remote pins: KIO reports errors in the panel
        return;
    }

    // local folder is missing: drive unplugged / not mounted, or folder removed
    Solid::Device device(pin.driveUdi);
    const bool canMount = device.isValid() && device.is<Solid::StorageAccess>()
                          && !device.as<Solid::StorageAccess>()->isAccessible();
    QString message = i18n("<p>The pinned folder <b>%1</b> is not available.</p>",
                           pin.url.toDisplayString(QUrl::PreferLocalFile).toHtmlEscaped());
    if (canMount)
        message += i18n("<p>The drive <b>%1</b> is connected but not mounted.</p>", pin.driveLabel.toHtmlEscaped());
    else if (!pin.driveLabel.isEmpty())
        message += i18n("<p>It is on the drive <b>%1</b>, which may be disconnected.</p>", pin.driveLabel.toHtmlEscaped());

    const auto answer = KMessageBox::warningTwoActionsCancel(
        this, message, i18n("Pinned Folder Unavailable"),
        canMount ? KGuiItem(i18n("Mount and Open"), QStringLiteral("media-mount")) : KGuiItem(i18n("Retry"), QStringLiteral("view-refresh")),
        KGuiItem(i18n("Unpin"), QStringLiteral("list-remove")));
    if (answer == KMessageBox::PrimaryAction) {
        if (canMount)
            tryMountAndOpen(pin);
        else
            openPin(item); // retry
    } else if (answer == KMessageBox::SecondaryAction) {
        delete item;
        savePins();
    }
}

void PaneroNavigationDock::pinContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_pins->itemAt(pos);
    QMenu menu(this);
    QAction *open = nullptr, *unpin = nullptr, *up = nullptr, *down = nullptr;
    if (item) {
        open = menu.addAction(Icon(QStringLiteral("document-open-folder")), i18n("Open in Active Panel"));
        up = menu.addAction(Icon(QStringLiteral("go-up")), i18n("Move Up"));
        down = menu.addAction(Icon(QStringLiteral("go-down")), i18n("Move Down"));
        menu.addSeparator();
        unpin = menu.addAction(Icon(QStringLiteral("list-remove")), i18n("Unpin"));
        menu.addSeparator();
    }
    QAction *pinCurrent = menu.addAction(Icon(QStringLiteral("list-add")), i18n("Pin Active Panel's Folder"));
    QAction *chosen = menu.exec(m_pins->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == open) {
        openPin(item);
    } else if (chosen == unpin) {
        delete item;
        savePins();
    } else if (chosen == up || chosen == down) {
        const int row = m_pins->row(item);
        const int target = chosen == up ? row - 1 : row + 1;
        if (target >= 0 && target < m_pins->count()) {
            m_pins->insertItem(target, m_pins->takeItem(row));
            m_pins->setCurrentRow(target);
            savePins();
        }
    } else if (chosen == pinCurrent) {
        pinCurrentFolder();
    }
}

void PaneroNavigationDock::savePins() const
{
    KConfigGroup g(krConfig, PIN_GROUP);
    QStringList urls, labels, udis, drives;
    for (int i = 0; i < m_pins->count(); ++i) {
        const QListWidgetItem *item = m_pins->item(i);
        urls << item->data(UrlRole).toUrl().toString();
        labels << item->text();
        udis << item->data(UdiRole).toString();
        drives << item->data(DriveLabelRole).toString();
    }
    g.writeEntry("Urls", urls);
    g.writeEntry("Labels", labels);
    g.writeEntry("Drive Udis", udis);
    g.writeEntry("Drive Labels", drives);
    g.sync();
}

void PaneroNavigationDock::loadPins()
{
    m_pins->clear();
    const KConfigGroup g(krConfig, PIN_GROUP);
    if (!g.hasKey("Urls")) {
        // first start: a sensible default
        Pin home;
        home.url = QUrl::fromLocalFile(QDir::homePath());
        home.label = i18n("Home");
        addPin(home);
#ifdef Q_OS_WIN
        // Windows: what Explorer's Quick access and "This PC" offer first
        const QList<QPair<QStandardPaths::StandardLocation, QString>> places = {
            {QStandardPaths::DesktopLocation, i18n("Desktop")},
            {QStandardPaths::DocumentsLocation, i18n("Documents")},
            {QStandardPaths::DownloadLocation, i18n("Downloads")}};
        for (const auto &place : places) {
            const QString path = QStandardPaths::writableLocation(place.first);
            if (path.isEmpty() || !QFileInfo::exists(path))
                continue;
            Pin pin;
            pin.url = QUrl::fromLocalFile(path);
            pin.label = place.second;
            addPin(pin);
        }
        for (const PaneroWinShell::Drive &drive : PaneroWinShell::drives()) {
            if (!QStorageInfo(drive.rootPath).isReady())
                continue; // empty card reader or DVD drive
            Pin pin;
            pin.url = QUrl::fromLocalFile(drive.rootPath);
            pin.label = drive.name;
            addPin(pin);
        }
#endif
        return;
    }
    const QStringList urls = g.readEntry("Urls", QStringList());
    const QStringList labels = g.readEntry("Labels", QStringList());
    const QStringList udis = g.readEntry("Drive Udis", QStringList());
    const QStringList drives = g.readEntry("Drive Labels", QStringList());
    for (int i = 0; i < urls.size(); ++i) {
        Pin pin;
        pin.url = QUrl(urls[i]);
        pin.label = labels.value(i, pin.url.fileName());
        pin.driveUdi = udis.value(i);
        pin.driveLabel = drives.value(i);
        if (pin.url.isValid())
            addPin(pin);
    }
}

void PaneroNavigationDock::saveSettings() const
{
    KConfigGroup g(krConfig, NAV_GROUP);
    g.writeEntry("Visible", isVisible());
    g.writeEntry("Width", width());
    m_tree->saveSettings(KConfigGroup(krConfig, "Panero Navigation Tree"));
    g.writeEntry("Splitter", m_splitter->saveState());
}

void PaneroNavigationDock::restoreSettings()
{
    const KConfigGroup g(krConfig, NAV_GROUP);
    // Panero default: root the tree at the nearest place (e.g. Home) instead of "/"
    KConfigGroup navTree(krConfig, "Panero Navigation Tree");
    KConfigGroup treeGroup(&navTree, "TreeView");
    if (!treeGroup.hasKey("StartFromPlace") || treeGroup.readEntry("StartFromCurrent", false)) {
        // Panero offers Home (start from place) or the whole file system only
        treeGroup.writeEntry("StartFromPlace", true);
        treeGroup.writeEntry("StartFromCurrent", false);
    }
    m_tree->restoreSettings(KConfigGroup(krConfig, "Panero Navigation Tree"));
    const QByteArray splitter = g.readEntry("Splitter", QByteArray());
    if (!splitter.isEmpty())
        m_splitter->restoreState(splitter);
    // single name column (default): no header needed
    m_tree->header()->setVisible(m_tree->header()->count() - m_tree->header()->hiddenSectionCount() > 1);
    setVisible(g.readEntry("Visible", true));
    resize(g.readEntry("Width", 230), height());
}
