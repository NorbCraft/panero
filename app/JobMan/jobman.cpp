/*
    SPDX-FileCopyrightText: 2016-2022 Krusader Krew <https://krusader.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "jobman.h"

// QtCore
#include <QDebug>
#include <QUrl>
// QtWidgets
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <KConfigCore/KSharedConfig>
#include <KI18n/KLocalizedString>
#include <KIOWidgets/KIO/FileUndoManager>
#include <kio_version.h>

#include "krjob.h"
#include "panerojobwindow.h"
#include "paneroundo.h"
#include "../krglobal.h"
#include "../icon.h"

#include <QEvent>
#include <QToolButton>

const int MAX_OLD_MENU_ACTIONS = 10;

/** The menu action entry for a job in the popup menu.*/
class JobMenuAction : public QWidgetAction
{
    Q_OBJECT
public:
    JobMenuAction(KrJob *krJob, QObject *parent, KJob *kJob = nullptr)
        : QWidgetAction(parent), m_krJob(krJob)
    {
        QWidget *container = new QWidget();
        auto *layout = new QGridLayout(container);
        // Panero: what and where in words; a click shows the job's progress window
        m_description = new QLabel(PaneroJobWindow::summary(krJob));
        m_description->setToolTip(i18n("Click to show the progress window"));
        m_description->setCursor(Qt::PointingHandCursor);
        m_description->installEventFilter(this);
        m_progressBar = new QProgressBar();
        layout->addWidget(m_description, 0, 0, 1, 4);
        layout->addWidget(m_progressBar, 1, 0);

        m_showButton = new QPushButton();
        m_showButton->setIcon(Icon("view-visible"));
        m_showButton->setToolTip(i18n("Show the progress window"));
        connect(m_showButton, &QPushButton::clicked, this, [this]() { requestShow(); });
        layout->addWidget(m_showButton, 1, 3);

        m_pauseResumeButton = new QPushButton();
        updatePauseResumeButton();
        connect(m_pauseResumeButton, &QPushButton::clicked, this,
              &JobMenuAction::slotPauseResumeButtonClicked);
        layout->addWidget(m_pauseResumeButton, 1, 1);

        m_cancelButton = new QPushButton();
        m_cancelButton->setIcon(Icon("remove"));
        m_cancelButton->setToolTip(i18n("Cancel Job"));
        connect(m_cancelButton, &QPushButton::clicked,
                this, &JobMenuAction::slotCancelButtonClicked);
        layout->addWidget(m_cancelButton, 1, 2);

        setDefaultWidget(container);

        if (kJob) {
            slotStarted(kJob);
        } else {
            connect(krJob, &KrJob::started, this, &JobMenuAction::slotStarted);
        }

        connect(krJob, &KrJob::terminated, this, &JobMenuAction::slotTerminated);
    }

    bool isDone() { return !m_krJob; }

signals:
    void showRequested(KrJob *krJob);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_description && event->type() == QEvent::MouseButtonRelease) {
            requestShow();
            return true;
        }
        return QWidgetAction::eventFilter(watched, event);
    }

    void requestShow()
    {
        if (!m_krJob)
            return;
        // close the menu first: the window comes up in front
        for (QWidget *w = defaultWidget(); w; w = w->parentWidget()) {
            if (auto *menu = qobject_cast<QMenu *>(w))
                menu->close();
        }
        emit showRequested(m_krJob);
    }

protected slots:
    void slotDescription(KJob *, const QString &description,
                       const QPair<QString, QString> &field1,
                       const QPair<QString, QString> &field2)
    {
        // Panero: the summary stays short ("Copying 3 items to Documents"); the file being
        // worked on is in the tooltip and in the progress window
        if (m_krJob)
            m_description->setText(PaneroJobWindow::summary(m_krJob));
        else
            m_description->setText(description);
        QStringList details;
        for (const auto &field : {field1, field2}) {
            if (!field.first.isEmpty())
                details << QString("%1: %2").arg(field.first, field.second);
        }
        m_description->setToolTip(details.join('\n') + (details.isEmpty() ? QString() : QStringLiteral("\n"))
                                  + i18n("Click to show the progress window"));

        if (!field2.first.isEmpty() && !field1.first.isEmpty()) {
            // NOTE: tooltips for QAction items in menu are not shown
            m_progressBar->setToolTip(QString("%1: %2").arg(field1.first, field1.second));
        }
    }

    void slotPercent(KJob *, unsigned long percent) { m_progressBar->setValue(static_cast<int>(percent)); }

    void updatePauseResumeButton()
    {
        m_pauseResumeButton->setIcon(Icon(
            m_krJob->isRunning() ? "media-playback-pause" :
            m_krJob->isPaused() ? "media-playback-start" : "chronometer-start"));
        m_pauseResumeButton->setToolTip(m_krJob->isRunning() ? i18n("Pause Job") :
                                       m_krJob->isPaused() ? i18n("Resume Job") :
                                                                    i18n("Start Job"));
    }

    void slotResult(KJob *job)
    {
        // NOTE: m_job may already set to NULL now
        if(!job->error()) {
            // percent signal is not reliable, set manually
            m_progressBar->setValue(100);
        }
    }

    void slotTerminated()
    {
        qDebug() << "job description=" << m_krJob->description();
        m_pauseResumeButton->setEnabled(false);
        m_showButton->setEnabled(false);
        m_description->setCursor(Qt::ArrowCursor);
        m_cancelButton->setIcon(Icon("edit-clear"));
        m_cancelButton->setToolTip(i18n("Clear"));

        m_krJob = nullptr;
    }

    void slotPauseResumeButtonClicked()
    {
        if (!m_krJob)
          return;

        if (m_krJob->isRunning())
            m_krJob->pause();
        else
            m_krJob->start();
    }

    void slotCancelButtonClicked()
    {
        if (m_krJob) {
            m_krJob->cancel();
        } else {
            deleteLater();
        }
    }

private slots:
    void slotStarted(KJob *job)
    {
        connect(job, &KJob::description, this, &JobMenuAction::slotDescription);
        connect(job, SIGNAL(percent(KJob*,ulong)), this, SLOT(slotPercent(KJob*,ulong)));
        connect(job, &KJob::suspended, this, &JobMenuAction::updatePauseResumeButton);
        connect(job, &KJob::resumed, this, &JobMenuAction::updatePauseResumeButton);
        connect(job, &KJob::result, this, &JobMenuAction::slotResult);
        connect(job, &KJob::warning, this, [](KJob *, const QString &plain, const QString &) {
            qWarning() << "unexpected job warning: " << plain;
        });

        updatePauseResumeButton();
    }

private:
    KrJob *m_krJob;

    QLabel *m_description;
    QProgressBar *m_progressBar;
    QPushButton *m_pauseResumeButton;
    QPushButton *m_cancelButton;
    QPushButton *m_showButton;
};

#include "jobman.moc" // required for class definitions with Q_OBJECT macro in implementation files


const QString JobMan::sDefaultToolTip = i18n("No jobs");

JobMan::JobMan(QObject *parent) : QObject(parent), m_messageBox(nullptr)
{
    // job control action
    m_controlAction = new KToolBarPopupAction(Icon("media-playback-pause"),
                                          i18n("Play/Pause &Job"), this);
    m_controlAction->setEnabled(false);
    connect(m_controlAction, &QAction::triggered, this, &JobMan::slotControlActionTriggered);

    auto *menu = new QMenu(krMainWindow);
    menu->setObjectName(QStringLiteral("panero_jobs_menu"));
    menu->setMinimumWidth(360);
    // make scrollable if menu is too long
    menu->setStyleSheet("QMenu { menu-scrollable: 1; }");
    m_controlAction->setMenu(menu);

    // Panero (D-022): the jobs button on the main toolbar, shown while jobs exist; the job
    // list is followed by Pause All, queue mode and Undo
    m_jobsAction = new PaneroJobsAction(menu, this);
    m_jobsAction->setVisible(false);
    m_noJobsAction = new QAction(i18n("No file operations"), this);
    m_noJobsAction->setEnabled(false);
    menu->addAction(m_noJobsAction);
    menu->addSeparator();
    m_pauseAllAction = new QAction(Icon("media-playback-pause"), i18n("Pause All Jobs"), this);
    m_pauseAllAction->setVisible(false);
    connect(m_pauseAllAction, &QAction::triggered, this, &JobMan::slotControlActionTriggered);
    menu->addAction(m_pauseAllAction);

    // progress bar action
    m_progressBar = new QProgressBar();
    m_progressBar->setToolTip(sDefaultToolTip);
    m_progressBar->setEnabled(false);
    // listen to clicks on progress bar
    m_progressBar->installEventFilter(this);

    auto *progressAction = new QWidgetAction(krMainWindow);
    progressAction->setText(i18n("Job Progress Bar"));
    progressAction->setDefaultWidget(m_progressBar);
    m_progressAction = progressAction;

    // job queue mode action
    KConfigGroup cfg(krConfig, "JobManager");
    m_queueMode = cfg.readEntry("Queue Mode", false);
    m_modeAction = new QAction(Icon("media-playlist-repeat"), i18n("Job Queue Mode"),
                              krMainWindow);
    m_modeAction->setToolTip(i18n("Run only one job in parallel"));
    m_modeAction->setCheckable(true);
    m_modeAction->setChecked(m_queueMode);
    connect(m_modeAction, &QAction::toggled, this, [=](bool checked) mutable {
        m_queueMode = checked;
        cfg.writeEntry("Queue Mode", m_queueMode);
    });
    menu->addAction(m_modeAction);

    // undo action
    KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();
    undoManager->uiInterface()->setParentWidget(krMainWindow);

    // Panero: Ctrl+Z, "Undo: Move" (also moves to the Windows Recycle Bin, PaneroUndo)
    PaneroUndo *undo = PaneroUndo::self();
    m_undoAction = new QAction(Icon("edit-undo"), undo->undoText(), krMainWindow);
    m_undoAction->setEnabled(undo->isUndoAvailable());
    connect(m_undoAction, &QAction::triggered, undo, &PaneroUndo::undo);
    connect(undo, &PaneroUndo::undoAvailable, m_undoAction, &QAction::setEnabled);
    connect(undo, &PaneroUndo::undoTextChanged, this, &JobMan::slotUndoTextChange);
    slotUndoTextChange(undo->undoText());
    menu->addAction(m_undoAction);
}

QAction *JobMan::jobsAction() const
{
    return m_jobsAction;
}

bool JobMan::waitForJobs(bool waitForUserInput)
{
    if (m_jobs.isEmpty() && !waitForUserInput)
        return true;

    // attempt to get all job threads does not work
    //QList<QThread *> threads = krMainWindow->findChildren<QThread *>();

    m_autoCloseMessageBox = !waitForUserInput;

    m_messageBox = new QMessageBox(krMainWindow);
    m_messageBox->setWindowTitle(i18n("Warning"));
    m_messageBox->setIconPixmap(Icon("dialog-warning")
                             .pixmap(QMessageBox::standardIcon(QMessageBox::Information).size()));
    m_messageBox->setText(i18n("Are you sure you want to quit?"));
    m_messageBox->addButton(QMessageBox::Abort);
    m_messageBox->addButton(QMessageBox::Cancel);
    m_messageBox->setDefaultButton(QMessageBox::Cancel);
    for (KrJob *job: qAsConst(m_jobs))
        connect(job, &KrJob::terminated, this, &JobMan::slotUpdateMessageBox);
    slotUpdateMessageBox();

    int result = m_messageBox->exec(); // blocking
    m_messageBox->deleteLater();
    m_messageBox = nullptr;

    // accepted -> cancel all jobs
    if (result == QMessageBox::Abort) {
        for (KrJob *job: qAsConst(m_jobs)) {
            job->cancel();
        }
        return true;
    }
    // else:
    return false;
}

void JobMan::manageJob(KrJob *job, StartMode startMode)
{
    qDebug() << "new job, startMode=" << startMode;
    managePrivate(job);

    connect(job, &KrJob::started, this, &JobMan::slotKJobStarted);

    const bool enqueue = startMode == Enqueue || (startMode == Default && m_queueMode);
    if (startMode == Start || (startMode == Default && !m_queueMode) ||
        (enqueue && !jobsAreRunning())) {
        job->start();
    }

    updateUI();
}

void JobMan::manageStartedJob(KrJob *krJob, KJob *kJob)
{
    managePrivate(krJob, kJob);
    slotKJobStarted(kJob);
    updateUI();
}

// #### protected slots

void JobMan::slotKJobStarted(KJob *job)
{
    // KJob has two percent() functions
    connect(job, SIGNAL(percent(KJob*,ulong)), this,
            SLOT(slotPercent(KJob*,ulong)));
    connect(job, &KJob::description, this, &JobMan::slotDescription);
    connect(job, &KJob::suspended, this, &JobMan::updateUI);
    connect(job, &KJob::resumed, this, &JobMan::updateUI);
}

void JobMan::slotControlActionTriggered()
{
    if (m_jobs.isEmpty()) {
        // clear the list of finished jobs (the footer stays)
        const QList<QAction *> actions = m_controlAction->menu()->actions();
        for (QAction *action : actions) {
            if (dynamic_cast<JobMenuAction *>(action)) {
                m_controlAction->menu()->removeAction(action);
                action->deleteLater();
            }
        }
        m_controlAction->setEnabled(false);
        m_noJobsAction->setVisible(true);
        return;
    }

    const bool anyRunning = jobsAreRunning();
    if (!anyRunning && m_queueMode) {
        m_jobs.first()->start();
    } else {
        for (KrJob *job : qAsConst(m_jobs)) {
            if (anyRunning)
                job->pause();
            else
                job->start();
        }
    }
}

void JobMan::slotPercent(KJob *, unsigned long)
{
    updateUI();
}

void JobMan::slotDescription(KJob*,const QString &description, const QPair<QString,QString> &field1,
                     const QPair<QString,QString> &field2)
{
    // TODO cache all descriptions
    if (m_jobs.length() > 1)
        return;

    m_progressBar->setToolTip(
        QString("%1\n%2: %3\n%4: %5")
            .arg(description, field1.first, field1.second, field2.first, field2.second));
}

void JobMan::slotTerminated(KrJob *krJob)
{
    qDebug() << "terminated, job description: " << krJob->description();

    m_jobs.removeAll(krJob);
    m_windows.remove(krJob);

    // NOTE: ignoring queue mode here. We assume that if queue mode is turned off, the user created
    // jobs which were not already started with a "queue" option and still wants queue behaviour.
    if (!m_jobs.isEmpty() && !jobsAreRunning()) {
        foreach (KrJob *job, m_jobs) {
            if (!job->isPaused()) {
                // start next job
                job->start();
                break;
            }
        }
    }

    updateUI();
    cleanupMenu();
}

void JobMan::slotUpdateControlAction()
{
    bool entries = false;
    const QList<QAction *> actions = m_controlAction->menu()->actions();
    for (QAction *a : actions)
        entries = entries || dynamic_cast<JobMenuAction *>(a);
    m_controlAction->setEnabled(entries);
    m_noJobsAction->setVisible(!entries);
}

void JobMan::slotUndoTextChange(const QString &text)
{
    m_undoAction->setText(text);
    m_undoAction->setToolTip(KLocalizedString::removeAcceleratorMarker(text));
}

void JobMan::slotUpdateMessageBox()
{
    if (!m_messageBox)
        return;

    if (m_jobs.isEmpty() && m_autoCloseMessageBox) {
        m_messageBox->done(QMessageBox::Abort);
        return;
    }

    if (m_jobs.isEmpty()) {
        m_messageBox->setInformativeText("");
        m_messageBox->setButtonText(QMessageBox::Abort, "Quit");
        return;
    }

    m_messageBox->setInformativeText(i18np("There is one job operation left.",
                                          "There are %1 job operations left.", m_jobs.length()));
    m_messageBox->setButtonText(QMessageBox::Abort, "Abort Jobs and Quit");
}

// #### private

void JobMan::managePrivate(KrJob *job, KJob *kJob)
{
    auto *menuAction = new JobMenuAction(job, m_controlAction, kJob);
    connect(menuAction, &QObject::destroyed, this, &JobMan::slotUpdateControlAction);
    connect(menuAction, &JobMenuAction::showRequested, this, &JobMan::showJobWindow);
    // newest job first
    m_controlAction->menu()->insertAction(m_controlAction->menu()->actions().value(0, m_noJobsAction), menuAction);
    cleanupMenu();

    // Panero: Krusader's progress window for every job (D-022)
    auto *window = new PaneroJobWindow(job, krMainWindow);
    m_windows.insert(job, window);
    connect(window, &PaneroJobWindow::finishedInBackground, this, &PaneroJobWindow::notify);

    slotUpdateControlAction();

    connect(job, &KrJob::terminated, this, &JobMan::slotTerminated);

    m_jobs.append(job);
    emit jobManaged(job);
}

void JobMan::showJobWindow(KrJob *krJob)
{
    const QPointer<PaneroJobWindow> window = m_windows.value(krJob);
    if (window)
        window->showNow();
}

void JobMan::cleanupMenu() {
    const QList<QAction *> actions = m_controlAction->menu()->actions();
    auto jobEntries = [this]() {
        int n = 0;
        const QList<QAction *> all = m_controlAction->menu()->actions();
        for (QAction *a : all)
            n += dynamic_cast<JobMenuAction *>(a) ? 1 : 0;
        return n;
    };
    // newest entries are on top: drop finished ones from the bottom
    for (auto it = actions.crbegin(); it != actions.crend(); ++it) {
        QAction *action = *it;
        if (jobEntries() <= MAX_OLD_MENU_ACTIONS)
            break;
        auto *jobAction = dynamic_cast<JobMenuAction *>(action);
        if (jobAction && jobAction->isDone()) {
            m_controlAction->menu()->removeAction(action);
            action->deleteLater();
        }
    }
}

void JobMan::updateUI()
{
    int totalPercent = 0;
    for (KrJob *job: qAsConst(m_jobs)) {
        totalPercent += job->percent();
    }
    const bool hasJobs = !m_jobs.isEmpty();
    m_progressBar->setEnabled(hasJobs);
    if (hasJobs) {
        m_progressBar->setValue(totalPercent / m_jobs.length());
    } else {
        m_progressBar->reset();
    }
    if (!hasJobs)
        m_progressBar->setToolTip(i18n("No Jobs"));
    if (m_jobs.length() > 1)
        m_progressBar->setToolTip(i18np("%1 Job", "%1 Jobs", m_jobs.length()));

    const bool running = jobsAreRunning();
    // Panero: the jobs button with its progress ring
    m_jobsAction->setVisible(hasJobs);
    m_jobsAction->setProgress(hasJobs ? totalPercent / m_jobs.length() : 0, m_jobs.length());
    m_pauseAllAction->setVisible(hasJobs);
    m_pauseAllAction->setText(running ? i18n("Pause All Jobs") : i18n("Resume All Jobs"));
    m_pauseAllAction->setIcon(Icon(running ? "media-playback-pause" : "media-playback-start"));
    m_controlAction->setIcon(Icon(
        !hasJobs ? "edit-clear" : running ? "media-playback-pause" : "media-playback-start"));
    m_controlAction->setToolTip(!hasJobs ? i18n("Clear Job List") : running ?
                                          i18n("Pause All Jobs") :
                                          i18n("Resume Job List"));
}

bool JobMan::jobsAreRunning()
{
    return std::any_of(m_jobs.cbegin(), m_jobs.cend(), [](KrJob *job) {return job->isRunning();});
}
