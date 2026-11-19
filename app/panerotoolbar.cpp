/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerotoolbar.h"

#include <QDir>
#include <QToolBar>
#include <QToolButton>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QWidget>

#include <KConfigCore/KConfigGroup>
#include <KConfigCore/KSharedConfig>

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

PaneroSidebarButtonAction::PaneroSidebarButtonAction(QAction *toggle, QObject *parent)
    : QWidgetAction(parent), m_toggle(toggle)
{
    setText(toggle->text());
    setIcon(toggle->icon());
    setToolTip(toggle->toolTip());
    connect(this, &QAction::triggered, toggle, &QAction::trigger);
}

QWidget *PaneroSidebarButtonAction::createWidget(QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIcon(m_toggle->icon());
    button->setToolTip(m_toggle->toolTip());
    button->setChecked(m_toggle->isChecked());
    connect(button, &QToolButton::clicked, m_toggle, &QAction::trigger);
    connect(m_toggle, &QAction::toggled, button, &QToolButton::setChecked);
    // twice as wide as a normal toolbar button: the sidebar toggle stands out
    auto resize = [button](const QSize &iconSize) {
        button->setIconSize(iconSize);
        button->setFixedWidth(2 * button->sizeHint().height());
    };
    if (auto *bar = qobject_cast<QToolBar *>(parent)) {
        connect(bar, &QToolBar::iconSizeChanged, button, resize);
        resize(bar->iconSize());
    } else {
        resize(button->iconSize());
    }
    return button;
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
