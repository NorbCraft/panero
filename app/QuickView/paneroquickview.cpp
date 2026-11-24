/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroquickview.h"
#include "paneroquickviewwidgets.h"

#include "../FileSystem/paneroslowpaths.h"
#ifdef Q_OS_WIN
#include "../FileSystem/panerocloudfiles.h"
#endif
#include "../icon.h"

#include <QApplication>

#include <cstdio>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMimeDatabase>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStackedWidget>
#include <QTextBlock>
#include <QThreadPool>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <KCoreAddons/KFormat>
#include <KI18n/KLocalizedString>
#include <KWidgetsAddons/KAcceleratorManager>

#ifdef PANERO_HAVE_SYNTAX
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#endif

#ifdef PANERO_HAVE_MULTIMEDIA
#include <QMediaPlayer>
#include <QVideoWidget>
#endif

using namespace PaneroQuickViewData;

struct PaneroQuickView::Loaded {
    Kind kind = Kind::Other;
    bool exists = true;
    bool gated = false; ///< online-only placeholder (Windows), found in the worker
    QString mime;
    QString mimeComment;
    qint64 size = -1;
    QDateTime modified;
    QDateTime created;
    QString permissions;
    QString owner;
    QString linkTarget;
    QString error;
    TextResult text;
    ImageResult image;
    ArchiveResult archive;
    PdfResult pdf;
};

namespace
{
QThreadPool *workerPool()
{
    // own pool: a read stuck on a network drive does not block Qt's global pool
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool(qApp);
        p->setMaxThreadCount(4);
        p->setExpiryTimeout(20000);
        return p;
    }();
    return pool;
}

QString formatSize(qint64 size)
{
    if (size < 0)
        return QString();
    if (size < 1024)
        return i18np("1 byte", "%1 bytes", size);
    return i18nc("size (bytes)", "%1 (%2 bytes)", KFormat().formatByteSize(double(size), 1),
                 QLocale().toString(size));
}

QString shortSize(qint64 size)
{
    return size < 0 ? QString() : KFormat().formatByteSize(double(size), 1);
}

QString formatDate(const QDateTime &dt)
{
    return dt.isValid() ? QLocale().toString(dt.toLocalTime(), QLocale::ShortFormat) : QString();
}

QString permissionString(const QFileInfo &fi)
{
    QString s = fi.isDir() ? QStringLiteral("d") : fi.isSymLink() ? QStringLiteral("l") : QStringLiteral("-");
    const QFile::Permissions p = fi.permissions();
    s += p & QFile::ReadOwner ? QLatin1Char('r') : QLatin1Char('-');
    s += p & QFile::WriteOwner ? QLatin1Char('w') : QLatin1Char('-');
    s += p & QFile::ExeOwner ? QLatin1Char('x') : QLatin1Char('-');
    s += p & QFile::ReadGroup ? QLatin1Char('r') : QLatin1Char('-');
    s += p & QFile::WriteGroup ? QLatin1Char('w') : QLatin1Char('-');
    s += p & QFile::ExeGroup ? QLatin1Char('x') : QLatin1Char('-');
    s += p & QFile::ReadOther ? QLatin1Char('r') : QLatin1Char('-');
    s += p & QFile::WriteOther ? QLatin1Char('w') : QLatin1Char('-');
    s += p & QFile::ExeOther ? QLatin1Char('x') : QLatin1Char('-');
    return s;
}

QLabel *wrapLabel(QWidget *parent, Qt::Alignment align = Qt::AlignCenter)
{
    auto *l = new QLabel(parent);
    l->setWordWrap(true);
    l->setAlignment(align);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

#ifdef PANERO_HAVE_SYNTAX
KSyntaxHighlighting::Repository *syntaxRepository()
{
    static KSyntaxHighlighting::Repository *repo = new KSyntaxHighlighting::Repository;
    return repo;
}
#endif

/// highlighting a long file blocks the window (QSyntaxHighlighter runs in the GUI thread)
constexpr int HighlightLimit = 384 * 1024;
} // namespace

PaneroQuickView::PaneroQuickView(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("paneroQuickView"));
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
    setFocusPolicy(Qt::NoFocus);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // header: icon, name, details, close
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("paneroQuickViewHeader"));
    header->setAutoFillBackground(true);
    header->setBackgroundRole(QPalette::Window);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(8, 4, 4, 4);
    hl->setSpacing(8);
    m_icon = new QLabel(header);
    m_icon->setFixedSize(22, 22);
    m_name = new QLabel(header);
    QFont bold = m_name->font();
    bold.setBold(true);
    m_name->setFont(bold);
    m_name->setTextFormat(Qt::PlainText);
    m_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_details = new QLabel(header);
    m_details->setTextFormat(Qt::PlainText);
    m_details->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred); // the name gives way
    m_details->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_details->setForegroundRole(QPalette::PlaceholderText);
    m_close = new QToolButton(header);
    m_close->setAutoRaise(true);
    m_close->setIcon(Icon(QStringLiteral("window-close")));
    m_close->setToolTip(i18n("Close Quick View (Ctrl+Q)"));
    m_close->setFocusPolicy(Qt::NoFocus);
    connect(m_close, &QToolButton::clicked, this, &PaneroQuickView::closeRequested);
    hl->addWidget(m_icon);
    hl->addWidget(m_name, 1);
    hl->addWidget(m_details);
    hl->addWidget(m_close);
    layout->addWidget(header);

    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack, 1);

    m_emptyPage = buildEmptyPage();
    m_waitingPage = buildWaitingPage();
    m_loadingPage = new QWidget(m_stack);
    {
        auto *l = new QVBoxLayout(m_loadingPage);
        auto *label = wrapLabel(m_loadingPage);
        label->setText(i18n("Loading..."));
        label->setForegroundRole(QPalette::PlaceholderText);
        l->addWidget(label);
    }
    m_image = new QuickImageView(m_stack);
    connect(m_image, &QuickImageView::zoomChanged, this, [this]() { updateHeader(); });
    m_textPage = buildTextPage();
    m_folderPage = buildFolderPage();
    m_archivePage = buildArchivePage();
    m_pdfPage = buildPdfPage();
    m_mediaPage = buildMediaPage();
    m_infoPage = buildInfoPage();
    for (QWidget *w : {m_emptyPage, m_waitingPage, m_loadingPage, static_cast<QWidget *>(m_image), m_textPage, m_folderPage,
                       m_archivePage, m_pdfPage, m_mediaPage, m_infoPage})
        m_stack->addWidget(w);
    setPage(m_emptyPage);
}

PaneroQuickView::~PaneroQuickView()
{
    cancelWork();
    stopMedia();
}

// ---------------------------------------------------------------- pages

QWidget *PaneroQuickView::buildEmptyPage()
{
    auto *page = new QWidget(m_stack);
    auto *l = new QVBoxLayout(page);
    m_emptyLabel = wrapLabel(page);
    m_emptyLabel->setForegroundRole(QPalette::PlaceholderText);
    m_emptyLabel->setText(i18n("Quick View shows the file under the cursor of the other panel."));
    l->addWidget(m_emptyLabel);
    return page;
}

QWidget *PaneroQuickView::buildWaitingPage()
{
    auto *page = new QWidget(m_stack);
    auto *l = new QVBoxLayout(page);
    l->addStretch(2);
    auto *icon = new QLabel(page);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(Icon(QStringLiteral("cloud-download"), Icon(QStringLiteral("folder-cloud"))).pixmap(64, 64));
    l->addWidget(icon);
    m_waitingText = wrapLabel(page);
    l->addWidget(m_waitingText);
    m_loadButton = new QPushButton(Icon(QStringLiteral("download")), i18n("Load"), page);
    m_loadButton->setToolTip(i18n("Read this file now (Space)"));
    connect(m_loadButton, &QPushButton::clicked, this, &PaneroQuickView::loadNow);
    auto *bl = new QHBoxLayout;
    bl->addStretch();
    bl->addWidget(m_loadButton);
    bl->addStretch();
    l->addLayout(bl);
    l->addStretch(3);
    return page;
}

QWidget *PaneroQuickView::buildTextPage()
{
    auto *page = new QWidget(m_stack);
    auto *l = new QVBoxLayout(page);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    m_text = new QuickTextView(page);
    l->addWidget(m_text, 1);
    m_text->installEventFilter(this);

    m_findBar = new QWidget(page);
    auto *fl = new QHBoxLayout(m_findBar);
    fl->setContentsMargins(4, 2, 4, 2);
    auto *findLabel = new QLabel(i18n("Find:"), m_findBar);
    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setClearButtonEnabled(true);
    m_findEdit->installEventFilter(this);
    m_findStatus = new QLabel(m_findBar);
    auto *prev = new QToolButton(m_findBar);
    prev->setIcon(Icon(QStringLiteral("go-up")));
    prev->setToolTip(i18n("Previous (Shift+Enter)"));
    prev->setAutoRaise(true);
    auto *next = new QToolButton(m_findBar);
    next->setIcon(Icon(QStringLiteral("go-down")));
    next->setToolTip(i18n("Next (Enter)"));
    next->setAutoRaise(true);
    fl->addWidget(findLabel);
    fl->addWidget(m_findEdit, 1);
    fl->addWidget(prev);
    fl->addWidget(next);
    fl->addWidget(m_findStatus);
    connect(prev, &QToolButton::clicked, this, [this]() { findNext(true); });
    connect(next, &QToolButton::clicked, this, [this]() { findNext(false); });
    connect(m_findEdit, &QLineEdit::textEdited, this, [this]() {
        // search as you type, from the top of the current match
        QTextCursor c = m_text->textCursor();
        c.setPosition(c.selectionStart());
        m_text->setTextCursor(c);
        findNext(false);
    });
    m_findBar->hide();
    l->addWidget(m_findBar);

#ifdef PANERO_HAVE_SYNTAX
    m_highlighter = new KSyntaxHighlighting::SyntaxHighlighter(m_text->document());
#endif
    return page;
}

QWidget *PaneroQuickView::buildFolderPage()
{
    auto *page = new QWidget(m_stack);
    auto *l = new QVBoxLayout(page);
    l->setContentsMargins(12, 12, 12, 8);
    m_folderSummary = wrapLabel(page, Qt::AlignLeft | Qt::AlignTop);
    m_folderSummary->setTextFormat(Qt::RichText);
    l->addWidget(m_folderSummary);
    m_folderList = new QTreeWidget(page);
    m_folderList->setRootIsDecorated(false);
    m_folderList->setHeaderLabels({i18n("Largest items"), i18n("Size"), i18n("Share")});
    m_folderList->header()->setStretchLastSection(false);
    m_folderList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_folderList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_folderList->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_folderList->setSelectionMode(QAbstractItemView::NoSelection);
    m_folderList->setFrameShape(QFrame::NoFrame);
    l->addWidget(m_folderList, 1);
    m_folderStop = new QPushButton(page);
    connect(m_folderStop, &QPushButton::clicked, this, [this]() {
        if (m_folderRunning) {
            cancelWork();
            m_folderRunning = false;
            m_folderStop->setText(i18n("Calculate"));
            setDetails(i18n("stopped"));
        } else {
            startFolderStats();
        }
    });
    auto *bl = new QHBoxLayout;
    bl->addStretch();
    bl->addWidget(m_folderStop);
    l->addLayout(bl);
    return page;
}

QWidget *PaneroQuickView::buildArchivePage()
{
    auto *page = new QWidget(m_stack);
    auto *l = new QVBoxLayout(page);
    l->setContentsMargins(0, 6, 0, 0);
    m_archiveSummary = wrapLabel(page, Qt::AlignLeft | Qt::AlignTop);
    m_archiveSummary->setContentsMargins(8, 0, 8, 4);
    l->addWidget(m_archiveSummary);
    m_archiveList = new QTreeWidget(page);
    m_archiveList->setRootIsDecorated(false);
    m_archiveList->setUniformRowHeights(true);
    m_archiveList->setHeaderLabels({i18n("Name"), i18n("Size"), i18n("Modified")});
    m_archiveList->header()->setStretchLastSection(false);
    m_archiveList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_archiveList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_archiveList->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_archiveList->setFrameShape(QFrame::NoFrame);
    l->addWidget(m_archiveList, 1);
    return page;
}

QWidget *PaneroQuickView::buildPdfPage()
{
    auto *page = new QWidget(m_stack);
    auto *l = new QVBoxLayout(page);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    auto *bar = new QWidget(page);
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(4, 2, 4, 2);
    m_pdfPrev = new QToolButton(bar);
    m_pdfPrev->setIcon(Icon(QStringLiteral("go-previous")));
    m_pdfPrev->setToolTip(i18n("Previous page (Page Up at the top)"));
    m_pdfPrev->setAutoRaise(true);
    m_pdfNext = new QToolButton(bar);
    m_pdfNext->setIcon(Icon(QStringLiteral("go-next")));
    m_pdfNext->setToolTip(i18n("Next page (Page Down at the bottom)"));
    m_pdfNext->setAutoRaise(true);
    m_pdfLabel = new QLabel(bar);
    bl->addWidget(m_pdfPrev);
    bl->addWidget(m_pdfLabel);
    bl->addWidget(m_pdfNext);
    bl->addStretch();
    l->addWidget(bar);
    m_pdfScroll = new QScrollArea(page);
    m_pdfScroll->setFrameShape(QFrame::NoFrame);
    m_pdfScroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_pdfScroll->viewport()->setBackgroundRole(QPalette::Mid); // the page stands out
    m_pdfScroll->viewport()->setAutoFillBackground(true);
    m_pdfImage = new QLabel(m_pdfScroll);
    m_pdfImage->setAlignment(Qt::AlignCenter);
    m_pdfScroll->setWidget(m_pdfImage);
    m_pdfScroll->installEventFilter(this);
    l->addWidget(m_pdfScroll, 1);
    connect(m_pdfPrev, &QToolButton::clicked, this, [this]() { showPdfPage(m_pdfIndex - 1); });
    connect(m_pdfNext, &QToolButton::clicked, this, [this]() { showPdfPage(m_pdfIndex + 1); });
    return page;
}

QWidget *PaneroQuickView::buildMediaPage()
{
    auto *page = new QWidget(m_stack);
    auto *l = new QVBoxLayout(page);
    l->setContentsMargins(0, 0, 0, 4);
    m_mediaIcon = new QLabel(page);
    m_mediaIcon->setAlignment(Qt::AlignCenter);
#ifdef PANERO_HAVE_MULTIMEDIA
    m_video = new QVideoWidget(page);
    m_video->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    l->addWidget(m_video, 1);
    l->addWidget(m_mediaIcon, 1);
    auto *controls = new QHBoxLayout;
    controls->setContentsMargins(6, 0, 6, 0);
    m_mediaPlay = new QToolButton(page);
    m_mediaPlay->setAutoRaise(true);
    m_mediaPlay->setIcon(Icon(QStringLiteral("media-playback-start")));
    m_mediaPlay->setToolTip(i18n("Play / Pause (Space while Quick View has the focus)"));
    m_mediaSlider = new QSlider(Qt::Horizontal, page);
    m_mediaTime = new QLabel(page);
    controls->addWidget(m_mediaPlay);
    controls->addWidget(m_mediaSlider, 1);
    controls->addWidget(m_mediaTime);
    l->addLayout(controls);

    m_player = new QMediaPlayer(this);
    m_player->setVideoOutput(m_video);
    connect(m_mediaPlay, &QToolButton::clicked, this, [this]() {
        if (m_player->state() == QMediaPlayer::PlayingState)
            m_player->pause();
        else
            m_player->play();
    });
    connect(m_player, &QMediaPlayer::stateChanged, this, [this](QMediaPlayer::State s) {
        m_mediaPlay->setIcon(Icon(s == QMediaPlayer::PlayingState ? QStringLiteral("media-playback-pause")
                                                                   : QStringLiteral("media-playback-start")));
    });
    auto timeText = [](qint64 ms) {
        const qint64 s = ms / 1000;
        return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'))
                         : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
    };
    connect(m_player, &QMediaPlayer::durationChanged, this, [this, timeText](qint64 d) {
        m_mediaSlider->setRange(0, int(d / 100));
        m_mediaTime->setText(timeText(m_player->position()) + QStringLiteral(" / ") + timeText(d));
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this, timeText](qint64 p) {
        if (!m_mediaSlider->isSliderDown())
            m_mediaSlider->setValue(int(p / 100));
        m_mediaTime->setText(timeText(p) + QStringLiteral(" / ") + timeText(m_player->duration()));
    });
    connect(m_mediaSlider, &QSlider::sliderMoved, this, [this](int v) { m_player->setPosition(qint64(v) * 100); });
    connect(m_player, &QMediaPlayer::videoAvailableChanged, this, [this](bool video) {
        m_video->setVisible(video);
        m_mediaIcon->setVisible(!video);
    });
    connect(m_player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this, [this](QMediaPlayer::Error) {
        if (m_stack->currentWidget() == m_mediaPage && m_player->media().isNull() == false)
            showInfo(i18n("This file cannot be played here: %1", m_player->errorString()));
    });
#else
    l->addWidget(m_mediaIcon, 1);
#endif
    return page;
}

QWidget *PaneroQuickView::buildInfoPage()
{
    auto *scroll = new QScrollArea(m_stack);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *page = new QWidget(scroll);
    auto *l = new QVBoxLayout(page);
    l->setContentsMargins(12, 12, 12, 12);
    m_infoForm = new QFormLayout;
    m_infoForm->setLabelAlignment(Qt::AlignRight);
    m_infoForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    l->addLayout(m_infoForm);
    m_infoNote = wrapLabel(page, Qt::AlignLeft);
    m_infoNote->setForegroundRole(QPalette::PlaceholderText);
    l->addWidget(m_infoNote);
    auto *bl = new QHBoxLayout;
    m_openButton = new QPushButton(Icon(QStringLiteral("document-open")), i18n("Open"), page);
    m_openButton->setToolTip(i18n("Open with the default application"));
    connect(m_openButton, &QPushButton::clicked, this, [this]() { emit openRequested(m_item.url); });
    m_hexButton = new QPushButton(Icon(QStringLiteral("format-number-percent"), Icon(QStringLiteral("document-preview"))),
                                  i18n("Show as Hex"), page);
    m_hexButton->setToolTip(i18n("Show the first %1 as hexadecimal bytes", KFormat().formatByteSize(double(HexLimit))));
    connect(m_hexButton, &QPushButton::clicked, this, &PaneroQuickView::requestHex);
    bl->addWidget(m_openButton);
    bl->addWidget(m_hexButton);
    bl->addStretch();
    l->addLayout(bl);
    m_hex = new QPlainTextEdit(page);
    m_hex->setReadOnly(true);
    m_hex->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_hex->setFont(mono);
    m_hex->hide();
    l->addWidget(m_hex, 1);
    l->addStretch();
    scroll->setWidget(page);
    KAcceleratorManager::setNoAccel(page); // no automatic &-shortcuts in the field names
    return scroll;
}

// ---------------------------------------------------------------- state

QString PaneroQuickView::pageName() const
{
    QWidget *w = m_stack->currentWidget();
    if (w == m_emptyPage)
        return QStringLiteral("empty");
    if (w == m_waitingPage)
        return QStringLiteral("waiting");
    if (w == m_loadingPage)
        return QStringLiteral("loading");
    if (w == m_image)
        return QStringLiteral("image");
    if (w == m_textPage)
        return QStringLiteral("text");
    if (w == m_folderPage)
        return QStringLiteral("folder");
    if (w == m_archivePage)
        return QStringLiteral("archive");
    if (w == m_pdfPage)
        return QStringLiteral("pdf");
    if (w == m_mediaPage)
        return QStringLiteral("media");
    return QStringLiteral("info");
}

void PaneroQuickView::trace(const QString &line)
{
    static const bool enabled = qEnvironmentVariableIsSet("PANERO_QUICKVIEW_TRACE");
    if (enabled) {
        fprintf(stderr, "quickview: %s\n", line.toLocal8Bit().constData());
        fflush(stderr);
    }
}

void PaneroQuickView::setPage(QWidget *page)
{
    m_stack->setCurrentWidget(page);
    trace(QStringLiteral("page=%1 item=%2").arg(pageName(), m_item.name));
}

void PaneroQuickView::setDetails(const QString &text)
{
    m_details->setText(text);
    m_details->setToolTip(text);
}

QString PaneroQuickView::localPath() const
{
    return m_item.url.isLocalFile() ? m_item.url.toLocalFile() : QString();
}

void PaneroQuickView::cancelWork()
{
    ++m_generation;
    if (m_cancel)
        *m_cancel = true;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    m_folderRunning = false;
}

void PaneroQuickView::stopMedia()
{
#ifdef PANERO_HAVE_MULTIMEDIA
    if (m_player && !m_player->media().isNull()) {
        m_player->stop();
        m_player->setMedia(QMediaContent());
    }
#endif
}

template<typename Work, typename Done>
void PaneroQuickView::runAsync(Work work, Done done)
{
    QPointer<PaneroQuickView> self(this);
    const quint64 generation = m_generation;
    QtConcurrent::run(workerPool(), [self, generation, work, done]() mutable {
        auto result = work();
        // back in the GUI thread; qApp outlives every view
        QMetaObject::invokeMethod(
            qApp,
            [self, generation, result, done]() mutable {
                if (self && self->m_generation == generation)
                    done(result);
            },
            Qt::QueuedConnection);
    });
}

void PaneroQuickView::clear()
{
    cancelWork();
    stopMedia();
    m_item = Item();
    m_loaded.reset();
    m_waiting = false;
    m_icon->clear();
    m_name->clear();
    setDetails(QString());
    setPage(m_emptyPage);
}

void PaneroQuickView::showItem(const Item &item)
{
    if (item.url == m_item.url && item.url.isValid() && item.isParent == m_item.isParent && item.modified == m_item.modified
        && item.size == m_item.size)
        return;
    cancelWork();
    stopMedia();
    m_item = item;
    m_loaded.reset();
    m_waiting = false;
    m_forced = false;
    m_hex->clear();
    m_hex->hide();
    m_findBar->hide();

    const QString iconName = item.iconName.isEmpty() ? (item.isDir ? QStringLiteral("folder") : QStringLiteral("unknown")) : item.iconName;
    m_icon->setPixmap(Icon(iconName).pixmap(22, 22));
    m_name->setText(item.name);
    m_name->setToolTip(item.url.toDisplayString(QUrl::PreferLocalFile));
    setDetails(QString());

    if (item.isParent || !item.url.isValid()) {
        m_icon->setPixmap(Icon(QStringLiteral("go-up")).pixmap(22, 22));
        m_emptyLabel->setText(i18n("Move the cursor to a file or folder to see it here."));
        setPage(m_emptyPage);
        return;
    }
    const QString path = localPath();
    if (path.isEmpty()) {
        // remote or virtual (sftp, archives, trash): only what the listing knows
        showInfo(i18n("Preview is available for local files."));
        return;
    }
    // cloud and network mounts: nothing is read until asked (D-018, D-022)
    if (PaneroSlowPaths::isSlowPath(path)) {
        showWaiting();
        return;
    }
    startLoad(false);
}

void PaneroQuickView::showWaiting()
{
    m_waiting = true;
    const QString size = m_item.isDir ? QString() : shortSize(m_item.size);
    m_waitingText->setText(m_item.isDir ? i18n("This folder is on a cloud or network drive.\nPress Space to calculate its size.")
                                        : i18n("This file is on a cloud or network drive (%1).\nPress Space to load it.",
                                               size.isEmpty() ? i18n("size unknown") : size));
    m_loadButton->setText(m_item.isDir ? i18n("Calculate") : i18n("Load"));
    setDetails(m_item.isDir ? i18n("not calculated") : size);
    setPage(m_waitingPage);
}

void PaneroQuickView::loadNow()
{
    if (!m_waiting)
        return;
    m_waiting = false;
    m_forced = true;
    startLoad(true);
}

void PaneroQuickView::startLoad(bool force)
{
    const QString path = localPath();
    const bool isDir = m_item.isDir;
    const int pdfWidth = qMax(200, int((m_stack->width() - 24) * devicePixelRatioF()));
    trace(QStringLiteral("load item=%1 forced=%2").arg(m_item.name).arg(force));
    setPage(m_loadingPage);
    auto cancel = m_cancel;
    runAsync(
        [path, isDir, force, pdfWidth, cancel]() {
            auto r = std::make_shared<Loaded>();
#ifdef Q_OS_WIN
            // online-only placeholders and streaming drives (Google Drive G:): reading downloads
            if (!force && (isDir ? PaneroCloudFiles::isInSyncRoot(path) && PaneroCloudFiles::avoidReading(path)
                                 : PaneroCloudFiles::avoidReading(path))) {
                r->gated = true;
                return r;
            }
#else
            Q_UNUSED(force)
#endif
            const QFileInfo fi(path);
            if (!fi.exists() && !fi.isSymLink()) {
                r->exists = false;
                return r;
            }
            r->size = fi.isDir() ? -1 : fi.size();
            r->modified = fi.lastModified();
            r->created = fi.birthTime();
            r->permissions = permissionString(fi);
            r->owner = fi.owner();
            if (!r->owner.isEmpty() && !fi.group().isEmpty())
                r->owner += QLatin1Char(':') + fi.group();
            if (fi.isSymLink())
                r->linkTarget = fi.symLinkTarget();
            const QMimeDatabase db;
            if (!fi.isDir() && !fi.isFile()) {
                // pipes, sockets, devices: reading could wait forever
                r->mime = db.mimeTypeForFile(fi, QMimeDatabase::MatchExtension).name();
                r->mimeComment = db.mimeTypeForName(r->mime).comment();
                r->kind = Kind::Other;
                return r;
            }
            r->mime = isDir || fi.isDir() ? QStringLiteral("inode/directory") : mimeTypeOf(path);
            r->mimeComment = db.mimeTypeForName(r->mime).comment();
            if (*cancel)
                return r;
            const QByteArray head = fi.isDir() ? QByteArray() : readHead(path, 4096);
            r->kind = classify(r->mime, fi.isDir(), head);
            if (!fi.isDir() && !fi.isReadable()) {
                r->kind = Kind::Other;
                r->error = QStringLiteral("unreadable");
                return r;
            }
            switch (r->kind) {
            case Kind::Text:
                r->text = loadText(path);
                if (!r->text.error.isEmpty())
                    r->kind = Kind::Other;
                break;
            case Kind::Image:
                r->image = loadImage(path);
                if (r->image.image.isNull())
                    r->kind = Kind::Other;
                break;
            case Kind::Archive:
                r->archive = listArchive(path, r->mime);
                if (r->archive.entries.isEmpty() && !r->archive.error.isEmpty())
                    r->kind = Kind::Other;
                break;
            case Kind::Pdf:
                r->pdf = renderPdfPage(path, 0, pdfWidth);
                if (r->pdf.page.isNull())
                    r->kind = Kind::Other;
                break;
            default:
                break;
            }
            return r;
        },
        [this](const std::shared_ptr<Loaded> &r) { showLoaded(r); });
}

void PaneroQuickView::showLoaded(const std::shared_ptr<Loaded> &r)
{
    m_loaded = r;
    if (r->gated) {
        showWaiting();
        return;
    }
    if (!r->exists) {
        m_emptyLabel->setText(i18n("This item no longer exists."));
        setPage(m_emptyPage);
        return;
    }
    switch (r->kind) {
    case Kind::Folder:
        startFolderStats();
        return;
    case Kind::Image: {
        const ImageResult &img = r->image;
        if (!img.animationData.isEmpty())
            m_image->setAnimation(img.animationData, img.format, img.image);
        else
            m_image->setImage(img.image, img.originalSize);
        setPage(m_image);
        updateHeader();
        return;
    }
    case Kind::Text: {
        const TextResult &t = r->text;
#ifdef PANERO_HAVE_SYNTAX
        m_highlighter->setDefinition(KSyntaxHighlighting::Definition());
#endif
        m_text->setPlainText(t.text);
        bool code = false;
#ifdef PANERO_HAVE_SYNTAX
        const bool dark = palette().color(QPalette::Base).lightness() < 128;
        const KSyntaxHighlighting::Theme theme = syntaxRepository()->defaultTheme(dark ? KSyntaxHighlighting::Repository::DarkTheme
                                                                                        : KSyntaxHighlighting::Repository::LightTheme);
        m_highlighter->setTheme(theme);
        KSyntaxHighlighting::Definition def = syntaxRepository()->definitionForFileName(m_item.name);
        if (!def.isValid())
            def = syntaxRepository()->definitionForMimeType(r->mime);
        code = def.isValid() && def.name() != QLatin1String("None");
        if (code && t.text.size() <= HighlightLimit) {
            m_highlighter->setDefinition(def);
            m_text->setThemeColors(QColor::fromRgba(theme.editorColor(KSyntaxHighlighting::Theme::BackgroundColor)),
                                   QColor::fromRgba(theme.textColor(KSyntaxHighlighting::Theme::Normal)));
        } else {
            m_text->setThemeColors(QColor(), QColor());
        }
#endif
        const bool prose = !code && (r->mime == QLatin1String("text/plain") || r->mime == QLatin1String("text/markdown"));
        m_text->setLineWrapMode(prose ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        m_text->moveCursor(QTextCursor::Start);
        setPage(m_textPage);
        updateHeader();
        return;
    }
    case Kind::Archive: {
        const ArchiveResult &a = r->archive;
        m_archiveList->setUpdatesEnabled(false);
        m_archiveList->clear();
        QList<QTreeWidgetItem *> items;
        items.reserve(a.entries.size());
        const QIcon folderIcon = Icon(QStringLiteral("folder"));
        const QIcon fileIcon = Icon(QStringLiteral("text-x-generic"));
        for (const ArchiveEntry &e : a.entries) {
            auto *it = new QTreeWidgetItem;
            it->setText(0, e.path);
            it->setIcon(0, e.isDir ? folderIcon : fileIcon);
            if (!e.isDir) {
                it->setText(1, shortSize(e.size));
                it->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            }
            it->setText(2, formatDate(e.date));
            items.append(it);
        }
        m_archiveList->addTopLevelItems(items);
        m_archiveList->setUpdatesEnabled(true);
        QString summary = i18np("1 file", "%1 files", a.files) + QStringLiteral(", ") + i18np("1 folder", "%1 folders", a.folders);
        if (a.totalSize > 0)
            summary += QStringLiteral(", ") + i18n("%1 unpacked", shortSize(a.totalSize));
        if (a.truncated)
            summary += QStringLiteral(" - ") + i18n("only the first %1 entries are listed", a.entries.size());
        m_archiveSummary->setText(summary);
        setPage(m_archivePage);
        updateHeader();
        return;
    }
    case Kind::Pdf:
        m_pdfCount = r->pdf.pageCount;
        m_pdfIndex = 0;
        {
            QPixmap pm = QPixmap::fromImage(r->pdf.page);
            pm.setDevicePixelRatio(devicePixelRatioF());
            m_pdfImage->setPixmap(pm);
            m_pdfImage->resize(pm.size() / devicePixelRatioF());
        }
        setPage(m_pdfPage);
        showPdfPage(0); // updates the page label (image already there)
        updateHeader();
        return;
    case Kind::Media: {
#ifdef PANERO_HAVE_MULTIMEDIA
        const bool video = r->mime.startsWith(QLatin1String("video/"));
        m_mediaIcon->setPixmap(Icon(video ? QStringLiteral("video-x-generic") : QStringLiteral("audio-x-generic")).pixmap(96, 96));
        m_video->setVisible(video);
        m_mediaIcon->setVisible(!video);
        m_mediaSlider->setValue(0);
        m_mediaTime->clear();
        m_player->setMedia(QUrl::fromLocalFile(localPath()));
        if (video)
            m_player->pause(); // first frame
        setPage(m_mediaPage);
        updateHeader();
#else
        showInfo(i18n("This build of Panero cannot play audio or video. Use Open to play it in another application."));
#endif
        return;
    }
    case Kind::None:
    case Kind::Other:
        break;
    }
    QString note;
    if (r->error == QLatin1String("unreadable"))
        note = i18n("This file cannot be read (no permission).");
    else if (r->mime == QLatin1String("application/pdf") && !pdfSupported())
        note = i18n("PDF pages are not shown in this build of Panero. Use Open to read it.");
    else if (!r->archive.error.isEmpty())
        note = r->archive.error == QLatin1String("no-tool")
            ? i18n("This archive type needs 7-Zip or unrar to list its contents.")
            : i18n("The archive could not be read: %1", r->archive.error);
    else if (!r->image.error.isEmpty())
        note = i18n("The image could not be read: %1", r->image.error);
    else if (!r->pdf.error.isEmpty() && r->pdf.error != QLatin1String("unsupported"))
        note = r->pdf.error == QLatin1String("locked") ? i18n("This PDF is password protected.") : i18n("The PDF could not be read.");
    showInfo(note);
}

void PaneroQuickView::updateHeader()
{
    if (!m_loaded)
        return;
    const Loaded &r = *m_loaded;
    QStringList parts;
    switch (r.kind) {
    case Kind::Image: {
        const ImageResult &img = r.image;
        if (img.originalSize.isValid())
            parts << QStringLiteral("%1 × %2").arg(img.originalSize.width()).arg(img.originalSize.height());
        if (!img.format.isEmpty())
            parts << QString::fromLatin1(img.format).toUpper();
        if (img.frameCount > 1)
            parts << i18np("1 frame", "%1 frames", img.frameCount);
        parts << shortSize(r.size);
        if (!img.exifDate.isEmpty())
            parts << i18nc("photo taken at date", "taken %1", img.exifDate);
        parts << QStringLiteral("%1%").arg(qRound(m_image->effectiveZoom() * 100));
        break;
    }
    case Kind::Text:
        parts << QString::fromLatin1(r.text.encoding) << shortSize(r.size);
        if (r.text.truncated)
            parts << i18n("first %1 shown", KFormat().formatByteSize(double(TextLimit)));
        break;
    case Kind::Archive:
        parts << r.mimeComment << shortSize(r.size);
        break;
    case Kind::Pdf:
        parts << i18np("1 page", "%1 pages", r.pdf.pageCount) << shortSize(r.size);
        break;
    default:
        parts << r.mimeComment << shortSize(r.size);
        break;
    }
    parts.removeAll(QString());
    setDetails(parts.join(QStringLiteral(" · ")));
}

void PaneroQuickView::showInfo(const QString &note)
{
    while (m_infoForm->rowCount() > 0)
        m_infoForm->removeRow(0);
    auto addRow = [this](const QString &label, const QString &value) {
        if (value.isEmpty())
            return;
        auto *v = new QLabel(value);
        v->setTextFormat(Qt::PlainText);
        v->setWordWrap(true);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_infoForm->addRow(label, v);
    };
    const Loaded *r = m_loaded.get();
    addRow(i18n("Name:"), m_item.name);
    QString type;
    if (r && !r->mimeComment.isEmpty()) {
        type = r->mimeComment;
    } else {
        // from the name only: nothing is read here
        const QList<QMimeType> byName = QMimeDatabase().mimeTypesForFileName(m_item.name);
        type = m_item.isDir ? i18n("Folder") : !byName.isEmpty() ? byName.first().comment() : QString();
    }
    addRow(i18n("Type:"), type);
    addRow(i18n("Size:"), formatSize(r && r->size >= 0 ? r->size : m_item.size));
    addRow(i18n("Modified:"), formatDate(r && r->modified.isValid() ? r->modified : m_item.modified));
    if (r) {
        addRow(i18n("Created:"), formatDate(r->created));
        addRow(i18n("Permissions:"), r->permissions);
        addRow(i18n("Owner:"), r->owner);
        addRow(i18n("Link to:"), r->linkTarget);
    }
    addRow(i18n("Location:"), m_item.url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).toDisplayString(QUrl::PreferLocalFile));
    m_infoNote->setText(note);
    m_infoNote->setVisible(!note.isEmpty());
    m_hexButton->setVisible(!m_item.isDir && !localPath().isEmpty() && r && !r->mime.startsWith(QLatin1String("inode/")));
    m_openButton->setVisible(!m_item.isDir);
    setPage(m_infoPage);
    if (r) {
        QStringList parts{r->mimeComment, shortSize(r->size)};
        parts.removeAll(QString());
        setDetails(parts.join(QStringLiteral(" · ")));
    } else {
        setDetails(shortSize(m_item.size));
    }
}

void PaneroQuickView::requestHex()
{
    const QString path = localPath();
    if (path.isEmpty())
        return;
    m_hex->setPlainText(i18n("Loading..."));
    m_hex->show();
    runAsync(
        [path]() {
            QString error;
            const QByteArray data = readHead(path, HexLimit, &error);
            return error.isEmpty() ? hexDump(data) : error;
        },
        [this](const QString &text) {
            m_hex->setPlainText(text);
            m_hex->setFocus();
        });
}

// ---------------------------------------------------------------- folders

void PaneroQuickView::startFolderStats()
{
    const QString path = localPath();
    if (path.isEmpty())
        return;
    cancelWork();
    m_folderRunning = true;
    m_folderStop->setText(i18n("Stop"));
    m_folderStop->show();
    m_folderList->clear();
    m_folderSummary->setText(i18n("Calculating..."));
    setDetails(i18n("calculating..."));
    setPage(m_folderPage);
    auto cancel = m_cancel;
    QPointer<PaneroQuickView> self(this);
    const quint64 generation = m_generation;
    runAsync(
        [path, cancel, self, generation]() {
            return folderStats(path, *cancel, [self, generation](const FolderStats &partial) {
                QMetaObject::invokeMethod(
                    qApp,
                    [self, generation, partial]() {
                        if (self && self->m_generation == generation)
                            self->showFolderStats(partial);
                    },
                    Qt::QueuedConnection);
            });
        },
        [this](const FolderStats &stats) {
            m_folderRunning = false;
            showFolderStats(stats);
        });
}

void PaneroQuickView::showFolderStats(const FolderStats &s)
{
    QString text = QStringLiteral("<p style='font-size:large'><b>%1</b></p>").arg(formatSize(s.totalSize).toHtmlEscaped());
    text += QStringLiteral("<p>%1<br>%2</p>")
                .arg(i18np("1 file", "%1 files", s.files).toHtmlEscaped(), i18np("1 folder", "%1 folders", s.folders).toHtmlEscaped());
    if (s.errors > 0)
        text += QStringLiteral("<p>%1</p>").arg(i18np("1 folder could not be read", "%1 folders could not be read", s.errors).toHtmlEscaped());
    if (!s.finished)
        text += QStringLiteral("<p><i>%1</i></p>").arg(i18n("Calculating...").toHtmlEscaped());
    m_folderSummary->setText(text);
    m_folderList->clear();
    for (const FolderEntry &e : s.largest) {
        auto *it = new QTreeWidgetItem(m_folderList);
        it->setText(0, e.name);
        it->setIcon(0, Icon(e.isDir ? QStringLiteral("folder") : QStringLiteral("text-x-generic")));
        it->setText(1, shortSize(e.size));
        it->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        if (s.totalSize > 0) {
            it->setText(2, QStringLiteral("%1%").arg(double(e.size) * 100.0 / double(s.totalSize), 0, 'f', 1));
            it->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        }
    }
    if (s.finished) {
        trace(QStringLiteral("folder done item=%1 files=%2 folders=%3 size=%4").arg(m_item.name).arg(s.files).arg(s.folders).arg(s.totalSize));
        m_folderStop->setText(i18n("Calculate Again"));
        setDetails(shortSize(s.totalSize));
    } else {
        setDetails(i18n("%1 so far", shortSize(s.totalSize)));
    }
}

// ---------------------------------------------------------------- PDF

void PaneroQuickView::showPdfPage(int index)
{
    if (m_pdfCount <= 0)
        return;
    index = qBound(0, index, m_pdfCount - 1);
    m_pdfPrev->setEnabled(index > 0);
    m_pdfNext->setEnabled(index < m_pdfCount - 1);
    m_pdfLabel->setText(i18n("Page %1 of %2", index + 1, m_pdfCount));
    if (index == m_pdfIndex && !m_pdfImage->pixmap(Qt::ReturnByValue).isNull())
        return;
    const bool forward = index > m_pdfIndex;
    m_pdfIndex = index;
    const QString path = localPath();
    const int width = qMax(200, int((m_pdfScroll->viewport()->width() - 16) * devicePixelRatioF()));
    const qreal dpr = devicePixelRatioF();
    runAsync([path, index, width]() { return renderPdfPage(path, index, width); },
             [this, forward, dpr](const PdfResult &page) {
                 QPixmap pm = QPixmap::fromImage(page.page);
                 pm.setDevicePixelRatio(dpr);
                 m_pdfImage->setPixmap(pm);
                 m_pdfImage->resize(pm.size() / dpr);
                 m_pdfScroll->verticalScrollBar()->setValue(forward ? 0 : m_pdfScroll->verticalScrollBar()->maximum());
             });
}

// ---------------------------------------------------------------- text search

void PaneroQuickView::openFind()
{
    m_findBar->show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void PaneroQuickView::findNext(bool backwards)
{
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) {
        m_findStatus->clear();
        return;
    }
    QTextDocument::FindFlags flags;
    if (backwards)
        flags |= QTextDocument::FindBackward;
    bool found = m_text->find(needle, flags);
    if (!found) {
        // wrap around
        QTextCursor c = m_text->textCursor();
        c.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
        m_text->setTextCursor(c);
        found = m_text->find(needle, flags);
    }
    m_findStatus->setText(found ? QString() : i18n("Not found"));
}

// ---------------------------------------------------------------- keyboard / focus

void PaneroQuickView::focusContent()
{
    QWidget *page = m_stack->currentWidget();
    QWidget *target = page;
    if (page == m_textPage)
        target = m_text;
    else if (page == m_pdfPage)
        target = m_pdfScroll;
    else if (page == m_archivePage)
        target = m_archiveList;
    else if (page == m_folderPage)
        target = m_folderList;
    else if (page == m_waitingPage)
        target = m_loadButton;
    else if (page == m_mediaPage && m_mediaPlay)
        target = m_mediaPlay;
    else if (page == m_infoPage)
        target = m_hex->isVisible() ? static_cast<QWidget *>(m_hex) : m_openButton;
    trace(QStringLiteral("focus=view"));
    if (target) {
        if (target->focusPolicy() == Qt::NoFocus)
            target->setFocusPolicy(Qt::StrongFocus);
        target->setFocus(Qt::TabFocusReason);
    }
}

bool PaneroQuickView::wantsEscape() const
{
    return m_findBar->isVisible() && m_findEdit->hasFocus();
}

bool PaneroQuickView::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (watched == m_text) {
            if ((ke->key() == Qt::Key_F && ke->modifiers() == Qt::ControlModifier) || ke->key() == Qt::Key_F7
                || (ke->key() == Qt::Key_Slash && ke->modifiers() == Qt::NoModifier)) {
                openFind();
                return true;
            }
            if (ke->key() == Qt::Key_F3) {
                if (m_findEdit->text().isEmpty())
                    openFind();
                else
                    findNext(ke->modifiers() & Qt::ShiftModifier);
                return true;
            }
        } else if (watched == m_findEdit) {
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                findNext(ke->modifiers() & Qt::ShiftModifier);
                return true;
            }
            if (ke->key() == Qt::Key_Escape) {
                m_findBar->hide();
                m_text->setFocus();
                return true;
            }
        } else if (watched == m_pdfScroll) {
            QScrollBar *sb = m_pdfScroll->verticalScrollBar();
            if ((ke->key() == Qt::Key_PageDown || ke->key() == Qt::Key_Space) && sb->value() >= sb->maximum() && m_pdfIndex < m_pdfCount - 1) {
                showPdfPage(m_pdfIndex + 1);
                return true;
            }
            if (ke->key() == Qt::Key_PageUp && sb->value() <= sb->minimum() && m_pdfIndex > 0) {
                showPdfPage(m_pdfIndex - 1);
                return true;
            }
            if (ke->key() == Qt::Key_Home && (ke->modifiers() & Qt::ControlModifier)) {
                showPdfPage(0);
                return true;
            }
            if (ke->key() == Qt::Key_End && (ke->modifiers() & Qt::ControlModifier)) {
                showPdfPage(m_pdfCount - 1);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PaneroQuickView::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange && m_loaded && m_loaded->kind == Kind::Text) {
        // dark/light theme switch: highlight with the matching colours
        auto loaded = m_loaded;
        showLoaded(loaded);
    }
    QWidget::changeEvent(event);
}
