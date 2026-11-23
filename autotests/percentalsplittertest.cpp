/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Double-click on the handle between the panels: 50 / 50 (D-022)

#include "percentalsplitter.h"

#include <QLabel>
#include <QTest>

class PercentalSplitterTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void doubleClickSharesEvenly()
    {
        PercentalSplitter splitter;
        splitter.addWidget(new QLabel(QStringLiteral("left")));
        splitter.addWidget(new QLabel(QStringLiteral("right")));
        splitter.resize(1006, 300);
        splitter.show();
        QVERIFY(QTest::qWaitForWindowExposed(&splitter));
        splitter.setSizes({250, 750});
        QVERIFY(splitter.sizes().at(1) > 2 * splitter.sizes().at(0)); // uneven before
        QSplitterHandle *handle = splitter.handle(1);
        QTest::mouseDClick(handle, Qt::LeftButton, Qt::NoModifier, handle->rect().center());
        const QList<int> sizes = splitter.sizes();
        QVERIFY2(qAbs(sizes.at(0) - sizes.at(1)) <= 1, qPrintable(QStringLiteral("%1 / %2").arg(sizes.at(0)).arg(sizes.at(1))));
        QCOMPARE(handle->toolTip(), QStringLiteral("50.00%"));
    }
};

QTEST_MAIN(PercentalSplitterTest)
#include "percentalsplittertest.moc"
