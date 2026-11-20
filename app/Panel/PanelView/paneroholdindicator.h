/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROHOLDINDICATOR_H
#define PANEROHOLDINDICATOR_H

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

/**
 * Feedback while the right mouse button is held: fills up until the context menu
 * opens (Panero Commander mode). Painted over the file list next to the pointer.
 * Style (Look&Feel / Hold Indicator): pie | ring | bar | none.
 */
class PaneroHoldIndicator : public QWidget
{
    Q_OBJECT
public:
    explicit PaneroHoldIndicator(QWidget *viewport);

    /// pointer position in viewport coordinates, total hold time in ms
    void start(const QPoint &pos, int holdMs);
    void stop();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void tick();
    enum class Style { None, Pie, Ring, Bar };
    static Style configuredStyle();
    Style m_style = Style::Pie;
    QPoint m_pos;
    int m_holdMs = 600;
    qreal m_progress = 0.0;
    QElapsedTimer m_clock;
    QTimer m_frame;
    QTimer m_showDelay;
};

#endif // PANEROHOLDINDICATOR_H
