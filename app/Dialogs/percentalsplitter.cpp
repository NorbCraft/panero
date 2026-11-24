/*
    SPDX-FileCopyrightText: 2006 Csaba Karai <krusader@users.sourceforge.net>
    SPDX-FileCopyrightText: 2006-2022 Krusader Krew <https://krusader.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "percentalsplitter.h"

// QtCore
#include <QList>
// QtGui
#include <QPainter>
#include <QCursor>
// QtWidgets
#include <QApplication>
#include <QLabel>
#include <QFrame>
#include <QToolTip>
#include <QMouseEvent>
#include <QSplitterHandle>

namespace
{
/// Panero: double-click the handle between the panels for 50 / 50
class EvenSplitterHandle : public QSplitterHandle
{
public:
    EvenSplitterHandle(Qt::Orientation orientation, QSplitter *parent)
        : QSplitterHandle(orientation, parent)
    {
    }

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return QSplitterHandle::mouseDoubleClickEvent(event);
        QSplitter *s = splitter();
        const int index = s->indexOf(this); // the handle sits before widget `index`
        QList<int> values = s->sizes();
        if (index <= 0 || index >= values.size())
            return;
        const int pair = values[index - 1] + values[index];
        values[index - 1] = pair / 2;
        values[index] = pair - pair / 2;
        s->setSizes(values);
        int before = 0; // the handle's position: the size of everything before it
        for (int i = 0; i < index; ++i)
            before += values[i];
        setToolTip(static_cast<PercentalSplitter *>(s)->toolTipString(before));
        event->accept();
    }
};
} // namespace

PercentalSplitter::PercentalSplitter(QWidget * parent) : QSplitter(parent), label(nullptr), opaqueOldPos(-1)
{
    connect(this, &PercentalSplitter::splitterMoved, this, &PercentalSplitter::slotSplitterMoved);
}

PercentalSplitter::~PercentalSplitter()
= default;

QString PercentalSplitter::toolTipString(int p)
{
    QList<int> values = sizes();
    if (values.count() != 0) {
        int sum = 0;

        QListIterator<int> it(values);
        while (it.hasNext())
            sum += it.next();

        if (sum == 0)
            sum++;

        auto percent = (int)(((double)p / (double)(sum)) * 10000. + 0.5);
        return QString("%1.%2%3").arg(percent / 100).arg((percent / 10) % 10).arg(percent % 10) + '%';
    }
    return QString();
}

void PercentalSplitter::slotSplitterMoved(int p, int index)
{
    handle(index) -> setToolTip(toolTipString(p));

    QToolTip::showText(QCursor::pos(), toolTipString(p) , this);
}

QSplitterHandle *PercentalSplitter::createHandle()
{
    return new EvenSplitterHandle(orientation(), this);
}

void PercentalSplitter::showEvent(QShowEvent * event)
{
    QList<int> values = sizes();

    for (int i = 0; i != count(); i++) {
        int p = 0;
        for (int j = 0; j < i; j++)
            p += values[ j ];

        handle(i) -> setToolTip(toolTipString(p));
    }

    QSplitter::showEvent(event);
}

