/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneroquickviewdata.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QMimeType>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextCodec>
#include <QtEndian>

#include <KArchive/K7Zip>
#include <KArchive/KArchiveDirectory>
#include <KArchive/KArchiveFile>
#include <KArchive/KTar>
#include <KArchive/KZip>
#include <KCodecs/KEncodingProber>

#include <algorithm>
#include <cstring>
#include <memory>

#ifdef PANERO_HAVE_POPPLER
#include <poppler-qt5.h>
#endif

namespace PaneroQuickViewData
{
namespace
{
const QStringList &zipMimes()
{
    static const QStringList m{QStringLiteral("application/zip"), QStringLiteral("application/x-zip-compressed"),
                               QStringLiteral("application/java-archive"), QStringLiteral("application/vnd.android.package-archive"),
                               QStringLiteral("application/x-xpinstall")};
    return m;
}
const QStringList &tarMimes()
{
    static const QStringList m{QStringLiteral("application/x-tar"), QStringLiteral("application/x-compressed-tar"),
                               QStringLiteral("application/x-bzip-compressed-tar"), QStringLiteral("application/x-bzip2-compressed-tar"),
                               QStringLiteral("application/x-xz-compressed-tar"), QStringLiteral("application/x-lzma-compressed-tar"),
                               QStringLiteral("application/x-zstd-compressed-tar"), QStringLiteral("application/x-gtar")};
    return m;
}
const QStringList &rarMimes()
{
    static const QStringList m{QStringLiteral("application/vnd.rar"), QStringLiteral("application/x-rar"),
                               QStringLiteral("application/x-rar-compressed")};
    return m;
}
const QString sevenZipMime = QStringLiteral("application/x-7z-compressed");

bool hasNul(const QByteArray &data)
{
    return data.indexOf('\0') >= 0;
}

/// strict UTF-8 check; an incomplete sequence at the very end is allowed (cut-off read)
bool isValidUtf8(const QByteArray &data)
{
    const auto *p = reinterpret_cast<const unsigned char *>(data.constData());
    const int n = data.size();
    int i = 0;
    while (i < n) {
        const unsigned char c = p[i];
        int extra;
        if (c < 0x80) {
            ++i;
            continue;
        } else if (c >= 0xC2 && c <= 0xDF) {
            extra = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            extra = 2;
        } else if (c >= 0xF0 && c <= 0xF4) {
            extra = 3;
        } else {
            return false;
        }
        if (i + extra >= n) {
            // the sequence runs past the end: fine when the data was cut there
            for (int k = i + 1; k < n; ++k)
                if ((p[k] & 0xC0) != 0x80)
                    return false;
            return true;
        }
        for (int k = 1; k <= extra; ++k)
            if ((p[i + k] & 0xC0) != 0x80)
                return false;
        i += extra + 1;
    }
    return true;
}

quint16 rd16(const uchar *p, bool le)
{
    return le ? qFromLittleEndian<quint16>(p) : qFromBigEndian<quint16>(p);
}
quint32 rd32(const uchar *p, bool le)
{
    return le ? qFromLittleEndian<quint32>(p) : qFromBigEndian<quint32>(p);
}

/// reads an ASCII tag from an IFD inside the TIFF block @p t (size @p n)
QString ifdAscii(const uchar *t, int n, quint32 ifd, quint16 wanted, bool le, quint32 *exifPointer = nullptr)
{
    if (ifd + 2 > quint32(n))
        return QString();
    const quint16 count = rd16(t + ifd, le);
    QString result;
    for (int e = 0; e < count; ++e) {
        const quint32 off = ifd + 2 + 12 * e;
        if (off + 12 > quint32(n))
            break;
        const quint16 tag = rd16(t + off, le);
        const quint16 type = rd16(t + off + 2, le);
        const quint32 cnt = rd32(t + off + 4, le);
        if (exifPointer && tag == 0x8769)
            *exifPointer = rd32(t + off + 8, le);
        if (tag == wanted && type == 2 && cnt > 0) {
            const quint32 at = cnt <= 4 ? off + 8 : rd32(t + off + 8, le);
            if (at + cnt <= quint32(n))
                result = QString::fromLatin1(reinterpret_cast<const char *>(t + at), int(cnt)).trimmed();
            result.remove(QChar(0));
        }
    }
    return result;
}

QString normalizeExifDate(QString d)
{
    // "2024:05:01 12:34:56" -> "2024-05-01 12:34:56"
    if (d.size() >= 10 && d[4] == QLatin1Char(':') && d[7] == QLatin1Char(':')) {
        d[4] = QLatin1Char('-');
        d[7] = QLatin1Char('-');
    }
    if (d.startsWith(QLatin1String("0000")))
        return QString();
    return d;
}

QString findTool(const QStringList &names)
{
    for (const QString &n : names) {
        const QString p = QStandardPaths::findExecutable(n);
        if (!p.isEmpty())
            return p;
    }
#ifdef Q_OS_WIN
    for (const QString &dir : {QStringLiteral("C:/Program Files/7-Zip"), QStringLiteral("C:/Program Files (x86)/7-Zip")}) {
        const QString p = QStandardPaths::findExecutable(QStringLiteral("7z"), {dir});
        if (!p.isEmpty() && names.contains(QStringLiteral("7z")))
            return p;
    }
#endif
    return QString();
}

void addArchiveDir(const KArchiveDirectory *dir, const QString &prefix, ArchiveResult &r, int maxEntries)
{
    const QStringList names = dir->entries();
    for (const QString &name : names) {
        if (r.entries.size() >= maxEntries) {
            r.truncated = true;
            return;
        }
        const KArchiveEntry *e = dir->entry(name);
        if (!e)
            continue;
        ArchiveEntry a;
        a.path = prefix + name;
        a.date = e->date();
        if (e->isDirectory()) {
            a.isDir = true;
            ++r.folders;
            r.entries.append(a);
            addArchiveDir(static_cast<const KArchiveDirectory *>(e), a.path + QLatin1Char('/'), r, maxEntries);
        } else {
            a.size = static_cast<const KArchiveFile *>(e)->size();
            r.totalSize += a.size;
            ++r.files;
            r.entries.append(a);
        }
    }
}

bool runTool(const QString &program, const QStringList &args, QString *out, QString *error)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(program, args, QIODevice::ReadOnly);
    if (!p.waitForStarted(5000)) {
        *error = p.errorString();
        return false;
    }
    p.closeWriteChannel();
    if (!p.waitForFinished(20000)) {
        p.kill();
        p.waitForFinished(2000);
        *error = QStringLiteral("timeout");
        return false;
    }
    *out = QString::fromLocal8Bit(p.readAllStandardOutput());
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        *error = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
        if (error->isEmpty())
            *error = QStringLiteral("exit code %1").arg(p.exitCode());
        return !out->isEmpty();
    }
    return true;
}
} // namespace

bool pdfSupported()
{
#ifdef PANERO_HAVE_POPPLER
    return true;
#else
    return false;
#endif
}

bool isArchiveMime(const QString &mimeName)
{
    if (zipMimes().contains(mimeName) || tarMimes().contains(mimeName) || rarMimes().contains(mimeName) || mimeName == sevenZipMime)
        return true;
    const QMimeType t = QMimeDatabase().mimeTypeForName(mimeName);
    return t.isValid() && (t.inherits(QStringLiteral("application/x-tar")));
}

QString mimeTypeOf(const QString &path)
{
    return QMimeDatabase().mimeTypeForFile(path).name();
}

Kind classify(const QString &mimeName, bool isDir, const QByteArray &head)
{
    if (isDir || mimeName == QLatin1String("inode/directory"))
        return Kind::Folder;
    const QMimeDatabase db;
    const QMimeType t = db.mimeTypeForName(mimeName);
    const bool looksText = !head.isEmpty() && !hasNul(head);

    if (mimeName.startsWith(QLatin1String("image/"))) {
        const QList<QByteArray> supported = QImageReader::supportedMimeTypes();
        if (supported.contains(mimeName.toLatin1()))
            return Kind::Image;
        if (t.isValid()) {
            for (const QString &alias : t.aliases())
                if (supported.contains(alias.toLatin1()))
                    return Kind::Image;
        }
        if (mimeName == QLatin1String("image/svg+xml") || mimeName == QLatin1String("image/svg+xml-compressed"))
            return looksText ? Kind::Text : Kind::Other;
        return Kind::Other;
    }
    if (mimeName == QLatin1String("application/pdf"))
        return pdfSupported() ? Kind::Pdf : Kind::Other;
    if (isArchiveMime(mimeName))
        return Kind::Archive;
    if (mimeName.startsWith(QLatin1String("audio/")) || mimeName.startsWith(QLatin1String("video/"))) {
        // ".ts" is TypeScript as often as an MPEG stream; media files have NUL bytes early
        if (looksText && QString::fromLatin1(head.left(64)).contains(QRegularExpression(QStringLiteral("^[\\x09\\x0a\\x0d\\x20-\\x7e]+$"))))
            return Kind::Text;
        return Kind::Media;
    }
    if (t.isValid() && (t.inherits(QStringLiteral("text/plain")) || mimeName.startsWith(QLatin1String("text/"))))
        return Kind::Text;
    static const QStringList textLike{QStringLiteral("application/json"), QStringLiteral("application/xml"),
                                      QStringLiteral("application/javascript"), QStringLiteral("application/x-shellscript"),
                                      QStringLiteral("application/x-yaml"), QStringLiteral("application/toml"),
                                      QStringLiteral("application/sql"), QStringLiteral("application/x-desktop"),
                                      QStringLiteral("application/x-subrip"), QStringLiteral("application/x-wine-extension-ini")};
    if (textLike.contains(mimeName))
        return Kind::Text;
    if (mimeName == QLatin1String("application/x-zerosize"))
        return Kind::Text;
    if (looksText)
        return Kind::Text;
    return Kind::Other;
}

// ----- text -----

QByteArray detectEncoding(const QByteArray &data)
{
    const auto *u = reinterpret_cast<const uchar *>(data.constData());
    const int n = data.size();
    if (n >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF)
        return QByteArrayLiteral("UTF-8");
    if (n >= 4 && u[0] == 0xFF && u[1] == 0xFE && u[2] == 0 && u[3] == 0)
        return QByteArrayLiteral("UTF-32LE");
    if (n >= 4 && u[0] == 0 && u[1] == 0 && u[2] == 0xFE && u[3] == 0xFF)
        return QByteArrayLiteral("UTF-32BE");
    if (n >= 2 && u[0] == 0xFF && u[1] == 0xFE)
        return QByteArrayLiteral("UTF-16LE");
    if (n >= 2 && u[0] == 0xFE && u[1] == 0xFF)
        return QByteArrayLiteral("UTF-16BE");
    if (isValidUtf8(data))
        return QByteArrayLiteral("UTF-8");
    KEncodingProber prober(KEncodingProber::Universal);
    prober.feed(data.left(256 * 1024));
    const QByteArray guess = prober.encoding();
    if (!guess.isEmpty() && prober.confidence() >= 0.5f && QTextCodec::codecForName(guess)
        && guess.toUpper() != "UTF-8") // not valid UTF-8 (checked above)
        return QTextCodec::codecForName(guess)->name();
    return QByteArrayLiteral("windows-1252");
}

QString decodeText(const QByteArray &data, QByteArray *encoding)
{
    const QByteArray enc = detectEncoding(data);
    if (encoding)
        *encoding = enc;
    QTextCodec *codec = QTextCodec::codecForName(enc);
    if (!codec)
        codec = QTextCodec::codecForName("ISO-8859-1");
    return codec->toUnicode(data);
}

TextResult loadText(const QString &path, qint64 limit)
{
    TextResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.error = f.errorString();
        return r;
    }
    r.fileSize = f.size();
    QByteArray data = f.read(limit);
    r.truncated = r.fileSize > data.size();
    if (r.truncated) {
        // end on a whole line: no half character at the cut
        const int nl = data.lastIndexOf('\n');
        if (nl > data.size() - 8192)
            data.truncate(nl + 1);
    }
    r.text = decodeText(data, &r.encoding);
    return r;
}

// ----- images -----

QString exifDate(const QByteArray &jpeg)
{
    const auto *d = reinterpret_cast<const uchar *>(jpeg.constData());
    const int n = jpeg.size();
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8)
        return QString();
    int pos = 2;
    while (pos + 4 <= n) {
        if (d[pos] != 0xFF)
            return QString();
        const uchar marker = d[pos + 1];
        if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7)) {
            pos += 2;
            continue;
        }
        if (marker == 0xDA || marker == 0xD9)
            return QString(); // image data: no more metadata
        const int len = (d[pos + 2] << 8) | d[pos + 3];
        if (len < 2)
            return QString();
        if (marker == 0xE1 && pos + 4 + 6 <= n && memcmp(d + pos + 4, "Exif\0\0", 6) == 0) {
            const int tiffStart = pos + 10;
            const int tiffLen = qMin(n, pos + 2 + len) - tiffStart;
            if (tiffLen < 8)
                return QString();
            const uchar *t = d + tiffStart;
            bool le;
            if (t[0] == 'I' && t[1] == 'I')
                le = true;
            else if (t[0] == 'M' && t[1] == 'M')
                le = false;
            else
                return QString();
            const quint32 ifd0 = rd32(t + 4, le);
            quint32 exifIfd = 0;
            const QString dateTime = ifdAscii(t, tiffLen, ifd0, 0x0132, le, &exifIfd);
            QString original;
            if (exifIfd)
                original = ifdAscii(t, tiffLen, exifIfd, 0x9003, le);
            return normalizeExifDate(original.isEmpty() ? dateTime : original);
        }
        pos += 2 + len;
    }
    return QString();
}

ImageResult loadImage(const QString &path, int maxSide)
{
    ImageResult r;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    r.format = reader.format();
    r.originalSize = reader.size();
    r.frameCount = reader.imageCount();
    const bool vector = r.format.startsWith("svg");
    const bool animated = reader.supportsAnimation() && r.frameCount != 1 && !vector;
    if (r.originalSize.isValid()) {
        QSize target = r.originalSize;
        if (vector)
            target.scale(QSize(2048, 2048), Qt::KeepAspectRatio); // vector: render sharp
        else if (qMax(target.width(), target.height()) > maxSide)
            target.scale(QSize(maxSide, maxSide), Qt::KeepAspectRatio);
        if (target != r.originalSize && !animated)
            reader.setScaledSize(target);
    }
    if (r.format == "jpeg" || r.format == "jpg") {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
            r.exifDate = exifDate(f.read(256 * 1024));
    } else {
        for (const char *key : {"DateTimeOriginal", "DateTime", "CreationDate"}) {
            const QString v = reader.text(QLatin1String(key));
            if (!v.isEmpty()) {
                r.exifDate = normalizeExifDate(v);
                break;
            }
        }
    }
    if (animated) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly) && f.size() < 96 * 1024 * 1024)
            r.animationData = f.readAll();
    }
    if (!reader.read(&r.image)) {
        r.error = reader.errorString();
        return r;
    }
    if (!r.originalSize.isValid())
        r.originalSize = r.image.size();
    if (!animated)
        r.frameCount = 1;
    return r;
}

// ----- folders -----

FolderStats folderStats(const QString &path, const std::atomic<bool> &cancel,
                        const std::function<void(const FolderStats &)> &progress, int largestCount)
{
    FolderStats s;
    QElapsedTimer clock;
    clock.start();
    qint64 lastReport = 0;
    const QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System;
    QVector<FolderEntry> top;

    auto sortTop = [&](QVector<FolderEntry> list) {
        std::sort(list.begin(), list.end(), [](const FolderEntry &a, const FolderEntry &b) {
            return a.size != b.size ? a.size > b.size : a.name < b.name;
        });
        if (list.size() > largestCount)
            list.resize(largestCount);
        return list;
    };
    auto report = [&]() {
        if (progress && clock.elapsed() - lastReport >= 250) {
            lastReport = clock.elapsed();
            FolderStats partial = s;
            partial.largest = sortTop(top);
            progress(partial);
        }
    };
    auto isLink = [](const QFileInfo &fi) {
#ifdef Q_OS_WIN
        return fi.isSymLink() || fi.isJunction();
#else
        return fi.isSymLink();
#endif
    };

    QDir root(path);
    if (!root.exists() || !root.isReadable()) {
        s.errors = 1;
        s.finished = true;
        return s;
    }
    QDirIterator topIt(path, filters);
    while (topIt.hasNext()) {
        if (cancel) {
            s.cancelled = true;
            return s;
        }
        topIt.next();
        const QFileInfo fi = topIt.fileInfo();
        FolderEntry entry;
        entry.name = fi.fileName();
        if (fi.isDir() && !isLink(fi)) {
            entry.isDir = true;
            ++s.folders;
            QStringList stack{fi.filePath()};
            while (!stack.isEmpty()) {
                if (cancel) {
                    s.cancelled = true;
                    return s;
                }
                const QString dir = stack.takeLast();
                QDirIterator it(dir, filters);
                if (!QFileInfo(dir).isReadable())
                    ++s.errors;
                while (it.hasNext()) {
                    it.next();
                    const QFileInfo sub = it.fileInfo();
                    if (sub.isDir() && !isLink(sub)) {
                        ++s.folders;
                        stack.append(sub.filePath());
                    } else {
                        ++s.files;
                        if (!isLink(sub)) {
                            entry.size += sub.size();
                            s.totalSize += sub.size();
                        }
                    }
                    if (cancel) {
                        s.cancelled = true;
                        return s;
                    }
                }
                report();
            }
        } else {
            ++s.files;
            if (!isLink(fi)) {
                entry.size = fi.size();
                s.totalSize += entry.size;
            }
        }
        top.append(entry);
        report();
    }
    s.largest = sortTop(top);
    s.finished = true;
    return s;
}

// ----- archives -----

QVector<ArchiveEntry> parse7zSlt(const QString &output)
{
    QVector<ArchiveEntry> result;
    const int start = output.indexOf(QLatin1String("\n----------"));
    const QString body = start >= 0 ? output.mid(start + 11) : output;
    const QStringList blocks = body.split(QRegularExpression(QStringLiteral("\\r?\\n\\s*\\r?\\n")), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        ArchiveEntry e;
        bool hasPath = false, archiveHeader = false;
        for (const QString &rawLine : block.split(QLatin1Char('\n'))) {
            const QString line = rawLine.trimmed();
            const int eq = line.indexOf(QLatin1String(" = "));
            const QString key = eq > 0 ? line.left(eq) : line.endsWith(QLatin1String(" =")) ? line.left(line.size() - 2) : QString();
            const QString value = eq > 0 ? line.mid(eq + 3) : QString();
            if (key == QLatin1String("Path")) {
                e.path = QDir::fromNativeSeparators(value);
                hasPath = true;
            } else if (key == QLatin1String("Type")) {
                archiveHeader = true;
            } else if (key == QLatin1String("Size")) {
                e.size = value.toLongLong();
            } else if (key == QLatin1String("Modified")) {
                e.date = QDateTime::fromString(value.left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            } else if (key == QLatin1String("Folder")) {
                e.isDir = e.isDir || value == QLatin1String("+");
            } else if (key == QLatin1String("Attributes")) {
                e.isDir = e.isDir || value.startsWith(QLatin1Char('D'));
            }
        }
        if (hasPath && !archiveHeader)
            result.append(e);
    }
    return result;
}

ArchiveResult listArchive(const QString &path, const QString &mimeName, int maxEntries)
{
    ArchiveResult r;
    std::unique_ptr<KArchive> archive;
    if (zipMimes().contains(mimeName))
        archive.reset(new KZip(path));
    else if (mimeName == sevenZipMime)
        archive.reset(new K7Zip(path));
    else if (tarMimes().contains(mimeName) || QMimeDatabase().mimeTypeForName(mimeName).inherits(QStringLiteral("application/x-tar")))
        archive.reset(new KTar(path));

    if (archive) {
        if (archive->open(QIODevice::ReadOnly)) {
            r.tool = QStringLiteral("KArchive");
            addArchiveDir(archive->directory(), QString(), r, maxEntries);
            archive->close();
            return r;
        }
        r.error = archive->errorString();
    }

    // rar (and whatever KArchive could not open): the archive tools, when installed
    QString out, error;
    const QString sevenZip = findTool({QStringLiteral("7z"), QStringLiteral("7zz"), QStringLiteral("7za")});
    QVector<ArchiveEntry> listed;
    if (!sevenZip.isEmpty() && runTool(sevenZip, {QStringLiteral("l"), QStringLiteral("-slt"), QStringLiteral("--"), path}, &out, &error)
        && !(listed = parse7zSlt(out)).isEmpty()) {
        r.entries = listed;
        r.tool = QStringLiteral("7z");
        r.error.clear();
    } else if (rarMimes().contains(mimeName)) {
        const QString unrar = findTool({QStringLiteral("unrar"), QStringLiteral("rar")});
        if (!unrar.isEmpty() && runTool(unrar, {QStringLiteral("lb"), QStringLiteral("--"), path}, &out, &error)) {
            for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                ArchiveEntry e;
                e.path = QDir::fromNativeSeparators(line.trimmed());
                r.entries.append(e);
            }
            r.tool = QStringLiteral("unrar");
            r.error.clear();
        } else if (r.error.isEmpty()) {
            r.error = error.isEmpty() ? QStringLiteral("no-tool") : error;
        }
    } else if (r.error.isEmpty()) {
        r.error = error.isEmpty() ? QStringLiteral("no-tool") : error;
    }
    if (r.entries.size() > maxEntries) {
        r.entries.resize(maxEntries);
        r.truncated = true;
    }
    for (const ArchiveEntry &e : qAsConst(r.entries)) {
        if (e.isDir) {
            ++r.folders;
        } else {
            ++r.files;
            r.totalSize += e.size;
        }
    }
    return r;
}

// ----- PDF -----

PdfResult renderPdfPage(const QString &path, int index, int width)
{
    PdfResult r;
#ifdef PANERO_HAVE_POPPLER
    std::unique_ptr<Poppler::Document> doc(Poppler::Document::load(path));
    if (!doc) {
        r.error = QStringLiteral("open");
        return r;
    }
    if (doc->isLocked()) {
        r.error = QStringLiteral("locked");
        return r;
    }
    r.pageCount = doc->numPages();
    r.title = doc->info(QStringLiteral("Title"));
    if (r.pageCount <= 0) {
        r.error = QStringLiteral("empty");
        return r;
    }
    r.pageIndex = qBound(0, index, r.pageCount - 1);
    doc->setRenderHint(Poppler::Document::Antialiasing);
    doc->setRenderHint(Poppler::Document::TextAntialiasing);
    doc->setRenderHint(Poppler::Document::TextHinting);
    std::unique_ptr<Poppler::Page> page(doc->page(r.pageIndex));
    if (!page) {
        r.error = QStringLiteral("page");
        return r;
    }
    const QSizeF points = page->pageSizeF();
    const double dpi = points.width() > 0 ? qBound(10.0, 72.0 * width / points.width(), 600.0) : 96.0;
    r.page = page->renderToImage(dpi, dpi);
    if (r.page.isNull())
        r.error = QStringLiteral("render");
#else
    Q_UNUSED(path)
    Q_UNUSED(index)
    Q_UNUSED(width)
    r.error = QStringLiteral("unsupported");
#endif
    return r;
}

// ----- other -----

QString hexDump(const QByteArray &data, qint64 offset)
{
    QString out;
    out.reserve(data.size() * 5);
    static const char hex[] = "0123456789abcdef";
    for (int line = 0; line < data.size(); line += 16) {
        out += QStringLiteral("%1  ").arg(offset + line, 8, 16, QLatin1Char('0'));
        QString ascii;
        for (int i = 0; i < 16; ++i) {
            if (line + i < data.size()) {
                const uchar c = uchar(data[line + i]);
                out += QLatin1Char(hex[c >> 4]);
                out += QLatin1Char(hex[c & 15]);
                out += QLatin1Char(' ');
                ascii += (c >= 0x20 && c < 0x7f) ? QChar(QLatin1Char(char(c))) : QChar(QLatin1Char('.'));
            } else {
                out += QLatin1String("   ");
            }
            if (i == 7)
                out += QLatin1Char(' ');
        }
        out += QLatin1String(" |") + ascii + QLatin1String("|\n");
    }
    return out;
}

QByteArray readHead(const QString &path, qint64 limit, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return QByteArray();
    }
    return f.read(limit);
}
} // namespace PaneroQuickViewData
