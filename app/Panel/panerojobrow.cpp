/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerojobrow.h"

#include "listpanel.h"
#include "../JobMan/jobman.h"
#include "../JobMan/krjob.h"
#include "../icon.h"
#include "../krglobal.h"
#include "../krusaderview.h"
#include "../panelmanager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QToolButton>

#include <KI18n/KLocalizedString>
#include <KJobWidgets/KWidgetJobTracker>
#include <KIOCore/KIO/Job>

PaneroJobRow::PaneroJobRow(ListPanel *panel)
    : QWidget(panel), m_panel(panel)
{
    setObjectName(QStringLiteral("panero_job_row"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 0, 0);
    layout->setSpacing(6);
    m_label = new QLabel(this);
    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 100);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(6);
    m_bar->setMinimumWidth(60);
    m_bar->setMaximumWidth(160);
    m_details = new QToolButton(this);
    m_details->setAutoRaise(true);
    m_details->setIcon(Icon(QStringLiteral("view-list-details")));
    m_details->setToolTip(i18n("Show details"));
    m_cancel = new QToolButton(this);
    m_cancel->setAutoRaise(true);
    m_cancel->setIcon(Icon(QStringLiteral("dialog-cancel")));
    m_cancel->setToolTip(i18n("Cancel"));
    layout->addWidget(m_label);
    layout->addWidget(m_bar);
    layout->addWidget(m_details);
    layout->addWidget(m_cancel);
    hide();

    connect(m_cancel, &QToolButton::clicked, this, [this]() {
        if (!m_jobs.isEmpty() && m_jobs.last())
            m_jobs.last()->cancel();
    });
    connect(m_details, &QToolButton::clicked, this, [this]() {
        if (m_jobs.isEmpty() || !m_jobs.last() || !m_jobs.last()->kioJob())
            return;
        // the classic KIO progress window, on demand
        auto *tracker = new KWidgetJobTracker(window());
        tracker->registerJob(m_jobs.last()->kioJob());
        connect(m_jobs.last()->kioJob(), &QObject::destroyed, tracker, &QObject::deleteLater);
    });
    if (krJobMan)
        connect(krJobMan, &JobMan::jobManaged, this, &PaneroJobRow::jobManaged);
}

bool PaneroJobRow::isTarget(KrJob *job) const
{
    // the panel showing the destination, else the one showing where the files come from,
    // else the active panel
    QList<ListPanel *> panels;
    if (MAIN_VIEW)
        panels << MAIN_VIEW->leftPanel() << MAIN_VIEW->rightPanel();
    auto shows = [](ListPanel *panel, const QUrl &url) {
        return panel && url.isValid()
            && panel->virtualPath().adjusted(QUrl::StripTrailingSlash) == url.adjusted(QUrl::StripTrailingSlash);
    };
    ListPanel *target = nullptr;
    for (ListPanel *panel : panels) {
        if (!target && shows(panel, job->destination()))
            target = panel;
    }
    const QUrl sourceFolder = job->sources().isEmpty() ? QUrl()
        : job->sources().first().adjusted(QUrl::StripTrailingSlash | QUrl::RemoveFilename);
    for (ListPanel *panel : panels) {
        if (!target && shows(panel, sourceFolder))
            target = panel;
    }
    if (!target && MAIN_VIEW)
        target = qobject_cast<ListPanel *>(MAIN_VIEW->activePanel()->gui);
    return target == m_panel;
}

void PaneroJobRow::jobManaged(KrJob *job)
{
    if (!job || !isTarget(job))
        return;
    m_jobs.append(job);
    connect(job, &KrJob::started, this, [this, job](KIO::Job *kjob) { jobStarted(job, kjob); });
    connect(job, &KrJob::terminated, this, &PaneroJobRow::jobTerminated);
    if (job->kioJob())
        jobStarted(job, job->kioJob());
    refresh();
}

void PaneroJobRow::jobStarted(KrJob *job, KJob *kjob)
{
    Q_UNUSED(job)
    if (!kjob)
        return;
    connect(kjob, &KJob::percentChanged, this, &PaneroJobRow::refresh);
    refresh();
}

void PaneroJobRow::jobTerminated(KrJob *job)
{
    m_jobs.removeAll(QPointer<KrJob>(job));
    m_jobs.removeAll(QPointer<KrJob>());
    refresh();
}

void PaneroJobRow::refresh()
{
    m_jobs.removeAll(QPointer<KrJob>());
    if (m_jobs.isEmpty()) {
        hide();
        return;
    }
    KrJob *job = m_jobs.last();
    const int percent = job->percent();
    QString text = job->description();
    if (m_jobs.count() > 1)
        text = i18nc("job description, number of more jobs", "%1 (+%2)", text, m_jobs.count() - 1);
    m_label->setText(i18nc("job description, percent", "%1 — %2 %", text, percent));
    m_label->setToolTip(job->description());
    m_bar->setValue(percent);
    show();
}
