/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroquickviewcontroller.h"
#include "paneroquickview.h"

#include "../FileSystem/fileitem.h"
#include "../FileSystem/filesystem.h"
#include "../Panel/PanelView/krview.h"
#include "../Panel/listpanel.h"
#include "../Panel/panelfunc.h"
#include "../icon.h"
#include "../krglobal.h"
#include "../krusaderview.h"
#include "../panelmanager.h"

#include <QApplication>
#include <QKeyEvent>

#include <KI18n/KLocalizedString>
#include <KXmlGui/KActionCollection>
#include <KWidgetsAddons/KToggleAction>

namespace
{
PaneroQuickViewController *s_instance = nullptr;
/// the viewer follows the cursor after this pause (ms): no work while scrolling fast
constexpr int FollowDelay = 150;
}

void PaneroQuickViewController::setupActions(KActionCollection *collection, QObject *parent)
{
    if (!s_instance)
        s_instance = new PaneroQuickViewController(parent);
    auto *action = new KToggleAction(Icon(QStringLiteral("view-preview")), i18n("&Quick View"), parent);
    action->setIconText(i18nc("toolbar button", "Quick View"));
    action->setToolTip(i18n("Quick View: show the file under the cursor in the other panel (Ctrl+Q)"));
    action->setWhatsThis(i18n("The other panel shows the file or folder under the cursor: pictures, text and code, "
                              "folder sizes, archive contents, PDF pages, audio and video. Tab moves into the viewer, "
                              "Tab or Esc back. Files on cloud drives load only when you press Space."));
    collection->addAction(QStringLiteral("panero quick view"), action);
    collection->setDefaultShortcut(action, QKeySequence(Qt::CTRL + Qt::Key_Q));
    connect(action, &QAction::triggered, s_instance, [](bool) { s_instance->toggle(); });
    s_instance->m_action = action;
}

PaneroQuickViewController *PaneroQuickViewController::instance()
{
    return s_instance;
}

PaneroQuickViewController::PaneroQuickViewController(QObject *parent)
    : QObject(parent)
{
    m_followTimer.setSingleShot(true);
    m_followTimer.setInterval(FollowDelay);
    connect(&m_followTimer, &QTimer::timeout, this, &PaneroQuickViewController::updateNow);
}

PaneroQuickViewController::~PaneroQuickViewController()
{
    ListPanel::cursorChangedHook = nullptr;
    qApp->removeEventFilter(this);
    if (s_instance == this)
        s_instance = nullptr;
}

bool PaneroQuickViewController::isOpen() const
{
    return !m_host.isNull();
}

PaneroQuickView *PaneroQuickViewController::view() const
{
    return m_view.data();
}

ListPanel *PaneroQuickViewController::sourcePanel() const
{
    if (!m_source || !m_source->currentPanel())
        return nullptr;
    return m_source->currentPanel()->gui;
}

void PaneroQuickViewController::toggle()
{
    if (isOpen())
        close();
    else
        open();
}

void PaneroQuickViewController::open()
{
    if (isOpen() || !MAIN_VIEW || !MAIN_VIEW->activeManager()) {
        if (m_action)
            m_action->setChecked(isOpen());
        return;
    }
    m_source = MAIN_VIEW->activeManager();
    m_host = MAIN_VIEW->inactiveManager();
    if (!m_view) {
        m_view = new PaneroQuickView;
        connect(m_view, &PaneroQuickView::closeRequested, this, &PaneroQuickViewController::close);
        connect(m_view, &PaneroQuickView::openRequested, this, [this](const QUrl &url) {
            ListPanel *panel = sourcePanel();
            if (panel && panel->func && url.isValid())
                panel->func->execute(url.fileName());
        });
    }
    m_host->setOverlay(m_view);

    m_connections << connect(m_source, &PanelManager::pathChanged, this, &PaneroQuickViewController::scheduleUpdate);
    // the covered side becomes active (its drive menu, a command): show it again
    m_connections << connect(m_host, &PanelManager::setActiveManager, this, &PaneroQuickViewController::close, Qt::QueuedConnection);
    ListPanel::cursorChangedHook = [this](ListPanel *panel) {
        if (panel == sourcePanel())
            scheduleUpdate();
    };
    qApp->installEventFilter(this);
    if (m_action)
        m_action->setChecked(true);
    updateNow(); // the first item at once
}

void PaneroQuickViewController::close()
{
    if (!isOpen()) {
        if (m_action)
            m_action->setChecked(false);
        return;
    }
    m_followTimer.stop();
    qApp->removeEventFilter(this);
    ListPanel::cursorChangedHook = nullptr;
    for (const auto &c : qAsConst(m_connections))
        disconnect(c);
    m_connections.clear();
    const bool viewHadFocus = m_view && QApplication::focusWidget() && m_view->isAncestorOf(QApplication::focusWidget());
    if (m_view)
        m_view->clear();
    if (m_host)
        m_host->setOverlay(nullptr);
    m_host = nullptr;
    if (m_action)
        m_action->setChecked(false);
    if (viewHadFocus)
        focusSource();
    m_source = nullptr;
}

void PaneroQuickViewController::scheduleUpdate()
{
    if (isOpen())
        m_followTimer.start();
}

void PaneroQuickViewController::updateNow()
{
    if (!isOpen() || !m_view)
        return;
    ListPanel *panel = sourcePanel();
    PaneroQuickView::Item item;
    if (panel && panel->view && panel->func) {
        const QString name = panel->view->getCurrentItem();
        if (name == QLatin1String("..")) {
            item.isParent = true;
            item.name = name;
        } else if (!name.isEmpty()) {
            if (FileItem *file = panel->func->files()->getFileItem(name)) {
                item.url = file->getUrl();
                item.name = file->getName();
                item.isDir = file->isDir();
                item.size = file->isDir() ? -1 : qint64(file->getUISize());
                if (file->getModificationTime() > 0)
                    item.modified = QDateTime::fromSecsSinceEpoch(file->getModificationTime());
                item.iconName = file->getIcon(); // the panel already shows it (cached)
            }
        }
    }
    m_view->showItem(item);
}

void PaneroQuickViewController::focusSource()
{
    if (ListPanel *panel = sourcePanel()) {
        panel->slotFocusOnMe();
        if (panel->view && panel->view->widget())
            panel->view->widget()->setFocus(Qt::TabFocusReason);
    }
}

bool PaneroQuickViewController::focusInSource(QWidget *focus) const
{
    ListPanel *panel = sourcePanel();
    if (!panel || !focus || !panel->view || !panel->view->widget())
        return false;
    QWidget *list = panel->view->widget();
    return focus == list || list->isAncestorOf(focus);
}

bool PaneroQuickViewController::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if ((type != QEvent::KeyPress && type != QEvent::ShortcutOverride) || !isOpen() || !m_view)
        return QObject::eventFilter(watched, event);
    QWidget *focus = QApplication::focusWidget();
    if (watched != focus)
        return QObject::eventFilter(watched, event); // each key once, at the focus widget
    auto *ke = static_cast<QKeyEvent *>(event);
    const Qt::KeyboardModifiers mods = ke->modifiers() & ~Qt::KeypadModifier;
    const bool inView = focus && m_view->isAncestorOf(focus);
    const bool inSource = !inView && focusInSource(focus);
    if (!inView && !inSource)
        return QObject::eventFilter(watched, event);

    enum { None, IntoView, BackToPanel, Load } act = None;
    if (ke->key() == Qt::Key_Tab && mods == Qt::NoModifier)
        act = inSource ? IntoView : BackToPanel;
    else if (ke->key() == Qt::Key_Backtab && inView)
        act = BackToPanel;
    else if (ke->key() == Qt::Key_Escape && mods == Qt::NoModifier && inView && !m_view->wantsEscape())
        act = BackToPanel;
    else if (ke->key() == Qt::Key_Space && mods == Qt::NoModifier && inSource && m_view->waitingForLoad()) {
        // the current item, not one the cursor just left (the viewer may lag by the follow delay)
        m_followTimer.stop();
        updateNow();
        if (m_view->waitingForLoad())
            act = Load;
    }
    if (act == None)
        return QObject::eventFilter(watched, event);
    if (type == QEvent::ShortcutOverride) {
        // Tab is the "switch panel" shortcut: take the key instead
        ke->accept();
        return true;
    }
    switch (act) {
    case IntoView:
        m_followTimer.stop();
        updateNow();
        m_view->focusContent();
        break;
    case BackToPanel:
        focusSource();
        break;
    case Load:
        m_view->loadNow();
        break;
    case None:
        break;
    }
    return true;
}
