/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KGPANERO_H
#define KGPANERO_H

#include "konfiguratorpage.h"

#include <QHash>
#include <QMap>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QSpinBox;
class QVBoxLayout;
class KColorButton;

/**
 * Base of Panero's own Settings pages (Appearance, Panels, Mouse & keys,
 * Drives & cloud, Windows integration). They use the same config keys as the
 * former Quick Settings dialog. Plain options can use the Konfigurator items
 * (createCheckBox, ...); options with their own logic implement load(),
 * save() and defaults(). After Apply the running application is updated
 * (colours, panels, sidebar) and a restart is offered for look changes.
 */
class KgPaneroPage : public KonfiguratorPage
{
    Q_OBJECT
public:
    explicit KgPaneroPage(bool first, QWidget *parent);

    bool apply() override;
    void setDefaults() override;
    void loadInitialValues() override;
    bool isChanged() override;

protected:
    virtual void load() {}
    /// Writes the own (non-Konfigurator) widgets; returns true if the panels must be recreated.
    virtual bool save() { return false; }
    virtual void defaults() {}
    /// The page changes the style, palette or icons (Appearance).
    virtual bool changesLook() const { return false; }

    QGroupBox *addGroup(const QString &title, QFormLayout **form);
    /// A note with links ("settings:<page>") that open another Settings page.
    QLabel *addNote(const QString &html, QWidget *parent = nullptr);
    /// Load the values and watch every input of the page for changes.
    void finishSetup();
    void markChanged();

    QVBoxLayout *m_layout;
    bool m_loading = false;
    bool m_changed = false;
};

class KgPaneroAppearance : public KgPaneroPage
{
    Q_OBJECT
public:
    explicit KgPaneroAppearance(bool first, QWidget *parent = nullptr);

protected:
    bool changesLook() const override { return true; }
    void load() override;
    bool save() override;
    void defaults() override;

private:
    QComboBox *m_theme;
    QCheckBox *m_paneroStyle;
    QComboBox *m_iconSet;
    QComboBox *m_iconTheme;
    QComboBox *m_folderColor;
    QComboBox *m_menuMode;
    QCheckBox *m_classicColors;
    QMap<QString, KColorButton *> m_colorButtons; ///< key: "Light <name>" / "Dark <name>"
    QHash<QString, QCheckBox *> m_transparent;
};

class KgPaneroPanels : public KgPaneroPage
{
    Q_OBJECT
public:
    explicit KgPaneroPanels(bool first, QWidget *parent = nullptr);

protected:
    void load() override;
    bool save() override;
    void defaults() override;

private:
    void densityChanged(int index);
    void sizesEdited();

    QComboBox *m_density;
    QSpinBox *m_rowHeight;
    QComboBox *m_iconSize;
    QCheckBox *m_showHidden;
    QCheckBox *m_previews;
    bool m_updatingDensity = false;
};

class KgPaneroMouse : public KgPaneroPage
{
    Q_OBJECT
public:
    explicit KgPaneroMouse(bool first, QWidget *parent = nullptr);

protected:
    void load() override;
    bool save() override;
    void defaults() override;

private:
    QComboBox *m_selection;
    QComboBox *m_holdFeedback;
};

class KgPaneroDrives : public KgPaneroPage
{
    Q_OBJECT
public:
    explicit KgPaneroDrives(bool first, QWidget *parent = nullptr);

protected:
    void load() override;
    bool save() override;
    void defaults() override;

private:
    QCheckBox *m_showDrives;
    QComboBox *m_driveLabels;
    QCheckBox *m_showCloud;
};

#ifdef Q_OS_WIN
class KgPaneroWindows : public KgPaneroPage
{
    Q_OBJECT
public:
    explicit KgPaneroWindows(bool first, QWidget *parent = nullptr);
};
#endif

/// The "Advanced" node: a short explanation above Krusader's original pages.
class KgPaneroAdvancedIntro : public KonfiguratorPage
{
    Q_OBJECT
public:
    explicit KgPaneroAdvancedIntro(bool first, QWidget *parent = nullptr);
};

#endif // KGPANERO_H
