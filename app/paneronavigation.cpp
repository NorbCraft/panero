/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneronavigation.h"

#include "icon.h"
#include "krglobal.h"
#include "krusaderview.h"
#include "Panel/krfiletreeview.h"
#include "Panel/krpanel.h"
#include "Panel/listpanel.h"
#include "Panel/panelfunc.h"
#include "MountMan/kmountman.h"

#include <memory>

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

#include <KConfigCore/KConfigGroup>
#include <KI18n/KLocalizedString>
#include <KWidgetsAddons/KMessageBox>
#include <Solid/Device>
#include <Solid/StorageAccess>
#include <Solid/StorageVolume>

namespace
{
const char *NAV_GROUP = "Panero Navigation";
const char *PIN_GROUP = "Panero Pins";
const int UrlRole = Qt::UserRole + 1;
const int UdiRole = Qt::UserRole + 2;
const int DriveLabelRole = Qt::UserRole + 3;

QString iconForUrl(const QUrl &url)
{
    if (!url.isLocalFile())
        return url.scheme() == QLatin1String("trash") ? QStringLiteral("user-trash") : QStringLiteral("folder-network");
    const QString path = url.toLocalFile();
    if (QDir::cleanPath(path) == QDir::homePath())
        return QStringLiteral("user-home");
    if (path == QLatin1String("/"))
        return QStringLiteral("drive-harddisk-root");
    return QStringLiteral("folder");
}

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    return label;
}
} // namespace

PaneroNavigationDock::PaneroNavigationDock(QWidget *parent)
    : QDockWidget(i18n("Navigation"), parent)
{
    setObjectName(QStringLiteral("panero_navigation_dock"));
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(4, 2, 2, 2);
    layout->setSpacing(3);

    m_target = new QLabel(content);
    m_target->setToolTip(i18n("Pins and folders open in the active panel"));
    QPalette p = m_target->palette();
    p.setColor(QPalette::WindowText, p.color(QPalette::Highlight));
    m_target->setPalette(p);
    layout->addWidget(m_target);

    // pinned folders
    auto *pinHeader = new QHBoxLayout;
    pinHeader->setSpacing(2);
    pinHeader->addWidget(sectionLabel(i18n("Pinned"), content));
    pinHeader->addStretch();
    m_pinButton = new QToolButton(content);
    m_pinButton->setIcon(Icon(QStringLiteral("list-add")));
    m_pinButton->setAutoRaise(true);
    m_pinButton->setToolTip(i18n("Pin the active panel's folder"));
    m_pinButton->setAccessibleName(i18n("Pin current folder"));
    connect(m_pinButton, &QToolButton::clicked, this, &PaneroNavigationDock::pinCurrentFolder);
    pinHeader->addWidget(m_pinButton);
    layout->addLayout(pinHeader);

    m_pins = new QListWidget(content);
    m_pins->setAccessibleName(i18n("Pinned folders"));
    m_pins->setContextMenuPolicy(Qt::CustomContextMenu);
    m_pins->setUniformItemSizes(true);
    m_pins->setMaximumHeight(170);
    m_pins->setFrameShape(QFrame::NoFrame);
    connect(m_pins, &QListWidget::itemActivated, this, &PaneroNavigationDock::openPin);
    connect(m_pins, &QListWidget::itemClicked, this, &PaneroNavigationDock::openPin);
    connect(m_pins, &QListWidget::customContextMenuRequested, this, &PaneroNavigationDock::pinContextMenu);
    layout->addWidget(m_pins);

    // folder tree
    auto *treeHeader = new QHBoxLayout;
    treeHeader->setSpacing(2);
    treeHeader->addWidget(sectionLabel(i18n("Folders"), content));
    treeHeader->addStretch();
    auto *treeOptions = new QToolButton(content);
    treeOptions->setIcon(Icon(QStringLiteral("configure")));
    treeOptions->setAutoRaise(true);
    treeOptions->setToolTip(i18n("Folder tree options (start from place, current folder or root; hidden folders)"));
    treeOptions->setAccessibleName(i18n("Folder tree options"));
    treeHeader->addWidget(treeOptions);
    layout->addLayout(treeHeader);
    m_tree = new KrFileTreeView(content);
    // the tree's own options menu (normally on its header, which is hidden here)
    connect(treeOptions, &QToolButton::clicked, m_tree, [this]() {
        QMetaObject::invokeMethod(m_tree, "showHeaderContextMenu");
        // "Show Details" adds columns: show the header only when there is more than one
        QHeaderView *header = m_tree->header();
        header->setVisible(header->count() - header->hiddenSectionCount() > 1);
    });
    m_tree->setAccessibleName(i18n("Folder tree"));
    connect(m_tree, &KrFileTreeView::urlActivated, this, &PaneroNavigationDock::treeUrlActivated);
    layout->addWidget(m_tree, 1);

    setWidget(content);
    setMinimumWidth(170);
    // compact: no separate title bar; the "Opens in" line is the header and the
    // toolbar's Folder Tree button shows/hides the sidebar
    setTitleBarWidget(new QWidget(this));
    updateTitle(true);
    loadPins();
}

void PaneroNavigationDock::updateTitle(bool leftPanel)
{
    m_target->setText(leftPanel ? i18n("Opens in: left panel") : i18n("Opens in: right panel"));
}

void PaneroNavigationDock::activeLocationChanged(const QUrl &url, bool leftPanel)
{
    updateTitle(leftPanel);
    const bool changed = url != m_currentUrl;
    m_currentUrl = url;
    if (changed && isVisible())
        m_tree->setCurrentUrl(url);
}

void PaneroNavigationDock::showEvent(QShowEvent *event)
{
    QDockWidget::showEvent(event);
    // the tree is only updated while visible: catch up with the active panel
    if (m_currentUrl.isValid())
        m_tree->setCurrentUrl(m_currentUrl);
}

void PaneroNavigationDock::treeUrlActivated(const QUrl &url)
{
    openUrlInActivePanel(url);
}

void PaneroNavigationDock::openUrlInActivePanel(const QUrl &url)
{
    if (!url.isValid() || !ACTIVE_PANEL)
        return;
    ACTIVE_FUNC->openUrl(url);
    ACTIVE_PANEL->gui->slotFocusOnMe();
}

void PaneroNavigationDock::pinCurrentFolder()
{
    if (!ACTIVE_PANEL)
        return;
    Pin pin;
    pin.url = ACTIVE_PANEL->virtualPath().adjusted(QUrl::StripTrailingSlash);
    for (int i = 0; i < m_pins->count(); ++i) {
        if (m_pins->item(i)->data(UrlRole).toUrl() == pin.url)
            return; // already pinned
    }
    pin.label = pin.url.isLocalFile() ? QFileInfo(pin.url.toLocalFile()).fileName() : pin.url.fileName();
    if (pin.label.isEmpty())
        pin.label = pin.url.toDisplayString(QUrl::PreferLocalFile);
    if (pin.url.isLocalFile()) {
        const QString udi = krMtMan.findUdiForPath(pin.url.toLocalFile(), Solid::DeviceInterface::StorageAccess);
        if (!udi.isEmpty()) {
            Solid::Device device(udi);
            pin.driveUdi = udi;
            pin.driveLabel = device.description();
        }
    }
    addPin(pin);
    savePins();
}

void PaneroNavigationDock::addPin(const Pin &pin)
{
    auto *item = new QListWidgetItem(Icon(iconForUrl(pin.url)), pin.label, m_pins);
    item->setData(UrlRole, pin.url);
    item->setData(UdiRole, pin.driveUdi);
    item->setData(DriveLabelRole, pin.driveLabel);
    QString tip = pin.url.toDisplayString(QUrl::PreferLocalFile);
    if (!pin.driveLabel.isEmpty())
        tip += QLatin1Char('\n') + i18n("Drive: %1", pin.driveLabel);
    item->setToolTip(tip);
}

PaneroNavigationDock::Pin PaneroNavigationDock::pinFromItem(const QListWidgetItem *item) const
{
    Pin pin;
    pin.url = item->data(UrlRole).toUrl();
    pin.label = item->text();
    pin.driveUdi = item->data(UdiRole).toString();
    pin.driveLabel = item->data(DriveLabelRole).toString();
    return pin;
}

bool PaneroNavigationDock::tryMountAndOpen(const Pin &pin)
{
    if (pin.driveUdi.isEmpty())
        return false;
    Solid::Device device(pin.driveUdi);
    auto *access = device.is<Solid::StorageAccess>() ? device.as<Solid::StorageAccess>() : nullptr;
    if (!device.isValid() || !access || access->isAccessible())
        return false;
    // normal desktop mount (udisks2, with the system's own authorisation if needed)
    const QUrl url = pin.url;
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(access, &Solid::StorageAccess::setupDone, this,
            [this, url, connection](Solid::ErrorType error, const QVariant &errorData, const QString &) {
                QObject::disconnect(*connection); // one-shot (Qt 5 has no SingleShotConnection)
                if (error == Solid::NoError)
                    openUrlInActivePanel(url);
                else
                    KMessageBox::error(this, i18n("The drive could not be mounted: %1", errorData.toString()));
            });
    access->setup();
    return true;
}

void PaneroNavigationDock::openPin(QListWidgetItem *item)
{
    if (!item)
        return;
    const Pin pin = pinFromItem(item);
    if (!pin.url.isLocalFile() || QFileInfo(pin.url.toLocalFile()).isDir()) {
        openUrlInActivePanel(pin.url); // remote pins: KIO reports errors in the panel
        return;
    }

    // local folder is missing: drive unplugged / not mounted, or folder removed
    Solid::Device device(pin.driveUdi);
    const bool canMount = device.isValid() && device.is<Solid::StorageAccess>()
                          && !device.as<Solid::StorageAccess>()->isAccessible();
    QString message = i18n("<p>The pinned folder <b>%1</b> is not available.</p>",
                           pin.url.toDisplayString(QUrl::PreferLocalFile).toHtmlEscaped());
    if (canMount)
        message += i18n("<p>The drive <b>%1</b> is connected but not mounted.</p>", pin.driveLabel.toHtmlEscaped());
    else if (!pin.driveLabel.isEmpty())
        message += i18n("<p>It is on the drive <b>%1</b>, which may be disconnected.</p>", pin.driveLabel.toHtmlEscaped());

    const auto answer = KMessageBox::warningTwoActionsCancel(
        this, message, i18n("Pinned Folder Unavailable"),
        canMount ? KGuiItem(i18n("Mount and Open"), QStringLiteral("media-mount")) : KGuiItem(i18n("Retry"), QStringLiteral("view-refresh")),
        KGuiItem(i18n("Unpin"), QStringLiteral("list-remove")));
    if (answer == KMessageBox::PrimaryAction) {
        if (canMount)
            tryMountAndOpen(pin);
        else
            openPin(item); // retry
    } else if (answer == KMessageBox::SecondaryAction) {
        delete item;
        savePins();
    }
}

void PaneroNavigationDock::pinContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_pins->itemAt(pos);
    QMenu menu(this);
    QAction *open = nullptr, *unpin = nullptr, *up = nullptr, *down = nullptr;
    if (item) {
        open = menu.addAction(Icon(QStringLiteral("document-open-folder")), i18n("Open in Active Panel"));
        up = menu.addAction(Icon(QStringLiteral("go-up")), i18n("Move Up"));
        down = menu.addAction(Icon(QStringLiteral("go-down")), i18n("Move Down"));
        menu.addSeparator();
        unpin = menu.addAction(Icon(QStringLiteral("list-remove")), i18n("Unpin"));
        menu.addSeparator();
    }
    QAction *pinCurrent = menu.addAction(Icon(QStringLiteral("list-add")), i18n("Pin Active Panel's Folder"));
    QAction *chosen = menu.exec(m_pins->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == open) {
        openPin(item);
    } else if (chosen == unpin) {
        delete item;
        savePins();
    } else if (chosen == up || chosen == down) {
        const int row = m_pins->row(item);
        const int target = chosen == up ? row - 1 : row + 1;
        if (target >= 0 && target < m_pins->count()) {
            m_pins->insertItem(target, m_pins->takeItem(row));
            m_pins->setCurrentRow(target);
            savePins();
        }
    } else if (chosen == pinCurrent) {
        pinCurrentFolder();
    }
}

void PaneroNavigationDock::savePins() const
{
    KConfigGroup g(krConfig, PIN_GROUP);
    QStringList urls, labels, udis, drives;
    for (int i = 0; i < m_pins->count(); ++i) {
        const QListWidgetItem *item = m_pins->item(i);
        urls << item->data(UrlRole).toUrl().toString();
        labels << item->text();
        udis << item->data(UdiRole).toString();
        drives << item->data(DriveLabelRole).toString();
    }
    g.writeEntry("Urls", urls);
    g.writeEntry("Labels", labels);
    g.writeEntry("Drive Udis", udis);
    g.writeEntry("Drive Labels", drives);
    g.sync();
}

void PaneroNavigationDock::loadPins()
{
    m_pins->clear();
    const KConfigGroup g(krConfig, PIN_GROUP);
    if (!g.hasKey("Urls")) {
        // first start: a sensible default
        Pin home;
        home.url = QUrl::fromLocalFile(QDir::homePath());
        home.label = i18n("Home");
        addPin(home);
        return;
    }
    const QStringList urls = g.readEntry("Urls", QStringList());
    const QStringList labels = g.readEntry("Labels", QStringList());
    const QStringList udis = g.readEntry("Drive Udis", QStringList());
    const QStringList drives = g.readEntry("Drive Labels", QStringList());
    for (int i = 0; i < urls.size(); ++i) {
        Pin pin;
        pin.url = QUrl(urls[i]);
        pin.label = labels.value(i, pin.url.fileName());
        pin.driveUdi = udis.value(i);
        pin.driveLabel = drives.value(i);
        if (pin.url.isValid())
            addPin(pin);
    }
}

void PaneroNavigationDock::saveSettings() const
{
    KConfigGroup g(krConfig, NAV_GROUP);
    g.writeEntry("Visible", isVisible());
    g.writeEntry("Width", width());
    m_tree->saveSettings(KConfigGroup(krConfig, "Panero Navigation Tree"));
}

void PaneroNavigationDock::restoreSettings()
{
    const KConfigGroup g(krConfig, NAV_GROUP);
    // Panero default: root the tree at the nearest place (e.g. Home) instead of "/"
    KConfigGroup navTree(krConfig, "Panero Navigation Tree");
    KConfigGroup treeGroup(&navTree, "TreeView");
    if (!treeGroup.hasKey("StartFromPlace"))
        treeGroup.writeEntry("StartFromPlace", true);
    m_tree->restoreSettings(KConfigGroup(krConfig, "Panero Navigation Tree"));
    // single name column (default): no header needed
    m_tree->header()->setVisible(m_tree->header()->count() - m_tree->header()->hiddenSectionCount() > 1);
    setVisible(g.readEntry("Visible", true));
    resize(g.readEntry("Width", 230), height());
}
