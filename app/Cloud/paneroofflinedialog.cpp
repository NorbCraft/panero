/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroofflinedialog.h"
#include "panerocloud.h"
#include "panerooffline.h"
#include "panerocloudutils.h"
#include "panerorclone.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <KI18n/KLocalizedString>
#include <KWidgetsAddons/KMessageBox>

PaneroOfflineDialog::PaneroOfflineDialog(const QString &remote, const QString &label, QWidget *parent)
    : QDialog(parent), m_remote(remote)
{
    setWindowTitle(i18n("Offline Files - %1", label));
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(i18n("<p>Files you open are downloaded when needed. Folders you keep on this device (context menu "
                                  "<b>Keep on This Device</b> on a folder of the cloud drive) are copied to <tt>%1</tt> and kept in "
                                  "sync both ways, also without internet.</p><p>Offline files are normal files on this computer; "
                                  "full-disk encryption protects them if the computer is lost.</p>",
                                  PaneroOffline::mirrorRoot(remote).toHtmlEscaped()),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout;
    m_keepOpened = new QCheckBox(i18n("Keep files I open available on this device (up to 30 days)"), this);
    m_limit = new QSpinBox(this);
    m_limit->setRange(1, 1000);
    m_limit->setSuffix(i18n(" GB"));
    m_removeOnSignOut = new QCheckBox(i18n("Delete the offline files from this computer when I remove the cloud drive"), this);
    form->addRow(m_keepOpened);
    form->addRow(i18n("Space for opened files:"), m_limit);
    form->addRow(m_removeOnSignOut);
    layout->addLayout(form);

    layout->addWidget(new QLabel(i18n("Folders kept on this device:"), this));
    m_pins = new QTreeWidget(this);
    m_pins->setRootIsDecorated(false);
    m_pins->setHeaderLabels({i18n("Folder"), i18n("State")});
    m_pins->header()->setStretchLastSection(true);
    m_pins->setMinimumHeight(120);
    layout->addWidget(m_pins, 1);

    auto *pinButtons = new QHBoxLayout;
    m_openPin = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open-folder")), i18n("Open"), this);
    m_sync = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Synchronise Now"), this);
    m_free = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear")), i18n("Free Up Space"), this);
    m_repair = new QPushButton(QIcon::fromTheme(QStringLiteral("tools-wizard")), i18n("Repair..."), this);
    m_repair->setToolTip(i18n("After an interrupted synchronisation: compares both sides again; the newer version of a file wins and "
                              "replaced local files are kept in a backup folder."));
    pinButtons->addWidget(m_openPin);
    pinButtons->addWidget(m_sync);
    pinButtons->addWidget(m_repair);
    pinButtons->addWidget(m_free);
    pinButtons->addStretch();
    m_clearCache = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Delete Opened Files Now"), this);
    m_clearCache->setToolTip(i18n("Deletes the downloaded copies of opened files (not the folders kept on this device). "
                                  "Disconnect the cloud drive first."));
    pinButtons->addWidget(m_clearCache);
    layout->addLayout(pinButtons);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const PaneroOffline::Settings s = PaneroOffline::instance()->settings(remote);
    m_keepOpened->setChecked(s.keepOpened);
    m_limit->setValue(s.cacheLimitGB);
    m_removeOnSignOut->setChecked(s.removeOnSignOut);
    connect(m_keepOpened, &QCheckBox::toggled, this, &PaneroOfflineDialog::save);
    connect(m_limit, QOverload<int>::of(&QSpinBox::valueChanged), this, &PaneroOfflineDialog::save);
    connect(m_removeOnSignOut, &QCheckBox::toggled, this, &PaneroOfflineDialog::save);

    connect(m_pins, &QTreeWidget::itemSelectionChanged, this, &PaneroOfflineDialog::reload);
    connect(m_sync, &QPushButton::clicked, this, [this]() {
        PaneroOffline::instance()->syncNow(m_remote, selectedPin());
    });
    connect(m_free, &QPushButton::clicked, this, [this]() {
        const QString pin = selectedPin();
        if (pin.isNull())
            return;
        const auto answer = KMessageBox::questionTwoActions(
            this, i18n("Free up space for <b>%1</b>? Changes are uploaded first, then the local copy is deleted.",
                       (pin.isEmpty() ? i18n("(whole drive)") : pin).toHtmlEscaped()),
            i18n("Free Up Space"), KGuiItem(i18n("Free Up Space"), QStringLiteral("edit-clear")), KStandardGuiItem::cancel());
        if (answer == KMessageBox::PrimaryAction)
            PaneroOffline::instance()->unpin(this, m_remote, pin);
        reload();
    });
    connect(m_repair, &QPushButton::clicked, this, [this]() {
        const QString pin = selectedPin();
        if (pin.isNull())
            return;
        const auto answer = KMessageBox::questionTwoActions(
            this, i18n("<p>Repair <b>%1</b>?</p><p>Both sides are compared again. Where a file differs, the newer version wins; local "
                       "files that are replaced are kept in %2.</p>",
                       (pin.isEmpty() ? i18n("(whole drive)") : pin).toHtmlEscaped(),
                       (QDir::homePath() + QLatin1String("/Cloud Offline/.panero-backup")).toHtmlEscaped()),
            i18n("Repair"), KGuiItem(i18n("Repair"), QStringLiteral("tools-wizard")), KStandardGuiItem::cancel());
        if (answer == KMessageBox::PrimaryAction)
            PaneroOffline::instance()->repair(m_remote, pin);
        reload();
    });
    connect(m_openPin, &QPushButton::clicked, this, [this]() {
        const QString pin = selectedPin();
        if (pin.isNull())
            return;
        m_open = QUrl::fromLocalFile(PaneroOffline::mirrorPath(m_remote, pin));
        accept();
    });
    connect(m_clearCache, &QPushButton::clicked, this, [this]() {
        if (PaneroCloud::instance()->mountDirs().contains(m_remote)) {
            KMessageBox::information(this, i18n("Disconnect the cloud drive first."), i18n("Offline Files"));
            return;
        }
        const int dirty = PaneroCloudUtils::dirtyCacheEntries(PaneroRclone::cacheDir(m_remote));
        if (dirty > 0) {
            KMessageBox::information(this,
                                     i18np("1 file has not been uploaded yet. Connect the drive and wait until it is uploaded.",
                                           "%1 files have not been uploaded yet. Connect the drive and wait until they are uploaded.", dirty),
                                     i18n("Offline Files"));
            return;
        }
        QDir(PaneroRclone::cacheDir(m_remote) + QLatin1String("/vfs")).removeRecursively();
        QDir(PaneroRclone::cacheDir(m_remote) + QLatin1String("/vfsMeta")).removeRecursively();
        KMessageBox::information(this, i18n("The downloaded copies of opened files were deleted."), i18n("Offline Files"));
    });
    connect(PaneroOffline::instance(), &PaneroOffline::changed, this, [this](const QString &r) {
        if (r == m_remote)
            reload();
    });
    reload();
    resize(620, 460);
}

QString PaneroOfflineDialog::selectedPin() const
{
    const QList<QTreeWidgetItem *> sel = m_pins->selectedItems();
    return sel.isEmpty() ? QString() : sel.first()->data(0, Qt::UserRole).toString();
}

void PaneroOfflineDialog::reload()
{
    const QString selected = selectedPin();
    {
        const QSignalBlocker blocker(m_pins);
        m_pins->clear();
        PaneroOffline *offline = PaneroOffline::instance();
        for (const QString &pin : offline->pins(m_remote)) {
            const PaneroOffline::PinStatus st = offline->status(m_remote, pin);
            QString state;
            if (st.syncing)
                state = i18n("Synchronising...");
            else if (!st.error.isEmpty())
                state = i18n("Problem: %1", st.error);
            else if (st.lastSync.isValid())
                state = i18n("Up to date (%1)", QLocale().toString(st.lastSync, QLocale::ShortFormat));
            else
                state = i18n("Waiting");
            if (st.conflicts > 0)
                state += QLatin1String(" - ") + i18np("1 conflict copy (.conflict)", "%1 conflict copies (.conflict)", st.conflicts);
            auto *item = new QTreeWidgetItem(m_pins, {pin.isEmpty() ? i18n("(whole drive)") : pin, state});
            item->setData(0, Qt::UserRole, pin);
            item->setToolTip(1, state);
            if (pin == selected)
                item->setSelected(true);
        }
    }
    const bool have = !selectedPin().isNull();
    m_sync->setEnabled(have);
    m_repair->setEnabled(have && PaneroOffline::instance()->status(m_remote, selectedPin()).needsRepair);
    m_free->setEnabled(have);
    m_openPin->setEnabled(have);
}

void PaneroOfflineDialog::save()
{
    PaneroOffline::Settings s;
    s.keepOpened = m_keepOpened->isChecked();
    s.cacheLimitGB = m_limit->value();
    s.removeOnSignOut = m_removeOnSignOut->isChecked();
    PaneroOffline::instance()->setSettings(m_remote, s);
}
