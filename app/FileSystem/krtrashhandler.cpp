/*
    SPDX-FileCopyrightText: 2009 Csaba Karai <krusader@users.sourceforge.net>
    SPDX-FileCopyrightText: 2009-2022 Krusader Krew <https://krusader.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "krtrashhandler.h"

// QtCore
#include <QDir>
#include <QStandardPaths>

#include <KConfigCore/KConfig>
#include <KConfigCore/KConfigGroup>
#include <KIO/EmptyTrashJob>
#include <KIO/Job>
#include <KIO/JobUiDelegate>
#include <KIO/RestoreJob>
#include <KJobWidgets/KJobWidgets>
#ifdef Q_OS_WIN
#include <KI18n/KLocalizedString>
#include <KWidgetsAddons/KMessageBox>
#endif

#include "filesystemprovider.h"
#include "../kractions.h"
#include "../krglobal.h"
#include "../icon.h"


KrTrashWatcher * KrTrashHandler::_trashWatcher = nullptr;

bool KrTrashHandler::isTrashEmpty()
{
    KConfig trashConfig("trashrc");
    KConfigGroup cfg(&trashConfig, "Status");
    return cfg.readEntry("Empty", false);
}

QString KrTrashHandler::trashIconName()
{
    return isTrashEmpty() ? "user-trash" : "user-trash-full";
}

void KrTrashHandler::emptyTrash()
{
#ifdef Q_OS_WIN
    // Windows: KIO's question speaks of the "Trash"
    if (KMessageBox::warningContinueCancel(krMainWindow,
                                           i18n("Do you really want to empty the Recycle Bin? All items in it will be "
                                                "permanently deleted."),
                                           i18n("Empty Recycle Bin"),
                                           KGuiItem(i18n("Empty Recycle Bin"), QStringLiteral("trash-empty")))
        != KMessageBox::Continue)
        return;
#else
    KIO::JobUiDelegate uiDelegate;
    uiDelegate.setWindow(krMainWindow);
    if (!uiDelegate.askDeleteConfirmation(QList<QUrl>(), KIO::JobUiDelegate::EmptyTrash,
                                          KIO::JobUiDelegate::DefaultConfirmation))
        return;
#endif

    KIO::Job *job = KIO::emptyTrash();
    KJobWidgets::setWindow(job, krMainWindow);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    const QUrl url = QUrl("trash:/");
    QObject::connect(job, &KIO::Job::result,
                     [=]() { FileSystemProvider::instance().refreshFilesystems(url, false); });
}

void KrTrashHandler::restoreTrashedFiles(const QList<QUrl> &urls)
{
    if (urls.isEmpty())
        return;

    KIO::RestoreJob *job = KIO::restoreFromTrash(urls);
    KJobWidgets::setWindow(job, krMainWindow);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    const QUrl url = urls.first().adjusted(QUrl::RemoveFilename);
    QObject::connect(job, &KIO::Job::result,
                     [=]() { FileSystemProvider::instance().refreshFilesystems(url, false); });
}

void KrTrashHandler::startWatcher()
{
    if (!_trashWatcher)
        _trashWatcher = new KrTrashWatcher();
}

void KrTrashHandler::stopWatcher()
{
    delete _trashWatcher;
    _trashWatcher = nullptr;
}


KrTrashWatcher::KrTrashWatcher()
{
    _watcher = new KDirWatch();
    connect(_watcher, &KDirWatch::created, this, &KrTrashWatcher::slotTrashChanged);
    connect(_watcher, &KDirWatch::dirty, this, &KrTrashWatcher::slotTrashChanged);
    const QString trashrcFile =
        QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)).filePath("trashrc");
    _watcher->addFile(trashrcFile);
    _watcher->startScan(true);
}

KrTrashWatcher::~KrTrashWatcher()
{
    delete _watcher;
    _watcher = nullptr;
}

void KrTrashWatcher::slotTrashChanged()
{
    KrActions::actTrashBin->setIcon(Icon(KrTrashHandler::trashIconName()));
}
