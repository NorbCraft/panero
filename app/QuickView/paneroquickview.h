/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROQUICKVIEW_H
#define PANEROQUICKVIEW_H

#include "paneroquickviewdata.h"

#include <QDateTime>
#include <QPointer>
#include <QUrl>
#include <QWidget>

#include <memory>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QuickImageView;
class QuickTextView;
class QMediaPlayer;
class QSlider;
class QVideoWidget;
class QFormLayout;
class QTimer;
namespace KSyntaxHighlighting
{
class SyntaxHighlighter;
}

/**
 * Quick View (D-022, Total Commander's Ctrl+Q): shows the item under the
 * cursor of the active panel in place of the other panel. Images, text and
 * code, folder sizes, archive contents, PDF pages, audio/video, file info and
 * a hex view. Files are read only in worker threads (D-018); on cloud and
 * network mounts and for online-only placeholders nothing is read until the
 * user asks for it (Space or the Load button).
 */
class PaneroQuickView : public QWidget
{
    Q_OBJECT
public:
    struct Item {
        QUrl url;
        QString name;
        QString iconName;
        bool isDir = false;
        bool isParent = false; ///< ".."
        qint64 size = -1;      ///< from the listing (-1: unknown)
        QDateTime modified;
    };

    explicit PaneroQuickView(QWidget *parent = nullptr);
    ~PaneroQuickView() override;

    /// Shows @p item; loading runs in the background. Same URL: nothing happens.
    void showItem(const Item &item);
    /// Stops loading and playback and shows the empty page.
    void clear();
    QUrl currentUrl() const { return m_item.url; }
    /// A cloud/network file is shown with "Press Space to load" and not loaded yet.
    bool waitingForLoad() const { return m_waiting; }
    /// Loads the waiting cloud/network file (Space or a click).
    void loadNow();
    /// Gives the keyboard focus to the content (text, image, list, player).
    void focusContent();
    /// The content handles Esc itself (find bar open).
    bool wantsEscape() const;
    /// The page on screen: "empty", "waiting", "loading", "image", "text", "folder",
    /// "archive", "pdf", "media", "info" (used by tests and the GUI test log).
    QString pageName() const;

signals:
    void closeRequested();
    void openRequested(const QUrl &url);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    struct Loaded;
    void startLoad(bool force);
    void showLoaded(const std::shared_ptr<Loaded> &loaded);
    void showWaiting();
    void showInfo(const QString &note = QString());
    void startFolderStats();
    void showFolderStats(const PaneroQuickViewData::FolderStats &stats);
    void showPdfPage(int index);
    void requestHex();
    void setDetails(const QString &text);
    void setPage(QWidget *page);
    void cancelWork();
    void stopMedia();
    void openFind();
    void findNext(bool backwards);
    void updateHeader();
    QString localPath() const;
    template<typename Work, typename Done>
    void runAsync(Work work, Done done);

    QWidget *buildEmptyPage();
    QWidget *buildWaitingPage();
    QWidget *buildTextPage();
    QWidget *buildFolderPage();
    QWidget *buildArchivePage();
    QWidget *buildPdfPage();
    QWidget *buildMediaPage();
    QWidget *buildInfoPage();

    Item m_item;
    std::shared_ptr<Loaded> m_loaded;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    bool m_waiting = false;
    bool m_forced = false; ///< a cloud file the user asked to load

    // header
    QLabel *m_icon = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_details = nullptr;
    QToolButton *m_close = nullptr;
    QStackedWidget *m_stack = nullptr;

    // pages
    QWidget *m_emptyPage = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QWidget *m_waitingPage = nullptr;
    QLabel *m_waitingText = nullptr;
    QPushButton *m_loadButton = nullptr;
    QWidget *m_loadingPage = nullptr;
    QuickImageView *m_image = nullptr;
    QWidget *m_textPage = nullptr;
    QuickTextView *m_text = nullptr;
    QWidget *m_findBar = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QLabel *m_findStatus = nullptr;
    KSyntaxHighlighting::SyntaxHighlighter *m_highlighter = nullptr;
    QWidget *m_folderPage = nullptr;
    QLabel *m_folderSummary = nullptr;
    QTreeWidget *m_folderList = nullptr;
    QPushButton *m_folderStop = nullptr;
    QWidget *m_archivePage = nullptr;
    QLabel *m_archiveSummary = nullptr;
    QTreeWidget *m_archiveList = nullptr;
    QWidget *m_pdfPage = nullptr;
    QLabel *m_pdfLabel = nullptr;
    QLabel *m_pdfImage = nullptr;
    QScrollArea *m_pdfScroll = nullptr;
    QToolButton *m_pdfPrev = nullptr;
    QToolButton *m_pdfNext = nullptr;
    int m_pdfIndex = 0;
    int m_pdfCount = 0;
    QWidget *m_mediaPage = nullptr;
    QLabel *m_mediaIcon = nullptr;
    QToolButton *m_mediaPlay = nullptr;
    QSlider *m_mediaSlider = nullptr;
    QLabel *m_mediaTime = nullptr;
    QMediaPlayer *m_player = nullptr;
    QVideoWidget *m_video = nullptr;
    QWidget *m_infoPage = nullptr;
    QFormLayout *m_infoForm = nullptr;
    QLabel *m_infoNote = nullptr;
    QPushButton *m_hexButton = nullptr;
    QPushButton *m_openButton = nullptr;
    QPlainTextEdit *m_hex = nullptr;
    bool m_folderRunning = false;
};

#endif // PANEROQUICKVIEW_H
