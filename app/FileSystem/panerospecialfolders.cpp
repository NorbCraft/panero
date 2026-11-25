/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerospecialfolders.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QStandardPaths>

namespace
{
QString clean(const QString &path)
{
    QString p = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    p = p.toLower();
#endif
    return p;
}

#ifndef Q_OS_WIN
/// XDG_PUBLICSHARE_DIR from user-dirs.dirs (Qt has no location for it)
QString publicShareDir()
{
    QFile f(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/user-dirs.dirs"));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    static const QRegularExpression re(QStringLiteral("^XDG_PUBLICSHARE_DIR=\"(.*)\"\\s*$"), QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = re.match(QString::fromUtf8(f.readAll()));
    if (!m.hasMatch())
        return QString();
    QString dir = m.captured(1);
    dir.replace(QStringLiteral("$HOME"), QDir::homePath());
    return dir;
}
#endif

const QHash<QString, QString> &table()
{
    static const QHash<QString, QString> folders = [] {
        QHash<QString, QString> t;
        const QString home = clean(QDir::homePath());
        auto add = [&](const QString &path, const char *icon) {
            const QString p = clean(path);
            // an unset XDG folder points at Home itself: keep Home's icon
            if (!path.isEmpty() && p != home && !t.contains(p))
                t.insert(p, QString::fromLatin1(icon));
        };
        add(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation), "user-desktop");
        add(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), "folder-documents");
        add(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), "folder-download");
        add(QStandardPaths::writableLocation(QStandardPaths::MusicLocation), "folder-music");
        add(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation), "folder-pictures");
        add(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation), "folder-videos");
#ifndef Q_OS_WIN
        add(QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/Templates"), "folder-templates");
        add(publicShareDir(), "folder-publicshare");
#endif
        t.insert(home, QStringLiteral("user-home"));
        return t;
    }();
    return folders;
}
} // namespace

QString PaneroSpecialFolders::iconName(const QString &localPath)
{
    if (localPath.isEmpty())
        return QString();
    return table().value(clean(localPath));
}
