/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerodrivefilter.h"

#include <QStringList>

#include <KLocalizedString>

namespace PaneroDriveFilter
{
namespace
{
bool under(const QString &path, const QString &base)
{
    return path == base || path.startsWith(base + QLatin1Char('/'));
}

bool isNetworkFsType(const QString &t)
{
    static const QStringList types{QStringLiteral("nfs"),   QStringLiteral("nfs4"),       QStringLiteral("cifs"),
                                   QStringLiteral("smb3"),  QStringLiteral("smbfs"),      QStringLiteral("fuse.sshfs"),
                                   QStringLiteral("sshfs"), QStringLiteral("davfs"),      QStringLiteral("fuse.davfs2"),
                                   QStringLiteral("afs"),   QStringLiteral("fuse.rclone"), QStringLiteral("9p")};
    return types.contains(t);
}
}

bool isSystemFsType(const QString &t)
{
    static const QStringList types{
        QStringLiteral("squashfs"), QStringLiteral("tmpfs"),     QStringLiteral("devtmpfs"),   QStringLiteral("ramfs"),
        QStringLiteral("proc"),     QStringLiteral("sysfs"),     QStringLiteral("cgroup"),     QStringLiteral("cgroup2"),
        QStringLiteral("overlay"),  QStringLiteral("nsfs"),      QStringLiteral("autofs"),     QStringLiteral("fusectl"),
        QStringLiteral("efivarfs"), QStringLiteral("debugfs"),   QStringLiteral("tracefs"),    QStringLiteral("securityfs"),
        QStringLiteral("pstore"),   QStringLiteral("bpf"),       QStringLiteral("hugetlbfs"),  QStringLiteral("mqueue"),
        QStringLiteral("configfs"), QStringLiteral("binfmt_misc"), QStringLiteral("devpts"),   QStringLiteral("swap"),
        QStringLiteral("rpc_pipefs"), QStringLiteral("nfsd"),    QStringLiteral("fuse.portal"), QStringLiteral("fuse.gvfsd-fuse"),
        QStringLiteral("fuse.snapfuse"), QStringLiteral("selinuxfs"), QStringLiteral("zram")};
    return types.contains(t);
}

Kind classify(const QString &mountPoint, const QString &fsType, const QString &device, const QString &home, bool paneroMount)
{
    if (mountPoint == QLatin1String("/"))
        return Kind::Root;
    if (paneroMount || mountPoint.isEmpty())
        return Kind::Hidden;
    if (isSystemFsType(fsType))
        return Kind::Hidden;
    // AppImages mount themselves (fuse.<Name>.AppImage, source <Name>.AppImage)
    if (fsType.endsWith(QLatin1String(".AppImage"), Qt::CaseInsensitive) || device.endsWith(QLatin1String(".AppImage"), Qt::CaseInsensitive))
        return Kind::Hidden;
    if (device.startsWith(QLatin1String("/dev/loop")))
        return Kind::Hidden;
    static const QStringList systemFolders{
        QStringLiteral("/snap"),          QStringLiteral("/var/snap"),         QStringLiteral("/var/lib/snapd"),
        QStringLiteral("/var/lib/docker"), QStringLiteral("/var/lib/containers"), QStringLiteral("/var/lib/lxd"),
        QStringLiteral("/run/docker"),    QStringLiteral("/run/containerd"),   QStringLiteral("/run/snapd"),
        QStringLiteral("/run/credentials"), QStringLiteral("/run/lock"),       QStringLiteral("/run/user"),
        QStringLiteral("/proc"),          QStringLiteral("/sys"),              QStringLiteral("/dev"),
        QStringLiteral("/boot"),          QStringLiteral("/efi"),              QStringLiteral("/recovery")};
    for (const QString &f : systemFolders) {
        if (under(mountPoint, f))
            return Kind::Hidden;
    }
    if (mountPoint.startsWith(QLatin1String("/tmp/.mount_"))) // AppImage, older naming
        return Kind::Hidden;
    // the home folder's own partition: the Home button stands for it
    if (!home.isEmpty() && under(home, mountPoint))
        return Kind::Hidden;
    return isNetworkFsType(fsType) ? Kind::Network : Kind::Local;
}

QString networkLabel(const QString &mountPoint, const QString &fsType, const QString &device)
{
    Q_UNUSED(fsType)
    QString server, share;
    if (device.startsWith(QLatin1String("//"))) { // //server/share (SMB)
        const QStringList parts = device.mid(2).split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            server = parts.at(0);
            share = parts.at(1);
        }
    } else if (device.contains(QLatin1Char(':'))) { // server:/path (NFS) or user@server:path (SSH)
        server = device.section(QLatin1Char(':'), 0, 0).section(QLatin1Char('@'), -1);
        share = device.section(QLatin1Char(':'), 1).section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty);
    }
    if (!server.isEmpty() && !share.isEmpty())
        return i18nc("network drive button: %1 share, %2 server", "%1 on %2", share, server);
    const QString name = mountPoint.section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty);
    return name.isEmpty() ? mountPoint : name;
}
}
