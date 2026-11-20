/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroviewmodeaction.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QToolBar>
#include <QToolButton>

#include <KI18n/KLocalizedString>

PaneroViewModeAction::PaneroViewModeAction(QAction *details, QAction *thumbnails, QObject *parent)
    : QWidgetAction(parent), m_details(details), m_thumbnails(thumbnails)
{
    setText(i18n("Details / Thumbnails"));
    setToolTip(i18n("Switch the active panel between the Details and Thumbnails views"));
    connect(this, &QAction::triggered, this, &PaneroViewModeAction::toggle);
}

void PaneroViewModeAction::toggle()
{
    if (!m_details || !m_thumbnails)
        return;
    // the view actions are an exclusive, checkable group that follows the active panel
    (m_details->isChecked() ? m_thumbnails : m_details)->trigger();
}

QWidget *PaneroViewModeAction::createWidget(QWidget *parent)
{
    // a framed pair of buttons that reads as one segmented control
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("panero_view_mode")); // rounded in the Panero style sheet
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setToolTip(toolTip());
    auto *layout = new QHBoxLayout(frame);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);

    auto makeButton = [&](QAction *source) {
        auto *button = new QToolButton(frame);
        button->setAutoRaise(true);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        if (source) {
            button->setIcon(source->icon());
            button->setText(source->iconText());
            button->setToolTip(i18n("%1 (click to switch views)", source->text().remove(QLatin1Char('&'))));
        }
        layout->addWidget(button);
        return button;
    };
    QToolButton *details = makeButton(m_details);
    QToolButton *thumbnails = makeButton(m_thumbnails);

    // follow the toolbar: icon size and text position (icons only by default)
    if (auto *bar = qobject_cast<QToolBar *>(parent)) {
        auto follow = [details, thumbnails, bar]() {
            for (QToolButton *button : {details, thumbnails}) {
                button->setIconSize(bar->iconSize());
                button->setToolButtonStyle(bar->toolButtonStyle());
                button->setMinimumSize(0, 0);
            }
            // both halves equally big, also with text ("Details" / "Thumbnails")
            const QSize a = details->sizeHint(), b = thumbnails->sizeHint();
            const QSize same(qMax(a.width(), b.width()), qMax(a.height(), b.height()));
            for (QToolButton *button : {details, thumbnails})
                button->setMinimumSize(same);
        };
        connect(bar, &QToolBar::iconSizeChanged, frame, follow);
        connect(bar, &QToolBar::toolButtonStyleChanged, frame, follow);
        follow();
    } else {
        for (QToolButton *button : {details, thumbnails})
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }

    auto sync = [this, details, thumbnails]() {
        const bool thumbs = m_thumbnails && m_thumbnails->isChecked();
        details->setChecked(!thumbs);
        thumbnails->setChecked(thumbs);
    };
    // any click switches to the other view; the checked state follows the panel
    for (QToolButton *button : {details, thumbnails})
        connect(button, &QToolButton::clicked, this, [this, sync]() {
            toggle();
            sync();
        });
    for (QAction *source : {m_details.data(), m_thumbnails.data()}) {
        if (source)
            connect(source, &QAction::changed, frame, sync);
    }
    sync();
    return frame;
}
