/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerojobwindow.h"
#include "krjob.h"
#include "../icon.h"
#include "../krglobal.h"
#include "../panerowording.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <KCoreAddons/KFormat>
#include <KI18n/KLocalizedString>
#include <KIOCore/KIO/Global>

#ifdef Q_OS_WIN
#include <QSystemTrayIcon>
#else
#include <KNotifications/KNotification>
#endif

namespace
{
/// A label that shows a long path elided in the middle; the full text is the tooltip.
class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel(QWidget *parent)
        : QLabel(parent)
    {
        setMinimumWidth(80);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    void setFullText(const QString &text)
    {
        if (text == m_full)
            return;
        m_full = text;
        setToolTip(text);
        updateElided();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateElided();
    }

private:
    void updateElided() { setText(fontMetrics().elidedText(m_full, Qt::ElideMiddle, qMax(40, width()))); }
    QString m_full;
};

QString shownUrl(const QUrl &url)
{
    return url.toDisplayString(QUrl::PreferLocalFile | QUrl::StripTrailingSlash);
}

/// "Source" / "Destination" fields from KIO arrive as URLs; show local paths
QString shownField(const QString &value)
{
    const QUrl url(value);
    if (url.isValid() && !url.scheme().isEmpty() && url.scheme().size() > 1)
        return shownUrl(url);
    return value;
}
} // namespace

PaneroJobWindow::PaneroJobWindow(KrJob *job, QWidget *parent)
    : QDialog(parent, Qt::Window), m_job(job)
{
    setObjectName(QStringLiteral("panero_job_window"));
    setAttribute(Qt::WA_ShowWithoutActivating); // keep typing in the panels
    setWindowIcon(Icon(QStringLiteral("edit-copy")));
    m_summary = summary(job);

    auto *layout = new QVBoxLayout(this);
    m_title = new QLabel(this);
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    m_title->setWordWrap(true);
    layout->addWidget(m_title);

    auto *grid = new QGridLayout();
    m_fromCaption = new QLabel(i18nc("progress window: source of a file operation", "From:"), this);
    m_from = new ElidedLabel(this);
    m_toCaption = new QLabel(i18nc("progress window: destination of a file operation", "To:"), this);
    m_to = new ElidedLabel(this);
    grid->addWidget(m_fromCaption, 0, 0);
    grid->addWidget(m_from, 0, 1);
    grid->addWidget(m_toCaption, 1, 0);
    grid->addWidget(m_to, 1, 1);
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 100);
    m_bar->setValue(0);
    layout->addWidget(m_bar);

    m_amount = new QLabel(this);
    m_speed = new QLabel(this);
    layout->addWidget(m_amount);
    layout->addWidget(m_speed);

    auto *buttons = new QHBoxLayout();
    m_pause = new QPushButton(Icon(QStringLiteral("media-playback-pause")), i18n("&Pause"), this);
    m_hide = new QPushButton(i18n("&Keep in Background"), this);
    m_hide->setToolTip(i18n("Hide this window; the job continues. The jobs button on the toolbar shows it again."));
    m_cancel = new QPushButton(Icon(QStringLiteral("dialog-cancel")), i18n("&Cancel"), this);
    m_pause->setAutoDefault(false);
    m_cancel->setAutoDefault(false);
    m_hide->setDefault(true);
    buttons->addWidget(m_pause);
    buttons->addStretch(1);
    buttons->addWidget(m_hide);
    buttons->addWidget(m_cancel);
    layout->addLayout(buttons);
    setMinimumWidth(480);

    connect(m_pause, &QPushButton::clicked, this, &PaneroJobWindow::togglePause);
    connect(m_hide, &QPushButton::clicked, this, &PaneroJobWindow::keepInBackground);
    connect(m_cancel, &QPushButton::clicked, this, [this]() {
        m_cancelled = true;
        if (m_job)
            m_job->cancel();
    });

    m_timer = new QTimer(this);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, &PaneroJobWindow::tick);
    m_showTimer = new QTimer(this);
    m_showTimer->setSingleShot(true);
    m_showTimer->setInterval(qEnvironmentVariableIsSet("PANERO_JOB_WINDOW_DELAY")
                                 ? qEnvironmentVariableIntValue("PANERO_JOB_WINDOW_DELAY") : 600);
    connect(m_showTimer, &QTimer::timeout, this, [this]() {
        if (m_kjob && !m_background)
            show();
    });

    connect(job, &KrJob::started, this, [this](KIO::Job *kjob) { attach(kjob); });
    connect(job, &KrJob::terminated, this, &PaneroJobWindow::terminated);
    if (job->kioJob())
        attach(job->kioJob());
    tick();
}

QString PaneroJobWindow::summary(KrJob *job)
{
    if (!job)
        return QString();
    const QList<QUrl> sources = job->sources();
    const QString what = sources.count() == 1 ? sources.first().adjusted(QUrl::StripTrailingSlash).fileName()
                                              : i18np("1 item", "%1 items", sources.count());
    QUrl dest = job->destination().adjusted(QUrl::StripTrailingSlash);
    if (sources.count() == 1 && dest.fileName() == what)
        dest = dest.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
    const QString where = dest.fileName().isEmpty() ? shownUrl(dest) : dest.fileName();
    switch (job->type()) {
    case KrJob::Copy:
        return i18nc("progress: copying <file(s)> to <folder>", "Copying %1 to %2", what, where);
    case KrJob::Move:
        return i18nc("progress: moving <file(s)> to <folder>", "Moving %1 to %2", what, where);
    case KrJob::Link:
        return i18nc("progress: linking <file(s)> in <folder>", "Linking %1 in %2", what, where);
    case KrJob::Trash:
        return PANERO_TEXT(i18nc("progress: moving <file(s)> to the trash", "Moving %1 to the Trash", what),
                           i18nc("progress: moving <file(s)> to the Windows Recycle Bin", "Moving %1 to the Recycle Bin", what));
    case KrJob::Delete:
        return i18nc("progress: deleting <file(s)>", "Deleting %1", what);
    case KrJob::Pack:
        return i18nc("progress: packing <file(s)> into <archive>", "Packing %1 into %2", what, dest.fileName());
    case KrJob::Unpack:
        return i18nc("progress: unpacking <archive(s)> to <folder>", "Unpacking %1 to %2", what, where);
    case KrJob::Test:
        return i18nc("progress: testing <archive(s)>", "Testing %1", what);
    }
    return job->description();
}

QString PaneroJobWindow::formatDuration(qint64 seconds)
{
    seconds = qMax<qint64>(0, seconds);
    const qint64 h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

void PaneroJobWindow::attach(KJob *job)
{
    if (!job || m_kjob == job)
        return;
    m_kjob = job;
    m_clock.start();
    m_samples.clear();
    connect(job, &KJob::description, this,
            [this](KJob *, const QString &, const QPair<QString, QString> &field1, const QPair<QString, QString> &field2) {
                m_field1 = field1;
                m_field2 = field2;
            });
    connect(job, &KJob::suspended, this, &PaneroJobWindow::tick);
    connect(job, &KJob::resumed, this, &PaneroJobWindow::tick);
    // archive jobs run an external packer: they cannot pause
    const bool archive = m_job && (m_job->type() == KrJob::Pack || m_job->type() == KrJob::Unpack || m_job->type() == KrJob::Test);
    m_pause->setEnabled((job->capabilities() & KJob::Suspendable) && !archive);
    m_timer->start();
    if (!m_background)
        m_showTimer->start();
    tick();
}

void PaneroJobWindow::tick()
{
    const bool paused = m_kjob && m_kjob->isSuspended();
    const int percent = m_kjob ? int(m_kjob->percent()) : 0;
    m_title->setText(m_summary);
    setWindowTitle(m_kjob ? i18nc("progress window title: percent, what", "%1 % - %2", percent, m_summary) : m_summary);

    // from / to: the file being worked on (KIO's description fields), else the job's folders
    // (KIO: field 1 is the source / the file being deleted, field 2 the destination)
    QString from = m_field1.second.isEmpty() ? QString() : shownField(m_field1.second);
    QString to = m_field2.second.isEmpty() ? QString() : shownField(m_field2.second);
    if (from.isEmpty() && m_job && !m_job->sources().isEmpty())
        from = m_job->sources().count() == 1 ? shownUrl(m_job->sources().first())
                                             : shownUrl(m_job->sources().first().adjusted(QUrl::RemoveFilename));
    if (to.isEmpty() && m_job)
        to = shownUrl(m_job->destination());
    static_cast<ElidedLabel *>(m_from)->setFullText(from);
    static_cast<ElidedLabel *>(m_to)->setFullText(to);
    m_to->setVisible(!to.isEmpty());
    m_toCaption->setVisible(!to.isEmpty());

    if (!m_kjob) {
        m_bar->setRange(0, 0);
        m_amount->setText(i18n("Waiting for other jobs to finish"));
        m_speed->clear();
        m_pause->setEnabled(false);
        return;
    }
    m_bar->setRange(0, 100);
    m_bar->setValue(percent);

    const qulonglong bytesTotal = m_kjob->totalAmount(KJob::Bytes);
    const qulonglong bytesDone = m_kjob->processedAmount(KJob::Bytes);
    const qulonglong filesTotal = m_kjob->totalAmount(KJob::Files);
    const qulonglong filesDone = m_kjob->processedAmount(KJob::Files);
    QStringList amount;
    if (bytesTotal > 0)
        amount << i18nc("progress: <size done> of <total size>", "%1 of %2", KIO::convertSize(bytesDone), KIO::convertSize(bytesTotal));
    if (filesTotal > 1)
        amount << i18ncp("progress: <files done> of <total files>", "%2 of 1 file", "%2 of %1 files", filesTotal, filesDone);
    m_amount->setText(amount.join(QStringLiteral("  ·  ")));

    // speed over the last few seconds
    const qint64 now = m_clock.elapsed();
    if (!paused)
        m_samples.append(qMakePair(now, bytesDone));
    while (m_samples.size() > 2 && now - m_samples.first().first > 5000)
        m_samples.removeFirst();
    QString speedText;
    if (paused) {
        speedText = i18nc("progress: job state", "Paused");
    } else if (m_samples.size() >= 2 && m_samples.last().first > m_samples.first().first && bytesTotal > 0) {
        const double seconds = (m_samples.last().first - m_samples.first().first) / 1000.0;
        const double speed = (m_samples.last().second - m_samples.first().second) / seconds;
        if (speed > 0) {
            speedText = i18nc("progress: transfer speed", "%1/s", KIO::convertSize(KIO::filesize_t(speed)));
            if (bytesTotal > bytesDone)
                speedText = i18nc("progress: <speed>, <time> left", "%1  ·  %2 left", speedText,
                                  formatDuration(qint64((bytesTotal - bytesDone) / speed)));
        }
    }
    m_speed->setText(speedText);
    m_pause->setText(paused ? i18n("&Resume") : i18n("&Pause"));
    m_pause->setIcon(Icon(paused ? QStringLiteral("media-playback-start") : QStringLiteral("media-playback-pause")));
}

void PaneroJobWindow::togglePause()
{
    if (!m_job || !m_kjob)
        return;
    if (m_kjob->isSuspended())
        m_job->start(); // resumes
    else
        m_job->pause();
    tick();
}

void PaneroJobWindow::keepInBackground()
{
    m_background = true;
    m_showTimer->stop();
    hide();
}

void PaneroJobWindow::reject()
{
    keepInBackground(); // Esc
}

void PaneroJobWindow::closeEvent(QCloseEvent *event)
{
    // the window's close button never cancels the job
    event->ignore();
    keepInBackground();
}

void PaneroJobWindow::showNow()
{
    m_background = false;
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    show();
    raise();
    activateWindow();
}

void PaneroJobWindow::terminated()
{
    m_timer->stop();
    m_showTimer->stop();
    if (m_background && !m_cancelled) {
        const int error = m_kjob ? m_kjob->error() : 0;
        if (error != KJob::KilledJobError) {
            const bool failed = error != 0;
            const QString title = failed ? i18nc("notice title", "File operation failed")
                                         : i18nc("notice title", "File operation finished");
            QString text = m_summary;
            if (failed && m_kjob && !m_kjob->errorString().isEmpty())
                text += QLatin1Char('\n') + m_kjob->errorString();
            emit finishedInBackground(title, text, failed);
        }
    }
    hide();
    deleteLater();
}

void PaneroJobWindow::notify(const QString &title, const QString &text, bool failed)
{
    if (qEnvironmentVariableIsSet("PANERO_DEBUG_JOBS"))
        qInfo().noquote() << "Panero notice:" << title << "|" << text;
#ifdef Q_OS_WIN
    // a tray message (a Windows notification); the icon leaves again after it
    static QPointer<QSystemTrayIcon> tray;
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    if (!tray) {
        tray = new QSystemTrayIcon(QApplication::windowIcon(), qApp);
        tray->setToolTip(QStringLiteral("Panero"));
        QObject::connect(tray, &QSystemTrayIcon::messageClicked, qApp, []() {
            if (QWidget *w = krMainWindow) {
                if (w->isMinimized())
                    w->showNormal();
                w->raise();
                w->activateWindow();
            }
        });
    }
    tray->show();
    tray->showMessage(title, text, failed ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information, 8000);
    QTimer::singleShot(15000, tray, [t = tray]() {
        if (t)
            t->hide();
    });
#else
    auto *notification = new KNotification(QStringLiteral("jobFinished"), KNotification::CloseOnTimeout);
    notification->setTitle(title);
    notification->setText(text.toHtmlEscaped());
    notification->setIconName(failed ? QStringLiteral("dialog-warning") : QStringLiteral("io.github.norbcraft.panero"));
    notification->sendEvent();
#endif
}

// ---------------------------------------------------------------------------

PaneroJobsAction::PaneroJobsAction(QMenu *menu, QObject *parent)
    : QWidgetAction(parent), m_menu(menu)
{
    setText(i18n("Jobs"));
    setToolTip(i18n("File operations"));
    setMenu(menu);
    setProgress(0, 0);
}

QIcon PaneroJobsAction::ringIcon(int percent, int jobs)
{
    const QIcon base = Icon(QStringLiteral("edit-copy"));
    const QPalette palette = QApplication::palette();
    QIcon icon;
    for (const int size : {16, 22, 24, 32, 48}) {
        for (const qreal dpr : {1.0, 2.0}) {
            QPixmap pixmap(QSize(size, size) * dpr);
            pixmap.setDevicePixelRatio(dpr);
            pixmap.fill(Qt::transparent);
            QPainter p(&pixmap);
            p.setRenderHint(QPainter::Antialiasing);
            const qreal pen = qMax<qreal>(2.0, size / 9.0);
            const QRectF ring = QRectF(0, 0, size, size).adjusted(pen / 2, pen / 2, -pen / 2, -pen / 2);
            QColor track = palette.color(QPalette::WindowText);
            track.setAlphaF(0.22);
            p.setPen(QPen(track, pen));
            p.drawEllipse(ring);
            if (jobs > 0) {
                QPen arc(palette.color(QPalette::Highlight), pen);
                arc.setCapStyle(Qt::FlatCap);
                p.setPen(arc);
                // at least a sliver, so a starting job is visible
                const int span = -qMax(8, qBound(0, percent, 100) * 360 / 100) * 16;
                p.drawArc(ring, 90 * 16, span);
            }
            const int inner = int(size - 4 * pen);
            if (inner >= 8) {
                const QRect r((size - inner) / 2, (size - inner) / 2, inner, inner);
                base.paint(&p, r);
            }
            p.end();
            icon.addPixmap(pixmap);
        }
    }
    return icon;
}

void PaneroJobsAction::setProgress(int percent, int jobs)
{
    if (percent == m_percent && jobs == m_jobs)
        return;
    m_percent = percent;
    m_jobs = jobs;
    const QIcon icon = ringIcon(percent, jobs);
    setIcon(icon);
    const QString tip = jobs == 0 ? i18n("No file operations")
                                  : i18np("1 file operation running - %2 %", "%1 file operations running - %2 %", jobs, percent);
    setToolTip(tip);
    for (QWidget *w : createdWidgets()) {
        if (auto *button = qobject_cast<QToolButton *>(w)) {
            button->setIcon(icon);
            button->setToolTip(tip);
        }
    }
}

QWidget *PaneroJobsAction::createWidget(QWidget *parent)
{
    auto *bar = qobject_cast<QToolBar *>(parent);
    if (!bar)
        return nullptr; // menus: a normal entry
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("panero_jobs_button"));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setMenu(m_menu);
    button->setIcon(icon());
    button->setToolTip(toolTip());
    button->setText(text());
    button->setIconSize(bar->iconSize());
    button->setToolButtonStyle(bar->toolButtonStyle());
    connect(bar, &QToolBar::iconSizeChanged, button, &QToolButton::setIconSize);
    connect(bar, &QToolBar::toolButtonStyleChanged, button, &QToolButton::setToolButtonStyle);
    return button;
}
