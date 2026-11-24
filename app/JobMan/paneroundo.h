/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROUNDO_H
#define PANEROUNDO_H

#include "panerorecyclebin.h"

#include <QObject>
#include <QVector>

/**
 * Undo for file operations (Ctrl+Z, D-022). KIO's FileUndoManager records copy, move,
 * rename, new folder and move to Trash (KrJob, DefaultFileSystem) and undoes them;
 * Windows moves to the Recycle Bin outside KIO (PaneroRecycleJob), so those are kept
 * here and restored item by item. The order of both kinds is kept: every command
 * FileUndoManager records leaves a marker on this stack.
 */
class PaneroUndo : public QObject
{
    Q_OBJECT
public:
    static PaneroUndo *self();

    bool isUndoAvailable() const;
    /// "Und&o: Move" (FileUndoManager's wording), "Und&o: Move to Recycle Bin"
    QString undoText() const;

    /// Items Panero moved to the Windows Recycle Bin, as one undo step.
    void recordRecycled(const QList<PaneroRecycleBin::Item> &items);

public Q_SLOTS:
    void undo();

Q_SIGNALS:
    void undoAvailable(bool available);
    void undoTextChanged(const QString &text);
    /// An undo finished (successfully or not): panels showing the folders may refresh.
    void undone();

private:
    explicit PaneroUndo(QObject *parent = nullptr);
    void kioRecorded();
    void update();
    void dropStaleMarkers();
    /// the newest entry is one of Panero's own (not FileUndoManager's)
    bool ownOnTop() const;

    struct Entry {
        bool kio = true;
        QList<PaneroRecycleBin::Item> items;
    };
    QVector<Entry> m_stack;
    bool m_lastAvailable = false;
    QString m_lastText;
};

#endif // PANEROUNDO_H
