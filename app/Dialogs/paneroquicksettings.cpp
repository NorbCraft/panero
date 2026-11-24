/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroquicksettings.h"

#include "panerosettings.h"
#include "../Panel/panerotheme.h"
#include "../krglobal.h"
#include "../krslots.h"
#include "../krusader.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QWindow>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <KI18n/KLocalizedString>

void PaneroQuickSettings::popup(QWidget *window, QWidget *anchor)
{
    static QPointer<PaneroQuickSettings> open;
    if (open) {
        open->close();
        return;
    }
    auto *popup = new PaneroQuickSettings(window);
    open = popup;
    popup->adjustSize();
    const QSize size = popup->sizeHint();
    QPoint pos;
    if (anchor && anchor->isVisible()) {
        // right-aligned under the gear button
        const QPoint bottomRight = anchor->mapToGlobal(QPoint(anchor->width(), anchor->height()));
        pos = QPoint(bottomRight.x() - size.width(), bottomRight.y() + 2);
    } else {
        const QRect frame = window->frameGeometry();
        pos = frame.center() - QPoint(size.width() / 2, size.height() / 2);
    }
    QScreen *screen = window->windowHandle() ? window->windowHandle()->screen() : QGuiApplication::primaryScreen();
    if (screen) {
        const QRect area = screen->availableGeometry();
        pos.setX(qBound(area.left(), pos.x(), area.right() - size.width()));
        pos.setY(qBound(area.top(), pos.y(), area.bottom() - size.height()));
    }
    popup->move(pos);
    popup->show();
    popup->activateWindow();
    if (QAbstractButton *first = popup->m_themes->checkedButton())
        first->setFocus(Qt::PopupFocusReason);
}

PaneroQuickSettings::PaneroQuickSettings(QWidget *window)
    : QFrame(window, Qt::Popup)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(QStringLiteral("PaneroQuickSettings"));
    setWindowTitle(i18n("Quick Settings"));
    setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
    m_restartSignature = PaneroSettings::restartSignature();

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(12, 10, 12, 10);
    auto *title = new QLabel(i18n("Quick Settings"), this);
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    m_layout->addWidget(title);

    // ---- Light / Dark / System
    auto *themeRow = new QHBoxLayout;
    themeRow->setSpacing(0);
    m_themes = new QButtonGroup(this);
    const struct {
        int value;
        QString text;
        const char *icon;
    } themes[] = {{1, i18n("Light"), "weather-clear"}, {2, i18n("Dark"), "weather-clear-night"}, {0, i18n("System"), "preferences-desktop-theme"}};
    for (const auto &t : themes) {
        auto *button = new QToolButton(this);
        button->setText(t.text);
        button->setIcon(QIcon::fromTheme(QLatin1String(t.icon)));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setCheckable(true);
        button->setAutoRaise(false);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setChecked(PaneroSettings::theme() == t.value);
        button->setToolTip(t.value == 0 ? i18n("Follow the desktop's light/dark setting") : QString());
        m_themes->addButton(button, t.value);
        themeRow->addWidget(button);
    }
    m_layout->addLayout(themeRow);
    connect(m_themes, QOverload<int>::of(&QButtonGroup::buttonClicked), this, [this](int value) {
        if (value == PaneroSettings::theme())
            return;
        PaneroSettings::setTheme(value);
        PaneroSettings::applyLive(true);
        updateRestartRow();
    });

    m_form = new QFormLayout;
    m_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_layout->addLayout(m_form);

    // ---- icon theme (choosing one resets the folder colour to the theme's own)
    auto *iconTheme = new QComboBox(this);
    if (PaneroSettings::fillIconThemeCombo(iconTheme)) {
        m_iconTheme = iconTheme;
        m_iconTheme->setCurrentIndex(qMax(0, m_iconTheme->findData(PaneroSettings::iconTheme())));
        m_form->addRow(i18n("Icons:"), m_iconTheme);
        connect(m_iconTheme, QOverload<int>::of(&QComboBox::activated), this, [this]() {
            PaneroSettings::setIconTheme(m_iconTheme->currentData().toString());
            PaneroSettings::applyLive(true);
            updateRestartRow();
        });
    } else {
        delete iconTheme;
    }

    // ---- density
    m_density = new QComboBox(this);
    PaneroSettings::fillDensityCombo(m_density);
    const int current = PaneroSettings::densityIndex(PaneroSettings::rowHeight(), PaneroSettings::iconSize());
    if (current != 3)
        m_density->removeItem(3); // "Custom" only when the sizes were set in Settings
    m_density->setCurrentIndex(current);
    m_form->addRow(i18n("Density:"), m_density);
    connect(m_density, QOverload<int>::of(&QComboBox::activated), this, [](int index) {
        int height, size;
        if (!PaneroSettings::densityValues(index, &height, &size))
            return;
        PaneroSettings::setRowHeightAndIconSize(height, size);
        PaneroSettings::applyLive();
    });

    // ---- switches
    QCheckBox *hidden = addSwitch(i18n("Hidden files"), i18n("Show hidden files (Alt+.)"));
    hidden->setChecked(PaneroSettings::showHidden());
    connect(hidden, &QCheckBox::toggled, this, [](bool on) {
        PaneroSettings::applyShowHidden(on);
    });

    QCheckBox *drives = addSwitch(i18n("Drive buttons"), i18n("Drive buttons above each panel"));
    drives->setChecked(PaneroSettings::showDriveButtons());
    connect(drives, &QCheckBox::toggled, this, [](bool on) {
        PaneroSettings::setShowDriveButtons(on);
        PaneroSettings::applyLive();
    });

    QCheckBox *cloud = addSwitch(i18n("Cloud section"), i18n("The Cloud section in the sidebar"));
    cloud->setChecked(PaneroSettings::showCloud());
    connect(cloud, &QCheckBox::toggled, this, [](bool on) {
        PaneroSettings::setShowCloud(on);
        PaneroSettings::applyLive();
    });

    QCheckBox *thumbnails = addSwitch(i18n("Thumbnails"), i18n("Thumbnails instead of icons in the details view"));
    thumbnails->setChecked(PaneroSettings::detailsPreviews());
    connect(thumbnails, &QCheckBox::toggled, this, [](bool on) {
        PaneroSettings::applyDetailsPreviews(on);
    });

    // (Windows: the right-click menu choice Explorer / Panero is added here)

    // ---- restart (look changes)
    m_restartRow = new QWidget(this);
    auto *restartLayout = new QHBoxLayout(m_restartRow);
    restartLayout->setContentsMargins(0, 4, 0, 0);
    auto *restartText = new QLabel(i18n("Restart to apply the new look."), m_restartRow);
    restartText->setWordWrap(true);
    restartLayout->addWidget(restartText, 1);
    auto *restart = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("&Restart"), m_restartRow);
    restartLayout->addWidget(restart);
    connect(restart, &QPushButton::clicked, this, []() {
        Krusader::restart();
    });
    m_restartRow->hide();
    m_layout->addWidget(m_restartRow);

    // ---- all settings
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    m_layout->addWidget(line);
    auto *all = new QPushButton(QIcon::fromTheme(QStringLiteral("configure")), i18n("&All Settings..."), this);
    all->setFlat(true);
    all->setToolTip(i18n("The Settings window (Ctrl+,)"));
    all->setStyleSheet(QStringLiteral("text-align: left; padding: 4px;"));
    m_layout->addWidget(all);
    connect(all, &QPushButton::clicked, this, [this]() {
        close();
        // after the popup is gone (its event loop must not run the modal Settings window)
        QTimer::singleShot(0, SLOTS, []() {
            SLOTS->runKonfigurator(false, QStringLiteral("appearance"));
        });
    });
    setMinimumWidth(qMax(300, sizeHint().width()));
}

QCheckBox *PaneroQuickSettings::addSwitch(const QString &text, const QString &toolTip)
{
    auto *box = new QCheckBox(text, this);
    box->setToolTip(toolTip);
    m_form->addRow(QString(), box);
    return box;
}

void PaneroQuickSettings::updateRestartRow()
{
    m_restartRow->setVisible(PaneroSettings::restartSignature() != m_restartSignature);
    adjustSize();
}

void PaneroQuickSettings::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QFrame::keyPressEvent(event);
}
