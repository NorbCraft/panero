/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerowinshell.h"

#include "panerotheme.h"
#include "../krglobal.h"

#include <QDir>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QProcess>
#include <QHash>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>

#include <KConfigCore/KConfigGroup>
#include <KConfigCore/KSharedConfig>
#include <KI18n/KLocalizedString>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace
{
QHash<QString, QIcon> &iconCache()
{
    static QHash<QString, QIcon> cache;
    return cache;
}

QFileIconProvider &provider()
{
    static QFileIconProvider p;
    return p;
}

int &shellSetting()
{
    static int value = -1; // -1 unknown, 0 theme icons, 1 shell icons for files, 3 also folders
    return value;
}

// Files whose icon is stored in the file itself (or in what it points to)
const QSet<QString> &ownIconSuffixes()
{
    static const QSet<QString> s = {QStringLiteral("exe"), QStringLiteral("lnk"), QStringLiteral("ico"),
                                    QStringLiteral("url"), QStringLiteral("scr"), QStringLiteral("cpl"),
                                    QStringLiteral("msc"), QStringLiteral("appref-ms"), QStringLiteral("cur"),
                                    QStringLiteral("ani")};
    return s;
}

const QSet<QString> &specialFolders()
{
    static QSet<QString> s;
    if (s.isEmpty()) {
        for (auto loc : {QStandardPaths::HomeLocation, QStandardPaths::DesktopLocation, QStandardPaths::DocumentsLocation,
                         QStandardPaths::DownloadLocation, QStandardPaths::MusicLocation, QStandardPaths::PicturesLocation,
                         QStandardPaths::MoviesLocation}) {
            const QString p = QStandardPaths::writableLocation(loc);
            if (!p.isEmpty())
                s.insert(QDir::cleanPath(p).toLower());
        }
    }
    return s;
}
} // namespace

namespace PaneroWinShell
{
QString driveName(const QString &rootPath)
{
    const QString root = QStorageInfo(rootPath).rootPath();
    const QString letter = (root.isEmpty() ? rootPath : root).left(2).toUpper(); // "C:"
#ifdef Q_OS_WIN
    SHFILEINFOW info = {};
    const std::wstring native = QDir::toNativeSeparators(letter + QLatin1Char('/')).toStdWString();
    if (SHGetFileInfoW(native.c_str(), 0, &info, sizeof(info), SHGFI_DISPLAYNAME))
        return QString::fromWCharArray(info.szDisplayName);
#endif
    const QString label = QStorageInfo(rootPath).name();
    return i18nc("drive name (drive letter)", "%1 (%2)", label.isEmpty() ? i18n("Local Disk") : label, letter);
}

QIcon driveIcon(const QString &rootPath)
{
    QString root = QStorageInfo(rootPath).rootPath();
    if (root.isEmpty())
        root = rootPath.left(2) + QLatin1Char('/');
    const QString key = QStringLiteral("drive:") + root.toLower();
    auto it = iconCache().constFind(key);
    if (it != iconCache().constEnd())
        return *it;
    const QIcon icon = provider().icon(QFileInfo(root));
    iconCache().insert(key, icon);
    return icon;
}

QList<Drive> drives()
{
    QList<Drive> list;
    for (const QFileInfo &fi : QDir::drives()) {
        const QString root = fi.absoluteFilePath(); // "C:/"
        list.append({root, driveName(root), driveIcon(root)});
    }
    return list;
}

bool useShellIcons(bool isDir)
{
    int &setting = shellSetting();
    if (setting < 0) {
        const KConfigGroup look(krConfig ? KConfigGroup(krConfig, PaneroTheme::GROUP)
                                         : KConfigGroup(KSharedConfig::openConfig(), PaneroTheme::GROUP));
        const bool line = look.readEntry("Icon Set", QString()) == QLatin1String("line");
        const bool folderColor = PaneroTheme::readColor(look.readEntry("Folder Color", QString())).isValid();
        setting = line ? 0 : (folderColor ? 1 : 3);
    }
    return isDir ? setting == 3 : setting >= 1;
}

QString iconKey(const QString &localPath, bool isDir)
{
    if (isDir) {
        const QString clean = QDir::cleanPath(localPath);
        if (clean.length() <= 3) // drive root
            return QStringLiteral("drive:") + clean.toLower();
        if (specialFolders().contains(clean.toLower()) || QFileInfo::exists(clean + QStringLiteral("/desktop.ini")))
            return QStringLiteral("dir:") + clean.toLower();
        return QStringLiteral("folder");
    }
    const QString suffix = QFileInfo(localPath).suffix().toLower();
    if (suffix.isEmpty())
        return QStringLiteral("file");
    if (ownIconSuffixes().contains(suffix))
        return QStringLiteral("own:") + localPath.toLower();
    return QStringLiteral("ext:") + suffix;
}

QIcon fileIcon(const QString &localPath, bool isDir)
{
    const QString key = iconKey(localPath, isDir);
    auto it = iconCache().constFind(key);
    if (it != iconCache().constEnd())
        return *it;
    QIcon icon = provider().icon(QFileInfo(localPath));
    if (icon.isNull())
        icon = provider().icon(isDir ? QFileIconProvider::Folder : QFileIconProvider::File);
    iconCache().insert(key, icon);
    return icon;
}

void reset()
{
    shellSetting() = -1;
    iconCache().clear();
}

bool createSymlink(const QString &target, const QString &linkPath)
{
#ifdef Q_OS_WIN
    DWORD flags = 0x2; // SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE (Developer Mode)
    if (QFileInfo(target).isDir())
        flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    const std::wstring link = QDir::toNativeSeparators(linkPath).toStdWString();
    const std::wstring dest = QDir::toNativeSeparators(target).toStdWString();
    return CreateSymbolicLinkW(link.c_str(), dest.c_str(), flags) != 0;
#else
    return QFile::link(target, linkPath);
#endif
}

bool startInNewConsole(const QStringList &command, const QString &workingDirectory)
{
    if (command.isEmpty())
        return false;
    QProcess process;
    process.setProgram(command.first());
    process.setArguments(command.mid(1));
    process.setWorkingDirectory(QDir::toNativeSeparators(workingDirectory));
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NEW_CONSOLE;
        args->flags &= ~DWORD(DETACHED_PROCESS | CREATE_NO_WINDOW);
        args->startupInfo->dwFlags &= ~DWORD(STARTF_USESTDHANDLES);
        args->inheritHandles = false;
    });
#endif
    return process.startDetached();
}

bool createHardLink(const QString &target, const QString &linkPath)
{
#ifdef Q_OS_WIN
    const std::wstring link = QDir::toNativeSeparators(linkPath).toStdWString();
    const std::wstring dest = QDir::toNativeSeparators(target).toStdWString();
    return CreateHardLinkW(link.c_str(), dest.c_str(), nullptr) != 0;
#else
    Q_UNUSED(target)
    Q_UNUSED(linkPath)
    return false;
#endif
}
} // namespace PaneroWinShell
