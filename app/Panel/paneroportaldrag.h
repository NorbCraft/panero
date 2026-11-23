/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROPORTALDRAG_H
#define PANEROPORTALDRAG_H

#include <QList>
#include <QString>
#include <QUrl>

class QMimeData;

/**
 * Files dragged or copied out of Panero also reach sandboxed applications
 * (Flatpak, Snap), e.g. a website in the Flatpak Firefox (D-022).
 *
 * A sandbox sees only some folders, so the plain paths in text/uri-list are
 * "not found" there. GTK file managers therefore also offer the files through
 * the desktop's document portal: they register them with the FileTransfer
 * portal and put its key into "application/vnd.portal.filetransfer"; the
 * receiving application asks the portal for the files and gets copies of the
 * paths it may read. Qt 5 does not do this, so Panero does it here.
 * Without a portal (or on Windows) nothing is added.
 */
namespace PaneroPortalDrag
{
inline const char *const MimeType = "application/vnd.portal.filetransfer";

/// Register the local files among @p urls with the FileTransfer portal and add
/// its key to @p mimeData. Returns the key, or an empty string.
QString addFiles(QMimeData *mimeData, const QList<QUrl> &urls);
}

#endif // PANEROPORTALDRAG_H
