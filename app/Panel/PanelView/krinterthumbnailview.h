/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KRINTERTHUMBNAILVIEW_H
#define KRINTERTHUMBNAILVIEW_H

#include "krinterview.h"

#include <QFont>
#include <QListView>

/**
 * Panero "Thumbnails" view: a grid of tiles (preview/icon above the file name).
 *
 * Previews come from KDE's KIO preview plugins installed on the system
 * (kio-extras, kdegraphics-thumbnailers, ffmpegthumbs, ...) and are generated
 * asynchronously by KrPreviews; files without a previewer show their normal
 * (coloured) mime type icon. Mouse and keyboard behaviour (marks, cursor,
 * hold-for-menu) is the same as in the other views; the arrow keys move in
 * the grid (Left/Right: previous/next tile, Up/Down: previous/next row).
 */
class KrInterThumbnailView : public QListView, public KrInterView
{
    Q_OBJECT
public:
    KrInterThumbnailView(QWidget *parent, KrViewInstance &instance, KConfig *cfg);
    ~KrInterThumbnailView() override;

    // ---- KrView ----
    int itemsPerPage() override;
    void updateView() override {}
    bool ensureVisibilityAfterSelect() override { return false; }
    void setSortMode(KrViewProperties::ColumnType sortColumn, bool descending) override;
    void setFileIconSize(int size) override;
    const QVector<int> &zoomSizes() const override
    {
        return thumbnailSizes;
    }

    // selections are handled by the mouse handler
    void setSelection(const QRect &, QItemSelectionModel::SelectionFlags) override {}
    void selectAll() override {}

protected slots:
    void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;
    void renameCurrentItem() override;

protected:
    bool handleKeyEvent(QKeyEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    bool event(QEvent *e) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *) override;

    void setup() override;
    void doRestoreSettings(KConfigGroup group) override;
    void copySettingsFrom(KrView *other) override;
    QRect itemRect(const FileItem *fileitem) override;
    void showContextMenu(const QPoint &) override {}

private:
    void updateGridSize();
    int columnsPerRow() const;
    void moveCurrentBy(int steps);

    QFont _viewFont;
};

#endif // KRINTERTHUMBNAILVIEW_H
