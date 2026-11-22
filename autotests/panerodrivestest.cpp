/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerodrivefilter.h"

#include <QTest>

using PaneroDriveFilter::Kind;

class PaneroDrivesTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void classify_data()
    {
        QTest::addColumn<QString>("mountPoint");
        QTest::addColumn<QString>("fsType");
        QTest::addColumn<QString>("device");
        QTest::addColumn<bool>("paneroMount");
        QTest::addColumn<int>("expected");
        const int hidden = int(Kind::Hidden), local = int(Kind::Local), network = int(Kind::Network), root = int(Kind::Root);
        // mount table of a Zorin OS 18 desktop
        QTest::newRow("root") << "/" << "ext4" << "/dev/nvme0n1p2" << false << root;
        QTest::newRow("efi") << "/boot/efi" << "vfat" << "/dev/nvme0n1p1" << false << hidden;
        QTest::newRow("snap") << "/snap/firefox/7084" << "squashfs" << "/dev/loop3" << false << hidden;
        QTest::newRow("snap on loop, other type") << "/media/x" << "ext4" << "/dev/loop9" << false << hidden;
        QTest::newRow("appimage") << "/tmp/.mount_BambuSPoCnbk" << "fuse.BambuStudio.AppImage" << "BambuStudio.AppImage" << false << hidden;
        QTest::newRow("portal") << "/run/user/1000/doc" << "fuse.portal" << "portal" << false << hidden;
        QTest::newRow("gvfs") << "/run/user/1000/gvfs" << "fuse.gvfsd-fuse" << "gvfsd-fuse" << false << hidden;
        QTest::newRow("tmpfs") << "/run" << "tmpfs" << "tmpfs" << false << hidden;
        QTest::newRow("panero cloud") << "/run/user/1000/panero-cloud/OneDrive" << "fuse.rclone" << "OneDrive:" << true << hidden;
        QTest::newRow("docker") << "/var/lib/docker/overlay2/x/merged" << "overlay" << "overlay" << false << hidden;
        QTest::newRow("home partition") << "/home" << "ext4" << "/dev/sda1" << false << hidden;
        // user-facing
        QTest::newRow("usb stick") << "/media/norbert/USB-STICK" << "vfat" << "/dev/sdb1" << false << local;
        QTest::newRow("data disk") << "/mnt/data" << "ext4" << "/dev/sdc1" << false << local;
        QTest::newRow("smb") << "/mnt/nas" << "cifs" << "//nas/share" << false << network;
        QTest::newRow("nfs") << "/mnt/backup" << "nfs4" << "nas:/export/backup" << false << network;
        QTest::newRow("sshfs") << "/home/norbert/server" << "fuse.sshfs" << "me@server:/srv" << false << network;
    }
    void classify()
    {
        QFETCH(QString, mountPoint);
        QFETCH(QString, fsType);
        QFETCH(QString, device);
        QFETCH(bool, paneroMount);
        QFETCH(int, expected);
        QCOMPARE(int(PaneroDriveFilter::classify(mountPoint, fsType, device, QStringLiteral("/home/norbert"), paneroMount)), expected);
    }
    void networkLabel()
    {
        QCOMPARE(PaneroDriveFilter::networkLabel(QStringLiteral("/mnt/nas"), QStringLiteral("cifs"), QStringLiteral("//nas/share")),
                 QStringLiteral("share on nas"));
        QCOMPARE(PaneroDriveFilter::networkLabel(QStringLiteral("/mnt/b"), QStringLiteral("nfs4"), QStringLiteral("nas:/export/backup")),
                 QStringLiteral("backup on nas"));
        QCOMPARE(PaneroDriveFilter::networkLabel(QStringLiteral("/mnt/s"), QStringLiteral("fuse.sshfs"), QStringLiteral("me@server:/srv/files")),
                 QStringLiteral("files on server"));
        QCOMPARE(PaneroDriveFilter::networkLabel(QStringLiteral("/mnt/odd"), QStringLiteral("9p"), QStringLiteral("hostshare")),
                 QStringLiteral("odd"));
    }
};

QTEST_GUILESS_MAIN(PaneroDrivesTest)
#include "panerodrivestest.moc"
