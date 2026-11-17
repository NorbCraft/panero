/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROIMPORT_H
#define PANEROIMPORT_H

#include <QString>
#include <QStringList>

class QWidget;

/**
 * Explicit, opt-in import of an existing Krusader configuration into Panero.
 *
 * Krusader's files are only ever read (copied); they are never modified, moved
 * or deleted. Existing Panero files that would be replaced are renamed to
 * "<name>.before-krusader-import-<timestamp>" first.
 *
 * What is imported: ~/.config/krusaderrc -> ~/.config/panerorc and the contents
 * of ~/.local/share/krusader/ (bookmarks, user actions, ...) ->
 * ~/.local/share/panero/. Toolbar/menu customisations (kxmlgui rc files) are not
 * imported because Panero ships a different default toolbar.
 */
namespace PaneroImport
{
struct Paths {
    QString krusaderConfig; ///< e.g. ~/.config/krusaderrc
    QString paneroConfig;   ///< e.g. ~/.config/panerorc
    QString krusaderData;   ///< e.g. ~/.local/share/krusader
    QString paneroData;     ///< e.g. ~/.local/share/panero
    QString pendingFlag;    ///< marker file requesting an import on next start
};

/// Paths derived from QStandardPaths (honours XDG_CONFIG_HOME / XDG_DATA_HOME).
Paths defaultPaths();

/// True if a Krusader configuration file exists.
bool krusaderSettingsExist(const Paths &paths);

/**
 * Copy Krusader's settings into Panero's locations.
 * @param copied receives the list of written target files
 * @param error receives a human readable error message on failure
 * @return true on success
 */
bool importKrusaderSettings(const Paths &paths, QStringList *copied, QString *error);

/// Request an import on the next start (used from the running application).
bool schedulePendingImport(const Paths &paths);

/// If an import was scheduled, perform it and remove the marker. Returns true if imported.
bool runPendingImport(const Paths &paths, QString *error);

/**
 * On a fresh Panero profile with an existing Krusader configuration, ask the
 * user once whether to import it. Must be called before the application
 * configuration is used. Returns true if settings were imported.
 */
bool offerImportOnFirstRun(QWidget *parent);

/// Settings-menu entry point: explain, then schedule the import for the next start.
void requestImportFromMenu(QWidget *parent);
}

#endif // PANEROIMPORT_H
