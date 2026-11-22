/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneromultirenamerules.h"

#include <QTest>

using namespace PaneroMultiRename;

class PaneroMultiRenameTest : public QObject
{
    Q_OBJECT
    static Item item(const QString &fileName, bool isDir = false)
    {
        Item i;
        i.fileName = fileName;
        i.isDir = isDir;
        i.modified = QDateTime(QDate(2026, 8, 3), QTime(14, 22, 10));
        i.parentName = QStringLiteral("Holiday");
        i.grandParentName = QStringLiteral("Photos");
        return i;
    }
    static QString rename(const QString &fileName, const Options &o, int index = 0, bool isDir = false)
    {
        return newName(item(fileName, isDir), o, index);
    }

private Q_SLOTS:
    void defaultKeepsName()
    {
        const Options o;
        QCOMPARE(rename(QStringLiteral("IMG_4021.jpg"), o), QStringLiteral("IMG_4021.jpg"));
        QCOMPARE(rename(QStringLiteral("archive.tar.gz"), o), QStringLiteral("archive.tar.gz"));
        QCOMPARE(rename(QStringLiteral(".bashrc"), o), QStringLiteral(".bashrc"));
        QCOMPARE(rename(QStringLiteral("My.Folder"), o, 0, true), QStringLiteral("My.Folder"));
        QCOMPARE(rename(QStringLiteral("README"), o), QStringLiteral("README"));
    }
    void ranges()
    {
        Options o;
        o.nameMask = QStringLiteral("[N1-3]_[N5-]_[N2,2]_[N-2-]_[N1]");
        QCOMPARE(rename(QStringLiteral("IMG_4021.jpg"), o), QStringLiteral("IMG_4021_MG_21_I.jpg"));
        o.nameMask = QStringLiteral("[N]");
        o.extMask = QStringLiteral("[E1-2]");
        QCOMPARE(rename(QStringLiteral("photo.jpeg"), o), QStringLiteral("photo.jp"));
    }
    void counter()
    {
        Options o;
        o.nameMask = QStringLiteral("[P] [C]");
        o.counterStart = 1;
        o.counterStep = 1;
        o.counterDigits = 3;
        QCOMPARE(rename(QStringLiteral("a.jpg"), o, 0), QStringLiteral("Holiday 001.jpg"));
        QCOMPARE(rename(QStringLiteral("b.jpg"), o, 4), QStringLiteral("Holiday 005.jpg"));
        o.nameMask = QStringLiteral("[C10+5:2]-[G]");
        QCOMPARE(rename(QStringLiteral("x.png"), o, 2), QStringLiteral("20-Photos.png"));
    }
    void dateTimeAndLiterals()
    {
        Options o;
        o.nameMask = QStringLiteral("[YMD]_[hms] [[][N][]] [Y]-[M]-[D] [h]h[n]m[s]s [y]");
        QCOMPARE(rename(QStringLiteral("x.txt"), o), QStringLiteral("20260803_142210 [x] 2026-08-03 14h22m10s 26.txt"));
    }
    void searchReplace()
    {
        Options o;
        o.search = QStringLiteral("img_|dsc_");
        o.replace = QStringLiteral("Photo ");
        QCOMPARE(rename(QStringLiteral("IMG_4021.jpg"), o), QStringLiteral("Photo 4021.jpg"));
        QCOMPARE(rename(QStringLiteral("DSC_0007.jpg"), o), QStringLiteral("Photo 0007.jpg"));
        o.caseSensitive = true;
        QCOMPARE(rename(QStringLiteral("IMG_4021.jpg"), o), QStringLiteral("IMG_4021.jpg"));
        // one replacement per term
        o = Options();
        o.search = QStringLiteral("a|b");
        o.replace = QStringLiteral("1|2");
        QCOMPARE(rename(QStringLiteral("abba.txt"), o), QStringLiteral("1221.txt"));
        // only the first match
        o = Options();
        o.search = QStringLiteral(" ");
        o.replace = QStringLiteral("_");
        o.firstOnly = true;
        QCOMPARE(rename(QStringLiteral("a b c.txt"), o), QStringLiteral("a_b c.txt"));
        // the extension is left alone unless asked
        o = Options();
        o.search = QStringLiteral("jpeg");
        o.replace = QStringLiteral("jpg");
        QCOMPARE(rename(QStringLiteral("x.jpeg"), o), QStringLiteral("x.jpeg"));
        o.inExtension = true;
        QCOMPARE(rename(QStringLiteral("x.jpeg"), o), QStringLiteral("x.jpg"));
        // plain text with special characters is literal
        o = Options();
        o.search = QStringLiteral("(1)");
        o.replace = QStringLiteral("\\copy");
        QCOMPARE(rename(QStringLiteral("doc (1).pdf"), o), QStringLiteral("doc \\copy.pdf"));
    }
    void regex()
    {
        Options o;
        o.regex = true;
        o.search = QStringLiteral("^(\\d{4})-(\\d{2})-(\\d{2}) (.*)$");
        o.replace = QStringLiteral("$4 ($3.$2.$1)");
        QCOMPARE(rename(QStringLiteral("2026-08-03 Beach.jpg"), o), QStringLiteral("Beach (03.08.2026).jpg"));
        o.firstOnly = true;
        QCOMPARE(rename(QStringLiteral("2026-08-03 Beach.jpg"), o), QStringLiteral("Beach (03.08.2026).jpg"));
        o.search = QStringLiteral("([a-z]+");
        QString error;
        QVERIFY(!searchIsValid(o, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(rename(QStringLiteral("abc.txt"), o), QStringLiteral("abc.txt")); // invalid: unchanged
    }
    void caseConversion()
    {
        Options o;
        o.nameCase = Case::WordsUpper;
        o.extCase = Case::Lower;
        QCOMPARE(rename(QStringLiteral("route MAP final.PDF"), o), QStringLiteral("Route Map Final.pdf"));
        o.nameCase = Case::FirstUpper;
        QCOMPARE(rename(QStringLiteral("hELLO world.TXT"), o), QStringLiteral("Hello world.txt"));
        o.nameCase = Case::Upper;
        o.extCase = Case::Unchanged;
        QCOMPARE(rename(QStringLiteral("abc.Jpg"), o), QStringLiteral("ABC.Jpg"));
    }
    void problems()
    {
        const QStringList oldNames{QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt"), QStringLiteral("d.txt")};
        const QStringList newNames{QStringLiteral("x.txt"), QStringLiteral("x.txt"), QStringLiteral("other.txt"), QStringLiteral("")};
        const QList<Problem> p = check(oldNames, newNames, {QStringLiteral("other.txt")}, false);
        QCOMPARE(p.at(0), Problem::Duplicate);
        QCOMPARE(p.at(1), Problem::Duplicate);
        QCOMPARE(p.at(2), Problem::Exists);
        QCOMPARE(p.at(3), Problem::Empty);
        QCOMPARE(check({QStringLiteral("a")}, {QStringLiteral("x/y")}, {}, false).at(0), Problem::InvalidCharacter);
        // case-insensitive file systems: X.txt and x.txt are the same name
        QCOMPARE(check({QStringLiteral("a"), QStringLiteral("b")}, {QStringLiteral("X.txt"), QStringLiteral("x.txt")}, {}, true).at(0), Problem::Duplicate);
        QCOMPARE(check({QStringLiteral("a"), QStringLiteral("b")}, {QStringLiteral("X.txt"), QStringLiteral("x.txt")}, {}, false).at(0), Problem::None);
    }
    void planSwap()
    {
        // a -> b, b -> a, c -> d: the busy names go via temporary names
        const auto steps = plan({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
                                {QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("d")}, QStringLiteral(".tmp-"), false);
        QStringList names{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")};
        for (const auto &s : steps) {
            QVERIFY2(!names.contains(s.second), qPrintable(QStringLiteral("target %1 still taken").arg(s.second)));
            const int i = names.indexOf(s.first);
            QVERIFY(i >= 0);
            names[i] = s.second;
        }
        names.sort();
        QCOMPARE(names, QStringList({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("d")}));
        // case only on a case-insensitive file system: via a temporary name
        const auto caseSteps = plan({QStringLiteral("abc")}, {QStringLiteral("ABC")}, QStringLiteral(".tmp-"), true);
        QCOMPARE(caseSteps.size(), 2);
        QCOMPARE(plan({QStringLiteral("abc")}, {QStringLiteral("ABC")}, QStringLiteral(".tmp-"), false).size(), 1);
        QVERIFY(plan({QStringLiteral("same")}, {QStringLiteral("same")}, QStringLiteral(".tmp-"), false).isEmpty());
    }
    void presets()
    {
        Options o;
        o.nameMask = QStringLiteral("[N] & [C]=x");
        o.search = QStringLiteral("a|b");
        o.replace = QStringLiteral("1|2");
        o.regex = true;
        o.nameCase = Case::Upper;
        o.counterDigits = 4;
        const Options back = Options::fromString(o.toString());
        QCOMPARE(back.nameMask, o.nameMask);
        QCOMPARE(back.search, o.search);
        QCOMPARE(back.replace, o.replace);
        QCOMPARE(back.regex, true);
        QCOMPARE(back.nameCase, Case::Upper);
        QCOMPARE(back.counterDigits, 4);
        QCOMPARE(Options::fromString(QString()).nameMask, QStringLiteral("[N]"));
    }
};

QTEST_GUILESS_MAIN(PaneroMultiRenameTest)
#include "paneromultirenametest.moc"
