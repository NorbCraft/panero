/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROQUICKVIEWWIDGETS_H
#define PANEROQUICKVIEWWIDGETS_H

#include <QAbstractScrollArea>
#include <QImage>
#include <QPlainTextEdit>
#include <QPoint>

class QBuffer;
class QMovie;

/// Quick View image page: fits big images into the panel, zoom with Ctrl+wheel,
/// + / - / 0 (fit) / 1 (100 %), drag to pan. Animated GIF/WebP play.
class QuickImageView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit QuickImageView(QWidget *parent = nullptr);
    ~QuickImageView() override;
    void setImage(const QImage &image, const QSize &originalSize);
    /// plays @p data with QMovie (from memory: the file is not read again)
    void setAnimation(const QByteArray &data, const QByteArray &format, const QImage &firstFrame);
    void clear();
    /// on-screen size / original size
    double effectiveZoom() const;
    void zoomBy(double factor);
    void setFit();
    void setActualSize();

signals:
    void zoomChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    double displayScale() const; ///< frame pixels -> screen pixels
    void updateScrollBars();
    void stopMovie();

    QImage m_image;
    QSize m_originalSize;
    QBuffer *m_buffer = nullptr;
    QMovie *m_movie = nullptr;
    bool m_fit = true;
    double m_zoom = 1.0; ///< relative to the original size, when not fitting
    QPoint m_dragStart;
    QPoint m_dragScroll;
    bool m_dragging = false;
};

/// Quick View text page: read-only, line numbers, optional theme colours.
class QuickTextView : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit QuickTextView(QWidget *parent = nullptr);
    int lineNumberWidth() const;
    void paintLineNumbers(QPaintEvent *event);
    /// colours of the syntax theme; invalid colours = the normal palette
    void setThemeColors(const QColor &background, const QColor &text);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateMargins();
    QWidget *m_numbers = nullptr;
};

#endif // PANEROQUICKVIEWWIDGETS_H
