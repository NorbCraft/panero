/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANERORECYCLEBIN_H
#define PANERORECYCLEBIN_H

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <KIO/Job>

#include <memory>

/**
 * Windows Recycle Bin support for "Move to Recycle Bin" and its Undo (D-022).
 *
 * Moving goes through QFile::moveToTrash (IFileOperation, as Explorer), in a worker
 * thread (PaneroRecycleJob). Undo restores exactly the items Panero recycled: the
 * recycled "$R..." item is put back at its original path after its "$I..." record
 * confirmed the original path and the deletion time; nothing else in the Recycle Bin
 * is touched. The record parser is portable (unit-tested everywhere); the file
 * operations are Windows-only.
 */
namespace PaneroRecycleBin
{
/// Contents of a "$I..." record next to a recycled "$R..." item.
struct Info {
    QString originalPath; ///< native path (backslashes)
    QDateTime deletedAt; ///< UTC
    qint64 size = 0;
};

/// One item Panero moved to the Recycle Bin.
struct Item {
    QString originalPath; ///< native path where it was
    QString recycledPath; ///< the "$R..." item (may be empty when Windows did not report it)
    QDateTime deletedAt; ///< UTC, Panero's clock when the move finished
};

/// Parses a "$I" record: version 1 (Windows Vista to 8.1) and version 2 (Windows 10 and later).
bool parseInfo(const QByteArray &data, Info *info);
/// Builds a version 2 record (for tests).
QByteArray makeInfo(const Info &info);
/// "...\$RAB12CD.txt" -> "...\$IAB12CD.txt"; empty when the name is not a recycled item.
QString infoPathFor(const QString &recycledPath);
/// Whether a record matches an item: same original path (case-insensitive, any separators)
/// and a deletion time within a few minutes of the item's.
bool matches(const Info &info, const Item &item);

/// Puts one item back. Returns an empty string on success, else a message.
/// Windows only (elsewhere it returns an error).
QString restore(const Item &item);
/// Looks in the current user's Recycle Bin folder of the item's drive for the record that
/// matches (original path + deletion time) and returns its "$R..." path, or an empty string.
QString findRecycled(const Item &item);
}

/**
 * Moves local files to the Windows Recycle Bin in a worker thread (IFileOperation can take
 * long for big folders, and never in the GUI thread). Reports files done / total. Killing it
 * stops after the item being moved. When the thread ends, the moved items are recorded for
 * Undo (PaneroUndo) - also after a kill.
 */
class PaneroRecycleJob : public KIO::Job
{
    Q_OBJECT
public:
    explicit PaneroRecycleJob(const QList<QUrl> &urls);
    ~PaneroRecycleJob() override;

protected:
    bool doKill() override;
    bool doSuspend() override { return false; }

private:
    void startWork();
    void poll();
    struct State;
    QList<QUrl> m_urls;
    std::shared_ptr<State> m_state;
    class QTimer *m_timer = nullptr;
};

#endif // PANERORECYCLEBIN_H
