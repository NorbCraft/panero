/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Windows build only (see CMakeLists.txt): Explorer's context menu (D-019).

#include "panerowinshellmenu.h"

#include "../FileSystem/panerocloudfiles.h"

#include <QAction>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMenu>
#include <QDir>
#include <QImage>
#include <QDebug>
#include <QPixmap>
#include <QThread>
#include <QWidget>
#include <QWindow>

#include <KConfigCore/KConfigGroup>
#include <KConfigCore/KSharedConfig>

#include <windows.h>
#include <commctrl.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstring>
#include <string>
#include <vector>

// PANERO_DEBUG_EXPLORER_MENU=1: the menus, choices and results in the log (GUI test)
static bool debugMenu()
{
    static const bool on = qEnvironmentVariableIsSet("PANERO_DEBUG_EXPLORER_MENU");
    return on;
}

namespace
{
constexpr UINT ShellFirst = 1;
constexpr UINT ShellLast = PaneroWinShell::MenuExtraFirst - 1;
constexpr UINT_PTR SubclassId = 0x50414e45; // "PANE"

template<typename T>
void release(T *&p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

QString itemText(HMENU menu, UINT item, bool byPosition)
{
    wchar_t buf[512];
    buf[0] = 0;
    MENUITEMINFOW mii = {};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = buf;
    mii.cch = 511;
    if (!GetMenuItemInfoW(menu, item, byPosition ? TRUE : FALSE, &mii))
        return QString();
    return QString::fromWCharArray(buf, int(qMin<UINT>(mii.cch, 511)));
}

/// A 32-bit premultiplied DIB for a menu item's picture (hbmpItem).
HBITMAP iconBitmap(const QIcon &icon)
{
    if (icon.isNull())
        return nullptr;
    const int size = GetSystemMetrics(SM_CXSMICON) > 0 ? GetSystemMetrics(SM_CXSMICON) : 16;
    // some icons (the application's) have no small size: take a larger one and scale it
    QImage img;
    for (const int request : {size, 32, 48, 64, 128, 256}) {
        QImage candidate = icon.pixmap(request, request).toImage();
        if (candidate.isNull())
            continue;
        if (candidate.width() != size || candidate.height() != size)
            candidate = candidate.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        candidate = candidate.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        bool visible = false;
        for (int y = 0; y < candidate.height() && !visible; ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(candidate.constScanLine(y));
            for (int x = 0; x < candidate.width() && !visible; ++x)
                visible = qAlpha(line[x]) > 0;
        }
        if (visible) {
            img = candidate;
            break;
        }
    }
    if (img.isNull())
        return nullptr;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = img.width();
    bmi.bmiHeader.biHeight = -img.height(); // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits)
        return bmp;
    for (int y = 0; y < img.height(); ++y) // QImage ARGB32 is B, G, R, A in memory, as the DIB
        std::memcpy(static_cast<uchar *>(bits) + size_t(y) * size_t(img.width()) * 4, img.constScanLine(y), size_t(img.width()) * 4);
    return bmp;
}
} // namespace

namespace PaneroWinShell
{
struct ShellMenu::Private {
    HWND hwnd = nullptr;
    QWidget *owner = nullptr;
    QString folder;
    bool ole = false;
    IContextMenu *cm = nullptr;
    IContextMenu2 *cm2 = nullptr;
    IContextMenu3 *cm3 = nullptr;
    HMENU menu = nullptr;
    std::vector<HBITMAP> bitmaps;
    QString error;
    UINT chosen = 0;
    POINT point = {0, 0};

    bool start(QWidget *w);
    void finish(UINT flags, const QStringList &names);

    QString verb(UINT id) const
    {
        if (!cm || id < ShellFirst || id > ShellLast)
            return QString();
        wchar_t buf[256];
        std::memset(buf, 0, sizeof(buf));
        if (FAILED(cm->GetCommandString(id - ShellFirst, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(buf), 255)))
            return QString();
        buf[255] = 0;
        return QString::fromWCharArray(buf);
    }

    static LRESULT CALLBACK ownerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
    {
        auto *d = reinterpret_cast<Private *>(ref);
        switch (msg) {
        case WM_INITMENUPOPUP:
        case WM_DRAWITEM:
        case WM_MEASUREITEM:
        case WM_MENUCHAR:
            // the submenus of shell extensions ("Open with", "Send to", "New", ...)
            if (d && d->cm3) {
                LRESULT result = 0;
                if (SUCCEEDED(d->cm3->HandleMenuMsg2(msg, wp, lp, &result)))
                    return msg == WM_INITMENUPOPUP ? 0 : (msg == WM_MENUCHAR ? result : TRUE);
            } else if (d && d->cm2 && msg != WM_MENUCHAR) {
                if (SUCCEEDED(d->cm2->HandleMenuMsg(msg, wp, lp)))
                    return msg == WM_INITMENUPOPUP ? 0 : TRUE;
            }
            break;
        default:
            break;
        }
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
};

namespace
{
QString nativeFolder(const QString &path)
{
    QString out = QDir::toNativeSeparators(QDir::cleanPath(path));
    if (out.endsWith(QLatin1Char(':'))) // "C:" -> "C:\"
        out += QLatin1Char('\\');
    return out;
}
} // namespace

bool ShellMenu::Private::start(QWidget *w)
{
    owner = w;
    if (!owner || QThread::currentThread() != QCoreApplication::instance()->thread()) {
        error = QStringLiteral("not in the GUI thread");
        return false;
    }
    hwnd = reinterpret_cast<HWND>(owner->window()->winId());
    // Qt's GUI thread is a single-threaded apartment with OLE already (drag and drop)
    const HRESULT init = OleInitialize(nullptr);
    if (FAILED(init)) {
        error = QStringLiteral("OleInitialize: 0x%1").arg(quint32(init), 8, 16, QLatin1Char('0'));
        return false;
    }
    ole = true;
    return true;
}

void ShellMenu::Private::finish(UINT flags, const QStringList &names)
{
    if (FAILED(cm->QueryInterface(IID_IContextMenu3, reinterpret_cast<void **>(&cm3))))
        cm3 = nullptr;
    if (!cm3 && FAILED(cm->QueryInterface(IID_IContextMenu2, reinterpret_cast<void **>(&cm2))))
        cm2 = nullptr;

    menu = CreatePopupMenu();
    const HRESULT hr = cm->QueryContextMenu(menu, 0, ShellFirst, ShellLast, flags);
    if (FAILED(hr)) {
        error = QStringLiteral("QueryContextMenu: 0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
        return;
    }
    // no separator at the very top or bottom
    while (GetMenuItemCount(menu) > 0 && (GetMenuState(menu, 0, MF_BYPOSITION) & MF_SEPARATOR))
        DeleteMenu(menu, 0, MF_BYPOSITION);
    while (GetMenuItemCount(menu) > 0 && (GetMenuState(menu, UINT(GetMenuItemCount(menu) - 1), MF_BYPOSITION) & MF_SEPARATOR))
        DeleteMenu(menu, UINT(GetMenuItemCount(menu) - 1), MF_BYPOSITION);
    if (debugMenu())
        qWarning() << "Explorer menu:" << "menu of" << folder << names << "flags" << Qt::hex << flags;
}

ShellMenu::ShellMenu(QWidget *owner, const QStringList &paths, bool extendedVerbs)
    : d(new Private)
{
    d->folder = nativeFolder(paths.value(0));
    if (paths.isEmpty()) {
        d->error = QStringLiteral("no items");
        return;
    }
    if (!d->start(owner))
        return;
    // the items' parent folder (for drives: "This PC") and their PIDLs in it
    std::vector<PIDLIST_ABSOLUTE> absolute;
    std::vector<PCUITEMID_CHILD> children;
    IShellFolder *parent = nullptr;
    HRESULT hr = S_OK;
    for (const QString &path : paths) {
        const std::wstring wpath = nativeFolder(path).toStdWString();
        PIDLIST_ABSOLUTE pidl = nullptr;
        hr = SHParseDisplayName(wpath.c_str(), nullptr, &pidl, 0, nullptr);
        if (FAILED(hr) || !pidl)
            break;
        absolute.push_back(pidl);
        PCUITEMID_CHILD child = nullptr;
        IShellFolder *itemParent = nullptr;
        hr = SHBindToParent(pidl, IID_IShellFolder, reinterpret_cast<void **>(&itemParent), &child);
        if (FAILED(hr))
            break;
        if (!parent)
            parent = itemParent; // the first item's parent (the others share it)
        else
            itemParent->Release();
        children.push_back(child);
    }
    if (SUCCEEDED(hr) && parent)
        hr = parent->GetUIObjectOf(d->hwnd, UINT(children.size()), children.data(), IID_IContextMenu, nullptr,
                                   reinterpret_cast<void **>(&d->cm));
    release(parent);
    for (PIDLIST_ABSOLUTE pidl : absolute)
        CoTaskMemFree(pidl); // the children point into these
    if (FAILED(hr) || !d->cm) {
        d->error = QStringLiteral("no shell menu for %1: 0x%2").arg(paths.join(QLatin1Char(' '))).arg(quint32(hr), 8, 16, QLatin1Char('0'));
        release(d->cm);
        return;
    }
    d->finish(CMF_NORMAL | (extendedVerbs ? CMF_EXTENDEDVERBS : 0), paths);
    if (debugMenu())
        qWarning() << "Explorer menu:" << "items" << items();
}

ShellMenu::ShellMenu(QWidget *owner, const QString &folder, const QStringList &names, bool extendedVerbs)
    : d(new Private)
{
    d->folder = nativeFolder(folder);
    if (!d->start(owner))
        return;

    const std::wstring wfolder = d->folder.toStdWString();
    PIDLIST_ABSOLUTE folderPidl = nullptr;
    IShellFolder *desktop = nullptr;
    IShellFolder *shellFolder = nullptr;
    std::vector<LPITEMIDLIST> children;
    HRESULT hr = SHParseDisplayName(wfolder.c_str(), nullptr, &folderPidl, 0, nullptr);
    if (SUCCEEDED(hr))
        hr = SHGetDesktopFolder(&desktop);
    if (SUCCEEDED(hr)) {
        if (folderPidl->mkid.cb == 0) {
            shellFolder = desktop;
            shellFolder->AddRef();
        } else {
            hr = desktop->BindToObject(folderPidl, nullptr, IID_IShellFolder, reinterpret_cast<void **>(&shellFolder));
        }
    }
    if (SUCCEEDED(hr)) {
        if (names.isEmpty()) {
            hr = shellFolder->CreateViewObject(d->hwnd, IID_IContextMenu, reinterpret_cast<void **>(&d->cm));
        } else {
            for (const QString &name : names) {
                std::wstring wname = name.toStdWString();
                PIDLIST_RELATIVE child = nullptr;
                hr = shellFolder->ParseDisplayName(d->hwnd, nullptr, &wname[0], nullptr, &child, nullptr);
                if (FAILED(hr) || !child)
                    break;
                children.push_back((LPITEMIDLIST)child);
            }
            if (SUCCEEDED(hr))
                hr = shellFolder->GetUIObjectOf(d->hwnd, UINT(children.size()), (PCUITEMID_CHILD_ARRAY)children.data(), IID_IContextMenu,
                                                nullptr, reinterpret_cast<void **>(&d->cm));
        }
    }
    for (LPITEMIDLIST child : children)
        CoTaskMemFree(child);
    if (folderPidl)
        CoTaskMemFree(folderPidl);
    release(shellFolder);
    release(desktop);
    if (FAILED(hr) || !d->cm) {
        d->error = QStringLiteral("no shell menu for %1: 0x%2").arg(d->folder).arg(quint32(hr), 8, 16, QLatin1Char('0'));
        release(d->cm);
        return;
    }
    d->finish(CMF_NORMAL | CMF_CANRENAME | (extendedVerbs ? CMF_EXTENDEDVERBS : 0), names);
    if (debugMenu())
        qWarning() << "Explorer menu:" << "items" << items();
}

ShellMenu::~ShellMenu()
{
    if (d->menu)
        DestroyMenu(d->menu);
    for (HBITMAP b : d->bitmaps)
        DeleteObject(b);
    release(d->cm3);
    release(d->cm2);
    release(d->cm);
    if (d->ole)
        OleUninitialize();
    delete d;
}

bool ShellMenu::isValid() const
{
    return d->cm && d->menu && d->error.isEmpty();
}

QString ShellMenu::error() const
{
    return d->error;
}

QStringList ShellMenu::items() const
{
    QStringList out;
    if (!d->menu)
        return out;
    const int count = GetMenuItemCount(d->menu);
    for (int i = 0; i < count; ++i) {
        MENUITEMINFOW mii = {};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_ID | MIIM_FTYPE | MIIM_SUBMENU;
        if (!GetMenuItemInfoW(d->menu, UINT(i), TRUE, &mii))
            continue;
        if (mii.fType & MFT_SEPARATOR) {
            out << QStringLiteral("-");
            continue;
        }
        const QString text = itemText(d->menu, UINT(i), true);
        out << text + QLatin1Char('\t') + (mii.hSubMenu ? QStringLiteral("(submenu)") : d->verb(mii.wID));
    }
    return out;
}

bool ShellMenu::hasProviderOfflineItems() const
{
    if (!d->menu)
        return false;
    std::vector<HMENU> menus{d->menu};
    for (size_t m = 0; m < menus.size() && m < 16; ++m) {
        const int count = GetMenuItemCount(menus[m]);
        for (int i = 0; i < count; ++i) {
            MENUITEMINFOW mii = {};
            mii.cbSize = sizeof(mii);
            mii.fMask = MIIM_ID | MIIM_FTYPE | MIIM_SUBMENU;
            if (!GetMenuItemInfoW(menus[m], UINT(i), TRUE, &mii) || (mii.fType & MFT_SEPARATOR))
                continue;
            if (mii.hSubMenu) {
                menus.push_back(mii.hSubMenu); // filled already (not one of the lazy ones)
                continue;
            }
            if (PaneroCloudFiles::isProviderOfflineItem(itemText(menus[m], UINT(i), true), d->verb(mii.wID)))
                return true;
        }
    }
    return false;
}

void ShellMenu::addExtras(const QList<MenuExtra> &extras)
{
    if (!d->menu || extras.isEmpty())
        return;
    UINT pos = 0;
    for (const MenuExtra &e : extras) {
        MENUITEMINFOW mii = {};
        mii.cbSize = sizeof(mii);
        std::wstring text;
        if (e.id == 0) {
            mii.fMask = MIIM_FTYPE;
            mii.fType = MFT_SEPARATOR;
        } else {
            text = (e.shortcut.isEmpty() ? e.text : e.text + QLatin1Char('\t') + e.shortcut).toStdWString();
            mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
            mii.wID = e.id;
            mii.dwTypeData = &text[0];
            mii.fState = ((e.checkable && e.checked) ? MFS_CHECKED : MFS_UNCHECKED) | (e.enabled ? MFS_ENABLED : MFS_DISABLED);
            if (!e.checkable) { // a check mark instead of the picture
                if (HBITMAP bmp = iconBitmap(e.icon)) {
                    d->bitmaps.push_back(bmp);
                    mii.fMask |= MIIM_BITMAP;
                    mii.hbmpItem = bmp;
                }
            }
        }
        InsertMenuItemW(d->menu, pos++, TRUE, &mii);
    }
    if (GetMenuItemCount(d->menu) > int(pos))
        InsertMenuW(d->menu, pos, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
}

ShellMenu::Choice ShellMenu::exec(const QPoint &globalPos)
{
    Choice choice;
    if (!isValid())
        return choice;
    // Qt's global coordinates -> the screen's pixels (high DPI scaling)
    POINT pt = {globalPos.x(), globalPos.y()};
    if (QWindow *w = d->owner->window()->windowHandle()) {
        const qreal dpr = w->devicePixelRatio();
        const QPoint local = w->mapFromGlobal(globalPos);
        pt.x = LONG(local.x() * dpr);
        pt.y = LONG(local.y() * dpr);
        ClientToScreen(d->hwnd, &pt);
    }
    d->point = pt;
    // the owner window forwards the menu messages to the shell extensions while the menu is open
    const bool subclassed = SetWindowSubclass(d->hwnd, &Private::ownerProc, SubclassId, reinterpret_cast<DWORD_PTR>(d));
    const UINT cmd = UINT(TrackPopupMenuEx(d->menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, d->hwnd, nullptr));
    if (subclassed)
        RemoveWindowSubclass(d->hwnd, &Private::ownerProc, SubclassId);
    if (cmd == 0)
        return choice;
    d->chosen = cmd;
    choice.text = itemText(d->menu, cmd, false);
    if (cmd >= MenuExtraFirst) {
        choice.extra = cmd;
    } else {
        choice.shell = true;
        choice.verb = d->verb(cmd).toLower();
    }
    if (debugMenu())
        qWarning() << "Explorer menu:" << "chosen" << cmd << choice.text << "verb" << choice.verb;
    return choice;
}

bool ShellMenu::invoke(QString *error)
{
    if (!isValid() || d->chosen < ShellFirst || d->chosen > ShellLast)
        return false;
    const UINT offset = d->chosen - ShellFirst;
    const std::wstring wdir = d->folder.toStdWString();
    const QByteArray adir = d->folder.toLocal8Bit();
    CMINVOKECOMMANDINFOEX ici = {};
    ici.cbSize = sizeof(ici);
    ici.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
    if (GetKeyState(VK_CONTROL) < 0)
        ici.fMask |= CMIC_MASK_CONTROL_DOWN;
    if (GetKeyState(VK_SHIFT) < 0)
        ici.fMask |= CMIC_MASK_SHIFT_DOWN;
    ici.hwnd = d->hwnd;
    ici.lpVerb = MAKEINTRESOURCEA(offset);
    ici.lpVerbW = MAKEINTRESOURCEW(offset);
    ici.lpDirectory = adir.constData();
    ici.lpDirectoryW = wdir.c_str();
    ici.nShow = SW_SHOWNORMAL;
    ici.ptInvoke = d->point;
    const HRESULT hr = d->cm->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&ici));
    if (debugMenu())
        qWarning() << "Explorer menu:" << "invoked" << d->chosen << "hr" << Qt::hex << quint32(hr);
    if (FAILED(hr) && error)
        *error = QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
    // HRESULT_FROM_WIN32(ERROR_CANCELLED): the user cancelled a dialog of the command
    return SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_CANCELLED);
}

bool explorerMenuEnabled()
{
    if (qEnvironmentVariableIsSet("PANERO_NO_EXPLORER_MENU"))
        return false;
    return KConfigGroup(KSharedConfig::openConfig(), "Look&Feel").readEntry("Explorer Menu", true);
}

bool execItemMenu(QWidget *owner, const QString &path, const QMenu *panero, const QPoint &globalPos, QAction **chosen,
                  QAction *openAction)
{
    if (chosen)
        *chosen = nullptr;
    if (!explorerMenuEnabled() || path.isEmpty())
        return false;
    const bool shift = QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier;
    ShellMenu menu(owner, QStringList{path}, shift);
    if (!menu.isValid()) {
        qWarning() << "Explorer menu not available:" << menu.error();
        return false;
    }
    // Panero's items: separators, no submenus, no doubled or edge separators
    QList<MenuExtra> extras;
    QList<QAction *> actions;
    if (panero) {
        const QList<QAction *> all = panero->actions();
        for (QAction *a : all) {
            if (!a->isVisible() || a->menu())
                continue;
            if (a->isSeparator()) {
                if (!extras.isEmpty() && extras.last().id != 0)
                    extras << MenuExtra();
                continue;
            }
            MenuExtra e;
            e.id = MenuExtraFirst + unsigned(actions.size());
            e.text = a->text().remove(QLatin1Char('&'));
            e.shortcut = a->shortcut().toString(QKeySequence::NativeText);
            e.icon = a->icon();
            e.checkable = a->isCheckable();
            e.checked = a->isChecked();
            e.enabled = a->isEnabled();
            extras << e;
            actions << a;
        }
    }
    while (!extras.isEmpty() && extras.last().id == 0)
        extras.removeLast();
    menu.addExtras(extras);
    const ShellMenu::Choice choice = menu.exec(globalPos);
    if (choice.extra) {
        if (chosen)
            *chosen = actions.value(int(choice.extra - MenuExtraFirst));
        return true;
    }
    if (!choice.shell)
        return true;
    if (openAction && choice.verb == QLatin1String("open")) {
        if (chosen)
            *chosen = openAction; // in a panel, not in an Explorer window
        return true;
    }
    QString error;
    if (!menu.invoke(&error))
        qWarning() << "Explorer menu command failed:" << choice.text << choice.verb << error;
    return true;
}
} // namespace PaneroWinShell
