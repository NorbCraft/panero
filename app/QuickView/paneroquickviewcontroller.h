/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROQUICKVIEWCONTROLLER_H
#define PANEROQUICKVIEWCONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QTimer>

class KActionCollection;
class KToggleAction;
class ListPanel;
class PanelManager;
class PaneroQuickView;

/**
 * Quick View (D-022): Ctrl+Q / the toolbar button show the other panel as a
 * viewer of the active panel's current item. The other panel is only covered
 * (PanelManager::setOverlay), so closing Quick View shows it unchanged: tabs,
 * folder, view mode, selection. The viewer follows the cursor after a short
 * pause (no work while the cursor moves fast). Tab moves the keyboard focus
 * into the viewer and Tab/Esc back; Space loads a waiting cloud file.
 */
class PaneroQuickViewController : public QObject
{
    Q_OBJECT
public:
    /// Creates the "panero quick view" action (Ctrl+Q) in @p collection.
    static void setupActions(KActionCollection *collection, QObject *parent);
    static PaneroQuickViewController *instance();

    bool isOpen() const;
    PaneroQuickView *view() const;

    void open();
    void close();
    void toggle();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit PaneroQuickViewController(QObject *parent);
    ~PaneroQuickViewController() override;
    ListPanel *sourcePanel() const;
    void scheduleUpdate();
    void updateNow();
    void focusSource();
    bool focusInSource(QWidget *focus) const;

    KToggleAction *m_action = nullptr;
    QPointer<PanelManager> m_source;
    QPointer<PanelManager> m_host;
    QPointer<PaneroQuickView> m_view;
    QTimer m_followTimer;
    QList<QMetaObject::Connection> m_connections;
};

#endif // PANEROQUICKVIEWCONTROLLER_H
