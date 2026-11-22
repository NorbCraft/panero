/*
    SPDX-FileCopyrightText: 2008 Csaba Karai <krusader@users.sourceforge.net>
    SPDX-FileCopyrightText: 2008-2022 Krusader Krew <https://krusader.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "krstyleproxy.h"
#include "../Panel/panerotheme.h"

#include <QAbstractItemView>

// QtGui
#include <QPen>
#include <QPainter>
// QtWidgets
#include <QStyleOption>
#include <QStyleOptionViewItem>

#include <KConfigCore/KSharedConfig>

#include "../krglobal.h"

void KrStyleProxy::drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                                 QPainter *painter, const QWidget *widget) const
{
    if (element == QStyle::PE_IndicatorHeaderArrow) {
        // Panero: the same sort arrow on every platform: ^ ascending, v descending.
        // QHeaderView passes SortDown for ascending order and each style draws
        // that its own way (Fusion/Breeze: v, Windows: ^).
        if (const auto *header = qstyleoption_cast<const QStyleOptionHeader *>(option)) {
            if (header->sortIndicator == QStyleOptionHeader::None)
                return;
            const bool ascending = header->sortIndicator == QStyleOptionHeader::SortDown;
            const QRectF r = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
            const qreal w = qMin<qreal>(qMin(r.width(), r.height() * 1.6), 10.0);
            const qreal h = w / 2.0;
            const QPointF c = r.center();
            QPolygonF chevron;
            if (ascending)
                chevron << QPointF(c.x() - w / 2, c.y() + h / 2) << QPointF(c.x(), c.y() - h / 2) << QPointF(c.x() + w / 2, c.y() + h / 2);
            else
                chevron << QPointF(c.x() - w / 2, c.y() - h / 2) << QPointF(c.x(), c.y() + h / 2) << QPointF(c.x() + w / 2, c.y() - h / 2);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(option->palette.color(QPalette::ButtonText), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            painter->drawPolyline(chevron);
            painter->restore();
            return;
        }
    }
    if (element == QStyle::PE_FrameFocusRect && widget && PaneroTheme::usePaneroColors()
            && (qobject_cast<const QAbstractItemView *>(widget)
                || qobject_cast<const QAbstractItemView *>(widget->parentWidget()))) {
        // Panero paints a solid cursor-row frame in the file views instead
        return;
    }
    if (element == QStyle::PE_FrameFocusRect) {
        if (const auto *fropt = qstyleoption_cast<const QStyleOptionFocusRect *>(option)) {
            QColor bg = fropt->backgroundColor;
            QPen oldPen = painter->pen();
            QPen newPen;
            if (bg.isValid()) {
                int h, s, v;
                bg.getHsv(&h, &s, &v);
                if (v >= 128)
                    newPen.setColor(Qt::black);
                else
                    newPen.setColor(Qt::white);
            } else {
                newPen.setColor(option->palette.windowText().color());
            }
            newPen.setWidth(0);
            newPen.setStyle(Qt::DotLine);
            painter->setPen(newPen);
            QRect focusRect = option->rect /*.adjusted(1, 1, -1, -1) */;
            painter->drawRect(focusRect.adjusted(0, 0, -1, -1)); //draw pen inclusive
            painter->setPen(oldPen);
        }
    } else
        QProxyStyle::drawPrimitive(element, option, painter, widget);
}

int KrStyleProxy::styleHint(QStyle::StyleHint hint, const QStyleOption *option,
                            const QWidget *widget, QStyleHintReturn *returnData) const
{
    if (hint == QStyle::SH_ToolTip_WakeUpDelay) {
        KConfigGroup group(krConfig, "Look&Feel");
        const int delay = group.readEntry("Panel Tooltip Delay", 1000);
        return delay < 0 ? INT_MAX : delay;
    }

    // disable instant consecutive tooltips
    if (hint == QStyle::SH_ToolTip_FallAsleepDelay) {
        return 0;
    }

    return QProxyStyle::styleHint(hint, option, widget, returnData);
}
