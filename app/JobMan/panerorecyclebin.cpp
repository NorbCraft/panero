/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerorecyclebin.h"
#include "paneroundo.h"
#include "../panerowording.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QtEndian>

#include <KI18n/KLocalizedString>
#include <KIO/JobUiDelegateFactory>

#include <QCoreApplication>
#include <limits>

#ifdef Q_OS_WIN
#include <windows.h>
#include <sddl.h>
#include <shlobj.h>
#endif

namespace
{
// 100 ns intervals between 1601-01-01 and 1970-01-01
constexpr qint64 FileTimeEpochOffset = 116444736000000000LL;

QDateTime fromFileTime(qint64 fileTime)
{
    return QDateTime::fromMSecsSinceEpoch((fileTime - FileTimeEpochOffset) / 10000, Qt::UTC);
}

qint64 toFileTime(const QDateTime &time)
{
    return time.toMSecsSinceEpoch() * 10000 + FileTimeEpochOffset;
}

QString normalized(const QString &path)
{
    QString p = QDir::toNativeSeparators(QDir::cleanPath(QDir::fromNativeSeparators(path)));
    return p.toLower();
}
} // namespace

bool PaneroRecycleBin::parseInfo(const QByteArray &data, Info *info)
{
    if (data.size() < 24)
        return false;
    const auto *d = reinterpret_cast<const uchar *>(data.constData());
    const qint64 version = qFromLittleEndian<qint64>(d);
    const qint64 size = qFromLittleEndian<qint64>(d + 8);
    const qint64 fileTime = qFromLittleEndian<qint64>(d + 16);
    QString path;
    if (version == 1) {
        // fixed MAX_PATH UTF-16 buffer
        if (data.size() < 24 + 2 * 260)
            return false;
        path = QString::fromUtf16(reinterpret_cast<const char16_t *>(d + 24), 260);
    } else if (version == 2) {
        if (data.size() < 28)
            return false;
        const qint32 chars = qFromLittleEndian<qint32>(d + 24);
        if (chars <= 0 || chars > 32768 || data.size() < 28 + 2 * chars)
            return false;
        path = QString::fromUtf16(reinterpret_cast<const char16_t *>(d + 28), chars);
    } else {
        return false;
    }
    const int nul = path.indexOf(QChar(0));
    if (nul >= 0)
        path.truncate(nul);
    if (path.isEmpty())
        return false;
    if (info) {
        info->originalPath = path;
        info->size = size;
        info->deletedAt = fromFileTime(fileTime);
    }
    return true;
}

QByteArray PaneroRecycleBin::makeInfo(const Info &info)
{
    QByteArray out(28, '\0');
    auto *d = reinterpret_cast<uchar *>(out.data());
    qToLittleEndian<qint64>(2, d);
    qToLittleEndian<qint64>(info.size, d + 8);
    qToLittleEndian<qint64>(toFileTime(info.deletedAt), d + 16);
    const QString path = info.originalPath + QChar(0);
    qToLittleEndian<qint32>(path.size(), d + 24);
    out.append(reinterpret_cast<const char *>(path.utf16()), path.size() * 2);
    return out;
}

QString PaneroRecycleBin::infoPathFor(const QString &recycledPath)
{
    const QString native = QDir::toNativeSeparators(recycledPath);
    const int slash = native.lastIndexOf(QLatin1Char('\\'));
    const QString name = native.mid(slash + 1);
    if (!name.startsWith(QLatin1String("$R")) || name.size() < 3)
        return QString();
    return native.left(slash + 1) + QLatin1String("$I") + name.mid(2);
}

bool PaneroRecycleBin::matches(const Info &info, const Item &item)
{
    if (normalized(info.originalPath) != normalized(item.originalPath))
        return false;
    if (!item.deletedAt.isValid() || !info.deletedAt.isValid())
        return true;
    // Panero takes its time when the move finished; Windows when it started
    return qAbs(info.deletedAt.secsTo(item.deletedAt)) <= 10 * 60;
}

#ifdef Q_OS_WIN
namespace
{
QString currentUserSid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return QString();
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray buffer(int(size), '\0');
    QString sid;
    if (size && GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        auto *user = reinterpret_cast<TOKEN_USER *>(buffer.data());
        LPWSTR text = nullptr;
        if (ConvertSidToStringSidW(user->User.Sid, &text)) {
            sid = QString::fromWCharArray(text);
            LocalFree(text);
        }
    }
    CloseHandle(token);
    return sid;
}

bool readInfo(const QString &infoPath, PaneroRecycleBin::Info *info)
{
    QFile file(infoPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return PaneroRecycleBin::parseInfo(file.read(70000), info);
}
} // namespace
#endif

QString PaneroRecycleBin::findRecycled(const Item &item)
{
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(item.originalPath);
    if (native.size() < 3 || native.at(1) != QLatin1Char(':'))
        return QString(); // only drive letters have a Recycle Bin folder
    const QString sid = currentUserSid();
    if (sid.isEmpty())
        return QString();
    const QString folder = native.left(3) + QLatin1String("$Recycle.Bin\\") + sid;
    QString best;
    qint64 bestDistance = std::numeric_limits<qint64>::max();
    QDirIterator it(folder, {QStringLiteral("$I*")}, QDir::Files | QDir::Hidden | QDir::System);
    while (it.hasNext()) {
        const QString infoPath = QDir::toNativeSeparators(it.next());
        Info info;
        if (!readInfo(infoPath, &info) || !matches(info, item))
            continue;
        const QString recycled = infoPath.left(infoPath.lastIndexOf(QLatin1Char('\\')) + 1) + QLatin1String("$R")
            + infoPath.mid(infoPath.lastIndexOf(QLatin1Char('\\')) + 3);
        if (!QFileInfo::exists(recycled))
            continue;
        const qint64 distance = item.deletedAt.isValid() ? qAbs(info.deletedAt.secsTo(item.deletedAt)) : 0;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = recycled;
        }
    }
    return best;
#else
    Q_UNUSED(item)
    return QString();
#endif
}

QString PaneroRecycleBin::restore(const Item &item)
{
#ifdef Q_OS_WIN
    QString recycled = QDir::toNativeSeparators(item.recycledPath);
    Info info;
    bool found = !recycled.isEmpty() && QFileInfo::exists(recycled) && readInfo(infoPathFor(recycled), &info)
        && matches(info, item);
    if (!found) {
        recycled = findRecycled(item);
        found = !recycled.isEmpty() && readInfo(infoPathFor(recycled), &info);
    }
    if (!found)
        return i18n("%1 is no longer in the Recycle Bin.", QDir::toNativeSeparators(item.originalPath));
    const QString original = QDir::toNativeSeparators(item.originalPath);
    if (QFileInfo::exists(original) || QFileInfo(original).isSymLink())
        return i18n("%1 already exists.", original);
    QDir().mkpath(QFileInfo(original).absolutePath());
    if (!MoveFileExW(reinterpret_cast<LPCWSTR>(recycled.utf16()), reinterpret_cast<LPCWSTR>(original.utf16()), 0)) {
        const DWORD error = GetLastError();
        return i18n("%1 could not be restored (error %2).", original, error);
    }
    const QString infoPath = infoPathFor(recycled);
    if (!DeleteFileW(reinterpret_cast<LPCWSTR>(infoPath.utf16())))
        qWarning() << "Panero: restored" << original << "but could not remove" << infoPath;
    // Explorer windows showing the Recycle Bin or the folder update themselves
    SHChangeNotify(QFileInfo(original).isDir() ? SHCNE_MKDIR : SHCNE_CREATE, SHCNF_PATHW,
                   reinterpret_cast<LPCWSTR>(original.utf16()), nullptr);
    SHChangeNotify(QFileInfo(original).isDir() ? SHCNE_RMDIR : SHCNE_DELETE, SHCNF_PATHW,
                   reinterpret_cast<LPCWSTR>(recycled.utf16()), nullptr);
    return QString();
#else
    Q_UNUSED(item)
    return QStringLiteral("The Recycle Bin is only available on Windows.");
#endif
}

// ---------------------------------------------------------------------------

struct PaneroRecycleJob::State {
    QMutex mutex;
    bool cancel = false;
    bool finished = false;
    int done = 0;
    QString current;
    QList<PaneroRecycleBin::Item> items;
    QStringList failed;
};

PaneroRecycleJob::PaneroRecycleJob(const QList<QUrl> &urls)
    : m_urls(urls), m_state(std::make_shared<State>())
{
    setCapabilities(KJob::Killable);
    // errors are shown like the ones of KIO's jobs (KrJob shows them on Windows)
    setUiDelegate(KIO::createDefaultJobUiDelegate());
    setTotalAmount(KJob::Files, quint64(urls.size()));
    QTimer::singleShot(0, this, &PaneroRecycleJob::startWork);
}

PaneroRecycleJob::~PaneroRecycleJob()
{
    QMutexLocker lock(&m_state->mutex);
    m_state->cancel = true;
}

bool PaneroRecycleJob::doKill()
{
    QMutexLocker lock(&m_state->mutex);
    m_state->cancel = true;
    return true;
}

void PaneroRecycleJob::startWork()
{
    QStringList paths;
    for (const QUrl &url : qAsConst(m_urls))
        paths << QDir::toNativeSeparators(url.toLocalFile());
    std::shared_ptr<State> state = m_state;
    QThread *thread = QThread::create([state, paths]() {
        for (const QString &path : paths) {
            {
                QMutexLocker lock(&state->mutex);
                if (state->cancel)
                    break;
                state->current = path;
            }
            QString inTrash;
            const bool ok = QFile::moveToTrash(path, &inTrash);
            QMutexLocker lock(&state->mutex);
            if (ok)
                state->items << PaneroRecycleBin::Item{path, QDir::toNativeSeparators(inTrash), QDateTime::currentDateTimeUtc()};
            else
                state->failed << path;
            state->done++;
        }
        QMutexLocker lock(&state->mutex);
        state->finished = true;
    });
    // the record for Undo is made when the thread ends, also if the job was killed meanwhile
    connect(thread, &QThread::finished, qApp, [state, thread]() {
        QList<PaneroRecycleBin::Item> items;
        {
            QMutexLocker lock(&state->mutex);
            items = state->items;
        }
        if (!items.isEmpty())
            PaneroUndo::self()->recordRecycled(items);
        thread->deleteLater();
    });
    connect(thread, &QThread::finished, this, [this]() {
        poll();
        QStringList failed;
        {
            QMutexLocker lock(&m_state->mutex);
            failed = m_state->failed;
        }
        if (!failed.isEmpty()) {
            qWarning() << "Panero: could not move to the Recycle Bin:" << failed;
            setError(KIO::ERR_CANNOT_DELETE);
            setErrorText(i18n("These items could not be moved to the Recycle Bin:\n%1", failed.join(QLatin1Char('\n'))));
        }
        emitResult();
    });
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &PaneroRecycleJob::poll);
    m_timer->start(200);
    emit description(this, PANERO_TEXT(i18n("Moving to the Trash"), i18n("Moving to the Recycle Bin")));
    thread->start();
}

void PaneroRecycleJob::poll()
{
    int done;
    QString current;
    {
        QMutexLocker lock(&m_state->mutex);
        done = m_state->done;
        current = m_state->current;
    }
    setProcessedAmount(KJob::Files, quint64(done));
    if (!m_urls.isEmpty())
        setPercent(static_cast<unsigned long>(done * 100 / m_urls.size()));
    if (!current.isEmpty())
        emit description(this, PANERO_TEXT(i18n("Moving to the Trash"), i18n("Moving to the Recycle Bin")),
                         qMakePair(i18nc("The source of a file operation", "Source"), current));
}
