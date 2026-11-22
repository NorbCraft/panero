/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroslowpaths.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>

#include <dirent.h>

namespace
{
QStringList bases()
{
    static QStringList b;
    if (b.isEmpty()) {
        // the same folders as PaneroCloudUtils::mountBaseDir()
        if (!qEnvironmentVariableIsEmpty("XDG_RUNTIME_DIR")) {
            const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
            if (!runtime.isEmpty())
                b << runtime + QLatin1String("/panero-cloud/") << runtime + QLatin1String("/gvfs/");
        }
        b << QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QLatin1String("/panero/mounts/");
    }
    return b;
}

std::atomic<bool> s_prefetching{false};
} // namespace

namespace PaneroSlowPaths
{
bool isSlowPath(const QString &path)
{
    for (const QString &b : bases()) {
        if (path.startsWith(b))
            return true;
    }
    return false;
}

QString mountRoot(const QString &path)
{
    for (const QString &b : bases()) {
        if (!path.startsWith(b))
            continue;
        const int slash = path.indexOf(QLatin1Char('/'), b.length());
        return slash < 0 ? path : path.left(slash);
    }
    return QString();
}

void prefetchListings(const QStringList &dirs, int max)
{
    if (dirs.isEmpty() || s_prefetching.exchange(true))
        return;
    const QStringList batch = dirs.mid(0, max);
    QtConcurrent::run([batch]() {
        for (const QString &d : batch) {
            // opendir/readdir only: no stat, no file opens
            DIR *dir = ::opendir(QFile::encodeName(d).constData());
            if (!dir)
                continue;
            while (::readdir(dir) != nullptr) {
            }
            ::closedir(dir);
        }
        s_prefetching = false;
    });
}
} // namespace PaneroSlowPaths
