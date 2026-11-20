/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneropaths.h"

#include <QDir>
#include <QStorageInfo>

QUrl PaneroPaths::homeOrDriveRoot(const QUrl &url)
{
    if (!url.isValid() || url.scheme().isEmpty()) // e.g. a new tab that has no folder yet
        return QUrl::fromLocalFile(QDir::cleanPath(QDir::homePath()));
    if (!url.isLocalFile()) {
        QUrl root = url;
        root.setPath(QStringLiteral("/"));
        root.setQuery(QString());
        root.setFragment(QString());
        return root;
    }
    const QString path = QDir::cleanPath(url.toLocalFile());
    const QString home = QDir::cleanPath(QDir::homePath());
    if (path == home || path.startsWith(home + QLatin1Char('/')))
        return QUrl::fromLocalFile(home);
    const QString mountPoint = QStorageInfo(path).rootPath();
    return QUrl::fromLocalFile(mountPoint.isEmpty() ? QDir::rootPath() : mountPoint);
}
