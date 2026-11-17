/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerotheme.h"

#include "../krglobal.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QRect>

#include <KConfigCore/KConfigGroup>
#include <KConfigCore/KSharedConfig>

namespace PaneroTheme
{
const char *const GROUP = "Panero Appearance";

namespace
{
struct Cache {
    bool valid = false;
    bool dark = false;
    Scheme scheme;
};
Cache &cache()
{
    static Cache c;
    return c;
}

KConfigGroup group()
{
    if (krConfig)
        return KConfigGroup(krConfig, GROUP);
    return KConfigGroup(KSharedConfig::openConfig(), GROUP);
}
} // namespace

const QVector<ColorKey> &colorKeys()
{
    static const QVector<ColorKey> keys = {
        {"Background", &Scheme::background},
        {"Alternate Background", &Scheme::alternateBackground},
        {"Text", &Scheme::text},
        {"Mark Foreground", &Scheme::markForeground},
        {"Mark Background", &Scheme::markBackground},
        {"Current Background", &Scheme::currentBackground},
        {"Current Frame", &Scheme::currentFrame},
        {"Inactive Current Frame", &Scheme::inactiveCurrentFrame},
    };
    return keys;
}

bool usePaneroColors()
{
    return group().readEntry("Use Panero Colors", true);
}

Mode mode()
{
    const int m = group().readEntry("Theme", 0);
    return (m == 1) ? Mode::Light : (m == 2) ? Mode::Dark : Mode::FollowSystem;
}

bool isDark()
{
    switch (mode()) {
    case Mode::Light:
        return false;
    case Mode::Dark:
        return true;
    case Mode::FollowSystem:
    default:
        break;
    }
    const QPalette p = QGuiApplication::palette();
    return p.color(QPalette::Window).lightness() < 128;
}

Scheme defaultScheme(bool dark)
{
    Scheme s;
    if (!dark) {
        s.background = QColor(0xff, 0xff, 0xff);
        s.alternateBackground = QColor(0xf3, 0xf5, 0xf8);
        s.text = QColor(0x1f, 0x23, 0x28);
        s.markForeground = QColor(0xc6, 0x28, 0x28);
        s.markBackground = QColor(0xfd, 0xe8, 0xe8);
        s.currentBackground = QColor(0xdd, 0xe9, 0xfc);
        s.currentFrame = QColor(0x25, 0x63, 0xeb);
        s.inactiveCurrentFrame = QColor(0x9c, 0xa3, 0xaf);
    } else {
        s.background = QColor(0x1d, 0x20, 0x25);
        s.alternateBackground = QColor(0x24, 0x28, 0x2e);
        s.text = QColor(0xe4, 0xe7, 0xeb);
        s.markForeground = QColor(0xff, 0x6b, 0x6b);
        s.markBackground = QColor(0x41, 0x22, 0x26);
        s.currentBackground = QColor(0x1e, 0x36, 0x5c);
        s.currentFrame = QColor(0x5b, 0x9c, 0xff);
        s.inactiveCurrentFrame = QColor(0x6b, 0x72, 0x80);
    }
    return s;
}

QColor readColor(const QString &value)
{
    const QString v = value.trimmed();
    if (v.isEmpty())
        return QColor();
    if (v.contains(QLatin1Char(','))) {
        const QStringList parts = v.split(QLatin1Char(','));
        if (parts.size() >= 3) {
            bool okR, okG, okB;
            const int r = parts[0].trimmed().toInt(&okR), g = parts[1].trimmed().toInt(&okG), b = parts[2].trimmed().toInt(&okB);
            if (okR && okG && okB)
                return QColor(r, g, b);
        }
        return QColor();
    }
    return QColor(v);
}

QString keyPrefix(bool dark)
{
    return dark ? QStringLiteral("Dark ") : QStringLiteral("Light ");
}

const Scheme &scheme()
{
    Cache &c = cache();
    const bool dark = isDark();
    if (c.valid && c.dark == dark)
        return c.scheme;
    Scheme s = defaultScheme(dark);
    const KConfigGroup g = group();
    const QString prefix = keyPrefix(dark);
    for (const ColorKey &k : colorKeys()) {
        const QColor configured = readColor(g.readEntry(prefix + QLatin1String(k.key), QString()));
        if (configured.isValid())
            s.*(k.member) = configured;
    }
    c.scheme = s;
    c.dark = dark;
    c.valid = true;
    return c.scheme;
}

bool alternatingRows()
{
    return group().readEntry("Alternating Rows", true);
}

bool markIndicator()
{
    return group().readEntry("Mark Indicator", true);
}

int configuredRowHeight()
{
    const int h = group().readEntry("Row Height", 0);
    return h <= 0 ? 0 : qBound(MinRowHeight, h, MaxRowHeight);
}

int effectiveRowHeight(const QFontMetrics &fm, int iconSize, int configured)
{
    // text needs its line height plus a little air; icons need their size plus 2 px
    const int needed = qMax(fm.height() + 2, iconSize > 0 ? iconSize + 2 : 0);
    if (configured <= 0)
        return qMax(needed, int(ComfortableRowHeight)); // automatic: comfortable
    return qBound(needed, configured, qMax(needed, int(MaxRowHeight)));
}

void invalidate()
{
    cache().valid = false;
}

void paintCurrentFrame(QPainter *painter, const QRect &rect, bool activePanel)
{
    if (!rect.isValid())
        return;
    const Scheme &s = scheme();
    const QColor color = activePanel ? s.currentFrame : s.inactiveCurrentFrame;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
    painter->setPen(QPen(color, activePanel ? 1.5 : 1.0));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    if (activePanel) {
        // leading accent bar
        const qreal barWidth = 3.0;
        const QRectF bar(r.left() + 1.5, r.top() + 2.5, barWidth, qMax<qreal>(2.0, r.height() - 5.0));
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawRoundedRect(bar, 1.5, 1.5);
    }
    painter->restore();
}

void paintMarkIndicator(QPainter *painter, const QRectF &area)
{
    const Scheme &s = scheme();
    const qreal d = qBound<qreal>(7.0, qMin(area.width(), area.height()) * 0.6, 14.0);
    const QRectF badge(area.right() - d + 1, area.bottom() - d + 1, d, d);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(s.background, 1.0));
    painter->setBrush(s.markForeground);
    painter->drawEllipse(badge);
    QPainterPath check;
    check.moveTo(badge.left() + d * 0.27, badge.top() + d * 0.52);
    check.lineTo(badge.left() + d * 0.44, badge.top() + d * 0.69);
    check.lineTo(badge.left() + d * 0.74, badge.top() + d * 0.33);
    painter->setPen(QPen(Qt::white, qMax<qreal>(1.2, d / 7.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(check);
    painter->restore();
}

} // namespace PaneroTheme
