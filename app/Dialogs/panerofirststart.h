/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROFIRSTSTART_H
#define PANEROFIRSTSTART_H

#include <QWizard>

class QButtonGroup;
class QCheckBox;
class QComboBox;

/**
 * First-start assistant (D-022): shown once to new users before the main
 * window opens (existing users never get it automatically), and from
 * Help > First-Start Assistant. Pages: look (Light/Dark/System, Panero style,
 * icon theme), drive buttons and Cloud section, mouse selection, done.
 * It writes the same keys as Settings and Quick Settings.
 */
class PaneroFirstStart : public QWizard
{
    Q_OBJECT
public:
    /// New user (no "First Time=false" in panerorc): run the assistant before the
    /// main window is created. Returns at once for existing users.
    static void runOnFirstStart();
    /// Help menu: shows the assistant for the running Panero (changes apply at
    /// once, look changes offer a restart).
    static void showFromHelp(QWidget *parent);

    explicit PaneroFirstStart(QWidget *parent = nullptr);

    /// Writes the choices (on Finish).
    void save();

private:
    QButtonGroup *m_theme;
    QCheckBox *m_paneroStyle;
    QComboBox *m_iconTheme = nullptr;
    QCheckBox *m_driveButtons;
    QCheckBox *m_cloud;
    QButtonGroup *m_mouse;
};

#endif // PANEROFIRSTSTART_H
