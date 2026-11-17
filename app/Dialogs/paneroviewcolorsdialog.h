/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROVIEWCOLORSDIALOG_H
#define PANEROVIEWCOLORSDIALOG_H

#include <QDialog>
#include <QMap>

class QCheckBox;
class QComboBox;
class QSpinBox;
class KColorButton;

/**
 * "View & Colors": the everyday appearance settings of Panero in one compact
 * dialog (theme, density, stripes, mark/cursor colours for light and dark,
 * hidden files). Changes apply live to all panels and tabs without losing
 * marks. The complete original settings stay available in Configure Panero.
 */
class PaneroViewColorsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PaneroViewColorsDialog(QWidget *parent = nullptr);

    /// Show the dialog (modal) for the main window.
    static void showDialog(QWidget *parent);

private slots:
    void apply();
    void restoreDefaults();
    void densityChanged(int index);
    void rowHeightEdited();

private:
    void load();
    void applyToPanels(int iconSize);

    QComboBox *m_theme;
    QCheckBox *m_paneroStyle;
    QComboBox *m_density;
    QSpinBox *m_rowHeight;
    QComboBox *m_iconSize;
    QCheckBox *m_alternating;
    QCheckBox *m_markIndicator;
    QCheckBox *m_showHidden;
    QCheckBox *m_classicColors;
    QMap<QString, KColorButton *> m_colorButtons; ///< key: "Light <name>" / "Dark <name>"
    bool m_updatingDensity = false;
};

#endif // PANEROVIEWCOLORSDIALOG_H
