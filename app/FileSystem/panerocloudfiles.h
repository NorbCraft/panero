/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROCLOUDFILES_H
#define PANEROCLOUDFILES_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QString>
#include <QStringList>

/**
 * Windows cloud folders (D-020): the sync folders of OneDrive, Google Drive for
 * desktop, Dropbox, ... and their Cloud Files placeholders.
 *
 * A placeholder (attribute RECALL_ON_DATA_ACCESS or OFFLINE) is a file whose
 * content is still in the cloud: reading it downloads it. Panero never reads
 * such a file while listing or painting (type from the extension, icons from
 * the attributes, no previews); opening it explicitly (Enter, F3, F4) may
 * download it, as in Explorer. "Always keep on this device" / "Free up space"
 * set the PINNED / UNPINNED attributes, which the provider acts on.
 *
 * The pure functions (attributes -> state, registry and JSON parsing) work on
 * every platform and are unit-tested on Linux; the rest only finds folders on
 * Windows (elsewhere there are none, so nothing changes on Linux).
 */
namespace PaneroCloudFiles
{
// Windows file attributes (winnt.h)
constexpr quint32 AttrReadOnly = 0x1;
constexpr quint32 AttrHidden = 0x2;
constexpr quint32 AttrSystem = 0x4;
constexpr quint32 AttrDirectory = 0x10;
constexpr quint32 AttrReparsePoint = 0x400;
constexpr quint32 AttrOffline = 0x1000;
constexpr quint32 AttrRecallOnOpen = 0x40000;
constexpr quint32 AttrPinned = 0x80000;
constexpr quint32 AttrUnpinned = 0x100000;
constexpr quint32 AttrRecallOnDataAccess = 0x400000;
constexpr quint32 InvalidAttributes = 0xFFFFFFFFu;

/// Badge state; the same values as PaneroOffline::State on Linux.
enum class State { NotCloud, OnlineOnly, Available, Pinned };

/// True when reading the content would download it (RECALL_ON_DATA_ACCESS, OFFLINE).
bool isPlaceholder(quint32 attributes);
/// The badge Explorer shows for an item inside a sync folder.
State stateFromAttributes(quint32 attributes, bool isDir);
/// Paints the state badge onto @p icon (cloud; green outline check; green filled check).
void decorate(QPixmap &icon, State state);
/// Explorer's offline actions: "Always keep on this device" (checked = AlwaysKeep,
/// unchecked again = Clear) and "Free up space".
enum class Pin { AlwaysKeep, FreeUp, Clear };
/// The attributes after @p pin: PINNED / UNPINNED / neither.
quint32 withPinState(quint32 attributes, Pin pin);

struct SyncRoot {
    QString path;     ///< "C:/Users/me/OneDrive" (forward slashes, no trailing slash)
    QString name;     ///< "OneDrive - Personal"
    QString provider; ///< "OneDrive", "GoogleDrive", "Dropbox", ...
    bool virtualDrive = false; ///< a streaming drive (Google Drive G:): every file is online
};

/// One key below ...\Explorer\SyncRootManager (registered Cloud Files sync roots).
struct RegistryRoot {
    QString id;                       ///< key name, "OneDrive!S-1-5-21-...!Personal|..."
    QString displayName;              ///< DisplayNameResource (indirect strings resolved)
    QHash<QString, QString> userRoots; ///< UserSyncRoots: user SID -> folder
};

/// "OneDrive!S-1-5-...!Personal|x" -> "OneDrive"
QString providerFromId(const QString &id);
/// Drive letters / folders of Google Drive for desktop ("mount_point_path" in
/// HKCU\Software\Google\DriveFS PerAccountPreferences).
QStringList parseGoogleDrivePreferences(const QByteArray &json);
/// Dropbox folders from %LOCALAPPDATA%\Dropbox\info.json: (path, "Dropbox"/"Dropbox (Business)").
QList<SyncRoot> parseDropboxInfo(const QByteArray &json);
/// Combines every source into one list: registered roots of @p userSid first, then
/// the OneDrive environment variables (OneDrive, OneDriveConsumer, OneDriveCommercial),
/// Google Drive and Dropbox; one entry per folder, sorted by name.
QList<SyncRoot> mergeRoots(const QList<RegistryRoot> &registry, const QString &userSid,
                           const QHash<QString, QString> &environment, const QStringList &googleDrive,
                           const QList<SyncRoot> &dropbox);
/// "C:\\x\\" -> "C:/x"; "G" / "G:" -> "G:/"
QString normalizePath(const QString &path);
/// Case-insensitive "is @p path the folder @p root or inside it".
bool isInside(const QString &path, const QString &root);

// --- runtime (Windows; elsewhere the list is empty) ---------------------------
/// The connected sync folders (read again at most every few seconds).
QList<SyncRoot> syncRoots();
/// Forget the cached list and attributes.
void invalidate();
/// True for a sync folder itself.
bool isSyncRoot(const QString &path);
/// True for paths inside a sync folder (not the folder itself).
bool isInSyncRoot(const QString &path);
/// The sync folder holding @p path (or the folder itself); false if none.
bool rootFor(const QString &path, SyncRoot *root = nullptr);
/// Windows attributes of a path inside a sync folder (cached briefly);
/// InvalidAttributes outside them or when unknown. Metadata only: never downloads.
quint32 attributes(const QString &path);
/// True when Panero must not read the file for its type, icon or preview: a
/// placeholder in a sync folder, anything on a streaming drive, or attributes unknown there.
bool avoidReading(const QString &path);
/// Badge of an item (NotCloud outside the sync folders and for the folders themselves).
State state(const QString &path, bool isDir);
/// Sets the pin state of files and folders (folders: everything inside, as
/// Explorer does). Returns the number of items changed; @p error names the first failure.
int setPinState(const QStringList &paths, Pin pin, QString *error = nullptr);
}

#endif // PANEROCLOUDFILES_H
