/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroimport.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <KConfigCore/KConfig>
#include <KConfigCore/KConfigGroup>
#include <KConfigCore/KSharedConfig>
#include <KI18n/KLocalizedString>
#include <KWidgetsAddons/KMessageBox>

namespace PaneroImport
{

static const char *GROUP = "Panero";

Paths defaultPaths()
{
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    Paths p;
    p.krusaderConfig = config + QStringLiteral("/krusaderrc");
    p.paneroConfig = config + QStringLiteral("/panerorc");
    p.krusaderData = data + QStringLiteral("/krusader");
    p.paneroData = data + QStringLiteral("/panero");
    p.pendingFlag = config + QStringLiteral("/panero-import-krusader.pending");
    return p;
}

bool krusaderSettingsExist(const Paths &paths)
{
    return QFileInfo(paths.krusaderConfig).isFile();
}

static QString backupSuffix()
{
    return QStringLiteral(".before-krusader-import-") +
           QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
}

// Copy one file, moving an existing target out of the way first.
static bool copyWithBackup(const QString &source, const QString &target, const QString &suffix,
                           QStringList *copied, QString *error)
{
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (QFileInfo::exists(target) && !QFile::rename(target, target + suffix)) {
        *error = i18n("Could not back up %1.", target);
        return false;
    }
    if (!QFile::copy(source, target)) {
        *error = i18n("Could not copy %1 to %2.", source, target);
        return false;
    }
    // QFile::copy keeps the source permissions; make sure the copy is writable for us
    QFile(target).setPermissions(QFile(target).permissions() | QFileDevice::WriteOwner | QFileDevice::ReadOwner);
    if (copied)
        copied->append(target);
    return true;
}

bool importKrusaderSettings(const Paths &paths, QStringList *copied, QString *error)
{
    QString dummy;
    if (!error)
        error = &dummy;
    if (!krusaderSettingsExist(paths)) {
        *error = i18n("No Krusader configuration was found at %1.", paths.krusaderConfig);
        return false;
    }
    const QString suffix = backupSuffix();

    if (!copyWithBackup(paths.krusaderConfig, paths.paneroConfig, suffix, copied, error))
        return false;

    // data files: bookmarks, user actions, ...
    if (QFileInfo(paths.krusaderData).isDir()) {
        const QDir sourceRoot(paths.krusaderData);
        QDirIterator it(paths.krusaderData, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString source = it.next();
            if (QFileInfo(source).isSymLink())
                continue; // do not follow links out of the Krusader data folder
            const QString target = paths.paneroData + QLatin1Char('/') + sourceRoot.relativeFilePath(source);
            if (!copyWithBackup(source, target, suffix, copied, error))
                return false;
        }
    }

    // mark the imported configuration
    KConfig imported(paths.paneroConfig, KConfig::SimpleConfig);
    KConfigGroup group(&imported, GROUP);
    group.writeEntry("Imported From Krusader", QDateTime::currentDateTime().toString(Qt::ISODate));
    group.writeEntry("Krusader Import Offered", true);
    imported.sync();
    return true;
}

bool schedulePendingImport(const Paths &paths)
{
    QDir().mkpath(QFileInfo(paths.pendingFlag).absolutePath());
    QFile flag(paths.pendingFlag);
    if (!flag.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    flag.write("Panero will import Krusader settings on the next start.\n");
    return true;
}

bool runPendingImport(const Paths &paths, QString *error)
{
    if (!QFileInfo::exists(paths.pendingFlag))
        return false;
    QFile::remove(paths.pendingFlag); // never retry endlessly
    return importKrusaderSettings(paths, nullptr, error);
}

static void reportResult(QWidget *parent, bool ok, const QString &error)
{
    if (ok) {
        KMessageBox::information(parent,
            i18n("Your Krusader settings, bookmarks and user actions were copied to Panero. "
                 "Krusader's own files were not changed."),
            i18n("Krusader Settings Imported"));
    } else if (!error.isEmpty()) {
        KMessageBox::error(parent, error, i18n("Importing Krusader Settings Failed"));
    }
}

bool offerImportOnFirstRun(QWidget *parent)
{
    const Paths paths = defaultPaths();

    if (QFileInfo::exists(paths.pendingFlag)) {
        QString error;
        const bool ok = runPendingImport(paths, &error);
        reportResult(parent, ok, error);
        KSharedConfig::openConfig()->reparseConfiguration();
        return ok;
    }

    if (QFileInfo::exists(paths.paneroConfig) || !krusaderSettingsExist(paths))
        return false;

    const auto answer = KMessageBox::questionTwoActions(parent,
        i18n("<p>An existing Krusader configuration was found.</p>"
             "<p>Do you want Panero to start with a copy of your Krusader settings, "
             "bookmarks and user actions? Krusader's own files are not changed.</p>"
             "<p>You can also do this later with <b>Settings &gt; Import Krusader Settings</b>.</p>"),
        i18n("Import Krusader Settings"),
        KGuiItem(i18n("Import"), QStringLiteral("document-import")),
        KGuiItem(i18n("Start Fresh"), QStringLiteral("document-new")));

    bool ok = false;
    if (answer == KMessageBox::PrimaryAction) {
        QString error;
        ok = importKrusaderSettings(paths, nullptr, &error);
        reportResult(parent, ok, error);
    } else {
        KConfig fresh(paths.paneroConfig, KConfig::SimpleConfig);
        KConfigGroup(&fresh, GROUP).writeEntry("Krusader Import Offered", true);
        fresh.sync();
    }
    KSharedConfig::openConfig()->reparseConfiguration();
    return ok;
}

void requestImportFromMenu(QWidget *parent)
{
    const Paths paths = defaultPaths();
    if (!krusaderSettingsExist(paths)) {
        KMessageBox::information(parent,
            i18n("No Krusader configuration was found (%1).", paths.krusaderConfig),
            i18n("Import Krusader Settings"));
        return;
    }
    const auto answer = KMessageBox::warningContinueCancel(parent,
        i18n("<p>Panero will replace its current settings with a copy of your Krusader settings, "
             "bookmarks and user actions the next time it starts.</p>"
             "<p>Krusader's files are not changed. Panero's current files are kept as backups "
             "named <i>*.before-krusader-import-&lt;date&gt;</i>.</p>"
             "<p>Restart Panero afterwards to complete the import.</p>"),
        i18n("Import Krusader Settings"),
        KGuiItem(i18n("Import on Next Start"), QStringLiteral("document-import")));
    if (answer != KMessageBox::Continue)
        return;
    if (!schedulePendingImport(paths))
        KMessageBox::error(parent, i18n("Could not write %1.", paths.pendingFlag));
}

} // namespace PaneroImport
