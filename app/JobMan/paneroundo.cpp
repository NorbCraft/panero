/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroundo.h"
#include "../panerowording.h"

#include <QApplication>
#include <QDebug>
#include <QWidget>

#include <KI18n/KLocalizedString>
#include <KIOWidgets/KIO/FileUndoManager>
#include <KWidgetsAddons/KMessageBox>

namespace
{
constexpr int MaxEntries = 100;
}

PaneroUndo *PaneroUndo::self()
{
    static PaneroUndo *instance = new PaneroUndo(qApp);
    return instance;
}

PaneroUndo::PaneroUndo(QObject *parent)
    : QObject(parent)
{
    KIO::FileUndoManager *manager = KIO::FileUndoManager::self();
    connect(manager, &KIO::FileUndoManager::jobRecordingFinished, this, &PaneroUndo::kioRecorded);
    connect(manager, QOverload<bool>::of(&KIO::FileUndoManager::undoAvailable), this, &PaneroUndo::update);
    connect(manager, &KIO::FileUndoManager::undoTextChanged, this, &PaneroUndo::update);
    connect(manager, &KIO::FileUndoManager::undoJobFinished, this, [this]() {
        update();
        emit undone();
    });
    update();
}

void PaneroUndo::kioRecorded()
{
    m_stack.append(Entry());
    if (m_stack.size() > MaxEntries)
        m_stack.removeFirst();
    update();
}

void PaneroUndo::recordRecycled(const QList<PaneroRecycleBin::Item> &items)
{
    Entry entry;
    entry.kio = false;
    entry.items = items;
    m_stack.append(entry);
    if (m_stack.size() > MaxEntries)
        m_stack.removeFirst();
    update();
}

void PaneroUndo::dropStaleMarkers()
{
    // FileUndoManager drops commands itself (a failed undo)
    while (!m_stack.isEmpty() && m_stack.last().kio && !KIO::FileUndoManager::self()->isUndoAvailable())
        m_stack.removeLast();
}

bool PaneroUndo::ownOnTop() const
{
    // stale markers (FileUndoManager has nothing left) are skipped
    for (int i = m_stack.size() - 1; i >= 0; --i) {
        if (!m_stack.at(i).kio)
            return true;
        if (KIO::FileUndoManager::self()->isUndoAvailable())
            return false;
    }
    return false;
}

bool PaneroUndo::isUndoAvailable() const
{
    return ownOnTop() || KIO::FileUndoManager::self()->isUndoAvailable();
}

QString PaneroUndo::undoText() const
{
    if (ownOnTop())
        return PANERO_TEXT(i18nc("@action undo", "Und&o: Move to Trash"), i18nc("@action undo", "Und&o: Move to Recycle Bin"));
    KIO::FileUndoManager *manager = KIO::FileUndoManager::self();
    return manager->isUndoAvailable() ? manager->undoText() : i18nc("@action", "&Undo");
}

void PaneroUndo::update()
{
    const bool available = isUndoAvailable();
    const QString text = undoText();
    if (available != m_lastAvailable) {
        m_lastAvailable = available;
        emit undoAvailable(available);
    }
    if (text != m_lastText) {
        m_lastText = text;
        emit undoTextChanged(text);
    }
}

void PaneroUndo::undo()
{
    dropStaleMarkers();
    if (!m_stack.isEmpty() && !m_stack.last().kio) {
        const QList<PaneroRecycleBin::Item> items = m_stack.takeLast().items;
        QStringList errors;
        // newest first, the reverse order of the move
        for (int i = items.size() - 1; i >= 0; --i) {
            const QString error = PaneroRecycleBin::restore(items.at(i));
            if (!error.isEmpty())
                errors << error;
        }
        update();
        emit undone();
        if (!errors.isEmpty()) {
            qWarning() << "Panero: undo of a move to the Recycle Bin:" << errors;
            KMessageBox::errorList(QApplication::activeWindow(),
                                   PANERO_TEXT(i18n("These items could not be restored from the Trash:"),
                                               i18n("These items could not be restored from the Recycle Bin:")),
                                   errors);
        }
        return;
    }
    KIO::FileUndoManager *manager = KIO::FileUndoManager::self();
    if (!manager->isUndoAvailable())
        return;
    if (!m_stack.isEmpty())
        m_stack.removeLast();
    manager->undo();
    update();
}
