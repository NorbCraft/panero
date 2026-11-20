/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROJOBROW_H
#define PANEROJOBROW_H

#include <QPointer>
#include <QWidget>

class KJob;
class KrJob;
class ListPanel;
class QLabel;
class QProgressBar;
class QToolButton;

/**
 * Copy/move/delete progress in a panel's bottom row (D-014, R6) instead of separate
 * progress windows: shown in the panel that receives the files (or the panel the
 * files are removed from), with Cancel and a Details button that opens the classic
 * KIO progress window on demand.
 */
class PaneroJobRow : public QWidget
{
    Q_OBJECT
public:
    explicit PaneroJobRow(ListPanel *panel);

private:
    void jobManaged(KrJob *job);
    void jobStarted(KrJob *job, KJob *kjob);
    void jobTerminated(KrJob *job);
    void refresh();
    bool isTarget(KrJob *job) const;

    ListPanel *m_panel;
    QList<QPointer<KrJob>> m_jobs;
    QLabel *m_label;
    QProgressBar *m_bar;
    QToolButton *m_details;
    QToolButton *m_cancel;
};

#endif // PANEROJOBROW_H
