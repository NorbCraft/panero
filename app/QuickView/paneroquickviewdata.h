/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROQUICKVIEWDATA_H
#define PANEROQUICKVIEWDATA_H

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>

/**
 * Quick View (D-022): everything that reads a file. These functions run in a
 * worker thread (never in the GUI thread, D-018) and have no widgets, so they
 * can be unit-tested. The widget side (PaneroQuickView) only shows results.
 */
namespace PaneroQuickViewData
{
enum class Kind {
    None,    ///< nothing to show (no item, "..")
    Image,
    Text,
    Folder,
    Archive,
    Pdf,
    Media,
    Other,   ///< file info, hex view on request
};

/// Text: the first 2 MB are shown.
constexpr qint64 TextLimit = 2 * 1024 * 1024;
/// Hex view: the first 64 KB.
constexpr qint64 HexLimit = 64 * 1024;
/// Archive listing: at most this many entries.
constexpr int ArchiveLimit = 20000;

/// Which viewer fits a file of MIME type @p mimeName. @p head (the first few
/// KB, may be empty) decides for unknown types: no NUL bytes = text.
Kind classify(const QString &mimeName, bool isDir, const QByteArray &head = QByteArray());
/// The MIME type of a local file (name and content). Reads the file.
QString mimeTypeOf(const QString &path);
/// True when this build can show PDF pages (Poppler).
bool pdfSupported();
/// True for archive MIME types listArchive() understands.
bool isArchiveMime(const QString &mimeName);

// ----- text -----
/// The text encoding of @p data: a BOM, valid UTF-8, then KEncodingProber;
/// falls back to ISO-8859-1. Returns a QTextCodec name ("UTF-8", ...).
QByteArray detectEncoding(const QByteArray &data);
/// Decodes @p data with detectEncoding(); @p encoding receives the name.
QString decodeText(const QByteArray &data, QByteArray *encoding = nullptr);

struct TextResult {
    QString text;
    QByteArray encoding;
    qint64 fileSize = 0;
    bool truncated = false;   ///< file larger than the limit
    QString error;
};
TextResult loadText(const QString &path, qint64 limit = TextLimit);

// ----- images -----
struct ImageResult {
    QImage image;             ///< first frame, scaled down to at most maxSide
    QSize originalSize;
    QByteArray format;        ///< "jpeg", "png", ...
    QByteArray animationData; ///< the whole file when animated (played with QMovie)
    int frameCount = 1;
    QString exifDate;         ///< "2024:05:01 12:34:56" when the image has one
    QString error;
};
ImageResult loadImage(const QString &path, int maxSide = 4096);
/// The EXIF date (DateTimeOriginal, else DateTime) of a JPEG file's bytes.
QString exifDate(const QByteArray &jpeg);

// ----- folders -----
struct FolderEntry {
    QString name;
    qint64 size = 0;
    bool isDir = false;
};
struct FolderStats {
    qint64 totalSize = 0;
    qint64 files = 0;
    qint64 folders = 0;
    int errors = 0;            ///< folders that could not be read
    QVector<FolderEntry> largest; ///< the biggest items directly inside, biggest first
    bool finished = false;
    bool cancelled = false;
};
/// Walks @p path (no symlinks followed). @p progress gets intermediate totals
/// about every 250 ms (from the calling thread). Stops early when @p cancel is set.
FolderStats folderStats(const QString &path, const std::atomic<bool> &cancel,
                        const std::function<void(const FolderStats &)> &progress = {}, int largestCount = 12);

// ----- archives -----
struct ArchiveEntry {
    QString path;
    qint64 size = 0;
    QDateTime date;
    bool isDir = false;
};
struct ArchiveResult {
    QVector<ArchiveEntry> entries;
    qint64 totalSize = 0;
    int files = 0;
    int folders = 0;
    bool truncated = false;
    QString tool;   ///< "KArchive", "7z", "unrar"
    QString error;
};
ArchiveResult listArchive(const QString &path, const QString &mimeName, int maxEntries = ArchiveLimit);
/// Parses the output of "7z l -slt" (exposed for the unit test).
QVector<ArchiveEntry> parse7zSlt(const QString &output);

// ----- PDF -----
struct PdfResult {
    QImage page;
    int pageCount = 0;
    int pageIndex = 0;
    QString title;
    QString error;
};
/// Renders page @p index of a PDF @p width pixels wide (Poppler).
PdfResult renderPdfPage(const QString &path, int index, int width);

// ----- other files -----
/// "00000000  48 65 6c 6c 6f ...  |Hello...|" lines.
QString hexDump(const QByteArray &data, qint64 offset = 0);
QByteArray readHead(const QString &path, qint64 limit, QString *error = nullptr);
}

#endif // PANEROQUICKVIEWDATA_H
