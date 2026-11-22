/*
    SPDX-FileCopyrightText: 2009 Csaba Karai <cskarai@freemail.hu>
    SPDX-FileCopyrightText: 2009-2022 Krusader Krew <https://krusader.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "krviewitemdelegate.h"

#include "krviewproperties.h"
#include "listmodel.h"
#include "../../defaults.h"
#include "../panerotheme.h"

#include <QAbstractItemView>
#include <QTreeView>
#include <QHeaderView>
#include <QListView>
#include <QTextLayout>
#include "../krglobal.h"
#include "../listpanel.h"
#include "../krcolorcache.h"
#include "../../compat.h"

// QtCore
#include <QDebug>
// QtGui
#include <QKeyEvent>
#include <QPainter>
// QtWidgets
#include <QDialog>
#include <QApplication>
#include <QLineEdit>

#include <KConfigCore/KSharedConfig>

KrViewItemDelegate::KrViewItemDelegate(QObject *parent) :
    QItemDelegate(parent), _currentlyEdited(-1), _dontDraw(false), _editor(nullptr) {}

void KrViewItemDelegate::paintTile(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    const QRect r = option.rect;
    const QVariant bg = index.data(Qt::BackgroundRole); // the model returns the mark colour for marked items
    if (bg.canConvert<QBrush>()) {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(bg.value<QBrush>());
        painter->drawRoundedRect(QRectF(r).adjusted(1, 1, -1, -1), 4, 4);
    }

    // preview / icon, centred at the top
    const QSize iconSize = option.decorationSize;
    const QRect iconRect(r.left() + (r.width() - iconSize.width()) / 2, r.top() + 6, iconSize.width(), iconSize.height());
    const QVariant deco = index.data(Qt::DecorationRole);
    QPixmap pixmap;
    if (deco.canConvert<QIcon>())
        pixmap = deco.value<QIcon>().pixmap(iconSize);
    else if (deco.canConvert<QPixmap>())
        pixmap = deco.value<QPixmap>();
    if (!pixmap.isNull()) {
        const QSize ps = pixmap.size() / pixmap.devicePixelRatio();
        const QRect target(iconRect.left() + (iconRect.width() - ps.width()) / 2,
                           iconRect.top() + (iconRect.height() - ps.height()) / 2, ps.width(), ps.height());
        painter->drawPixmap(target, pixmap);
        if (PaneroTheme::usePaneroColors() && PaneroTheme::markIndicator() && index.data(ListModel::MarkedRole).toBool())
            PaneroTheme::paintMarkIndicator(painter, QRectF(target));
    }

    // name: up to two lines, wrapping anywhere, elided at the end of the last line
    if (!(_dontDraw || _currentlyEdited == index.row())) {
        const QString text = index.data(Qt::DisplayRole).toString();
        const QRect textRect(r.left() + 4, iconRect.bottom() + 4, r.width() - 8, r.bottom() - iconRect.bottom() - 6);
        QVariant fg = index.data(Qt::ForegroundRole);
        painter->setPen(fg.canConvert<QBrush>() ? fg.value<QBrush>().color() : option.palette.color(QPalette::Text));
        // the file list font comes from the model (Qt::FontRole), as in the other views
        const QVariant fontData = index.data(Qt::FontRole);
        const QFont font = fontData.canConvert<QFont>() ? fontData.value<QFont>() : option.font;
        painter->setFont(font);
        const QFontMetrics fm(font);
        QTextLayout layout(text, font);
        QTextOption to(Qt::AlignHCenter);
        to.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(to);
        layout.beginLayout();
        QStringList lines;
        const int maxLines = qMax(1, textRect.height() / fm.lineSpacing());
        while (lines.size() < maxLines) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(textRect.width());
            if (lines.size() == maxLines - 1)
                lines << fm.elidedText(text.mid(line.textStart()), Qt::ElideRight, textRect.width());
            else
                lines << text.mid(line.textStart(), line.textLength());
        }
        layout.endLayout();
        int y = textRect.top();
        for (const QString &line : qAsConst(lines)) {
            painter->drawText(QRect(textRect.left(), y, textRect.width(), fm.lineSpacing()), Qt::AlignHCenter | Qt::AlignTop, line);
            y += fm.lineSpacing();
        }
    }
    painter->restore();
}

void KrViewItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (const auto *list = qobject_cast<const QListView *>(option.widget)) {
        if (list->viewMode() == QListView::IconMode) {
            _dontDraw = false;
            paintTile(painter, option, index);
            if (PaneroTheme::usePaneroColors() && list->currentIndex() == index)
                PaneroTheme::paintCurrentFrame(painter, option.rect, index.data(ListModel::ActivePanelRole).toBool());
            return;
        }
    }
    QStyleOptionViewItem opt = option;
    opt.state &= ~QStyle::State_Selected;
    const bool panero = PaneroTheme::usePaneroColors();
    if (panero) {
        // Panero draws a solid cursor-row frame instead of the style's dotted focus rect
        opt.state &= ~QStyle::State_HasFocus;
    }
    _dontDraw = (_currentlyEdited == index.row()) && (index.column() == KrViewProperties::Ext);
    _paintIndex = index;
    _decorationDrawn = false;
    if (panero) {
        // leave room for the cursor accent bar in the first visible column of the detailed view
        const auto *tree = qobject_cast<const QTreeView *>(option.widget);
        if (tree) {
            const QHeaderView *header = tree->header();
            int first = -1;
            for (int v = 0; v < header->count() && first < 0; ++v) {
                const int logical = header->logicalIndex(v);
                if (!header->isSectionHidden(logical))
                    first = logical;
            }
            if (index.column() == first) {
                const QVariant bg = index.data(Qt::BackgroundRole);
                if (bg.canConvert<QBrush>())
                    painter->fillRect(option.rect, bg.value<QBrush>());
                opt.rect.setLeft(opt.rect.left() + 5);
            }
        }
    }
    QItemDelegate::paint(painter, opt, index);
    _paintIndex = QModelIndex();

    // Item-based views (brief/thumbnail mode) draw the cursor frame per item;
    // the detailed view draws it across the whole row in drawRow().
    if (panero) {
        const auto *view = qobject_cast<const QAbstractItemView *>(option.widget);
        if (view && !qobject_cast<const QTreeView *>(view) && view->currentIndex() == index)
            PaneroTheme::paintCurrentFrame(painter, option.rect, index.data(ListModel::ActivePanelRole).toBool());
    }
}

void KrViewItemDelegate::drawDecoration(QPainter *painter, const QStyleOptionViewItem &option, const QRect &rect,
                                        const QPixmap &pixmap) const
{
    QItemDelegate::drawDecoration(painter, option, rect, pixmap);
    if (pixmap.isNull() || !_paintIndex.isValid())
        return;
    _decorationDrawn = true;
    if (PaneroTheme::usePaneroColors() && PaneroTheme::markIndicator()
            && _paintIndex.data(ListModel::MarkedRole).toBool())
        PaneroTheme::paintMarkIndicator(painter, QRectF(rect));
}

void KrViewItemDelegate::drawDisplay(QPainter * painter, const QStyleOptionViewItem & option, const QRect & rect, const QString & text) const
{
    if (_dontDraw)
        return;
    QRect textRect = rect;
    // no icon shown (icons disabled): put the mark indicator in front of the name
    if (!_decorationDrawn && _paintIndex.isValid() && _paintIndex.column() == KrViewProperties::Name
            && PaneroTheme::usePaneroColors() && PaneroTheme::markIndicator()
            && _paintIndex.data(ListModel::MarkedRole).toBool()) {
        const int d = qMin(rect.height(), option.fontMetrics.height());
        PaneroTheme::paintMarkIndicator(painter, QRectF(rect.left(), rect.center().y() - d / 2.0, d, d));
        textRect.setLeft(rect.left() + d + 3);
    }
    QItemDelegate::drawDisplay(painter, option, textRect, text);
}

QWidget * KrViewItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &sovi, const QModelIndex &index) const
{
    _currentlyEdited = index.row();
    _editor = QItemDelegate::createEditor(parent, sovi, index);
    return _editor;
}

void KrViewItemDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    QItemDelegate::setEditorData(editor, index);
    auto *lineEdit = qobject_cast<QLineEdit *> (editor);
    if (lineEdit) {
        KConfigGroup gl(krConfig, "Look&Feel");
        QFont font = index.data(Qt::FontRole).value<QFont>();
        lineEdit->setFont(font);
        if (gl.readEntry("Rename Selects Extension", _RenameSelectsExtension))
            lineEdit->selectAll();
        else {
            QString nameWithoutExt = index.data(Qt::UserRole).toString();
            lineEdit->deselect();
            lineEdit->setSelection(0, nameWithoutExt.length());
        }

        KrColorSettings colorSettings;

        if (!colorSettings.getBoolValue("KDE Default")) {
            QPalette renamePalette = lineEdit->palette();

            if (!colorSettings.getColorTextValue("Rename Foreground").isEmpty())
                renamePalette.setColor(QPalette::Text, colorSettings.getColorValue("Rename Foreground"));

            if (!colorSettings.getColorTextValue("Rename Background").isEmpty())
                renamePalette.setColor(QPalette::Base, colorSettings.getColorValue("Rename Background"));

            lineEdit->setPalette(renamePalette);
        }
    }
}

QSize KrViewItemDelegate::sizeHint(const QStyleOptionViewItem & option, const QModelIndex & index) const
{
    QSize size = QItemDelegate::sizeHint(option, index);
    if (size.isEmpty()) {
        // prevent items without text from bloating the view vertically
        return QSize(0, 0);
    }
    // thumbnails grid: every tile has the grid size
    if (const auto *list = qobject_cast<const QListView *>(option.widget)) {
        if (list->viewMode() == QListView::IconMode && list->gridSize().isValid())
            return list->gridSize() - QSize(2 * list->spacing(), 2 * list->spacing());
    }
    // Panero: adjustable row height in the detailed view (never below what the
    // font and the icon need, so nothing is clipped)
    if (qobject_cast<const QTreeView *>(option.widget)) {
        const bool hasIcon = !index.data(Qt::DecorationRole).isNull();
        const int iconSize = hasIcon ? option.decorationSize.height() : 0;
        size.setHeight(PaneroTheme::effectiveRowHeight(option.fontMetrics, iconSize,
                                                       PaneroTheme::configuredRowHeight()));
    }
    return size;
}

bool KrViewItemDelegate::eventFilter(QObject *object, QEvent *event)
{
    QWidget *editor = qobject_cast<QWidget*>(object);
    if (!editor)
        return false;
    if (event->type() == QEvent::KeyPress) {
        switch (dynamic_cast<QKeyEvent *>(event)->key()) {
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            onEditorClose();
            emit closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
            return true;
        case Qt::Key_Enter:
        case Qt::Key_Return:
            if (auto *e = qobject_cast<QLineEdit*>(editor)) {
                if (!e->hasAcceptableInput())
                    return true;
                event->accept();
                emit commitData(editor);
                emit closeEditor(editor, QAbstractItemDelegate::SubmitModelCache);
                onEditorClose();
                return true;
            }
            return false;
        case Qt::Key_Escape:
            event->accept();
            // don't commit data
            onEditorClose();
            emit closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
            break;
        default:
            return false;
        }

        if (editor->parentWidget())
            editor->parentWidget()->setFocus();
        return true;
    } else if (event->type() == QEvent::FocusOut) {
        if (!editor->isActiveWindow() || (QApplication::focusWidget() != editor)) {
            QWidget *w = QApplication::focusWidget();
            while (w) { // don't worry about focus changes internally in the editor
                if (w == editor)
                    return false;
                w = w->parentWidget();
            }
            // Opening a modal dialog will start a new eventloop
            // that will process the deleteLater event.
            if (QApplication::activeModalWidget()
                    && !QApplication::activeModalWidget()->isAncestorOf(editor)
                    && qobject_cast<QDialog*>(QApplication::activeModalWidget()))
                return false;
            onEditorClose();
            // manually set focus back to panel after rename canceled by focusing another window
            ACTIVE_PANEL->gui->slotFocusOnMe();
            emit closeEditor(editor, RevertModelCache);
        }
    } else if (event->type() == QEvent::ShortcutOverride) {
        const QKeyEvent *ke = dynamic_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape ||
            (ke->key() == Qt::Key_Backspace && ke->modifiers() == Qt::ControlModifier)) {
            event->accept();
            return true;
        }
    }
    return false;
}

//! Helper class to represent an editor selection
class EditorSelection : public QPair<int, int>
{
public:
    EditorSelection(int start, int length) : QPair<int, int>(start, length) {}

    int start() const { return first; }
    int length() const { return second; }
};

//! Generate helpful file name selections: full name (always present), name candidates, extension candidates
static QList<EditorSelection> generateFileNameSelections(const QString &text)
{
    auto selections = QList<EditorSelection>();
    auto length = text.length();
    auto parts = text.split('.');

    // append full selection
    selections.append(EditorSelection(0, length));

    // append forward selections
    int selectionLength = 0;
    bool isFirstPart = true;
    for (auto part : parts) {
        // if the part is not the first one, we need to add one character to account for the dot
        selectionLength += part.length() + !isFirstPart;
        isFirstPart = false;
        // if we reached the full length, don't add the selection, since it's a full selection
        if (selectionLength == length)
            break;

        // don't add empty selections (could happen if the full name starts with a dot)
        if (selectionLength > 0)
            selections.append(EditorSelection(0, selectionLength));
    }

    // append backward selections
    std::reverse(parts.begin(), parts.end());
    selectionLength = 0;
    isFirstPart = true;
    for (auto part : parts) {
        // if the part is not the first one, we need to add one character to account for the dot
        selectionLength += part.length() + !isFirstPart;
        isFirstPart = false;
        // if we reached the full length, don't add the selection, since it's a full selection
        if (selectionLength == length)
            break;

        // don't add empty selections (could happen if the full name ends with a dot)
        if (selectionLength > 0)
            selections.append(EditorSelection(length - selectionLength, selectionLength));
    }

    return selections;
}

void KrViewItemDelegate::cycleEditorSelection()
{
    auto editor = qobject_cast<QLineEdit *>(_editor);
    if (!editor) {
        qWarning() << "Unable to cycle through editor selections due to a missing or unsupported type of item editor" << _editor;
        return;
    }

    EditorSelection currentSelection(editor->selectionStart(), editor->selectionLength());
    auto text = editor->text();
    const auto selections = generateFileNameSelections(text);

    // try to find current selection in the list
    int currentIndex = 0;
    for (auto selection : selections) {
        if (selection == currentSelection)
            break;
        currentIndex++;
    }

    // if we found current selection, pick the next in the cycle
    auto selectionCount = selections.length();
    if (currentIndex < selections.length())
        currentIndex = (currentIndex + 1) % selectionCount;
    // otherwise pick the first one - the full selection
    else
        currentIndex = 0;

    // set the selection
    auto selection = selections[currentIndex];
    qDebug() << "setting selection" << selection << "index" << currentIndex;
    editor->setSelection(selection.start(), selection.length());
}
