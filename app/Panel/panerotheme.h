/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROTHEME_H
#define PANEROTHEME_H

#include <QColor>
#include <QString>
#include <QVector>

class QPainter;
class QRect;
class QRectF;
class QFontMetrics;

/**
 * Panero's file list appearance: light/dark colour schemes with user overrides,
 * the current-row (cursor) frame, the mark indicator and row metrics.
 *
 * Settings live in the "Panero Appearance" config group. When "Use Panero Colors"
 * is false, the classic Krusader colour settings (Configure > Colors) are used.
 */
namespace PaneroTheme
{
enum class Mode { FollowSystem = 0, Light = 1, Dark = 2 };

struct Scheme {
    QColor background;           ///< row background (first stripe)
    QColor alternateBackground;  ///< second stripe
    QColor text;                 ///< normal text
    QColor markForeground;       ///< text of marked rows (red by default)
    QColor markBackground;       ///< background of marked rows
    QColor currentBackground;    ///< fill of the cursor row in the active panel
    QColor currentFrame;         ///< frame + accent bar of the cursor row (active panel)
    QColor inactiveCurrentFrame; ///< frame of the cursor row in the inactive panel
};

/// One user-configurable colour; key is stored as "<Light|Dark> <key>".
struct ColorKey {
    const char *key;
    QColor Scheme::*member;
};
const QVector<ColorKey> &colorKeys();

/// Config group used by all Panero appearance settings.
extern const char *const GROUP;

bool usePaneroColors();
Mode mode();
/// Effective dark/light decision (forced mode or the application palette).
bool isDark();
Scheme defaultScheme(bool dark);
/// Effective scheme including user overrides for the current light/dark mode.
const Scheme &scheme();
QString keyPrefix(bool dark);
/// Read a colour stored as "#rrggbb" (Panero) or "r,g,b" (KDE); invalid if absent.
QColor readColor(const QString &value);

bool alternatingRows();
bool markIndicator();

/// Row height setting in logical pixels (0 = automatic from font/icon size).
int configuredRowHeight();
/// Minimum/maximum row height offered to users.
constexpr int MinRowHeight = 16;
constexpr int MaxRowHeight = 48;
constexpr int CompactRowHeight = 20;
constexpr int ComfortableRowHeight = 24;
/**
 * Effective row height: the configured height, but never smaller than what the
 * font (and icon, when shown) needs, so text is never clipped.
 */
int effectiveRowHeight(const QFontMetrics &fm, int iconSize, int configured);

/// Drop cached values (call after configuration or palette changes).
void invalidate();

/// Draw the cursor-row frame (solid border + leading accent bar when active).
void paintCurrentFrame(QPainter *painter, const QRect &rect, bool activePanel);
/// Draw the mark indicator (check badge) inside @p area (usually the icon rect).
void paintMarkIndicator(QPainter *painter, const QRectF &area);
}

#endif // PANEROTHEME_H
