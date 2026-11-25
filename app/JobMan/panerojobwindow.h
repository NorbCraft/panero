/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROJOBWINDOW_H
#define PANEROJOBWINDOW_H

#include <QDialog>
#include <QElapsedTimer>
#include <QPair>
#include <QPointer>
#include <QVector>
#include <QWidgetAction>

class KJob;
class KrJob;
class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QTimer;
class QToolButton;

/**
 * Progress window of one file operation (copy, move, delete, pack ...), D-022: as in
 * Krusader every job gets its own window, shown when the job runs longer than a moment:
 * what and where, a progress bar, amount done, speed, time left; Pause/Resume,
 * "Keep in Background" (also the window's close button and Esc) and Cancel. A window kept
 * in the background is reopened from the jobs button on the toolbar; when such a job
 * ends, a short notice appears (finishedInBackground()).
 */
class PaneroJobWindow : public QDialog
{
    Q_OBJECT
public:
    PaneroJobWindow(KrJob *job, QWidget *parent);

    /// "Copying 3 items to Documents", "Moving report.pdf to Archive", "Deleting 2 items"
    static QString summary(KrJob *job);
    /// "1:05" / "1:02:03"
    static QString formatDuration(qint64 seconds);
    /// Desktop notice (KNotification on Linux, a tray message on Windows)
    static void notify(const QString &title, const QString &text, bool failed);

    /// show and raise the window (jobs button menu)
    void showNow();
    bool keptInBackground() const { return m_background; }

Q_SIGNALS:
    void finishedInBackground(const QString &title, const QString &text, bool failed);

protected:
    void reject() override;
    void closeEvent(QCloseEvent *event) override;

private:
    void attach(KJob *job);
    void terminated();
    void keepInBackground();
    void togglePause();
    void tick();

    QPointer<KrJob> m_job;
    QPointer<KJob> m_kjob;
    QString m_summary;
    bool m_background = false;
    bool m_cancelled = false;
    QPair<QString, QString> m_field1, m_field2;

    QLabel *m_title;
    QLabel *m_fromCaption, *m_from, *m_toCaption, *m_to;
    QProgressBar *m_bar;
    QLabel *m_amount;
    QLabel *m_speed;
    QPushButton *m_pause;
    QPushButton *m_hide;
    QPushButton *m_cancel;
    QTimer *m_timer;
    QTimer *m_showTimer;
    QElapsedTimer m_clock;
    QVector<QPair<qint64, qulonglong>> m_samples; // (ms, bytes done)
};

/**
 * The jobs button on the main toolbar (D-022): shown while file operations run; its icon
 * has a ring with the overall progress; its menu lists the jobs (click: the job's window).
 */
class PaneroJobsAction : public QWidgetAction
{
    Q_OBJECT
public:
    PaneroJobsAction(QMenu *menu, QObject *parent);
    /// overall progress 0..100 and number of jobs
    void setProgress(int percent, int jobs);
    /// the icon: a theme icon in the middle, a progress ring around it
    static QIcon ringIcon(int percent, int jobs);

protected:
    QWidget *createWidget(QWidget *parent) override;

private:
    QMenu *m_menu;
    int m_percent = -1;
    int m_jobs = -1;
};

#endif // PANEROJOBWINDOW_H
