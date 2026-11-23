/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerocloudfiles.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <sddl.h>
#endif

namespace PaneroCloudFiles
{
bool isPlaceholder(quint32 attributes)
{
    return attributes != InvalidAttributes && (attributes & (AttrRecallOnDataAccess | AttrOffline));
}

State stateFromAttributes(quint32 attributes, bool isDir)
{
    if (attributes == InvalidAttributes)
        return State::NotCloud;
    if (isDir) {
        // a folder: its pin state (Explorer shows the folder's own state)
        if (attributes & AttrPinned)
            return State::Pinned;
        if (attributes & (AttrUnpinned | AttrRecallOnOpen | AttrRecallOnDataAccess | AttrOffline))
            return State::OnlineOnly;
        return State::Available;
    }
    if (isPlaceholder(attributes))
        return State::OnlineOnly; // also while an "Always keep" download is pending
    if (attributes & AttrPinned)
        return State::Pinned;
    return State::Available; // downloaded (also UNPINNED until the provider frees it)
}

quint32 withPinState(quint32 attributes, Pin pin)
{
    const quint32 cleared = attributes & ~(AttrPinned | AttrUnpinned);
    switch (pin) {
    case Pin::AlwaysKeep:
        return cleared | AttrPinned;
    case Pin::FreeUp:
        return cleared | AttrUnpinned;
    case Pin::Clear:
        break;
    }
    return cleared;
}

QString providerFromId(const QString &id)
{
    const int bang = id.indexOf(QLatin1Char('!'));
    return bang < 0 ? id : id.left(bang);
}

namespace
{
void collectMountPoints(const QJsonValue &value, QStringList *out)
{
    if (value.isObject()) {
        const QJsonObject o = value.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (it.key() == QLatin1String("mount_point_path") && it.value().isString()) {
                const QString p = it.value().toString().trimmed();
                if (!p.isEmpty() && !out->contains(p))
                    out->append(p);
            } else {
                collectMountPoints(it.value(), out);
            }
        }
    } else if (value.isArray()) {
        for (const QJsonValue &v : value.toArray())
            collectMountPoints(v, out);
    }
}

QString folderName(const QString &path)
{
    const QString p = normalizePath(path);
    const QString name = p.mid(p.lastIndexOf(QLatin1Char('/')) + 1);
    return name.isEmpty() ? p : name;
}

SyncRoot makeRoot(const QString &path, const QString &name, const QString &provider)
{
    SyncRoot r;
    r.path = path;
    r.name = name;
    r.provider = provider;
    return r;
}
} // namespace

QStringList parseGoogleDrivePreferences(const QByteArray &json)
{
    QStringList out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (doc.isObject())
        collectMountPoints(doc.object(), &out);
    else if (doc.isArray())
        collectMountPoints(doc.array(), &out);
    return out;
}

QList<SyncRoot> parseDropboxInfo(const QByteArray &json)
{
    QList<SyncRoot> out;
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    for (const char *kind : {"personal", "business"}) {
        const QString path = o.value(QLatin1String(kind)).toObject().value(QLatin1String("path")).toString();
        if (path.isEmpty())
            continue;
        SyncRoot r;
        r.path = normalizePath(path);
        r.provider = QStringLiteral("Dropbox");
        const QString folder = folderName(r.path);
        r.name = qstrcmp(kind, "personal") == 0 ? QStringLiteral("Dropbox")
                                               : (folder.contains(QLatin1Char('(')) ? folder : QStringLiteral("Dropbox (Business)"));
        out.append(r);
    }
    return out;
}

QString normalizePath(const QString &path)
{
    QString p = path.trimmed();
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (p.length() == 1 && p.at(0).isLetter())
        return p.toUpper() + QStringLiteral(":/");
    if (p.length() == 2 && p.at(1) == QLatin1Char(':'))
        return p.left(1).toUpper() + QStringLiteral(":/");
    if (p.length() == 3 && p.at(1) == QLatin1Char(':') && p.at(2) == QLatin1Char('/'))
        return p.left(1).toUpper() + QStringLiteral(":/");
    while (p.length() > 1 && p.endsWith(QLatin1Char('/')))
        p.chop(1);
    if (p.length() >= 2 && p.at(1) == QLatin1Char(':'))
        p[0] = p.at(0).toUpper();
    return p;
}

bool isInside(const QString &path, const QString &root)
{
    if (root.isEmpty())
        return false;
    const QString p = normalizePath(path);
    const QString r = normalizePath(root);
    if (p.compare(r, Qt::CaseInsensitive) == 0)
        return true;
    const QString prefix = r.endsWith(QLatin1Char('/')) ? r : r + QLatin1Char('/');
    return p.startsWith(prefix, Qt::CaseInsensitive);
}

QList<SyncRoot> mergeRoots(const QList<RegistryRoot> &registry, const QString &userSid,
                           const QHash<QString, QString> &environment, const QStringList &googleDrive,
                           const QList<SyncRoot> &dropbox)
{
    QList<SyncRoot> roots;
    auto add = [&roots](SyncRoot r) {
        r.path = normalizePath(r.path);
        if (r.path.isEmpty())
            return;
        for (const SyncRoot &have : roots) {
            if (have.path.compare(r.path, Qt::CaseInsensitive) == 0)
                return;
        }
        if (r.name.isEmpty())
            r.name = folderName(r.path);
        roots.append(r);
    };

    for (const RegistryRoot &reg : registry) {
        QStringList paths;
        if (!userSid.isEmpty())
            paths << reg.userRoots.value(userSid);
        else
            paths = reg.userRoots.values();
        for (const QString &path : paths) {
            if (path.isEmpty())
                continue;
            SyncRoot r;
            r.path = path;
            r.provider = providerFromId(reg.id);
            r.name = reg.displayName;
            if (r.name.isEmpty() && r.provider == QLatin1String("OneDrive"))
                r.name = reg.id.contains(QLatin1String("!Personal")) ? QStringLiteral("OneDrive - Personal") : folderName(path);
            add(r);
        }
    }
    const QString consumer = environment.value(QStringLiteral("OneDriveConsumer"));
    const QString commercial = environment.value(QStringLiteral("OneDriveCommercial"));
    if (!consumer.isEmpty())
        add(makeRoot(consumer, QStringLiteral("OneDrive - Personal"), QStringLiteral("OneDrive")));
    if (!commercial.isEmpty())
        add(makeRoot(commercial, folderName(normalizePath(commercial)), QStringLiteral("OneDrive")));
    const QString oneDrive = environment.value(QStringLiteral("OneDrive"));
    if (!oneDrive.isEmpty())
        add(makeRoot(oneDrive, QStringLiteral("OneDrive"), QStringLiteral("OneDrive")));
    for (const QString &g : googleDrive) {
        SyncRoot r;
        r.path = normalizePath(g);
        r.name = QStringLiteral("Google Drive");
        r.provider = QStringLiteral("GoogleDrive");
        r.virtualDrive = r.path.length() == 3; // the streaming drive letter
        add(r);
    }
    for (const SyncRoot &d : dropbox)
        add(d);
    std::stable_sort(roots.begin(), roots.end(), [](const SyncRoot &a, const SyncRoot &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return roots;
}

// ---------------------------------------------------------------------------
// runtime

namespace
{
QMutex s_mutex;
QList<SyncRoot> s_roots;
bool s_loaded = false;
QElapsedTimer s_age;
QHash<QString, QPair<quint32, qint64>> s_attributes;
QElapsedTimer s_clock;

#ifdef Q_OS_WIN
QString regString(HKEY key, const wchar_t *sub, const wchar_t *name)
{
    DWORD size = 0;
    if (RegGetValueW(key, sub, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size < 2)
        return QString();
    std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(key, sub, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, &buffer[0], &size) != ERROR_SUCCESS)
        return QString();
    return QString::fromWCharArray(buffer.c_str());
}

QString resolveIndirect(const QString &text)
{
    if (!text.startsWith(QLatin1Char('@')))
        return text;
    // "@C:\...\x.dll,-123": a string resource (shlwapi, loaded at run time)
    using LoadIndirect = HRESULT(WINAPI *)(PCWSTR, PWSTR, UINT, void **);
    static const auto load = reinterpret_cast<LoadIndirect>(
        reinterpret_cast<void *>(GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "SHLoadIndirectString")));
    wchar_t out[512] = {};
    const std::wstring in = text.toStdWString();
    if (load && load(in.c_str(), out, 512, nullptr) == S_OK)
        return QString::fromWCharArray(out);
    return QString();
}

QList<RegistryRoot> readSyncRootManager(HKEY hive)
{
    QList<RegistryRoot> list;
    HKEY base = nullptr;
    if (RegOpenKeyExW(hive, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SyncRootManager", 0, KEY_READ, &base) != ERROR_SUCCESS)
        return list;
    for (DWORD i = 0;; ++i) {
        wchar_t name[512];
        DWORD len = 512;
        if (RegEnumKeyExW(base, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        RegistryRoot r;
        r.id = QString::fromWCharArray(name, int(len));
        HKEY sub = nullptr;
        if (RegOpenKeyExW(base, name, 0, KEY_READ, &sub) != ERROR_SUCCESS)
            continue;
        r.displayName = resolveIndirect(regString(sub, nullptr, L"DisplayNameResource"));
        HKEY users = nullptr;
        if (RegOpenKeyExW(sub, L"UserSyncRoots", 0, KEY_READ, &users) == ERROR_SUCCESS) {
            for (DWORD j = 0;; ++j) {
                wchar_t valueName[256];
                DWORD valueLen = 256;
                wchar_t data[1024];
                DWORD dataSize = sizeof(data) - sizeof(wchar_t);
                DWORD type = 0;
                const LONG rc = RegEnumValueW(users, j, valueName, &valueLen, nullptr, &type, reinterpret_cast<BYTE *>(data), &dataSize);
                if (rc == ERROR_NO_MORE_ITEMS)
                    break;
                if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
                    continue;
                data[dataSize / sizeof(wchar_t)] = L'\0';
                r.userRoots.insert(QString::fromWCharArray(valueName, int(valueLen)), QString::fromWCharArray(data));
            }
            RegCloseKey(users);
        }
        RegCloseKey(sub);
        list.append(r);
    }
    RegCloseKey(base);
    return list;
}

QString currentUserSid()
{
    static QString sid;
    if (!sid.isEmpty())
        return sid;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return QString();
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray buffer(int(size), '\0');
    if (size && GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        LPWSTR text = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &text)) {
            sid = QString::fromWCharArray(text);
            LocalFree(text);
        }
    }
    CloseHandle(token);
    return sid;
}

bool driveExists(const QString &root)
{
    if (root.length() != 3)
        return QFileInfo::exists(root);
    const int letter = root.at(0).toUpper().unicode() - 'A';
    return letter >= 0 && letter < 26 && (GetLogicalDrives() & (1u << letter));
}

QList<SyncRoot> detectRoots()
{
    QList<RegistryRoot> registry = readSyncRootManager(HKEY_LOCAL_MACHINE);
    registry += readSyncRootManager(HKEY_CURRENT_USER);
    QHash<QString, QString> env;
    for (const char *name : {"OneDrive", "OneDriveConsumer", "OneDriveCommercial"}) {
        const QString v = qEnvironmentVariable(name);
        if (!v.isEmpty())
            env.insert(QLatin1String(name), v);
    }
    QStringList google = parseGoogleDrivePreferences(
        regString(HKEY_CURRENT_USER, L"Software\\Google\\DriveFS", L"PerAccountPreferences").toUtf8());
    if (google.isEmpty()) {
        const QString def = regString(HKEY_CURRENT_USER, L"Software\\Google\\DriveFS", L"DefaultMountPoint");
        const QString machine = regString(HKEY_LOCAL_MACHINE, L"Software\\Google\\DriveFS", L"DefaultMountPoint");
        if (!def.isEmpty())
            google << def;
        else if (!machine.isEmpty())
            google << machine;
    }
    QList<SyncRoot> dropbox;
    for (const char *var : {"LOCALAPPDATA", "APPDATA"}) {
        QFile info(qEnvironmentVariable(var) + QStringLiteral("/Dropbox/info.json"));
        if (dropbox.isEmpty() && info.open(QIODevice::ReadOnly))
            dropbox = parseDropboxInfo(info.readAll());
    }
    QList<SyncRoot> roots;
    // only folders that exist (a signed-out account leaves its registry key behind)
    for (const SyncRoot &r : mergeRoots(registry, currentUserSid(), env, google, dropbox)) {
        if (r.virtualDrive ? driveExists(r.path) : QFileInfo(r.path).isDir())
            roots.append(r);
    }
    return roots;
}
#endif

QList<SyncRoot> roots()
{
    QMutexLocker lock(&s_mutex);
#ifdef Q_OS_WIN
    if (!s_loaded || s_age.elapsed() > 5000) {
        s_roots = detectRoots();
        s_loaded = true;
        s_age.start();
    }
#endif
    return s_roots;
}
} // namespace

QList<SyncRoot> syncRoots()
{
    return roots();
}

void invalidate()
{
    QMutexLocker lock(&s_mutex);
    s_loaded = false;
    s_attributes.clear();
}

bool rootFor(const QString &path, SyncRoot *root)
{
#ifdef Q_OS_WIN
    if (path.isEmpty())
        return false;
    for (const SyncRoot &r : roots()) {
        if (isInside(path, r.path)) {
            if (root)
                *root = r;
            return true;
        }
    }
#else
    Q_UNUSED(path)
    Q_UNUSED(root)
#endif
    return false;
}

bool isSyncRoot(const QString &path)
{
    SyncRoot r;
    return rootFor(path, &r) && normalizePath(path).compare(r.path, Qt::CaseInsensitive) == 0;
}

bool isInSyncRoot(const QString &path)
{
    SyncRoot r;
    return rootFor(path, &r) && normalizePath(path).compare(r.path, Qt::CaseInsensitive) != 0;
}

quint32 attributes(const QString &path)
{
#ifdef Q_OS_WIN
    if (!rootFor(path))
        return InvalidAttributes;
    const QString key = normalizePath(path).toLower();
    {
        QMutexLocker lock(&s_mutex);
        if (!s_clock.isValid())
            s_clock.start();
        auto it = s_attributes.constFind(key);
        if (it != s_attributes.constEnd() && s_clock.elapsed() - it->second < 2000)
            return it->first;
    }
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD attrs = GetFileAttributesW(native.c_str()); // metadata only: never downloads
    QMutexLocker lock(&s_mutex);
    if (s_attributes.size() > 20000)
        s_attributes.clear();
    s_attributes.insert(key, qMakePair(quint32(attrs), s_clock.elapsed()));
    return quint32(attrs);
#else
    Q_UNUSED(path)
    return InvalidAttributes;
#endif
}

bool avoidReading(const QString &path)
{
#ifdef Q_OS_WIN
    SyncRoot r;
    if (!rootFor(path, &r))
        return false;
    if (r.virtualDrive)
        return true; // a streaming drive downloads whatever is read
    const quint32 a = attributes(path);
    return a == InvalidAttributes || isPlaceholder(a);
#else
    Q_UNUSED(path)
    return false;
#endif
}

State state(const QString &path, bool isDir)
{
    SyncRoot r;
    if (!rootFor(path, &r) || normalizePath(path).compare(r.path, Qt::CaseInsensitive) == 0)
        return State::NotCloud;
    if (r.virtualDrive)
        return State::NotCloud; // no Cloud Files attributes there
    return stateFromAttributes(attributes(path), isDir);
}

#ifdef Q_OS_WIN
namespace
{
// cfapi.h (cldapi.dll, Windows 10 1709+), loaded at run time
enum { CfPinUnspecified = 0, CfPinPinned = 1, CfPinUnpinned = 2 };
enum { CfSetPinRecurse = 1 };
using CfSetPinStateFn = HRESULT(WINAPI *)(HANDLE, int, int, LPOVERLAPPED);

CfSetPinStateFn cfSetPinState()
{
    static CfSetPinStateFn fn = []() -> CfSetPinStateFn {
        HMODULE lib = LoadLibraryExW(L"cldapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return lib ? reinterpret_cast<CfSetPinStateFn>(reinterpret_cast<void *>(GetProcAddress(lib, "CfSetPinState"))) : nullptr;
    }();
    return fn;
}

constexpr quint32 SettableAttributes = AttrReadOnly | AttrHidden | AttrSystem | 0x20 /*archive*/ | 0x100 /*temporary*/
    | AttrOffline | 0x2000 /*not content indexed*/ | AttrPinned | AttrUnpinned;

bool setAttributes(const QString &path, Pin pin)
{
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD a = GetFileAttributesW(native.c_str());
    if (a == INVALID_FILE_ATTRIBUTES)
        return false;
    return SetFileAttributesW(native.c_str(), withPinState(a, pin) & SettableAttributes) != 0;
}
} // namespace
#endif

int setPinState(const QStringList &paths, Pin pin, QString *error)
{
    int changed = 0;
#ifdef Q_OS_WIN
    for (const QString &path : paths) {
        const bool isDir = QFileInfo(path).isDir();
        bool ok = false;
        if (CfSetPinStateFn fn = cfSetPinState()) {
            const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
            // opening without data access never downloads
            HANDLE h = CreateFileW(native.c_str(), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                ok = SUCCEEDED(fn(h, pin == Pin::AlwaysKeep ? CfPinPinned : pin == Pin::FreeUp ? CfPinUnpinned : CfPinUnspecified, isDir ? CfSetPinRecurse : 0, nullptr));
                CloseHandle(h);
            }
        }
        if (!ok) {
            // not a placeholder (or no Cloud Files API): the attributes themselves
            ok = setAttributes(path, pin);
            if (ok && isDir) {
                QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                QDirIterator::Subdirectories);
                while (it.hasNext())
                    setAttributes(it.next(), pin);
            }
        }
        if (ok)
            ++changed;
        else if (error && error->isEmpty())
            *error = QDir::toNativeSeparators(path);
    }
    invalidate();
#else
    Q_UNUSED(paths)
    Q_UNUSED(pin)
    Q_UNUSED(error)
#endif
    return changed;
}

void decorate(QPixmap &icon, State state)
{
    if (state == State::NotCloud || icon.isNull())
        return;
    const qreal dpr = icon.devicePixelRatio();
    const qreal w = icon.width() / dpr;
    const qreal d = qMax<qreal>(7.0, w * 0.45);
    const QRectF r(0.5, w - d - 0.5, d, d);
    QPainter p(&icon);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor green(0x2e, 0x9e, 0x4f);
    const QColor blue(0x3a, 0x7b, 0xd5);
    if (state == State::OnlineOnly) {
        // a small cloud
        QPainterPath cloud;
        const qreal x = r.left(), y = r.top() + d * 0.2, cw = d, ch = d * 0.62;
        cloud.addEllipse(QRectF(x + cw * 0.05, y + ch * 0.35, cw * 0.42, ch * 0.62));
        cloud.addEllipse(QRectF(x + cw * 0.25, y, cw * 0.5, ch * 0.75));
        cloud.addEllipse(QRectF(x + cw * 0.53, y + ch * 0.3, cw * 0.42, ch * 0.66));
        cloud.addRect(QRectF(x + cw * 0.25, y + ch * 0.6, cw * 0.5, ch * 0.37));
        p.setPen(QPen(blue, qMax<qreal>(1.0, d / 9.0)));
        p.setBrush(Qt::white);
        p.drawPath(cloud.simplified());
        return;
    }
    const bool pinned = state == State::Pinned;
    p.setPen(QPen(green, qMax<qreal>(1.0, d / 8.0)));
    p.setBrush(pinned ? QBrush(green) : QBrush(Qt::white));
    p.drawEllipse(r.adjusted(0.5, 0.5, -0.5, -0.5));
    QPainterPath check;
    check.moveTo(r.left() + d * 0.27, r.top() + d * 0.52);
    check.lineTo(r.left() + d * 0.44, r.top() + d * 0.69);
    check.lineTo(r.left() + d * 0.74, r.top() + d * 0.35);
    p.setPen(QPen(pinned ? Qt::white : green, qMax<qreal>(1.2, d / 7.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(check);
}
} // namespace PaneroCloudFiles
