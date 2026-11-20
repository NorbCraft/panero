/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroholdindicator.h"

#include "../../krglobal.h"

#include <QPainter>
#include <QPainterPath>

#include <KConfigCore/KConfig>
#include <KConfigCore/KConfigGroup>

namespace
{
const int ShowDelayMs = 120;  // quick clicks never show the indicator
const int PieSize = 20;       // diameter of the pie
const int RingSize = 30;      // diameter of the ring around the pointer
const int BarWidth = 40, BarHeight = 5;
}

PaneroHoldIndicator::PaneroHoldIndicator(QWidget *viewport)
    : QWidget(viewport)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
    m_frame.setInterval(16);
    connect(&m_frame, &QTimer::timeout, this, &PaneroHoldIndicator::tick);
    m_showDelay.setSingleShot(true);
    connect(&m_showDelay, &QTimer::timeout, this, [this]() {
        raise();
        show();
        m_frame.start();
        tick();
    });
}

PaneroHoldIndicator::Style PaneroHoldIndicator::configuredStyle()
{
    const QString style = KConfigGroup(krConfig, "Look&Feel").readEntry("Hold Indicator", QStringLiteral("pie"));
    if (style == QLatin1String("none"))
        return Style::None;
    if (style == QLatin1String("ring"))
        return Style::Ring;
    if (style == QLatin1String("bar"))
        return Style::Bar;
    return Style::Pie;
}

void PaneroHoldIndicator::start(const QPoint &pos, int holdMs)
{
    m_style = configuredStyle();
    if (m_style == Style::None || holdMs <= ShowDelayMs)
        return;
    m_pos = pos;
    m_holdMs = holdMs;
    m_progress = 0.0;
    m_clock.start();
    // geometry: pie down-right of the pointer (where the menu opens), ring centred on
    // the pointer, bar centred below it
    QRect r;
    switch (m_style) {
    case Style::Ring:
        r = QRect(0, 0, RingSize + 4, RingSize + 4);
        r.moveCenter(pos);
        break;
    case Style::Bar:
        r = QRect(0, 0, BarWidth + 4, BarHeight + 4);
        r.moveCenter(pos + QPoint(0, 26));
        break;
    default:
        r = QRect(pos + QPoint(14, 16), QSize(PieSize + 4, PieSize + 4));
        break;
    }
    setGeometry(r);
    m_showDelay.start(ShowDelayMs);
}

void PaneroHoldIndicator::stop()
{
    m_showDelay.stop();
    m_frame.stop();
    hide();
}

void PaneroHoldIndicator::tick()
{
    const QByteArray freeze = qgetenv("PANERO_DEV_HOLD_FREEZE"); // screenshots only
    m_progress = !freeze.isEmpty() ? freeze.toDouble()
                                   : qBound<qreal>(0.0, qreal(m_clock.elapsed() - ShowDelayMs) / qreal(m_holdMs - ShowDelayMs), 1.0);
    if (!freeze.isEmpty())
        m_frame.stop();
    update();
}

void PaneroHoldIndicator::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor accent = palette().color(QPalette::Highlight);
    QColor track = palette().color(QPalette::Text);
    track.setAlphaF(0.18);
    QColor halo = palette().color(QPalette::Base);
    halo.setAlphaF(0.85);
    const QRectF area = QRectF(rect()).adjusted(2, 2, -2, -2);

    switch (m_style) {
    case Style::Bar: {
        const qreal radius = area.height() / 2;
        p.setPen(QPen(halo, 2));
        p.setBrush(track);
        p.drawRoundedRect(area, radius, radius);
        QRectF fill = area;
        fill.setWidth(qMax(area.height(), area.width() * m_progress));
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawRoundedRect(fill, radius, radius);
        break;
    }
    case Style::Ring: {
        const qreal stroke = 3.0;
        const QRectF ring = area.adjusted(stroke / 2, stroke / 2, -stroke / 2, -stroke / 2);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(halo, stroke + 2));
        p.drawEllipse(ring);
        p.setPen(QPen(track, stroke));
        p.drawEllipse(ring);
        p.setPen(QPen(accent, stroke, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(ring, 90 * 16, -int(360 * 16 * m_progress));
        break;
    }
    default: { // pie
        p.setPen(QPen(halo, 2));
        p.setBrush(track);
        p.drawEllipse(area);
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawPie(area.adjusted(1.5, 1.5, -1.5, -1.5), 90 * 16, -int(360 * 16 * m_progress));
        p.setPen(QPen(accent, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(area.adjusted(0.5, 0.5, -0.5, -0.5));
        break;
    }
    }
}
