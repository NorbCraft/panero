/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANERODRIVEBAR_H
#define PANERODRIVEBAR_H

#include <QIcon>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QWidget>

class ListPanel;
class QTimer;
class QToolButton;
class QSocketNotifier;
class QFile;

/// One drive button (D-019).
struct PaneroDrive {
    enum Kind { System, Home, Volume, Network, Cloud, NetworkPlaces };
    Kind kind = Volume;
    QString id;      ///< stable id for "hide this button"
    QString label;
    QString shortLabel; ///< "C:" on Windows; empty: the short button shows only the icon
    QIcon icon;
    QUrl url;        ///< where a click goes; invalid while not mounted
    QString udi;     ///< Solid device (volumes)
    QString cloudId; ///< PaneroCloud entry (clouds)
    bool removable = false;
    bool optical = false;
    QString toolTip;
};

/**
 * The drives shown as buttons, shared by both panels. Built from the mount
 * table, Solid (udisks) and Panero's cloud entries only - no file access in
 * the GUI thread (D-018). Updates itself when something is mounted,
 * plugged in or removed.
 */
class PaneroDrives : public QObject
{
    Q_OBJECT
public:
    static PaneroDrives *instance();
    QList<PaneroDrive> drives() const
    {
        return m_drives;
    }
    /// Rebuild soon (several triggers in a row cause one rebuild).
    void scheduleRefresh();

signals:
    void changed();

private:
    PaneroDrives();
    void refresh();
    QList<PaneroDrive> m_drives;
    QTimer *m_debounce = nullptr;
    QFile *m_mountInfo = nullptr;
    QSocketNotifier *m_mountNotifier = nullptr;
};

/**
 * A row of drive buttons above a panel (Total Commander style). A click opens
 * the drive in this panel; the drive the panel is on is highlighted.
 * Shown when "Show Drive Buttons" is on (Quick Settings, off by default).
 */
class PaneroDriveBar : public QWidget
{
    Q_OBJECT
public:
    explicit PaneroDriveBar(ListPanel *panel);
    static bool enabledInSettings();
    /// "short": letter only (Windows) / icon only (Linux); else icon and name
    static bool shortLabelsInSettings();

public slots:
    void setCurrentUrl(const QUrl &url);

private:
    void rebuild();
    void updateVisibility();
    void updateChecked();
    void open(const PaneroDrive &drive);
    void showMenu(const PaneroDrive &drive, QToolButton *button, const QPoint &pos);
    /// Windows: network drives, Network Locations and "Open Network Path..."
    void showNetworkMenu(QToolButton *button);
    ListPanel *m_panel;
    QUrl m_url;
    QList<QPointer<QToolButton>> m_buttons;
    QList<PaneroDrive> m_shown;
};

#endif // PANERODRIVEBAR_H
