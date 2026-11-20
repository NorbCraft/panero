/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROAPPEARANCE_H
#define PANEROAPPEARANCE_H

#include <QObject>
#include <QIcon>
#include <QPalette>

class QDBusVariant;

/**
 * Application-wide (Panero only) look: follows the desktop's light/dark
 * preference (XDG desktop portal "org.freedesktop.appearance color-scheme",
 * as set by GNOME/Zorin, with a gsettings fallback) or a forced mode, and
 * applies the Fusion style with a Panero palette. Nothing outside Panero is
 * changed: the desktop theme and other applications are left alone.
 *
 * Settings ("Panero Appearance" group): "Theme" (0 follow system, 1 light,
 * 2 dark) and "Panero Style" (true: Fusion + Panero palette, false: keep the
 * system Qt style and palette).
 */
class PaneroAppearance : public QObject
{
    Q_OBJECT
public:
    static PaneroAppearance *instance();

    /// Apply style, palette and icon theme according to the current settings.
    void apply();

    /// Desktop preference: 1 = prefer dark, 2 = prefer light, 0 = unknown/no preference.
    int systemColorScheme() const { return m_systemScheme; }

    static QPalette paneroPalette(bool dark);

    /// A GNOME "symbolic" icon of the icon theme (single colour SVG) recoloured to

    /// \a color - Qt does not recolour them, so they would stay dark in dark mode.

    /// Returns a null icon when the theme has no such SVG.

    static QIcon symbolicIcon(const QString &name, const QColor &color);

signals:
    void appearanceChanged();

private slots:
    void portalSettingChanged(const QString &ns, const QString &key, const QDBusVariant &value);

private:
    PaneroAppearance();
    void readSystemScheme();
    void applyIconTheme(bool dark);
    void applyStyleSheet(bool paneroStyle);

    int m_systemScheme = 0;
    QString m_originalStyle;
    QString m_originalIconTheme;
    bool m_applied = false;
};

#endif // PANEROAPPEARANCE_H
