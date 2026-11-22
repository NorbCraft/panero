/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANERONAVIGATION_H
#define PANERONAVIGATION_H

#include <QDockWidget>

#include "Cloud/panerocloud.h"
#include <QPointer>
#include <QUrl>

class QLabel;
class QListWidget;
class QProgressDialog;
class QListWidgetItem;
class QToolButton;
class KrFileTreeView;
class QMenu;
class QSplitter;
class KConfigGroup;

/**
 * Panero's single left sidebar: pinned favourite folders and a folder tree.
 *
 * It always acts on the *active* panel (left or right) - the title shows which
 * one - follows that panel's location, and opens pins/tree folders there.
 * The tree is KIO based (lazy, asynchronous), so network or removable locations
 * do not block the UI. Pins keep the full URL plus the identity of the drive
 * they live on, so an unmounted drive can be mounted through the normal system
 * authorisation, and an unavailable pin gives a recoverable error.
 *
 * Settings: "Panero Navigation" (visibility, tree mode) and "Panero Pins".
 */
class PaneroNavigationDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit PaneroNavigationDock(QWidget *parent);
    /// The main window's sidebar (null before it exists).
    static PaneroNavigationDock *instance();
    /// Opens a cloud drive or server in the active panel, connecting it
    /// first when needed (progress window, offline fallback).
    void openCloudEntry(const PaneroCloud::Entry &entry);

    /// Pin the active panel's current folder.
    void pinCurrentFolder();
    /// Pin a folder URL; returns false if it is invalid or already pinned.
    bool pinUrl(const QUrl &folderUrl);
    void saveSettings() const;
    void restoreSettings();

public slots:
    /// The active panel changed or navigated.
    void activeLocationChanged(const QUrl &url, bool leftPanel);

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void openPin(QListWidgetItem *item);
    void pinContextMenu(const QPoint &pos);
    void treeUrlActivated(const QUrl &url);

private:
    void updatePinHighlight();
    /// Highlights the cloud drive or server the active panel is in, else none.
    void updateCloudHighlight();
    void loadCloud();
    void cloudContextMenu(const QPoint &pos);
    QWidget *m_cloudSection = nullptr;
    QListWidget *m_cloud = nullptr;
    QString m_pendingCloud;
    QPointer<QProgressDialog> m_connecting; ///< "Connecting to ..." while a drive mounts
    struct Pin {
        QUrl url;
        QString label;
        QString driveUdi;   ///< Solid UDI of the storage the folder lives on (local pins)
        QString driveLabel; ///< human readable drive name at pin time
    };

    void addPin(const Pin &pin);
    void savePins() const;
    void loadPins();
    void openUrlInActivePanel(const QUrl &url);
    bool tryMountAndOpen(const Pin &pin);
    Pin pinFromItem(const QListWidgetItem *item) const;
    QMenu *createTreeOptionsMenu(QWidget *parent);
    KConfigGroup treeConfig() const;

    QListWidget *m_pins;
    QToolButton *m_pinButton;
    KrFileTreeView *m_tree;
    QSplitter *m_splitter;
    QUrl m_currentUrl;
};

#endif // PANERONAVIGATION_H
