/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroappearance.h"
#include "panerotheme.h"
#include "paneroiconthemes.h"

#include "../krglobal.h"

#include <QApplication>
#include <QDebug>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QIcon>
#include <QProcess>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>

#include <KConfigCore/KConfigGroup>
#include <KConfigCore/KSharedConfig>

static const char PORTAL_SERVICE[] = "org.freedesktop.portal.Desktop";
static const char PORTAL_PATH[] = "/org/freedesktop/portal/desktop";
static const char PORTAL_SETTINGS[] = "org.freedesktop.portal.Settings";

PaneroAppearance *PaneroAppearance::instance()
{
    static auto *self = new PaneroAppearance();
    return self;
}

PaneroAppearance::PaneroAppearance()
{
    m_originalStyle = QApplication::style() ? QApplication::style()->objectName() : QString();
    m_originalIconTheme = QIcon::themeName();
    readSystemScheme();
    QDBusConnection::sessionBus().connect(QLatin1String(PORTAL_SERVICE), QLatin1String(PORTAL_PATH),
                                          QLatin1String(PORTAL_SETTINGS), QStringLiteral("SettingChanged"), this,
                                          SLOT(portalSettingChanged(QString,QString,QDBusVariant)));
}

static int unwrapScheme(QVariant v)
{
    // Read() returns variant<variant<uint32>>; SettingChanged delivers variant<uint32>
    while (v.canConvert<QDBusVariant>() && v.userType() == qMetaTypeId<QDBusVariant>())
        v = v.value<QDBusVariant>().variant();
    bool ok = false;
    const uint scheme = v.toUInt(&ok);
    return ok ? int(scheme) : 0;
}

void PaneroAppearance::readSystemScheme()
{
    m_systemScheme = 0;
    // Raw call without introspection or service auto-start: on GNOME/Zorin the portal is
    // already running; elsewhere we must not block startup waiting for it.
    QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(PORTAL_SERVICE), QLatin1String(PORTAL_PATH),
                                                      QLatin1String(PORTAL_SETTINGS), QStringLiteral("Read"));
    msg << QStringLiteral("org.freedesktop.appearance") << QStringLiteral("color-scheme");
    msg.setAutoStartService(false);
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 300);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        m_systemScheme = unwrapScheme(reply.arguments().first());
        return;
    }
    // fallback for sessions without the portal: GNOME setting
    if (!QStandardPaths::findExecutable(QStringLiteral("gsettings")).isEmpty()) {
        QProcess p;
        p.start(QStringLiteral("gsettings"), {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
                                               QStringLiteral("color-scheme")});
        if (p.waitForFinished(500)) {
            const QString out = QString::fromUtf8(p.readAllStandardOutput());
            if (out.contains(QLatin1String("prefer-dark")))
                m_systemScheme = 1;
            else if (out.contains(QLatin1String("prefer-light")) || out.contains(QLatin1String("default")))
                m_systemScheme = 2;
        }
    }
}

void PaneroAppearance::portalSettingChanged(const QString &ns, const QString &key, const QDBusVariant &value)
{
    if (ns != QLatin1String("org.freedesktop.appearance") || key != QLatin1String("color-scheme"))
        return;
    m_systemScheme = unwrapScheme(QVariant::fromValue(value));
    if (PaneroTheme::mode() == PaneroTheme::Mode::FollowSystem)
        apply();
}

QPalette PaneroAppearance::paneroPalette(bool dark)
{
    QPalette p;
    // (C++11: no default arguments for lambda parameters)
    auto set3 = [&p](QPalette::ColorRole role, const QColor &c, const QColor &disabled) {
        p.setColor(QPalette::Active, role, c);
        p.setColor(QPalette::Inactive, role, c);
        p.setColor(QPalette::Disabled, role, disabled.isValid() ? disabled : c);
    };
    if (!dark) {
        set3(QPalette::Window, QColor(0xf5, 0xf6, 0xf8), QColor());
        set3(QPalette::WindowText, QColor(0x1f, 0x23, 0x28), QColor(0x9a, 0xa0, 0xa6));
        set3(QPalette::Base, QColor(0xff, 0xff, 0xff), QColor(0xf5, 0xf6, 0xf8));
        set3(QPalette::AlternateBase, QColor(0xf3, 0xf5, 0xf8), QColor());
        set3(QPalette::Text, QColor(0x1f, 0x23, 0x28), QColor(0x9a, 0xa0, 0xa6));
        set3(QPalette::Button, QColor(0xee, 0xf0, 0xf3), QColor());
        set3(QPalette::ButtonText, QColor(0x1f, 0x23, 0x28), QColor(0x9a, 0xa0, 0xa6));
        set3(QPalette::BrightText, QColor(0xff, 0xff, 0xff), QColor());
        set3(QPalette::Highlight, PaneroTheme::accentColor(false), QColor(0xc4, 0xc9, 0xd0));
        set3(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff), QColor());
        set3(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xff), QColor());
        set3(QPalette::ToolTipText, QColor(0x1f, 0x23, 0x28), QColor());
        set3(QPalette::PlaceholderText, QColor(0x8a, 0x91, 0x99), QColor());
        set3(QPalette::Link, QColor(0x1d, 0x4e, 0xd8), QColor());
        set3(QPalette::LinkVisited, QColor(0x6d, 0x28, 0xd9), QColor());
        set3(QPalette::Light, QColor(0xff, 0xff, 0xff), QColor());
        set3(QPalette::Midlight, QColor(0xe6, 0xe8, 0xec), QColor());
        set3(QPalette::Mid, QColor(0xc9, 0xcd, 0xd3), QColor());
        set3(QPalette::Dark, QColor(0xa3, 0xa9, 0xb1), QColor());
        set3(QPalette::Shadow, QColor(0x6b, 0x72, 0x7b), QColor());
    } else {
        set3(QPalette::Window, QColor(0x23, 0x26, 0x2b), QColor());
        set3(QPalette::WindowText, QColor(0xe4, 0xe7, 0xeb), QColor(0x6b, 0x72, 0x80));
        set3(QPalette::Base, QColor(0x1d, 0x20, 0x25), QColor(0x23, 0x26, 0x2b));
        set3(QPalette::AlternateBase, QColor(0x24, 0x28, 0x2e), QColor());
        set3(QPalette::Text, QColor(0xe4, 0xe7, 0xeb), QColor(0x6b, 0x72, 0x80));
        set3(QPalette::Button, QColor(0x2c, 0x30, 0x36), QColor());
        set3(QPalette::ButtonText, QColor(0xe4, 0xe7, 0xeb), QColor(0x6b, 0x72, 0x80));
        set3(QPalette::BrightText, QColor(0xff, 0xff, 0xff), QColor());
        set3(QPalette::Highlight, PaneroTheme::accentColor(true), QColor(0x3a, 0x3f, 0x47));
        set3(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff), QColor());
        set3(QPalette::ToolTipBase, QColor(0x2c, 0x30, 0x36), QColor());
        set3(QPalette::ToolTipText, QColor(0xe4, 0xe7, 0xeb), QColor());
        set3(QPalette::PlaceholderText, QColor(0x80, 0x87, 0x91), QColor());
        set3(QPalette::Link, QColor(0x6e, 0xa8, 0xff), QColor());
        set3(QPalette::LinkVisited, QColor(0xb3, 0x92, 0xf0), QColor());
        set3(QPalette::Light, QColor(0x3a, 0x3f, 0x47), QColor());
        set3(QPalette::Midlight, QColor(0x33, 0x37, 0x3e), QColor());
        set3(QPalette::Mid, QColor(0x2a, 0x2e, 0x34), QColor());
        set3(QPalette::Dark, QColor(0x15, 0x17, 0x1a), QColor());
        set3(QPalette::Shadow, QColor(0x0b, 0x0c, 0x0e), QColor());
    }
    return p;
}

QIcon PaneroAppearance::symbolicIcon(const QString &name, const QColor &color)
{
    QStringList themes = {QIcon::themeName()};
    // Zorin's Blue/Grey/... variants inherit the symbolic icons from "Zorin"
    for (const QString &t : {QStringLiteral("Zorin"), QStringLiteral("Adwaita"), QIcon::fallbackThemeName()}) {
        if (!themes.contains(t))
            themes << t;
    }
    QString source;
    for (const QString &theme : qAsConst(themes)) {
        for (const QString &root : QIcon::themeSearchPaths()) {
            for (const QString &kind : {QStringLiteral("scalable"), QStringLiteral("symbolic")}) {
                const QDir dir(root + QLatin1Char('/') + theme + QLatin1Char('/') + kind);
                for (const QString &context : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                    const QString file = dir.filePath(context + QLatin1Char('/') + name + QStringLiteral(".svg"));
                    if (QFile::exists(file)) {
                        source = file;
                        break;
                    }
                }
                if (!source.isEmpty())
                    break;
            }
            if (!source.isEmpty())
                break;
        }
        if (!source.isEmpty())
            break;
    }
    QFile in(source);
    if (source.isEmpty() || !in.open(QIODevice::ReadOnly))
        return QIcon();
    QString svg = QString::fromUtf8(in.readAll());
    svg.replace(QRegularExpression(QStringLiteral("#[0-9a-fA-F]{6}\\b|#[0-9a-fA-F]{3}\\b")), color.name());
    // cached as a file: the Qt SVG icon engine renders it at every size
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/icons");
    QDir().mkpath(cacheDir);
    const QString cached = cacheDir + QLatin1Char('/') + name + QLatin1Char('-') + color.name().mid(1) + QStringLiteral(".svg");
    QFile out(cached);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QIcon();
    out.write(svg.toUtf8());
    out.close();
    return QIcon(cached);
}

static bool iconThemeExists(const QString &name)
{
    if (name.isEmpty())
        return false;
    for (const QString &path : QIcon::themeSearchPaths()) {
        if (QFileInfo::exists(path + QLatin1Char('/') + name + QStringLiteral("/index.theme")))
            return true;
    }
    return false;
}

void PaneroAppearance::applyIconTheme(bool dark)
{
    // Keep the user's icon theme, but use its light/dark counterpart when Panero's
    // look does not match the desktop (e.g. ZorinBlue-Light <-> ZorinBlue-Dark).
    QString theme = m_originalIconTheme;
    // Panero Appearance / Icon Theme: an icon set chosen in Panero (empty = the desktop's)
    const KConfigGroup look(krConfig ? KConfigGroup(krConfig, PaneroTheme::GROUP)
                                     : KConfigGroup(KSharedConfig::openConfig(), PaneroTheme::GROUP));
    const QString chosen = look.readEntry("Icon Theme", QString());
    if (!chosen.isEmpty() && iconThemeExists(chosen)) {
        theme = chosen;
        if (dark) {
            for (const QString &variant : {chosen + QStringLiteral("-Dark"), chosen + QStringLiteral("-dark")}) {
                if (iconThemeExists(variant)) {
                    theme = variant;
                    break;
                }
            }
        }
    }
    const bool themeIsDark = theme.contains(QLatin1String("dark"), Qt::CaseInsensitive);
    if (dark != themeIsDark) {
        QString counterpart = theme;
        if (dark) {
            counterpart.replace(QLatin1String("Light"), QLatin1String("Dark"));
            counterpart.replace(QLatin1String("light"), QLatin1String("dark"));
            if (counterpart == theme)
                counterpart = theme + QStringLiteral("-dark");
        } else {
            counterpart.replace(QLatin1String("Dark"), QLatin1String("Light"));
            counterpart.replace(QLatin1String("-dark"), QString());
            counterpart.replace(QLatin1String("dark"), QLatin1String("light"));
        }
        if (iconThemeExists(counterpart))
            theme = counterpart;
        else if (iconThemeExists(dark ? QStringLiteral("breeze-dark") : QStringLiteral("breeze")))
            theme = dark ? QStringLiteral("breeze-dark") : QStringLiteral("breeze");
    }
    // Panero layers: "Panero Line" icons and recoloured folders (D-014)
    const bool line = look.readEntry("Icon Set", QString()) == QLatin1String("line");
    const QColor folderColor = PaneroTheme::readColor(look.readEntry("Folder Color", QString()));
    theme = PaneroIconThemes::compose(theme, line, dark, folderColor);
    if (!theme.isEmpty() && theme != QIcon::themeName())
        QIcon::setThemeName(theme);
    // KIconThemes makes light Breeze Qt's global fallback theme; icons missing from the
    // desktop theme (most KDE action icons on Zorin) would then be dark glyphs on a dark
    // window. Use the Breeze variant that matches Panero's look.
    const QString breeze = dark ? QStringLiteral("breeze-dark") : QStringLiteral("breeze");
    if (iconThemeExists(breeze))
        QIcon::setFallbackThemeName(breeze);
    qDebug() << "Panero appearance: dark =" << dark << "icon theme =" << QIcon::themeName()
             << "style =" << (QApplication::style() ? QApplication::style()->objectName() : QString());
}

void PaneroAppearance::applyStyleSheet(bool paneroStyle)
{
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app)
        return;
    QString sheet;
    // development: try a style sheet on the real UI (dev/scripts/make-mockups.sh)
    const QString devStyle = qEnvironmentVariable("PANERO_DEV_STYLESHEET");
    QFile file(!devStyle.isEmpty() ? devStyle : QStringLiteral(":/panero-modern.qss"));
    if ((paneroStyle || !devStyle.isEmpty()) && file.open(QIODevice::ReadOnly)) {
        sheet = QString::fromUtf8(file.readAll());
        if (!PaneroTheme::usePaneroColors()) // classic Krusader colours for the status rows
            sheet.remove(QRegularExpression(QStringLiteral("/\\* @panero-colors.*?@end \\*/"),
                                            QRegularExpression::DotMatchesEverythingOption));
    }
    // set again even if unchanged: palette() references follow the new palette
    app->setStyleSheet(QString());
    app->setStyleSheet(sheet);
}

void PaneroAppearance::apply()
{
    const KConfigGroup group(krConfig ? KConfigGroup(krConfig, PaneroTheme::GROUP)
                                      : KConfigGroup(KSharedConfig::openConfig(), PaneroTheme::GROUP));
    const bool paneroStyle = group.readEntry("Panero Style", true);

    bool dark;
    switch (PaneroTheme::mode()) {
    case PaneroTheme::Mode::Light:
        dark = false;
        break;
    case PaneroTheme::Mode::Dark:
        dark = true;
        break;
    default:
        if (m_systemScheme == 1)
            dark = true;
        else if (m_systemScheme == 2)
            dark = false;
        else // no desktop preference known: keep what the platform palette says
            dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
    }

    if (paneroStyle) {
        if (!QApplication::style() || QApplication::style()->objectName().compare(QLatin1String("fusion"), Qt::CaseInsensitive) != 0)
            QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QApplication::setPalette(paneroPalette(dark));
        m_applied = true;
    } else if (m_applied) {
        // back to the system style/palette
        if (!m_originalStyle.isEmpty())
            QApplication::setStyle(QStyleFactory::create(m_originalStyle));
        QApplication::setPalette(QApplication::style()->standardPalette());
        m_applied = false;
    }
    applyStyleSheet(paneroStyle);
    applyIconTheme(paneroStyle ? dark : QApplication::palette().color(QPalette::Window).lightness() < 128);
    PaneroTheme::invalidate();
    emit appearanceChanged();
}
