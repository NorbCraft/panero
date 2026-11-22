/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerodrivebar.h"

#include "listpanel.h"
#include "panelfunc.h"
#include "../GUI/panerodrivefilter.h"
#include "paneroappearance.h"
#include "../icon.h"
#include "../krglobal.h"
#include "../paneronavigation.h"
#include "../FileSystem/paneroslowpaths.h"

#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QLayout>
#include <QMenu>
#include <QSet>
#include <QSocketNotifier>
#include <QStorageInfo>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>

#include <KConfigCore/KConfigGroup>
#include <KConfigCore/KSharedConfig>
#include <KCoreAddons/KFormat>
#include <KI18n/KLocalizedString>
#include <KIOCore/KMountPoint>
#include <Solid/Block>
#include <Solid/Device>
#include <Solid/DeviceNotifier>
#include <Solid/OpticalDisc>
#include <Solid/OpticalDrive>
#include <Solid/StorageAccess>
#include <Solid/StorageDrive>
#include <Solid/StorageVolume>

#ifndef Q_OS_WIN
#include "../Cloud/panerocloud.h"
#else
#include "panerowinshell.h"
#endif

namespace
{
const char *const SETTINGS_GROUP = "Panero Drive Buttons";

QStringList hiddenIds()
{
    return KConfigGroup(krConfig, SETTINGS_GROUP).readEntry("Hidden", QStringList());
}

void setHiddenIds(const QStringList &ids)
{
    KConfigGroup g(krConfig, SETTINGS_GROUP);
    if (ids.isEmpty())
        g.deleteEntry("Hidden");
    else
        g.writeEntry("Hidden", ids);
    g.sync();
}

/// Wraps the buttons into more rows when the panel is narrow, so the bar
/// never forces the panel to be wider than the window allows.
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent) : QLayout(parent)
    {
        setContentsMargins(2, 2, 2, 2);
    }
    ~FlowLayout() override
    {
        while (QLayoutItem *item = takeAt(0))
            delete item;
    }
    void addItem(QLayoutItem *item) override
    {
        m_items.append(item);
    }
    int count() const override
    {
        return m_items.size();
    }
    QLayoutItem *itemAt(int index) const override
    {
        return m_items.value(index);
    }
    QLayoutItem *takeAt(int index) override
    {
        return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override
    {
        return {};
    }
    bool hasHeightForWidth() const override
    {
        return true;
    }
    int heightForWidth(int width) const override
    {
        return doLayout(QRect(0, 0, width, 0), true);
    }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        doLayout(rect, false);
    }
    QSize sizeHint() const override
    {
        return minimumSize();
    }
    QSize minimumSize() const override
    {
        QSize size;
        for (QLayoutItem *item : m_items)
            size = size.expandedTo(item->minimumSize());
        const QMargins m = contentsMargins();
        return size + QSize(m.left() + m.right(), m.top() + m.bottom());
    }

private:
    int doLayout(const QRect &rect, bool testOnly) const
    {
        const QMargins m = contentsMargins();
        const QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
        const int gap = 3;
        int x = area.x(), y = area.y(), lineHeight = 0;
        for (QLayoutItem *item : m_items) {
            if (item->isEmpty())
                continue;
            const QSize hint = item->sizeHint();
            if (x + hint.width() > area.right() + 1 && lineHeight > 0) {
                x = area.x();
                y += lineHeight + gap;
                lineHeight = 0;
            }
            if (!testOnly)
                item->setGeometry(QRect(QPoint(x, y), hint));
            x += hint.width() + gap;
            lineHeight = qMax(lineHeight, hint.height());
        }
        return y + lineHeight - rect.y() + m.bottom();
    }
    QList<QLayoutItem *> m_items;
};

bool isDataFsType(const QString &t)
{
    static const QStringList types{QStringLiteral("ntfs"),  QStringLiteral("ntfs3"), QStringLiteral("exfat"), QStringLiteral("ext4"),
                                   QStringLiteral("ext3"),  QStringLiteral("ext2"),  QStringLiteral("btrfs"), QStringLiteral("xfs"),
                                   QStringLiteral("f2fs"),  QStringLiteral("hfsplus"), QStringLiteral("vfat"), QStringLiteral("udf")};
    return types.contains(t);
}

QString formatSize(qint64 bytes)
{
    return KFormat().formatByteSize(double(bytes));
}
}

// ---- PaneroDrives -----------------------------------------------------------

PaneroDrives *PaneroDrives::instance()
{
    static PaneroDrives *self = new PaneroDrives;
    return self;
}

PaneroDrives::PaneroDrives()
{
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(300);
    connect(m_debounce, &QTimer::timeout, this, &PaneroDrives::refresh);

    Solid::DeviceNotifier *notifier = Solid::DeviceNotifier::instance();
    connect(notifier, &Solid::DeviceNotifier::deviceAdded, this, &PaneroDrives::scheduleRefresh);
    connect(notifier, &Solid::DeviceNotifier::deviceRemoved, this, &PaneroDrives::scheduleRefresh);
#ifndef Q_OS_WIN
    // the kernel flags /proc/self/mountinfo when the mount table changes
    m_mountInfo = new QFile(QStringLiteral("/proc/self/mountinfo"), this);
    if (m_mountInfo->open(QIODevice::ReadOnly)) {
        m_mountInfo->readAll();
        m_mountNotifier = new QSocketNotifier(int(m_mountInfo->handle()), QSocketNotifier::Exception, this);
        connect(m_mountNotifier, &QSocketNotifier::activated, this, [this]() {
            m_mountInfo->seek(0);
            m_mountInfo->readAll();
            scheduleRefresh();
        });
    }
    auto *cloud = PaneroCloud::instance();
    connect(cloud, &PaneroCloud::entriesChanged, this, &PaneroDrives::scheduleRefresh);
    connect(cloud, &PaneroCloud::mounted, this, &PaneroDrives::scheduleRefresh);
    connect(cloud, &PaneroCloud::unmounted, this, &PaneroDrives::scheduleRefresh);
#else
    // drive letters come and go (USB sticks, network drives): look again now and then
    auto *poll = new QTimer(this);
    poll->setInterval(5000);
    connect(poll, &QTimer::timeout, this, &PaneroDrives::scheduleRefresh);
    poll->start();
#endif
    refresh();
}

void PaneroDrives::scheduleRefresh()
{
    m_debounce->start();
}

void PaneroDrives::refresh()
{
    QList<PaneroDrive> drives;
#ifdef Q_OS_WIN
    // QStorageInfo asks every volume (a disconnected network drive can take
    // seconds): in the background, then publish
    auto *watcher = new QFutureWatcher<QList<PaneroDrive>>(this);
    connect(watcher, &QFutureWatcher<QList<PaneroDrive>>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        QList<PaneroDrive> found = watcher->result();
        // icons only in the GUI thread (QIcon/QPixmap are not thread-safe),
        // the ones Explorer shows for each drive
        for (PaneroDrive &d : found)
            d.icon = PaneroWinShell::driveIcon(d.url.toLocalFile());
        bool same = found.size() == m_drives.size();
        for (int i = 0; same && i < found.size(); ++i)
            same = found[i].id == m_drives[i].id && found[i].label == m_drives[i].label;
        m_drives = found;
        if (!same)
            emit changed();
    });
    watcher->setFuture(QtConcurrent::run([]() {
        QList<PaneroDrive> result;
        for (const QStorageInfo &v : QStorageInfo::mountedVolumes()) {
            const QString root = QDir::toNativeSeparators(v.rootPath()); // "C:\"
            if (root.size() < 2 || root.at(1) != QLatin1Char(':'))
                continue;
            PaneroDrive d;
            d.kind = PaneroDrive::Volume;
            d.id = QStringLiteral("drive:") + root.left(2).toUpper();
            d.label = v.name().isEmpty() ? root.left(2).toUpper() : QStringLiteral("%1 %2").arg(root.left(2).toUpper(), v.name());
            d.url = QUrl::fromLocalFile(v.rootPath());
            d.toolTip = root;
            if (v.isReady() && v.bytesTotal() > 0)
                d.toolTip += QLatin1Char('\n') + i18n("%1 free of %2", formatSize(v.bytesAvailable()), formatSize(v.bytesTotal()));
            result << d;
        }
        return result;
    }));
    return;
#else
    const QString home = QDir::homePath();
    PaneroDrive system;
    system.kind = PaneroDrive::System;
    system.id = QStringLiteral("system");
    system.label = i18nc("drive button for /", "System");
    system.icon = Icon(QStringLiteral("drive-harddisk"));
    system.url = QUrl::fromLocalFile(QStringLiteral("/"));
    system.toolTip = i18n("The whole system (/)");
    drives << system;
    PaneroDrive homeDrive;
    homeDrive.kind = PaneroDrive::Home;
    homeDrive.id = QStringLiteral("home");
    homeDrive.label = i18nc("drive button for the home folder", "Home");
    homeDrive.icon = Icon(QStringLiteral("user-home"));
    homeDrive.url = QUrl::fromLocalFile(home);
    homeDrive.toolTip = home;
    drives << homeDrive;

    // the mount table: which mount points are user-facing
    struct MountInfo {
        QString fsType, device;
        PaneroDriveFilter::Kind kind;
    };
    QHash<QString, MountInfo> mounts;
    for (const KMountPoint::Ptr &mp : KMountPoint::currentMountPoints()) {
        const QString point = mp->mountPoint();
        const auto kind = PaneroDriveFilter::classify(point, mp->mountType(), mp->mountedFrom(), home,
                                                      PaneroSlowPaths::isCloudRoot(point));
        mounts.insert(point, {mp->mountType(), mp->mountedFrom(), kind});
    }

    // disks, partitions and removable media (udisks): also unmounted ones
    QSet<QString> covered;
    QList<PaneroDrive> volumes;
    for (const Solid::Device &dev : Solid::Device::listFromType(Solid::DeviceInterface::StorageAccess)) {
        const auto *access = dev.as<Solid::StorageAccess>();
        const auto *vol = dev.as<Solid::StorageVolume>();
        if (!access || !vol || vol->isIgnored() || vol->usage() != Solid::StorageVolume::FileSystem)
            continue;
        const QString fs = vol->fsType();
        if (PaneroDriveFilter::isSystemFsType(fs))
            continue;
        const auto *block = dev.as<Solid::Block>();
        const QString devicePath = block ? block->device() : QString();
        if (devicePath.startsWith(QLatin1String("/dev/loop")))
            continue;
        const Solid::Device driveDev = dev.parent();
        const auto *storageDrive = driveDev.as<Solid::StorageDrive>();
        const bool removable = storageDrive && (storageDrive->isRemovable() || storageDrive->isHotpluggable());
        const bool optical = dev.is<Solid::OpticalDisc>();
        PaneroDrive d;
        d.kind = PaneroDrive::Volume;
        d.udi = dev.udi();
        d.id = QStringLiteral("volume:") + (vol->uuid().isEmpty() ? dev.udi() : vol->uuid());
        d.removable = removable || optical;
        d.optical = optical;
        d.label = !vol->label().isEmpty() ? vol->label() : dev.description();
        d.icon = Icon(dev.icon().isEmpty() ? QStringLiteral("drive-harddisk") : dev.icon());
        if (access->isAccessible()) {
            const QString point = access->filePath();
            const auto known = mounts.constFind(point);
            const auto kind = known != mounts.constEnd() ? known->kind
                                                         : PaneroDriveFilter::classify(point, fs, devicePath, home, false);
            covered.insert(point);
            if (kind != PaneroDriveFilter::Kind::Local)
                continue; // "/", the home partition, /boot ...
            d.url = QUrl::fromLocalFile(point);
            d.toolTip = QStringLiteral("%1\n%2").arg(d.label, point);
        } else {
            // not mounted: removable media always; internal partitions only
            // when they look like data (EFI and recovery partitions do not)
            if (!d.removable && !(isDataFsType(fs) && vol->size() >= qint64(2) * 1024 * 1024 * 1024))
                continue;
            d.toolTip = i18n("%1 (not mounted, a click mounts it)", d.label);
        }
        if (vol->size() > 0)
            d.toolTip += QLatin1Char('\n') + formatSize(vol->size());
        volumes << d;
    }
    std::sort(volumes.begin(), volumes.end(), [](const PaneroDrive &a, const PaneroDrive &b) {
        if (a.removable != b.removable)
            return !a.removable; // internal first, then removable
        return a.label.localeAwareCompare(b.label) < 0;
    });
    drives << volumes;

    // other local mounts (no udisks device, e.g. from fstab) and network shares
    QList<PaneroDrive> network;
    for (auto it = mounts.constBegin(); it != mounts.constEnd(); ++it) {
        if (covered.contains(it.key()))
            continue;
        if (it->kind == PaneroDriveFilter::Kind::Local || it->kind == PaneroDriveFilter::Kind::Network) {
            PaneroDrive d;
            const bool net = it->kind == PaneroDriveFilter::Kind::Network;
            d.kind = net ? PaneroDrive::Network : PaneroDrive::Volume;
            d.id = QStringLiteral("mount:") + it.key();
            d.label = net ? PaneroDriveFilter::networkLabel(it.key(), it->fsType, it->device)
                          : it.key().section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty);
            d.icon = Icon(net ? QStringLiteral("folder-network") : QStringLiteral("drive-harddisk"));
            d.url = QUrl::fromLocalFile(it.key());
            d.toolTip = QStringLiteral("%1\n%2").arg(it->device, it.key());
            network << d;
        }
    }
    std::sort(network.begin(), network.end(), [](const PaneroDrive &a, const PaneroDrive &b) {
        return a.label.localeAwareCompare(b.label) < 0;
    });
    drives << network;

    // Panero's cloud drives and servers, in the sidebar's order
    auto *cloud = PaneroCloud::instance();
    for (const PaneroCloud::Entry &e : cloud->entries()) {
        PaneroDrive d;
        d.kind = PaneroDrive::Cloud;
        d.id = QStringLiteral("cloud:") + e.id;
        d.cloudId = e.id;
        d.label = e.label;
        const bool ownIcon = e.rcloneType == QLatin1String("drive") || e.rcloneType == QLatin1String("onedrive");
        d.icon = e.isServer() ? Icon(QStringLiteral("network-server"))
                              : ownIcon ? PaneroCloud::iconForType(e.rcloneType) : Icon(QStringLiteral("folder-cloud"));
        if (e.isServer())
            d.url = e.url;
        else if (cloud->isMounted(e.id))
            d.url = QUrl::fromLocalFile(cloud->mountPath(e.id));
        d.toolTip = e.isServer() ? e.url.toDisplayString() : i18n("Cloud drive %1", e.label);
        drives << d;
    }
#endif

    bool same = drives.size() == m_drives.size();
    for (int i = 0; same && i < drives.size(); ++i)
        same = drives[i].id == m_drives[i].id && drives[i].label == m_drives[i].label && drives[i].url == m_drives[i].url;
    m_drives = drives;
    if (!same)
        emit changed();
}

// ---- PaneroDriveBar ----------------------------------------------------------

PaneroDriveBar::PaneroDriveBar(ListPanel *panel) : QWidget(panel), m_panel(panel)
{
    setObjectName(QStringLiteral("paneroDriveBar"));
    new FlowLayout(this);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    connect(PaneroAppearance::instance(), &PaneroAppearance::settingsChanged, this, &PaneroDriveBar::updateVisibility);
    updateVisibility();
}

bool PaneroDriveBar::enabledInSettings()
{
    return KConfigGroup(krConfig, "Panero Appearance").readEntry("Show Drive Buttons", false);
}

void PaneroDriveBar::updateVisibility()
{
    const bool on = enabledInSettings();
    if (on) {
        // the drive list is created on first use: with the bar off nobody
        // pays for the udisks queries
        connect(PaneroDrives::instance(), &PaneroDrives::changed, this, &PaneroDriveBar::rebuild, Qt::UniqueConnection);
        rebuild();
    }
    setVisible(on);
}

void PaneroDriveBar::rebuild()
{
    for (const QPointer<QToolButton> &b : qAsConst(m_buttons))
        delete b.data();
    m_buttons.clear();
    m_shown.clear();
    const QStringList hidden = hiddenIds();
    for (const PaneroDrive &d : PaneroDrives::instance()->drives()) {
        if (hidden.contains(d.id))
            continue;
        auto *b = new QToolButton(this);
        b->setAutoRaise(true);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setIconSize(QSize(16, 16));
        b->setIcon(d.icon);
        b->setText(d.label);
        b->setToolTip(d.toolTip);
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        const int index = m_shown.size();
        connect(b, &QToolButton::clicked, this, [this, index]() {
            if (index < m_shown.size())
                open(m_shown.at(index));
            updateChecked();
        });
        connect(b, &QToolButton::customContextMenuRequested, this, [this, index, b](const QPoint &pos) {
            if (index < m_shown.size())
                showMenu(m_shown.at(index), b, pos);
        });
        layout()->addWidget(b);
        m_buttons << b;
        m_shown << d;
    }
    updateChecked();
    updateGeometry();
}

void PaneroDriveBar::setCurrentUrl(const QUrl &url)
{
    m_url = url;
    updateChecked();
}

void PaneroDriveBar::updateChecked()
{
    // the drive with the longest matching location (Home beats System)
    int best = -1, bestLength = -1;
    const QString path = m_url.isLocalFile() ? QDir::cleanPath(m_url.toLocalFile()) : QString();
    for (int i = 0; i < m_shown.size(); ++i) {
        const QUrl &u = m_shown.at(i).url;
        if (!u.isValid())
            continue;
        int length = -1;
        if (u.isLocalFile() && !path.isEmpty()) {
            const QString base = QDir::cleanPath(u.toLocalFile());
            if (path == base || path.startsWith(base == QLatin1String("/") ? base : base + QLatin1Char('/')))
                length = base.length();
        } else if (!u.isLocalFile() && !m_url.isLocalFile() && u.scheme() == m_url.scheme() && u.host() == m_url.host()) {
            length = u.path().length();
        }
        if (length > bestLength) {
            best = i;
            bestLength = length;
        }
    }
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (m_buttons.at(i))
            m_buttons.at(i)->setChecked(i == best);
    }
}

void PaneroDriveBar::open(const PaneroDrive &drive)
{
    m_panel->slotFocusOnMe();
#ifndef Q_OS_WIN
    if (drive.kind == PaneroDrive::Cloud) {
        // as a click in the sidebar: connects first when needed, in the active panel
        if (PaneroNavigationDock *dock = PaneroNavigationDock::instance()) {
            dock->openCloudEntry(PaneroCloud::instance()->entry(drive.cloudId));
            return;
        }
    }
#endif
    if (drive.url.isValid()) {
        m_panel->func->openUrl(drive.url);
        return;
    }
    if (!drive.udi.isEmpty()) {
        Solid::Device dev(drive.udi);
        auto *access = dev.as<Solid::StorageAccess>();
        if (!access)
            return;
        QPointer<ListPanel> panel = m_panel;
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection = connect(access, &Solid::StorageAccess::setupDone, this,
                              [panel, connection, udi = drive.udi](Solid::ErrorType error, const QVariant &, const QString &doneUdi) {
                                  if (doneUdi != udi)
                                      return;
                                  QObject::disconnect(*connection); // once
                                  if (error != Solid::NoError || !panel)
                                      return;
                                  auto *a = Solid::Device(udi).as<Solid::StorageAccess>();
                                  if (a && a->isAccessible())
                                      panel->func->openUrl(QUrl::fromLocalFile(a->filePath()));
                              });
        access->setup();
    }
}

void PaneroDriveBar::showMenu(const PaneroDrive &drive, QToolButton *button, const QPoint &pos)
{
    QMenu menu(this);
    QAction *openOther = nullptr, *unmount = nullptr, *eject = nullptr, *hide = nullptr, *showAll = nullptr;
    if (drive.url.isValid() && drive.kind != PaneroDrive::Cloud)
        openOther = menu.addAction(Icon(QStringLiteral("view-split-left-right")), i18n("Open in Other Panel"));
    if (!drive.udi.isEmpty() && drive.url.isValid() && drive.removable) {
        if (drive.optical)
            eject = menu.addAction(Icon(QStringLiteral("media-eject")), i18n("Eject"));
        else
            unmount = menu.addAction(Icon(QStringLiteral("media-eject")), i18n("Safely Remove"));
    }
    if (!menu.isEmpty())
        menu.addSeparator();
    hide = menu.addAction(Icon(QStringLiteral("view-hidden")), i18n("Hide This Button"));
    const int hiddenCount = hiddenIds().size();
    if (hiddenCount > 0)
        showAll = menu.addAction(Icon(QStringLiteral("view-visible")), i18np("Show the Hidden Button", "Show the %1 Hidden Buttons", hiddenCount));
    QAction *chosen = menu.exec(button->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == openOther) {
        m_panel->otherPanel()->func->openUrl(drive.url);
    } else if (chosen == hide) {
        QStringList ids = hiddenIds();
        ids << drive.id;
        setHiddenIds(ids);
        emit PaneroDrives::instance()->changed(); // both panels rebuild
    } else if (chosen == showAll) {
        setHiddenIds({});
        emit PaneroDrives::instance()->changed();
    } else if (chosen == unmount || chosen == eject) {
        Solid::Device dev(drive.udi);
        if (chosen == eject) {
            if (auto *opticalDrive = dev.parent().as<Solid::OpticalDrive>()) {
                opticalDrive->eject();
                return;
            }
        }
        if (auto *access = dev.as<Solid::StorageAccess>())
            access->teardown();
    }
}
