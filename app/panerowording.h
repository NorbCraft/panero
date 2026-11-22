/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROWORDING_H
#define PANEROWORDING_H

#include <QtGlobal>

/**
 * Words that differ between Linux and Windows (D-019): Windows says "Recycle
 * Bin" for the Trash and "Settings" for the Konfigurator, and its terminal is
 * Windows Terminal or PowerShell. Both texts stay in the code (both are
 * extracted for translation); Linux keeps its text unchanged.
 *
 *   PANERO_TEXT(i18n("Move to Trash"), i18n("Move to Recycle Bin"))
 */
#ifdef Q_OS_WIN
#define PANERO_TEXT(linuxText, windowsText) (windowsText)
#else
#define PANERO_TEXT(linuxText, windowsText) (linuxText)
#endif

#endif // PANEROWORDING_H
