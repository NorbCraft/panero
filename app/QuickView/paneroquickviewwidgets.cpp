/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroquickviewwidgets.h"

#include <QBuffer>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMovie>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QWheelEvent>

#include <cmath>

// ---------------------------------------------------------------- images

QuickImageView::QuickImageView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setBackgroundRole(QPalette::Base);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

QuickImageView::~QuickImageView()
{
    stopMovie();
}

void QuickImageView::stopMovie()
{
    if (m_movie) {
        m_movie->stop();
        delete m_movie;
        m_movie = nullptr;
    }
    delete m_buffer;
    m_buffer = nullptr;
}

void QuickImageView::clear()
{
    stopMovie();
    m_image = QImage();
    m_originalSize = QSize();
    viewport()->update();
}

void QuickImageView::setImage(const QImage &image, const QSize &originalSize)
{
    stopMovie();
    m_image = image;
    m_originalSize = originalSize.isValid() ? originalSize : image.size();
    m_fit = true;
    m_zoom = 1.0;
    updateScrollBars();
    viewport()->update();
    emit zoomChanged();
}

void QuickImageView::setAnimation(const QByteArray &data, const QByteArray &format, const QImage &firstFrame)
{
    setImage(firstFrame, firstFrame.size());
    m_buffer = new QBuffer;
    m_buffer->setData(data);
    m_buffer->open(QIODevice::ReadOnly);
    m_movie = new QMovie(m_buffer, format);
    m_movie->setCacheMode(QMovie::CacheNone);
    connect(m_movie, &QMovie::frameChanged, this, [this]() {
        m_image = m_movie->currentImage();
        viewport()->update();
    });
    m_movie->start();
}

double QuickImageView::displayScale() const
{
    if (m_image.isNull())
        return 1.0;
    // the loaded frame may be smaller than the original (big photos are scaled down)
    const double frameToOriginal = m_originalSize.width() > 0 ? double(m_originalSize.width()) / m_image.width() : 1.0;
    if (m_fit) {
        const QSize area = viewport()->size();
        const double fit = qMin(double(area.width()) / m_originalSize.width(), double(area.height()) / m_originalSize.height());
        return qMin(1.0, fit) * frameToOriginal;
    }
    return m_zoom * frameToOriginal;
}

double QuickImageView::effectiveZoom() const
{
    if (m_image.isNull() || m_originalSize.width() <= 0)
        return 1.0;
    return displayScale() * m_image.width() / m_originalSize.width();
}

void QuickImageView::zoomBy(double factor)
{
    if (m_image.isNull())
        return;
    const double current = effectiveZoom();
    m_fit = false;
    m_zoom = qBound(0.02, current * factor, 32.0);
    updateScrollBars();
    viewport()->update();
    emit zoomChanged();
}

void QuickImageView::setFit()
{
    m_fit = true;
    updateScrollBars();
    viewport()->update();
    emit zoomChanged();
}

void QuickImageView::setActualSize()
{
    m_fit = false;
    m_zoom = 1.0;
    updateScrollBars();
    viewport()->update();
    emit zoomChanged();
}

void QuickImageView::updateScrollBars()
{
    const double s = displayScale();
    const QSize shown(qRound(m_image.width() * s), qRound(m_image.height() * s));
    const QSize area = viewport()->size();
    horizontalScrollBar()->setRange(0, qMax(0, shown.width() - area.width()));
    verticalScrollBar()->setRange(0, qMax(0, shown.height() - area.height()));
    horizontalScrollBar()->setPageStep(area.width());
    verticalScrollBar()->setPageStep(area.height());
    horizontalScrollBar()->setSingleStep(24);
    verticalScrollBar()->setSingleStep(24);
}

void QuickImageView::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    if (m_image.isNull())
        return;
    const double s = displayScale();
    const QSizeF shown(m_image.width() * s, m_image.height() * s);
    const QSize area = viewport()->size();
    const double x = shown.width() < area.width() ? (area.width() - shown.width()) / 2 : -horizontalScrollBar()->value();
    const double y = shown.height() < area.height() ? (area.height() - shown.height()) / 2 : -verticalScrollBar()->value();
    const QRectF target(x, y, shown.width(), shown.height());
    if (m_image.hasAlphaChannel()) {
        // checkerboard behind transparent images
        const int cell = 8;
        const QColor a = palette().color(QPalette::Base);
        const QColor b = a.lightness() < 128 ? a.lighter(130) : a.darker(108);
        p.save();
        p.setClipRect(target);
        for (int cy = int(target.top()) / cell * cell; cy < target.bottom(); cy += cell)
            for (int cx = int(target.left()) / cell * cell; cx < target.right(); cx += cell)
                p.fillRect(cx, cy, cell, cell, ((cx / cell + cy / cell) & 1) ? a : b);
        p.restore();
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, s < 2.0);
    p.drawImage(target, m_image);
}

void QuickImageView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
    if (m_fit)
        emit zoomChanged();
}

void QuickImageView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int delta = event->angleDelta().y();
        if (delta != 0)
            zoomBy(delta > 0 ? 1.25 : 0.8);
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

void QuickImageView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomBy(1.25);
        return;
    case Qt::Key_Minus:
        zoomBy(0.8);
        return;
    case Qt::Key_0:
    case Qt::Key_F:
        setFit();
        return;
    case Qt::Key_1:
        setActualSize();
        return;
    default:
        break;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void QuickImageView::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragStart = event->pos();
        m_dragScroll = QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
        viewport()->setCursor(Qt::ClosedHandCursor);
    }
}

void QuickImageView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging)
        return;
    const QPoint d = event->pos() - m_dragStart;
    horizontalScrollBar()->setValue(m_dragScroll.x() - d.x());
    verticalScrollBar()->setValue(m_dragScroll.y() - d.y());
    viewport()->update();
}

void QuickImageView::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
    viewport()->unsetCursor();
}

void QuickImageView::mouseDoubleClickEvent(QMouseEvent *)
{
    if (m_fit)
        setActualSize();
    else
        setFit();
}

// ---------------------------------------------------------------- text

namespace
{
class LineNumbers : public QWidget
{
public:
    explicit LineNumbers(QuickTextView *view)
        : QWidget(view)
        , m_view(view)
    {
    }
    QSize sizeHint() const override
    {
        return QSize(m_view->lineNumberWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        m_view->paintLineNumbers(event);
    }

private:
    QuickTextView *m_view;
};
} // namespace

QuickTextView::QuickTextView(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setFrameShape(QFrame::NoFrame);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    m_numbers = new LineNumbers(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, &QuickTextView::updateMargins);
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        if (dy)
            m_numbers->scroll(0, dy);
        else
            m_numbers->update(0, rect.y(), m_numbers->width(), rect.height());
    });
    updateMargins();
}

int QuickTextView::lineNumberWidth() const
{
    int digits = 1;
    for (int max = qMax(1, blockCount()); max >= 10; max /= 10)
        ++digits;
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits);
}

void QuickTextView::updateMargins()
{
    setViewportMargins(lineNumberWidth(), 0, 0, 0);
}

void QuickTextView::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_numbers->setGeometry(QRect(cr.left(), cr.top(), lineNumberWidth(), cr.height()));
}

void QuickTextView::setThemeColors(const QColor &background, const QColor &text)
{
    QPalette pal = parentWidget() ? parentWidget()->palette() : palette();
    if (background.isValid() && background.alpha() > 0)
        pal.setColor(QPalette::Base, background);
    if (text.isValid() && text.alpha() > 0)
        pal.setColor(QPalette::Text, text);
    setPalette(pal);
}

void QuickTextView::paintLineNumbers(QPaintEvent *event)
{
    QPainter p(m_numbers);
    const QColor base = palette().color(QPalette::Base);
    p.fillRect(event->rect(), base.lightness() < 128 ? base.lighter(115) : base.darker(104));
    QColor numberColor = palette().color(QPalette::Text);
    numberColor.setAlphaF(0.45);
    p.setPen(numberColor);
    p.setFont(font());
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int width = m_numbers->width() - 6;
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top())
            p.drawText(0, top, width, fontMetrics().height(), Qt::AlignRight, QString::number(number + 1));
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}
