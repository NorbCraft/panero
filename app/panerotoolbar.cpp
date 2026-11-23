/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerotoolbar.h"

#include <QDir>
#include <QToolBar>
#include <QToolButton>
#include <QFile>
#include <QFileInfo>
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
    button->setText(m_toggle->iconText());
    // twice as wide as a normal toolbar button: the sidebar toggle stands out; it
    // follows the toolbar's icon size and text position like the other buttons
    auto *bar = qobject_cast<QToolBar *>(parent);
    auto resize = [button, bar]() {
        if (bar) {
            button->setIconSize(bar->iconSize());
            button->setToolButtonStyle(bar->toolButtonStyle());
        }
        button->setMinimumWidth(0);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
        const QSize hint = button->sizeHint();
        const int iconOnly = 2 * (button->iconSize().height() + 10);
        button->setFixedWidth(qMax(hint.width(), iconOnly));
    };
    if (bar) {
        connect(bar, &QToolBar::iconSizeChanged, button, resize);
        connect(bar, &QToolBar::toolButtonStyleChanged, button, resize);
    }
    resize();
    return button;
}

void PaneroToolBar::migrateLayout()
{
    KConfigGroup panero(KSharedConfig::openConfig(), QStringLiteral("Panero"));
    if (panero.readEntry("Toolbar Layout", 0) >= LayoutVersion)
        return;

    const QString relative = QStringLiteral("kxmlgui5/panero/paneroui.rc");
    const QString local = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/') + relative;
    QFile file(local);
    if (file.open(QIODevice::ReadOnly)) {
        const QString localXml = QString::fromUtf8(file.readAll());
        file.close();
        const QRegularExpression version(QStringLiteral("<gui[^>]*\\bversion=\"(\\d+)\""));
        const QRegularExpressionMatch m = version.match(localXml);
        if ((m.hasMatch() ? m.captured(1).toInt() : 0) < LayoutVersion) {
            QFile::remove(local + QStringLiteral(".bak"));
            QFile::copy(local, local + QStringLiteral(".bak"));
            // KXMLGUI would keep the old file's toolbars: rebuild the local file from the
            // installed one and carry over only the user's shortcuts (ActionProperties)
            QString shipped;
            for (const QString &path : QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, relative)) {
                if (QFileInfo(path).canonicalFilePath() != QFileInfo(local).canonicalFilePath()) {
                    QFile f(path);
                    if (f.open(QIODevice::ReadOnly))
                        shipped = QString::fromUtf8(f.readAll());
                    break;
                }
            }
            const QRegularExpression properties(QStringLiteral("<ActionProperties\\b.*?</ActionProperties>"),
                                                QRegularExpression::DotMatchesEverythingOption);
            const QRegularExpressionMatch userProperties = properties.match(localXml);
            if (!shipped.isEmpty() && userProperties.hasMatch()) {
                if (properties.match(shipped).hasMatch())
                    shipped.replace(properties, userProperties.captured(0));
                else
                    shipped.replace(QStringLiteral("</gui>"), userProperties.captured(0) + QStringLiteral("\n</gui>"));
            }
            if (!shipped.isEmpty() && userProperties.hasMatch() && file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.write(shipped.toUtf8());
                file.close();
            } else {
                QFile::remove(local); // nothing to keep: use the installed file
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

void PaneroToolBar::migrateSettings()
{
    KConfigGroup panero(KSharedConfig::openConfig(), QStringLiteral("Panero"));
    const int version = panero.readEntry("Settings Version", 0);
    if (version < 4) {
        // preview 4: no check badge on marked files by default (the dialog used to save it)
        KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("Panero Appearance")).deleteEntry("Mark Indicator");
    }
#ifdef Q_OS_WIN
    if (version < 5) {
        // preview 13: up to preview 11 the Cloud option was hidden and off on
        // Windows, and Quick Settings saved "Show Cloud=false" on every OK; the
        // Cloud section (preview 12) then stayed hidden. Back to the default.
        KConfigGroup look(KSharedConfig::openConfig(), QStringLiteral("Panero Appearance"));
        if (look.hasKey("Show Cloud") && !look.readEntry("Show Cloud", true))
            look.deleteEntry("Show Cloud");
    }
#endif
    if (version < 5) {
        panero.writeEntry("Settings Version", 5);
        panero.sync();
    }
}
