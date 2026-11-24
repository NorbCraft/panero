/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROQUICKSETTINGS_H
#define PANEROQUICKSETTINGS_H

#include <QFrame>

class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QVBoxLayout;

/**
 * Quick Settings: a short popup under the toolbar's gear with the most used
 * switches (D-022): Light/Dark/System, icon theme, density, hidden files,
 * drive buttons, Cloud section, thumbnails (+ the Windows right-click menu).
 * Every change applies at once; look changes offer a restart in the popup.
 * "All settings..." opens the Settings window at Appearance.
 */
class PaneroQuickSettings : public QFrame
{
    Q_OBJECT
public:
    /// Opens the popup under @p anchor (the gear button), or centred on @p window.
    static void popup(QWidget *window, QWidget *anchor);

protected:
    explicit PaneroQuickSettings(QWidget *window);
    void keyPressEvent(QKeyEvent *event) override;

private:
    void updateRestartRow();
    QCheckBox *addSwitch(const QString &text, const QString &toolTip);

    QVBoxLayout *m_layout;
    QFormLayout *m_form;
    QButtonGroup *m_themes;
    QComboBox *m_iconTheme = nullptr;
    QComboBox *m_density;
    QWidget *m_restartRow;
    QString m_restartSignature;
};

#endif // PANEROQUICKSETTINGS_H
