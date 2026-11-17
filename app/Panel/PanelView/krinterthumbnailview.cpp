/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "krinterthumbnailview.h"

#include "krmousehandler.h"
#include "krviewitem.h"
#include "krviewitemdelegate.h"
#include "listmodel.h"
#include "../../defaults.h"
#include "../../GUI/krstyleproxy.h"

#include <QDebug>
#include <QFontMetrics>
#include <QKeyEvent>

#include <KConfigCore/KConfig>
#include <KConfigCore/KConfigGroup>

/// Default thumbnail size in logical pixels (must be one of KrView::iconSizes)
static const int DefaultThumbnailSize = 64;

KrInterThumbnailView::KrInterThumbnailView(QWidget *parent, KrViewInstance &instance, KConfig *cfg)
    : QListView(parent)
    , KrInterView(instance, cfg, this)
{
    setWidget(this);
    setModel(_model);
    setSelectionMode(QAbstractItemView::NoSelection);
    setSelectionModel(new DummySelectionModel(_model, this));

    KConfigGroup grpSvr(_config, "Look&Feel");
    _viewFont = grpSvr.readEntry("Filelist Font", _FilelistFont);

    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setWordWrap(true);
    setTextElideMode(Qt::ElideMiddle);
    setSpacing(4);

    auto *style = new KrStyleProxy();
    style->setParent(this);
    setStyle(style);
    viewport()->setStyle(style);
    setItemDelegate(new KrViewItemDelegate(this));

    setMouseTracking(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);

    connect(_mouseHandler, &KrMouseHandler::renameCurrentItem, this, &KrInterThumbnailView::renameCurrentItem);
    _model->setExtensionEnabled(false); // tiles show the full file name
    _model->setThumbnailMode(true);
}

KrInterThumbnailView::~KrInterThumbnailView()
{
    delete _properties;
    _properties = nullptr;
    delete _operator;
    _operator = nullptr;
}

void KrInterThumbnailView::setup()
{
    setSortMode(_properties->sortColumn, (_properties->sortOptions & KrViewProperties::Descending));
}

void KrInterThumbnailView::doRestoreSettings(KConfigGroup group)
{
    KrInterView::doRestoreSettings(group);
    // the point of this view is previews: on unless the user switched them off here
    showPreviews(group.readEntry("ShowPreviews", true));
    if (fileIconSize() < 48) // thumbnails are at least 48 px (default 64)
        setFileIconSize(DefaultThumbnailSize);
    updateGridSize();
}

void KrInterThumbnailView::copySettingsFrom(KrView *other)
{
    if (other->instance() == instance()) {
        auto *v = dynamic_cast<KrInterThumbnailView *>(other);
        _model->sort(v->_model->lastSortOrder(), v->_model->lastSortDir());
        setFileIconSize(v->fileIconSize());
    }
}

void KrInterThumbnailView::setSortMode(KrViewProperties::ColumnType sortColumn, bool descending)
{
    _model->sort(sortColumn, descending ? Qt::DescendingOrder : Qt::AscendingOrder);
}

void KrInterThumbnailView::setFileIconSize(int size)
{
    KrView::setFileIconSize(size);
    setIconSize(QSize(fileIconSize(), fileIconSize()));
    updateGridSize();
}

void KrInterThumbnailView::updateGridSize()
{
    const int icon = fileIconSize() > 0 ? fileIconSize() : DefaultThumbnailSize;
    const QFontMetrics fm(_viewFont);
    // tile: thumbnail on top, two lines of (wrapped/elided) name below
    const int width = qMax(icon + 24, fm.averageCharWidth() * 14);
    const int height = icon + 2 * fm.lineSpacing() + 14;
    setGridSize(QSize(width, height));
    setIconSize(QSize(icon, icon));
    doItemsLayout();
}

int KrInterThumbnailView::columnsPerRow() const
{
    const int w = gridSize().width() + spacing();
    return qMax(1, w > 0 ? viewport()->width() / w : 1);
}

int KrInterThumbnailView::itemsPerPage()
{
    const int h = gridSize().height() + spacing();
    const int rows = qMax(1, h > 0 ? viewport()->height() / h : 1);
    return rows * columnsPerRow();
}

QRect KrInterThumbnailView::itemRect(const FileItem *item)
{
    return visualRect(_model->fileItemIndex(item));
}

void KrInterThumbnailView::moveCurrentBy(int steps)
{
    KrViewItem *item = getCurrentKrViewItem();
    if (!item)
        item = getFirst();
    if (!item)
        return;
    KrViewItem *target = item;
    for (int i = 0; i < qAbs(steps); ++i) {
        KrViewItem *next = steps > 0 ? getNext(target) : getPrev(target);
        if (!next)
            break;
        target = next;
    }
    setCurrentKrViewItem(target);
    makeItemVisible(target);
}

bool KrInterThumbnailView::handleKeyEvent(QKeyEvent *e)
{
    const Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
    if (mods == Qt::NoModifier) {
        switch (e->key()) {
        case Qt::Key_Left:
            moveCurrentBy(-1);
            return true;
        case Qt::Key_Right:
            moveCurrentBy(1);
            return true;
        case Qt::Key_Up:
            moveCurrentBy(-columnsPerRow());
            return true;
        case Qt::Key_Down:
            moveCurrentBy(columnsPerRow());
            return true;
        default:
            break;
        }
    }
    return KrView::handleKeyEvent(e);
}

void KrInterThumbnailView::keyPressEvent(QKeyEvent *e)
{
    if (!e || !_model->ready())
        return;
    if (handleKeyEvent(e))
        return;
    QListView::keyPressEvent(e);
}

void KrInterThumbnailView::currentChanged(const QModelIndex &current, const QModelIndex &previous)
{
    if (_model->ready()) {
        KrViewItem *item = getKrViewItem(currentIndex());
        op()->emitCurrentChanged(item);
    }
    QListView::currentChanged(current, previous);
}

void KrInterThumbnailView::renameCurrentItem()
{
    const QModelIndex nameIndex = _model->index(currentIndex().row(), KrViewProperties::Name);
    if (state() == QAbstractItemView::EditingState) {
        if (auto *delegate = dynamic_cast<KrViewItemDelegate *>(itemDelegate(nameIndex)))
            delegate->cycleEditorSelection();
        return;
    }
    edit(nameIndex);
    updateEditorData();
    update(nameIndex);
}

bool KrInterThumbnailView::event(QEvent *e)
{
    _mouseHandler->otherEvent(e);
    return QListView::event(e);
}

void KrInterThumbnailView::mousePressEvent(QMouseEvent *ev)
{
    if (!_mouseHandler->mousePressEvent(ev))
        QListView::mousePressEvent(ev);
}

void KrInterThumbnailView::mouseReleaseEvent(QMouseEvent *ev)
{
    if (!_mouseHandler->mouseReleaseEvent(ev))
        QListView::mouseReleaseEvent(ev);
}

void KrInterThumbnailView::mouseDoubleClickEvent(QMouseEvent *ev)
{
    if (!_mouseHandler->mouseDoubleClickEvent(ev))
        QListView::mouseDoubleClickEvent(ev);
}

void KrInterThumbnailView::mouseMoveEvent(QMouseEvent *ev)
{
    if (!_mouseHandler->mouseMoveEvent(ev))
        QListView::mouseMoveEvent(ev);
}

void KrInterThumbnailView::wheelEvent(QWheelEvent *ev)
{
    if (!_mouseHandler->wheelEvent(ev))
        QListView::wheelEvent(ev);
}

void KrInterThumbnailView::dragEnterEvent(QDragEnterEvent *ev)
{
    if (!_mouseHandler->dragEnterEvent(ev))
        QListView::dragEnterEvent(ev);
}

void KrInterThumbnailView::dragMoveEvent(QDragMoveEvent *ev)
{
    QListView::dragMoveEvent(ev);
    _mouseHandler->dragMoveEvent(ev);
}

void KrInterThumbnailView::dragLeaveEvent(QDragLeaveEvent *ev)
{
    if (!_mouseHandler->dragLeaveEvent(ev))
        QListView::dragLeaveEvent(ev);
}

void KrInterThumbnailView::dropEvent(QDropEvent *ev)
{
    if (!_mouseHandler->dropEvent(ev))
        QListView::dropEvent(ev);
}
