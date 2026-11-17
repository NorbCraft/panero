/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "commandergesture.h"

#include <QTest>

using A = CommanderGesture::Action;
Q_DECLARE_METATYPE(CommanderGesture::Action)

class CommanderGestureTest : public QObject
{
    Q_OBJECT
    static constexpr int Drag = 10;

private Q_SLOTS:
    void shortClickTogglesOnRelease()
    {
        CommanderGesture g;
        g.press(QPoint(10, 10), QStringLiteral("a.txt"), false);
        QVERIFY(g.isPending());
        QVERIFY(g.wantsTimer());
        // small jitter below the drag distance is still a click
        QCOMPARE(g.move(QPoint(12, 11), Drag, QStringLiteral("a.txt")), A::None);
        QCOMPARE(g.release(), A::Toggle);
        QCOMPARE(g.item(), QStringLiteral("a.txt"));
        QVERIFY(!g.isPending());
    }

    void holdOpensMenuWithoutToggle()
    {
        CommanderGesture g;
        g.press(QPoint(10, 10), QStringLiteral("a.txt"), true);
        QCOMPARE(g.holdTimeout(), A::ItemMenu);
        QVERIFY(!g.wantsTimer());
        // a second timeout does not open a second menu
        QCOMPARE(g.holdTimeout(), A::None);
        // releasing after the menu must not toggle (no double trigger)
        QCOMPARE(g.release(), A::None);
    }

    void holdOnBackgroundOpensBackgroundMenu()
    {
        CommanderGesture g;
        g.press(QPoint(5, 400), QString(), false);
        QCOMPARE(g.holdTimeout(), A::BackgroundMenu);
        QCOMPARE(g.release(), A::None);
    }

    void shortClickOnBackgroundDoesNothing()
    {
        CommanderGesture g;
        g.press(QPoint(5, 400), QString(), false);
        QCOMPARE(g.release(), A::None);
    }

    void dragBecomesSweepWithoutMenuOrToggle()
    {
        CommanderGesture g;
        g.press(QPoint(10, 10), QStringLiteral("a"), false);
        QCOMPARE(g.move(QPoint(10, 30), Drag, QStringLiteral("b")), A::SweepStart);
        QVERIFY(g.sweepMarks()); // a was unmarked -> sweep marks
        QVERIFY(!g.wantsTimer());
        QCOMPARE(g.move(QPoint(10, 40), Drag, QStringLiteral("c")), A::SweepTo);
        QCOMPARE(g.target(), QStringLiteral("c"));
        QCOMPARE(g.move(QPoint(10, 41), Drag, QStringLiteral("c")), A::None); // same target
        QCOMPARE(g.holdTimeout(), A::None);  // no menu after sweeping
        QCOMPARE(g.release(), A::None);      // no toggle on release
    }

    void sweepFromMarkedItemUnmarks()
    {
        CommanderGesture g;
        g.press(QPoint(10, 10), QStringLiteral("a"), true);
        QCOMPARE(g.move(QPoint(10, 30), Drag, QStringLiteral("b")), A::SweepStart);
        QVERIFY(!g.sweepMarks());
    }

    void cancelDropsEverything()
    {
        CommanderGesture g;
        g.press(QPoint(10, 10), QStringLiteral("a"), false);
        g.cancel(); // focus lost / window switched
        QVERIFY(!g.isPending());
        QCOMPARE(g.holdTimeout(), A::None);
        QCOMPARE(g.release(), A::None);
    }

    void releaseWithoutPressIsIgnored()
    {
        CommanderGesture g;
        QCOMPARE(g.release(), A::None);
        QCOMPARE(g.move(QPoint(50, 50), Drag, QStringLiteral("x")), A::None);
    }

    void operationTargets_data()
    {
        QTest::addColumn<QStringList>("marked");
        QTest::addColumn<QString>("current");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("marks win") << QStringList{"a", "b"} << "c" << QStringList{"a", "b"};
        QTest::newRow("one mark") << QStringList{"a"} << "c" << QStringList{"a"};
        QTest::newRow("no marks -> current") << QStringList{} << "c" << QStringList{"c"};
        QTest::newRow("dotdot never") << QStringList{} << ".." << QStringList{};
        QTest::newRow("dotdot filtered from marks") << QStringList{"..", "a"} << "x" << QStringList{"a"};
        QTest::newRow("nothing") << QStringList{} << QString() << QStringList{};
    }
    void operationTargets()
    {
        QFETCH(QStringList, marked);
        QFETCH(QString, current);
        QFETCH(QStringList, expected);
        QCOMPARE(CommanderTargets::operationTargets(marked, current), expected);
        QCOMPARE(CommanderTargets::contextMenuTargets(false, marked, current), expected);
    }

    void backgroundMenuNeverTargetsFiles()
    {
        QVERIFY(CommanderTargets::contextMenuTargets(true, {"a", "b"}, "c").isEmpty());
        QVERIFY(CommanderTargets::contextMenuTargets(true, {}, "c").isEmpty());
    }
};

QTEST_GUILESS_MAIN(CommanderGestureTest)
#include "commandergesturetest.moc"
