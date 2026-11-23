/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneropaths.h"
#include "../FileSystem/paneroslowpaths.h"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

QUrl PaneroPaths::homeOrDriveRoot(const QUrl &url)
{
    if (!url.isValid() || url.scheme().isEmpty()) // e.g. a new tab that has no folder yet
        return QUrl::fromLocalFile(QDir::cleanPath(QDir::homePath()));
    // An archive opened as a folder (krarc:/home/me/a.zip/sub): its path is a
    // local path, and "krarc:/" itself is no folder (KIO reports "does not
    // exist"). The tree shows the local folders the archive is in.
    static const QStringList archiveSchemes = {QStringLiteral("krarc"), QStringLiteral("zip"), QStringLiteral("tar"),
                                               QStringLiteral("iso"),   QStringLiteral("gzip"), QStringLiteral("bzip2"),
                                               QStringLiteral("xz"),    QStringLiteral("lzma"), QStringLiteral("ar")};
    if (archiveSchemes.contains(url.scheme())) {
        QString path = QDir::cleanPath(url.path());
        if (!PaneroSlowPaths::isSlowPath(path)) {
            while (path.length() > 1 && !QFileInfo(path).isDir())
                path = QFileInfo(path).path();
        }
        return homeOrDriveRoot(QUrl::fromLocalFile(path));
    }
    // Krusader's virtual folders (search results, virt:/) are no KIO folders
    if (url.scheme() == QLatin1String("virt"))
        return QUrl::fromLocalFile(QDir::cleanPath(QDir::homePath()));
    if (!url.isLocalFile()) {
        QUrl root = url;
        root.setPath(QStringLiteral("/"));
        root.setQuery(QString());
        root.setFragment(QString());
        return root;
    }
    const QString path = QDir::cleanPath(url.toLocalFile());
    // a cloud drive: its top folder (and no statfs: a network request there)
    const QString cloudRoot = PaneroSlowPaths::mountRoot(path);
    if (!cloudRoot.isEmpty())
        return QUrl::fromLocalFile(cloudRoot);
    const QString home = QDir::cleanPath(QDir::homePath());
    if (path == home || path.startsWith(home + QLatin1Char('/')))
        return QUrl::fromLocalFile(home);
    const QString mountPoint = QStorageInfo(path).rootPath();
    return QUrl::fromLocalFile(mountPoint.isEmpty() ? QDir::rootPath() : mountPoint);
}
