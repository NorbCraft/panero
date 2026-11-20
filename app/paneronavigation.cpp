/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneronavigation.h"
#include "Cloud/panerocloud.h"
#include "Cloud/paneroaddclouddialog.h"
#include "Cloud/paneroconnectserverdialog.h"
#include "Panel/paneroappearance.h"

#include <QSignalBlocker>

#include "icon.h"
#include "krglobal.h"
#include "krusaderview.h"
#include "Panel/krfiletreeview.h"
#include "Panel/krpanel.h"
#include "Panel/listpanel.h"
#include "Panel/panelfunc.h"
#include "MountMan/kmountman.h"

#include <memory>

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QActionGroup>
#include <QDropEvent>
#include <QMimeData>
#include <QSplitter>

#include <KCoreAddons/KUrlMimeData>
#include <QLabel>
#include <QListWidget>
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

PaneroNavigationDock::PaneroNavigationDock(QWidget *parent)
    : QDockWidget(i18n("Navigation"), parent)
{
    setObjectName(QStringLiteral("panero_navigation_dock"));
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
        loadCloud();
        if (id == m_pendingCloud) {
            m_pendingCloud.clear();
            openUrlInActivePanel(url);
        }
    });
    connect(cloud, &PaneroCloud::mountFailed, this, [this](const QString &id, const QString &message) {
        loadCloud();
        if (id == m_pendingCloud)
            m_pendingCloud.clear();
        KMessageBox::error(this, message, i18n("Cloud Drive"));
    });
    connect(cloud, &PaneroCloud::unmounted, this, &PaneroNavigationDock::loadCloud);

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

void PaneroNavigationDock::loadCloud()
{
    const bool show = KConfigGroup(krConfig, "Panero Appearance").readEntry("Show Cloud", PANERO_CLOUD_DEFAULT);
    m_cloudSection->setVisible(show);
    if (!show)
        return;
    auto *cloud = PaneroCloud::instance();
    m_cloud->clear();
    for (const PaneroCloud::Entry &e : cloud->entries()) {
        QString icon = QStringLiteral("folder-cloud");
        if (e.isServer())
            icon = QStringLiteral("network-server");
        auto *item = new QListWidgetItem(Icon(icon), e.label, m_cloud);
        item->setData(UrlRole, e.id);
        item->setSizeHint(QSize(0, KrFileTreeView::RowHeight));
        QString tip = e.isServer() ? e.url.toDisplayString() : i18n("Cloud drive (rclone: %1)", e.rcloneRemote);
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
}

void PaneroNavigationDock::cloudContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_cloud->itemAt(pos);
    auto *cloud = PaneroCloud::instance();
    const PaneroCloud::Entry entry = item ? cloud->entry(item->data(UrlRole).toString()) : PaneroCloud::Entry();
    QMenu menu(this);
    QAction *open = nullptr, *disconnect = nullptr, *remove = nullptr;
    if (!entry.id.isEmpty()) {
        open = menu.addAction(Icon(QStringLiteral("document-open-folder")), i18n("Open in Active Panel"));
        if (entry.isRclone() && cloud->isMounted(entry.id))
            disconnect = menu.addAction(Icon(QStringLiteral("media-eject")), i18n("Disconnect"));
        menu.addSeparator();
        remove = menu.addAction(Icon(QStringLiteral("list-remove")), i18n("Remove..."));
        menu.addSeparator();
    }
    QAction *addCloud = menu.addAction(Icon(QStringLiteral("folder-cloud")), i18n("Add Cloud..."));
    QAction *addServer = menu.addAction(Icon(QStringLiteral("network-server")), i18n("Connect to Server..."));
    QAction *chosen = menu.exec(m_cloud->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == open) {
        openCloudEntry(entry);
    } else if (chosen == disconnect) {
        cloud->unmount(entry.id);
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

void PaneroNavigationDock::activeLocationChanged(const QUrl &url, bool leftPanel)
{
    Q_UNUSED(leftPanel)
    const bool changed = url != m_currentUrl;
    m_currentUrl = url;
    updatePinHighlight();
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

bool PaneroNavigationDock::pinUrl(const QUrl &folderUrl)
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
