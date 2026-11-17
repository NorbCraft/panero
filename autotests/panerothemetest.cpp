/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerotheme.h"
#include "krglobal.h"

#include <QApplication>
#include <QFontMetrics>
#include <QTemporaryDir>
#include <QTest>

#include <KConfigCore/KConfig>
#include <KConfigCore/KConfigGroup>

// the only KrGlobal member PaneroTheme uses
KConfig *KrGlobal::config = nullptr;

class PaneroThemeTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    KConfig *m_config = nullptr;

    KConfigGroup group() { return KConfigGroup(m_config, PaneroTheme::GROUP); }

private Q_SLOTS:
    void init()
    {
        QVERIFY(m_dir.isValid());
        delete m_config;
        static int n = 0;
        m_config = new KConfig(m_dir.path() + QStringLiteral("/panerorc-%1").arg(++n), KConfig::SimpleConfig);
        KrGlobal::config = m_config;
        PaneroTheme::invalidate();
    }
    void cleanupTestCase()
    {
        KrGlobal::config = nullptr;
        delete m_config;
    }

    void defaults()
    {
        QVERIFY(PaneroTheme::usePaneroColors());
        QVERIFY(PaneroTheme::alternatingRows());
        QVERIFY(PaneroTheme::markIndicator());
        QCOMPARE(PaneroTheme::configuredRowHeight(), 0);
        QCOMPARE(PaneroTheme::mode(), PaneroTheme::Mode::FollowSystem);
    }

    void rowHeightIsPersistedAndClamped()
    {
        group().writeEntry("Row Height", 20);
        QCOMPARE(PaneroTheme::configuredRowHeight(), 20);
        group().writeEntry("Row Height", 5);
        QCOMPARE(PaneroTheme::configuredRowHeight(), PaneroTheme::MinRowHeight);
        group().writeEntry("Row Height", 200);
        QCOMPARE(PaneroTheme::configuredRowHeight(), PaneroTheme::MaxRowHeight);
    }

    void effectiveRowHeightNeverClips()
    {
        const QFontMetrics fm(QApplication::font());
        // a configured height below what the font/icon needs is raised
        QVERIFY(PaneroTheme::effectiveRowHeight(fm, 22, 18) >= 24);
        QVERIFY(PaneroTheme::effectiveRowHeight(fm, 0, 10) >= fm.height() + 2);
        // a large configured height is honoured
        QCOMPARE(PaneroTheme::effectiveRowHeight(fm, 16, 40), 40);
        // automatic: comfortable default
        QCOMPARE(PaneroTheme::effectiveRowHeight(fm, 16, 0), qMax(fm.height() + 2, int(PaneroTheme::ComfortableRowHeight)));
    }

    void colourOverridesPerLightAndDark()
    {
        const QColor green(0, 200, 0), purple(120, 0, 160);
        group().writeEntry("Light Mark Foreground", green.name());      // Panero format
        group().writeEntry("Dark Alternate Background", QStringLiteral("120,0,160")); // KDE format

        group().writeEntry("Theme", 1); // light
        PaneroTheme::invalidate();
        QCOMPARE(PaneroTheme::mode(), PaneroTheme::Mode::Light);
        QVERIFY(!PaneroTheme::isDark());
        QCOMPARE(PaneroTheme::scheme().markForeground, green);
        QCOMPARE(PaneroTheme::scheme().alternateBackground, PaneroTheme::defaultScheme(false).alternateBackground);

        group().writeEntry("Theme", 2); // dark
        PaneroTheme::invalidate();
        QVERIFY(PaneroTheme::isDark());
        QCOMPARE(PaneroTheme::scheme().markForeground, PaneroTheme::defaultScheme(true).markForeground);
        QCOMPARE(PaneroTheme::scheme().alternateBackground, purple);
    }

    void defaultSchemesHaveContrast()
    {
        // marks must stay distinguishable from both stripes and the cursor fill
        for (bool dark : {false, true}) {
            const PaneroTheme::Scheme s = PaneroTheme::defaultScheme(dark);
            QVERIFY(s.markBackground != s.background);
            QVERIFY(s.markBackground != s.alternateBackground);
            QVERIFY(s.currentBackground != s.background);
            QVERIFY(s.background != s.alternateBackground);
            QVERIFY(qAbs(s.markForeground.lightness() - s.background.lightness()) > 60);
            QVERIFY(qAbs(s.text.lightness() - s.background.lightness()) > 120);
        }
    }

    void classicColoursSwitch()
    {
        group().writeEntry("Use Panero Colors", false);
        QVERIFY(!PaneroTheme::usePaneroColors());
    }
};

QTEST_MAIN(PaneroThemeTest)
#include "panerothemetest.moc"
