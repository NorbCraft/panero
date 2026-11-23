/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerowinshell.h"

#include "panerotheme.h"
#include "../krglobal.h"
#include "../FileSystem/panerocloudfiles.h"

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
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

// QtGui (Qt 5) exports the HICON conversion that QtWinExtras uses
Q_GUI_EXPORT QPixmap qt_pixmapFromWinHICON(HICON icon);
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

/// The shell's icon (16, 32 and 48 px) of a path. @p generic: only the name and
/// @p attributes count (SHGFI_USEFILEATTRIBUTES, nothing is read from disk).
/// Pixmaps are made at once and checked, so a null result means "no icon".
QIcon shellIcon(const QString &path, bool generic, unsigned long attributes, bool overlays)
{
    QIcon icon;
#ifdef Q_OS_WIN
    static const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // the GUI thread has OLE already
    Q_UNUSED(com)
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    // overlays: the shell's own (shortcut arrow, sharing, ...); never with a generic icon
    const UINT base = (generic ? SHGFI_USEFILEATTRIBUTES : 0) | (overlays && !generic ? SHGFI_ADDOVERLAYS : 0);
    const DWORD attrs = generic ? DWORD(attributes) : 0;
    for (const UINT size : {UINT(SHGFI_SMALLICON), UINT(SHGFI_LARGEICON)}) {
        SHFILEINFOW info = {};
        if (SHGetFileInfoW(native.c_str(), attrs, &info, sizeof(info), SHGFI_ICON | size | base) && info.hIcon) {
            const QPixmap pm = qt_pixmapFromWinHICON(info.hIcon);
            DestroyIcon(info.hIcon);
            if (!pm.isNull())
                icon.addPixmap(pm);
        }
    }
    if (icon.isNull())
        return icon;
    // 48 px from the system image list (larger panel icon sizes)
    using GetImageList = HRESULT(WINAPI *)(int, REFIID, void **);
    static const auto getImageList = reinterpret_cast<GetImageList>(
        reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"shell32.dll"), "SHGetImageList")));
    SHFILEINFOW info = {};
    if (getImageList && SHGetFileInfoW(native.c_str(), attrs, &info, sizeof(info), SHGFI_SYSICONINDEX | (generic ? SHGFI_USEFILEATTRIBUTES : 0))) {
        static const GUID iidImageList = {0x46eb5926, 0x582e, 0x4017, {0x9f, 0xdf, 0xe8, 0x99, 0x8d, 0xaa, 0x09, 0x50}};
        void *list = nullptr;
        if (SUCCEEDED(getImageList(0x2 /*SHIL_EXTRALARGE*/, iidImageList, &list)) && list) {
            // an IImageList is also an HIMAGELIST
            HICON h = ImageList_GetIcon(static_cast<HIMAGELIST>(list), info.iIcon & 0xffffff, ILD_TRANSPARENT);
            if (h) {
                const QPixmap pm = qt_pixmapFromWinHICON(h);
                DestroyIcon(h);
                if (!pm.isNull() && pm.width() > 32)
                    icon.addPixmap(pm);
            }
            static_cast<IUnknown *>(list)->Release();
        }
    }
#else
    Q_UNUSED(path)
    Q_UNUSED(generic)
    Q_UNUSED(attributes)
    Q_UNUSED(overlays)
#endif
    return icon;
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
    QIcon icon = shellIcon(root, false, 0, true);
    if (icon.isNull())
        icon = provider().icon(QFileIconProvider::Drive);
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
        if (specialFolders().contains(clean.toLower()) || PaneroCloudFiles::isSyncRoot(clean))
            return QStringLiteral("dir:") + clean.toLower();
        // inside OneDrive & co. folders look like folders: no desktop.ini lookups
        // there (they would make the provider list the folder)
        if (!PaneroCloudFiles::isInSyncRoot(clean) && QFileInfo::exists(clean + QStringLiteral("/desktop.ini")))
            return QStringLiteral("dir:") + clean.toLower();
        return QStringLiteral("folder");
    }
    const QString suffix = QFileInfo(localPath).suffix().toLower();
    if (suffix.isEmpty())
        return QStringLiteral("file");
    // a cloud placeholder is never opened for its icon (that would download it)
    if (ownIconSuffixes().contains(suffix) && !PaneroCloudFiles::avoidReading(localPath))
        return QStringLiteral("own:") + localPath.toLower();
    return QStringLiteral("ext:") + suffix;
}

namespace
{
/// Icon of a kind of item (folder, file type), never read from disk; cached by key.
QIcon genericIcon(const QString &key, bool isDir)
{
    auto it = iconCache().constFind(key);
    if (it != iconCache().constEnd())
        return *it;
    constexpr unsigned long normal = 0x80; // FILE_ATTRIBUTE_NORMAL
    QIcon icon;
    if (key == QLatin1String("folder"))
        icon = shellIcon(QStringLiteral("folder"), true, PaneroCloudFiles::AttrDirectory, false);
    else if (key.startsWith(QLatin1String("ext:")))
        icon = shellIcon(QStringLiteral("file.") + key.mid(4), true, normal, false);
    else
        icon = shellIcon(QStringLiteral("file"), true, normal, false);
    if (icon.isNull()) // the shell gave nothing: the theme's icon, never an empty cell
        icon = QIcon::fromTheme(isDir ? QStringLiteral("folder") : QStringLiteral("unknown"),
                                provider().icon(isDir ? QFileIconProvider::Folder : QFileIconProvider::File));
    iconCache().insert(key, icon);
    return icon;
}
} // namespace

QIcon fileIcon(const QString &localPath, bool isDir)
{
    const QString key = iconKey(localPath, isDir);
    if (key == QLatin1String("folder") || key == QLatin1String("file") || key.startsWith(QLatin1String("ext:")))
        return genericIcon(key, isDir);
    auto it = iconCache().constFind(key);
    if (it != iconCache().constEnd())
        return *it;
    // own icon of this folder, drive or file; inside the sync folders without
    // the provider's overlays (Panero draws its own badges)
    QIcon icon = shellIcon(localPath, false, 0, !PaneroCloudFiles::rootFor(localPath));
    if (icon.isNull()) { // e.g. access denied: the icon of its kind
        const QString suffix = QFileInfo(localPath).suffix().toLower();
        icon = genericIcon(isDir ? QStringLiteral("folder") : suffix.isEmpty() ? QStringLiteral("file") : QStringLiteral("ext:") + suffix, isDir);
    }
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
