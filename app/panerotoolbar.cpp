/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerotoolbar.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QDir>
#include <QEvent>
#include <QMainWindow>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QWidget>

#include <KConfigCore/KConfigGroup>
#include <KConfigCore/KSharedConfig>
#include <KI18n/KLocalizedString>

PaneroToolBarSpacer::PaneroToolBarSpacer(QObject *parent)
    : QWidgetAction(parent)
{
    setText(QStringLiteral("Spacer"));
}

QWidget *PaneroToolBarSpacer::createWidget(QWidget *parent)
{
    auto *gap = new QWidget(parent);
    gap->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    gap->setAttribute(Qt::WA_TransparentForMouseEvents);
    return gap;
}

PaneroSidebarButtonAction::PaneroSidebarButtonAction(QAction *toggle, QDockWidget *dock, QObject *parent)
    : QWidgetAction(parent), m_toggle(toggle), m_dock(dock)
{
    setText(toggle->text());
    setIcon(toggle->icon());
    setToolTip(toggle->toolTip());
    connect(this, &QAction::triggered, toggle, &QAction::trigger);
    dock->installEventFilter(this);
}

static bool headerStyle()
{
    return KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("Panero Appearance"))
               .readEntry("Sidebar Button", QStringLiteral("icon")) == QLatin1String("header");
}

QWidget *PaneroSidebarButtonAction::createWidget(QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setIcon(m_toggle->icon());
    button->setToolTip(m_toggle->toolTip());
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // header style content: [icon] Places ........ «
    auto *row = new QHBoxLayout(button);
    row->setContentsMargins(6, 0, 8, 0);
    row->setSpacing(6);
    auto *icon = new QLabel(button);
    auto *title = new QLabel(i18nc("sidebar header button", "Places"), button);
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    auto *chevron = new QLabel(button);
    for (QLabel *label : {icon, title, chevron})
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    icon->setObjectName(QStringLiteral("icon"));
    title->setObjectName(QStringLiteral("title"));
    chevron->setObjectName(QStringLiteral("chevron"));
    row->addWidget(icon);
    row->addWidget(title);
    row->addStretch(1);
    row->addWidget(chevron);

    connect(button, &QToolButton::clicked, m_toggle, &QAction::trigger);
    connect(m_toggle, &QAction::changed, button, [this, button]() { sync(button); });
    if (auto *bar = qobject_cast<QToolBar *>(parent)) {
        button->setIconSize(bar->iconSize());
        connect(bar, &QToolBar::iconSizeChanged, button, [this, button](const QSize &size) {
            button->setIconSize(size);
            sync(button);
        });
    }
    m_buttons.append(button);
    connect(button, &QObject::destroyed, this, [this, button]() { m_buttons.removeAll(button); });
    QTimer::singleShot(0, button, [this, button]() { sync(button); });
    return button;
}

void PaneroSidebarButtonAction::sync(QToolButton *button) const
{
    const bool shown = m_toggle->isChecked();
    auto *icon = button->findChild<QLabel *>(QStringLiteral("icon"));
    auto *title = button->findChild<QLabel *>(QStringLiteral("title"));
    auto *chevron = button->findChild<QLabel *>(QStringLiteral("chevron"));
    const bool header = headerStyle();
    for (QLabel *label : {icon, title, chevron})
        label->setVisible(header);
    if (!header) {
        button->setIcon(m_toggle->icon());
        button->setChecked(shown);
        button->setMinimumWidth(0);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
        return;
    }
    // the header is not a pressed button; the sidebar under it shows the state
    button->setIcon(QIcon());
    button->setChecked(false);
    icon->setPixmap(m_toggle->icon().pixmap(button->iconSize()));
    title->setVisible(shown);
    chevron->setText(shown ? QStringLiteral("\u00AB") : QStringLiteral("\u00BB"));
    QFont big = chevron->font();
    big.setPointSizeF(QFont().pointSizeF() * 1.3);
    chevron->setFont(big);
    button->setMinimumHeight(button->iconSize().height() + 8);
    if (shown) {
        // as wide as the sidebar below it (from the button to the dock's right edge)
        const QWidget *window = button->window();
        const int left = button->mapTo(window, QPoint(0, 0)).x();
        const int right = m_dock->mapTo(window, QPoint(m_dock->width(), 0)).x();
        button->setFixedWidth(qMax(120, right - left - 2));
    } else {
        button->setFixedWidth(button->iconSize().width() + 40);
    }
}

bool PaneroSidebarButtonAction::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_dock && (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
        for (QToolButton *button : qAsConst(m_buttons))
            sync(button);
    }
    return QWidgetAction::eventFilter(watched, event);
}

void PaneroToolBar::migrateLayout()
{
    KConfigGroup panero(KSharedConfig::openConfig(), QStringLiteral("Panero"));
    if (panero.readEntry("Toolbar Layout", 0) >= LayoutVersion)
        return;

    const QString local = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/kxmlgui5/panero/paneroui.rc");
    QFile file(local);
    if (file.open(QIODevice::ReadOnly)) {
        QString xml = QString::fromUtf8(file.readAll());
        file.close();
        const QRegularExpression version(QStringLiteral("<gui[^>]*\\bversion=\"(\\d+)\""));
        const QRegularExpressionMatch m = version.match(xml);
        const int localVersion = m.hasMatch() ? m.captured(1).toInt() : 0;
        if (localVersion < LayoutVersion) {
            QFile::remove(local + QStringLiteral(".bak"));
            QFile::copy(local, local + QStringLiteral(".bak"));
            xml.remove(QRegularExpression(QStringLiteral("\\s*<ToolBar\\b.*?</ToolBar>"),
                                          QRegularExpression::DotMatchesEverythingOption));
            xml.remove(QRegularExpression(QStringLiteral("\\s*<ToolBar\\b[^>]*/>")));
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.write(xml.toUtf8());
                file.close();
            }
        }
    }
    // saved icon size / text position of the old layout
    KSharedConfig::openConfig()->deleteGroup(QStringLiteral("Main Toolbar"));
    KSharedConfig::openConfig()->deleteGroup(QStringLiteral("View Toolbar"));
    // saved toolbar positions/rows of the old layout (window state)
    KConfigGroup startup(KSharedConfig::openConfig(), QStringLiteral("Startup"));
    startup.deleteEntry("State");
    panero.writeEntry("Toolbar Layout", LayoutVersion);
    panero.sync();
}
