/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroiconthemes.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#include <KI18n/KLocalizedString>

namespace
{
QString cacheRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/panero/icon-themes");
}

/// where the bundled Panero Line icons are installed (or the source tree in a dev run)
QString lineIconsDir(bool dark)
{
    const QString sub = dark ? QStringLiteral("panero-line/dark") : QStringLiteral("panero-line/light");
    const QString installed = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                     QStringLiteral("panero/icons/") + sub, QStandardPaths::LocateDirectory);
    return installed;
}

void ensureSearchPath()
{
    const QString root = cacheRoot();
    QStringList paths = QIcon::themeSearchPaths();
    if (!paths.contains(root)) {
        paths.prepend(root);
        QIcon::setThemeSearchPaths(paths);
    }
}

QString themeDir(const QString &name)
{
    for (const QString &root : QIcon::themeSearchPaths()) {
        const QString dir = root + QLatin1Char('/') + name;
        if (QFileInfo::exists(dir + QStringLiteral("/index.theme")))
            return dir;
    }
    return QString();
}

/// first theme in the inheritance chain of @p name that has folder icons
QString folderProvider(const QString &name, int depth = 0)
{
    const QString dir = themeDir(name);
    if (dir.isEmpty() || depth > 6)
        return QString();
    QSettings index(dir + QStringLiteral("/index.theme"), QSettings::IniFormat);
    const QStringList dirs = index.value(QStringLiteral("Icon Theme/Directories")).toStringList();
    for (const QString &d : dirs) {
        if (d.contains(QLatin1String("places")) && (QFileInfo::exists(dir + QLatin1Char('/') + d + QStringLiteral("/folder.png"))
                                                    || QFileInfo::exists(dir + QLatin1Char('/') + d + QStringLiteral("/folder.svg"))))
            return name;
    }
    for (const QString &parent : index.value(QStringLiteral("Icon Theme/Inherits")).toStringList()) {
        const QString found = folderProvider(parent.trimmed(), depth + 1);
        if (!found.isEmpty())
            return found;
    }
    return QString();
}

QString colorKey(const QColor &c)
{
    return c.name().mid(1);
}

/// Panero Line layer: index.theme inheriting @p base + a symlink to the installed icons
QString lineTheme(const QString &base, bool dark)
{
    const QString icons = lineIconsDir(dark);
    if (icons.isEmpty())
        return base;
    const QString name = QStringLiteral("panero-line-") + (dark ? QStringLiteral("dark-") : QStringLiteral("light-")) + base;
    const QString dir = cacheRoot() + QLatin1Char('/') + name;
    QDir().mkpath(dir);
    QFile::remove(dir + QStringLiteral("/actions"));
    QFile::link(icons, dir + QStringLiteral("/actions"));
    QFile index(dir + QStringLiteral("/index.theme"));
    if (index.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        index.write(QStringLiteral("[Icon Theme]\nName=Panero Line\nComment=Lucide icons for Panero\n"
                                   "Inherits=%1,hicolor\nDirectories=actions\n\n"
                                   "[actions]\nSize=24\nMinSize=8\nMaxSize=512\nType=Scalable\nContext=Actions\n")
                        .arg(base)
                        .toUtf8());
    }
    return name;
}

bool isFolderIcon(const QString &file)
{
    static const QRegularExpression re(QStringLiteral("^(folder|user-home|user-desktop|inode-directory)[^/]*\\.(png|svg)$"));
    return re.match(file).hasMatch();
}

/// Folder layer: the provider's folder icons recoloured, inheriting @p base
QString folderTheme(const QString &base, const QColor &color)
{
    const QString provider = folderProvider(base);
    if (provider.isEmpty())
        return base;
    const QString name = QStringLiteral("panero-folders-") + colorKey(color) + QLatin1Char('-') + base;
    const QString dir = cacheRoot() + QLatin1Char('/') + name;
    const QString providerDir = themeDir(provider);
    if (QFileInfo::exists(dir + QStringLiteral("/.complete")))
        return name;

    QSettings source(providerDir + QStringLiteral("/index.theme"), QSettings::IniFormat);
    QStringList written;
    QString sections;
    for (const QString &d : source.value(QStringLiteral("Icon Theme/Directories")).toStringList()) {
        if (!d.contains(QLatin1String("places")))
            continue;
        source.beginGroup(d);
        const int size = source.value(QStringLiteral("Size")).toInt();
        const int scale = qMax(1, source.value(QStringLiteral("Scale"), 1).toInt());
        QString section = QStringLiteral("[%1]\n").arg(d);
        for (const QString &key : source.childKeys())
            section += key + QLatin1Char('=') + source.value(key).toString() + QLatin1Char('\n');
        source.endGroup();
        if (size * scale > 256) // panels and thumbnails never need more; keeps generation fast
            continue;
        const QDir in(providerDir + QLatin1Char('/') + d);
        bool any = false;
        for (const QString &file : in.entryList(QDir::Files)) {
            if (!isFolderIcon(file))
                continue;
            QDir().mkpath(dir + QLatin1Char('/') + d);
            const QString target = dir + QLatin1Char('/') + d + QLatin1Char('/') + file;
            if (file.endsWith(QLatin1String(".png"))) {
                QImage image(in.filePath(file));
                if (image.isNull())
                    continue;
                image = image.convertToFormat(QImage::Format_ARGB32);
                for (int y = 0; y < image.height(); ++y) {
                    auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
                    for (int x = 0; x < image.width(); ++x) {
                        if (qAlpha(line[x]) == 0)
                            continue;
                        line[x] = PaneroIconThemes::recolor(QColor::fromRgba(line[x]), color).rgba();
                    }
                }
                image.save(target);
            } else {
                QFile f(in.filePath(file));
                if (!f.open(QIODevice::ReadOnly))
                    continue;
                QString svg = QString::fromUtf8(f.readAll());
                static const QRegularExpression hex(QStringLiteral("#([0-9a-fA-F]{6})\\b"));
                QString out;
                int last = 0;
                for (auto it = hex.globalMatch(svg); it.hasNext();) {
                    const auto m = it.next();
                    out += svg.mid(last, m.capturedStart() - last) + PaneroIconThemes::recolor(QColor(m.captured(0)), color).name();
                    last = m.capturedEnd();
                }
                out += svg.mid(last);
                QFile o(target);
                if (o.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    o.write(out.toUtf8());
            }
            any = true;
        }
        if (any) {
            written << d;
            sections += section + QLatin1Char('\n');
        }
    }
    if (written.isEmpty())
        return base;
    QFile index(dir + QStringLiteral("/index.theme"));
    if (index.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        index.write(QStringLiteral("[Icon Theme]\nName=Panero folders\nInherits=%1,hicolor\nDirectories=%2\n\n%3")
                        .arg(base, written.join(QLatin1Char(',')), sections)
                        .toUtf8());
    }
    QFile done(dir + QStringLiteral("/.complete"));
    done.open(QIODevice::WriteOnly);
    return name;
}
} // namespace

QColor PaneroIconThemes::recolor(const QColor &pixel, const QColor &target)
{
    qreal h, s, l, a;
    pixel.getHslF(&h, &s, &l, &a);
    if (s < 0.18) // greys, white highlights and dark outlines stay as they are
        return pixel;
    qreal th, ts, tl, ta;
    target.getHslF(&th, &ts, &tl, &ta);
    // keep the icon's shading (lightness), take the target's hue and saturation
    // (scaled by how saturated this pixel was, so shades keep their relation)
    const qreal saturation = qBound<qreal>(0.0, ts * (s / 0.75), 1.0);
    // shift the lightness towards the target so pale/dark presets look right
    const qreal lightness = qBound<qreal>(0.0, l + (tl - 0.55) * 0.6, 1.0);
    QColor c;
    c.setHslF(th < 0 ? 0 : th, saturation, lightness, a);
    return c;
}

QList<QPair<QString, QColor>> PaneroIconThemes::folderColorPresets()
{
    // GNOME accent palette, readable in light and dark
    return {
        {i18nc("folder colour", "Blue"), QColor(0x35, 0x84, 0xe4)},
        {i18nc("folder colour", "Teal"), QColor(0x21, 0x90, 0xa4)},
        {i18nc("folder colour", "Green"), QColor(0x3a, 0x94, 0x4a)},
        {i18nc("folder colour", "Yellow"), QColor(0xc8, 0x88, 0x00)},
        {i18nc("folder colour", "Orange"), QColor(0xed, 0x5b, 0x00)},
        {i18nc("folder colour", "Red"), QColor(0xe6, 0x2d, 0x42)},
        {i18nc("folder colour", "Pink"), QColor(0xd5, 0x61, 0x99)},
        {i18nc("folder colour", "Purple"), QColor(0x91, 0x41, 0xac)},
        {i18nc("folder colour", "Slate"), QColor(0x6f, 0x83, 0x96)},
    };
}

QString PaneroIconThemes::compose(const QString &base, bool line, bool dark, const QColor &folders)
{
    if (!line && !folders.isValid())
        return base;
    ensureSearchPath();
    QString theme = base;
    if (line)
        theme = lineTheme(theme, dark);
    if (folders.isValid())
        theme = folderTheme(theme, folders);
    return theme;
}
