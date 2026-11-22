/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroslowpaths.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QThreadPool>
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

/// own folders below the mount bases (not gvfs: those are not Panero's)
QStringList cloudBases()
{
    static QStringList b;
    if (b.isEmpty()) {
        if (!qEnvironmentVariableIsEmpty("XDG_RUNTIME_DIR")) {
            const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
            if (!runtime.isEmpty())
                b << runtime + QLatin1String("/panero-cloud/");
        }
        b << QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QLatin1String("/panero/mounts/");
    }
    return b;
}

std::atomic<int> s_generation{0};

QThreadPool *prefetchPool()
{
    static QThreadPool *pool = nullptr;
    if (!pool) {
        pool = new QThreadPool;
        // a few listings at a time: faster than one by one, gentle on the provider
        pool->setMaxThreadCount(4);
        pool->setExpiryTimeout(30000);
    }
    return pool;
}

PaneroSlowPaths::Revalidator &revalidator()
{
    static PaneroSlowPaths::Revalidator r;
    return r;
}

PaneroSlowPaths::NameProvider &nameProvider()
{
    static PaneroSlowPaths::NameProvider n;
    return n;
}
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

bool isCloudRoot(const QString &path)
{
    QString p = path;
    while (p.length() > 1 && p.endsWith(QLatin1Char('/')))
        p.chop(1);
    for (const QString &b : cloudBases()) {
        if (p.startsWith(b) && p.length() > b.length() && p.indexOf(QLatin1Char('/'), b.length()) < 0)
            return true;
    }
    return false;
}

void setCurrentDir(const QString &path)
{
    QDir::setCurrent(isSlowPath(path) ? QDir::homePath() : path);
}

void prefetchListings(const QStringList &dirs, int max)
{
    const int generation = ++s_generation;
    if (dirs.isEmpty())
        return;
    QThreadPool *pool = prefetchPool();
    pool->clear(); // folders of the previous folder that did not start yet
    for (const QString &d : dirs.mid(0, max)) {
        QtConcurrent::run(pool, [d, generation]() {
            if (generation != s_generation)
                return; // the user went somewhere else
            // opendir/readdir only: no stat, no file opens
            DIR *dir = ::opendir(QFile::encodeName(d).constData());
            if (!dir)
                return;
            while (::readdir(dir) != nullptr) {
            }
            ::closedir(dir);
        });
    }
}

void setRevalidator(const Revalidator &r)
{
    revalidator() = r;
}

bool revalidate(const QString &dir, QObject *context, const std::function<void()> &done)
{
    return revalidator() && revalidator()(dir, context, done);
}
void setNameProvider(const NameProvider &provider)
{
    nameProvider() = provider;
}

QString displayName(const QString &mountRoot)
{
    return nameProvider() ? nameProvider()(mountRoot) : QString();
}
} // namespace PaneroSlowPaths
