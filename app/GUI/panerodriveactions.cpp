/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerodriveactions.h"
#include "../FileSystem/paneroslowpaths.h"
#include "../icon.h"

#include <KI18n/KLocalizedString>
#include <KIOCore/KMountPoint>
#include <KIOWidgets/KPropertiesDialog>
#include <Solid/Block>
#include <Solid/Device>
#include <Solid/OpticalDisc>
#include <Solid/OpticalDrive>
#include <Solid/StorageAccess>
#include <Solid/StorageDrive>

#include <QDir>
#include <QMenu>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace
{
/// the block device of a mount point ("/dev/nvme0n1p2" for "/"), from the mount table
QString deviceOf(const QString &mountPoint)
{
    if (mountPoint.isEmpty())
        return QString();
    const KMountPoint::Ptr mp = KMountPoint::currentMountPoints().findByPath(mountPoint);
    if (!mp || QDir::cleanPath(mp->mountPoint()) != QDir::cleanPath(mountPoint))
        return QString(); // e.g. Home on the system partition: no own device
    const QString dev = mp->realDeviceName().isEmpty() ? mp->mountedFrom() : mp->realDeviceName();
    return dev.startsWith(QLatin1String("/dev/")) ? dev : QString();
}

bool removableDrive(Solid::Device volume)
{
    for (Solid::Device d = volume; d.isValid(); d = d.parent()) {
        if (const auto *drive = d.as<Solid::StorageDrive>())
            return drive->isRemovable() || drive->isHotpluggable();
    }
    return false;
}

Solid::OpticalDrive *opticalDrive(Solid::Device volume)
{
    for (Solid::Device d = volume; d.isValid(); d = d.parent()) {
        if (auto *drive = d.as<Solid::OpticalDrive>())
            return drive;
    }
    return nullptr;
}
} // namespace

int PaneroDriveActions::addTo(QMenu *menu, const QString &udi, const QString &mountPointIn, QWidget *dialogParent)
{
#ifdef Q_OS_WIN
    Q_UNUSED(menu)
    Q_UNUSED(udi)
    Q_UNUSED(mountPointIn)
    Q_UNUSED(dialogParent)
    return 0;
#else
    int added = 0;
    QString mountPoint = mountPointIn;
    QString blockDevice;
    Solid::Device volume(udi);
    Solid::StorageAccess *access = udi.isEmpty() ? nullptr : volume.as<Solid::StorageAccess>();
    bool mounted = !mountPoint.isEmpty();
    if (access) {
        mounted = access->isAccessible();
        if (mounted && mountPoint.isEmpty())
            mountPoint = access->filePath();
    }
    if (const auto *block = udi.isEmpty() ? nullptr : volume.as<Solid::Block>())
        blockDevice = block->device();
    if (blockDevice.isEmpty() && mounted)
        blockDevice = deviceOf(mountPoint);

    // Properties: the folder's dialog shows the drive's usage. Not for cloud
    // and network mounts (it reads the drive in the GUI thread, D-018).
    if (mounted && !mountPoint.isEmpty() && !PaneroSlowPaths::isSlowPath(mountPoint)) {
        const QUrl url = QUrl::fromLocalFile(mountPoint);
        QPointer<QWidget> parent = dialogParent;
        menu->addAction(Icon(QStringLiteral("document-properties")), i18n("Properties"), menu, [url, parent]() {
            KPropertiesDialog::showDialog(url, parent);
        });
        ++added;
    }

    if (access) {
        const bool removable = removableDrive(volume);
        Solid::OpticalDrive *optical = opticalDrive(volume);
        if (!mounted) {
            menu->addAction(Icon(QStringLiteral("media-mount")), i18n("Mount"), menu, [udi]() {
                if (auto *a = Solid::Device(udi).as<Solid::StorageAccess>())
                    a->setup();
            });
            ++added;
        } else if (!optical) {
            menu->addAction(Icon(removable ? QStringLiteral("media-eject") : QStringLiteral("media-unmount")),
                            removable ? i18n("Safely Remove") : i18n("Unmount"), menu, [udi]() {
                                if (auto *a = Solid::Device(udi).as<Solid::StorageAccess>())
                                    a->teardown();
                            });
            ++added;
        }
        if (optical) {
            menu->addAction(Icon(QStringLiteral("media-eject")), i18n("Eject"), menu, [udi]() {
                Solid::Device dev(udi);
                if (auto *a = dev.as<Solid::StorageAccess>(); a && a->isAccessible())
                    a->teardown();
                if (auto *d = opticalDrive(dev))
                    d->eject();
            });
            ++added;
        }
    }

    // the desktop's disk tool on this drive (format, partitions, check, SMART)
    if (!blockDevice.isEmpty()) {
        const QString disks = QStandardPaths::findExecutable(QStringLiteral("gnome-disks"));
        const QString partitionManager = QStandardPaths::findExecutable(QStringLiteral("partitionmanager"));
        if (!disks.isEmpty() || !partitionManager.isEmpty()) {
            menu->addAction(Icon(QStringLiteral("drive-harddisk")), i18n("Format or Edit in Disks..."), menu, [=]() {
                if (!disks.isEmpty())
                    QProcess::startDetached(disks, {QStringLiteral("--block-device"), blockDevice});
                else
                    QProcess::startDetached(partitionManager, {});
            });
            ++added;
        }
    }
    return added;
#endif
}
