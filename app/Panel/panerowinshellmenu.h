/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROWINSHELLMENU_H
#define PANEROWINSHELLMENU_H

#include <QIcon>
#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>

class QAction;
class QMenu;
class QWidget;

namespace PaneroWinShell
{
/// Command ids of Panero's own items start here; the shell's items use 1..0x7FFF.
constexpr unsigned MenuExtraFirst = 0x8000;

/// One of Panero's items at the top of the Explorer menu (id 0: a separator).
struct MenuExtra {
    unsigned id = 0;
    QString text;
    QString shortcut; ///< shown right-aligned, e.g. "F5"
    QIcon icon;
    bool checkable = false;
    bool checked = false;
    bool enabled = true;
};

/// False when the user chose Panero's own menu (Quick Settings > Right-click menu,
/// "Look&Feel" / "Explorer Menu") or PANERO_NO_EXPLORER_MENU is set.
bool explorerMenuEnabled();

/**
 * Explorer's menu of a drive (or another item given by its full path, "E:/")
 * with the items of @p panero on top: their texts, icons, check marks and
 * enabled states, separators; submenus are left out. A chosen shell command
 * (Properties, Format, Eject, BitLocker, ...) runs here; the shell's "Open"
 * returns @p openAction instead (Panero opens the drive in a panel).
 * @return false when Explorer's menu is off or the shell has none: the caller
 * shows @p panero itself. @p chosen: the action of @p panero, or nullptr.
 */
bool execItemMenu(QWidget *owner, const QString &path, const QMenu *panero, const QPoint &globalPos, QAction **chosen,
                  QAction *openAction = nullptr);

/**
 * Windows build only (D-019): the Windows shell's (Explorer's) context menu of
 * items in one folder, or of the folder's background, with Panero's own items
 * on top. Shell extensions ("Open with", "Send to", "New", third-party items)
 * get their menu messages through the owner window (IContextMenu2/3).
 *
 * Must be used in the GUI thread (single-threaded COM apartment). Building the
 * menu reads no file contents (cloud placeholders stay online-only, D-020).
 */
class ShellMenu
{
public:
    /// @p names: items in @p folder; empty for the folder's background menu.
    /// @p extendedVerbs: the menu Explorer shows with Shift held.
    ShellMenu(QWidget *owner, const QString &folder, const QStringList &names, bool extendedVerbs);
    /// The menu of items given by their full paths (all in one parent, e.g. drives
    /// in "This PC"), without the shell's Rename.
    ShellMenu(QWidget *owner, const QStringList &paths, bool extendedVerbs);
    ~ShellMenu();
    ShellMenu(const ShellMenu &) = delete;
    ShellMenu &operator=(const ShellMenu &) = delete;

    bool isValid() const;
    QString error() const;
    /// The shell's top-level items as "text<TAB>verb" (for the log and the tests).
    QStringList items() const;
    /// True when the shell (the cloud provider) already has "Always keep on this
    /// device" / "Free up space" or the like.
    bool hasProviderOfflineItems() const;
    /// Puts @p extras at the top, followed by a separator.
    void addExtras(const QList<MenuExtra> &extras);

    struct Choice {
        unsigned extra = 0;  ///< one of Panero's ids, or 0
        bool shell = false;  ///< an item of the shell
        QString verb;        ///< its canonical verb ("rename", "delete", "open", ...), may be empty
        QString text;
    };
    /// Shows the menu at @p globalPos (Qt's global coordinates) and returns the choice.
    Choice exec(const QPoint &globalPos);
    /// Runs the shell item that exec() returned.
    bool invoke(QString *error = nullptr);

private:
    struct Private;
    Private *const d;
};
}

#endif
