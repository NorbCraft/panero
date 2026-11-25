/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Quick View (D-022): the file reading side (worker-thread functions).

#include "paneroquickviewdata.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCodec>

#include <KArchive/KTar>
#include <KArchive/KZip>

using namespace PaneroQuickViewData;

class PaneroQuickViewTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }
    void write(const QString &name, const QByteArray &data)
    {
        QFile f(path(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }

    /// a minimal JPEG header with an EXIF block holding DateTime / DateTimeOriginal
    static QByteArray jpegWithExif(bool bigEndian, const QByteArray &dateTime, const QByteArray &original)
    {
        auto u16 = [bigEndian](quint16 v) {
            QByteArray b(2, 0);
            b[bigEndian ? 0 : 1] = char(v >> 8);
            b[bigEndian ? 1 : 0] = char(v & 0xff);
            return b;
        };
        auto u32 = [bigEndian](quint32 v) {
            QByteArray b(4, 0);
            for (int i = 0; i < 4; ++i)
                b[bigEndian ? 3 - i : i] = char((v >> (8 * i)) & 0xff);
            return b;
        };
        // TIFF: header(8) + IFD0 (2 entries) at 8, Exif IFD after it, strings at the end
        const quint32 ifd0 = 8;
        const quint32 ifd0Size = 2 + 2 * 12 + 4;
        const quint32 exifIfd = ifd0 + ifd0Size;
        const quint32 exifSize = 2 + 1 * 12 + 4;
        const quint32 str1 = exifIfd + exifSize;
        const quint32 str2 = str1 + quint32(dateTime.size()) + 1;
        QByteArray tiff = bigEndian ? QByteArray("MM") : QByteArray("II");
        tiff += u16(42) + u32(ifd0);
        tiff += u16(2);
        tiff += u16(0x0132) + u16(2) + u32(quint32(dateTime.size()) + 1) + u32(str1);
        tiff += u16(0x8769) + u16(4) + u32(1) + u32(exifIfd);
        tiff += u32(0);
        tiff += u16(1);
        tiff += u16(0x9003) + u16(2) + u32(quint32(original.size()) + 1) + u32(str2);
        tiff += u32(0);
        tiff += dateTime + QByteArray(1, 0) + original + QByteArray(1, 0);
        QByteArray app1 = QByteArray("Exif\0\0", 6) + tiff;
        QByteArray jpeg("\xFF\xD8", 2);
        jpeg += QByteArray("\xFF\xE1", 2);
        const int len = app1.size() + 2;
        jpeg += char(len >> 8);
        jpeg += char(len & 0xff);
        jpeg += app1;
        jpeg += QByteArray("\xFF\xD9", 2);
        return jpeg;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
    }

    void classifyByMime()
    {
        QCOMPARE(classify(QStringLiteral("inode/directory"), true), Kind::Folder);
        QCOMPARE(classify(QStringLiteral("image/png"), false), Kind::Image);
        QCOMPARE(classify(QStringLiteral("image/jpeg"), false), Kind::Image);
        QCOMPARE(classify(QStringLiteral("text/plain"), false), Kind::Text);
        QCOMPARE(classify(QStringLiteral("text/x-c++src"), false), Kind::Text);
        QCOMPARE(classify(QStringLiteral("application/x-shellscript"), false), Kind::Text);
        QCOMPARE(classify(QStringLiteral("application/json"), false), Kind::Text);
        QCOMPARE(classify(QStringLiteral("application/zip"), false), Kind::Archive);
        QCOMPARE(classify(QStringLiteral("application/x-compressed-tar"), false), Kind::Archive);
        QCOMPARE(classify(QStringLiteral("application/x-7z-compressed"), false), Kind::Archive);
        QCOMPARE(classify(QStringLiteral("application/vnd.rar"), false), Kind::Archive);
        QCOMPARE(classify(QStringLiteral("audio/mpeg"), false, QByteArray("ID3\x03\0\0", 6)), Kind::Media);
        QCOMPARE(classify(QStringLiteral("video/mp4"), false, QByteArray("\0\0\0\x18" "ftypmp42", 12)), Kind::Media);
        // ".ts" TypeScript mistaken for an MPEG stream
        QCOMPARE(classify(QStringLiteral("video/mp2t"), false, QByteArray("export const x = 1;\n")), Kind::Text);
        QCOMPARE(classify(QStringLiteral("application/pdf"), false), pdfSupported() ? Kind::Pdf : Kind::Other);
        QCOMPARE(classify(QStringLiteral("application/octet-stream"), false, QByteArray("plain words\n")), Kind::Text);
        QCOMPARE(classify(QStringLiteral("application/octet-stream"), false, QByteArray("\x7f" "ELF\x02\x01\x01\0", 8)), Kind::Other);
        QCOMPARE(classify(QStringLiteral("application/x-executable"), false, QByteArray("\x7f" "ELF\x02\x01\x01\0", 8)), Kind::Other);
    }

    void encodings()
    {
        QCOMPARE(detectEncoding(QByteArray("plain ascii\n")), QByteArray("UTF-8"));
        QCOMPARE(detectEncoding(QStringLiteral("Grüße, 日本\n").toUtf8()), QByteArray("UTF-8"));
        // UTF-8 cut inside a character (the 2 MB limit): still UTF-8
        QByteArray cut = QStringLiteral("abc€").toUtf8();
        cut.chop(1);
        QCOMPARE(detectEncoding(cut), QByteArray("UTF-8"));
        QCOMPARE(detectEncoding(QByteArray("\xEF\xBB\xBFhi")), QByteArray("UTF-8"));
        QCOMPARE(detectEncoding(QByteArray("\xFF\xFEh\0i\0", 6)), QByteArray("UTF-16LE"));
        QCOMPARE(detectEncoding(QByteArray("\xFE\xFF\0h\0i", 6)), QByteArray("UTF-16BE"));
        // Latin-1 / Windows-1252 text is not UTF-8
        const QByteArray latin = QTextCodec::codecForName("windows-1252")->fromUnicode(
            QStringLiteral("Größe und Gewicht: schön, Straße, Ärger, Öl, Übung. Café crème brûlée à la carte.\n"));
        const QByteArray enc = detectEncoding(latin);
        QVERIFY2(enc != "UTF-8", enc.constData());
        QByteArray used;
        QCOMPARE(decodeText(latin, &used), QStringLiteral("Größe und Gewicht: schön, Straße, Ärger, Öl, Übung. Café crème brûlée à la carte.\n"));
        QCOMPARE(decodeText(QByteArray("\xFF\xFEh\0i\0", 6)), QStringLiteral("hi"));
    }

    void textLimit()
    {
        QByteArray big;
        for (int i = 0; big.size() < 3 * 1024 * 1024; ++i)
            big += "line " + QByteArray::number(i) + " äöü\n";
        write(QStringLiteral("big.txt"), big);
        const TextResult r = loadText(path(QStringLiteral("big.txt")));
        QVERIFY(r.error.isEmpty());
        QVERIFY(r.truncated);
        QCOMPARE(r.fileSize, qint64(big.size()));
        QCOMPARE(r.encoding, QByteArray("UTF-8"));
        QVERIFY(r.text.toUtf8().size() <= TextLimit);
        QVERIFY(r.text.endsWith(QLatin1Char('\n'))); // cut at a line end
        QVERIFY(r.text.startsWith(QStringLiteral("line 0 äöü\n")));

        write(QStringLiteral("small.cpp"), "int main() { return 0; }\n");
        const TextResult s = loadText(path(QStringLiteral("small.cpp")));
        QVERIFY(!s.truncated);
        QCOMPARE(s.text, QStringLiteral("int main() { return 0; }\n"));

        const TextResult missing = loadText(path(QStringLiteral("does-not-exist")));
        QVERIFY(!missing.error.isEmpty());
    }

    void exif()
    {
        QCOMPARE(exifDate(jpegWithExif(false, "2020:01:02 03:04:05", "2019:12:31 23:59:58")), QStringLiteral("2019-12-31 23:59:58"));
        QCOMPARE(exifDate(jpegWithExif(true, "2020:01:02 03:04:05", "2018:06:07 08:09:10")), QStringLiteral("2018-06-07 08:09:10"));
        QCOMPARE(exifDate(jpegWithExif(true, "2020:01:02 03:04:05", "")), QStringLiteral("2020-01-02 03:04:05"));
        QCOMPARE(exifDate(QByteArray("not a jpeg")), QString());
        QCOMPARE(exifDate(QByteArray("\xFF\xD8\xFF\xE1\x00", 5)), QString()); // truncated
        QCOMPARE(exifDate(QByteArray("\xFF\xD8\xFF\xDA\x00\x02", 6)), QString());
    }

    void images()
    {
        QImage img(640, 480, QImage::Format_ARGB32);
        img.fill(Qt::red);
        QVERIFY(img.save(path(QStringLiteral("a.png"))));
        ImageResult r = loadImage(path(QStringLiteral("a.png")));
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.originalSize, QSize(640, 480));
        QCOMPARE(r.image.size(), QSize(640, 480));
        QCOMPARE(r.format, QByteArray("png"));
        QCOMPARE(r.frameCount, 1);
        QVERIFY(r.animationData.isEmpty());

        // big pictures are scaled down in the worker, the original size is kept
        QImage huge(5000, 2500, QImage::Format_RGB32);
        huge.fill(Qt::blue);
        QVERIFY(huge.save(path(QStringLiteral("huge.jpg")), "JPEG", 50));
        r = loadImage(path(QStringLiteral("huge.jpg")), 1000);
        QCOMPARE(r.originalSize, QSize(5000, 2500));
        QCOMPARE(r.image.size(), QSize(1000, 500));

        write(QStringLiteral("broken.png"), "\x89PNG\r\n\x1a\nnot really");
        r = loadImage(path(QStringLiteral("broken.png")));
        QVERIFY(r.image.isNull());
        QVERIFY(!r.error.isEmpty());

        // a JPEG with EXIF in front of real image data
        QBuffer buf;
        buf.open(QIODevice::WriteOnly);
        QImage small(32, 16, QImage::Format_RGB32);
        small.fill(Qt::green);
        small.save(&buf, "JPEG");
        QByteArray jpeg = buf.data();
        QByteArray exifJpeg = jpegWithExif(false, "2021:02:03 04:05:06", "2021:02:03 04:05:06");
        exifJpeg.chop(2); // no EOI
        jpeg = exifJpeg + jpeg.mid(2);
        write(QStringLiteral("photo.jpg"), jpeg);
        r = loadImage(path(QStringLiteral("photo.jpg")));
        QCOMPARE(r.exifDate, QStringLiteral("2021-02-03 04:05:06"));
        QCOMPARE(r.originalSize, QSize(32, 16));
    }

    void folders()
    {
        const QString root = path(QStringLiteral("tree"));
        QDir().mkpath(root + QStringLiteral("/big/deeper"));
        QDir().mkpath(root + QStringLiteral("/empty"));
        auto put = [](const QString &file, int size) {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(size, 'x'));
        };
        put(root + QStringLiteral("/big/a.bin"), 3000);
        put(root + QStringLiteral("/big/deeper/b.bin"), 5000);
        put(root + QStringLiteral("/one.txt"), 100);
        put(root + QStringLiteral("/.hidden"), 10);
        std::atomic<bool> cancel{false};
        int progressCalls = 0;
        FolderStats s = folderStats(root, cancel, [&](const FolderStats &) { ++progressCalls; });
        QVERIFY(s.finished);
        QVERIFY(!s.cancelled);
        QCOMPARE(s.totalSize, qint64(8110));
        QCOMPARE(s.files, qint64(4));
        QCOMPARE(s.folders, qint64(3)); // big, big/deeper, empty
        QCOMPARE(s.largest.size(), 4);
        QCOMPARE(s.largest.first().name, QStringLiteral("big"));
        QCOMPARE(s.largest.first().size, qint64(8000));
        QVERIFY(s.largest.first().isDir);
        QCOMPARE(s.largest.at(1).name, QStringLiteral("one.txt"));

        cancel = true;
        s = folderStats(root, cancel);
        QVERIFY(s.cancelled);
        QVERIFY(!s.finished);

        cancel = false;
        s = folderStats(path(QStringLiteral("no-such-folder")), cancel);
        QVERIFY(s.finished);
        QCOMPARE(s.errors, 1);
    }

    void archives()
    {
        {
            KZip zip(path(QStringLiteral("a.zip")));
            QVERIFY(zip.open(QIODevice::WriteOnly));
            zip.writeFile(QStringLiteral("docs/readme.txt"), QByteArray("hello zip"));
            zip.writeFile(QStringLiteral("top.bin"), QByteArray(1234, 'z'));
            zip.close();
        }
        ArchiveResult r = listArchive(path(QStringLiteral("a.zip")), QStringLiteral("application/zip"));
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.tool, QStringLiteral("KArchive"));
        QCOMPARE(r.files, 2);
        QCOMPARE(r.folders, 1);
        QCOMPARE(r.totalSize, qint64(9 + 1234));
        QStringList paths;
        for (const ArchiveEntry &e : r.entries)
            paths << e.path;
        QVERIFY(paths.contains(QStringLiteral("docs/readme.txt")));
        QVERIFY(paths.contains(QStringLiteral("top.bin")));

        {
            KTar tar(path(QStringLiteral("a.tar.gz")));
            QVERIFY(tar.open(QIODevice::WriteOnly));
            for (int i = 0; i < 30; ++i)
                tar.writeFile(QStringLiteral("f%1.txt").arg(i), QByteArray("x"));
            tar.close();
        }
        r = listArchive(path(QStringLiteral("a.tar.gz")), QStringLiteral("application/x-compressed-tar"), 10);
        QCOMPARE(r.entries.size(), 10);
        QVERIFY(r.truncated);

        write(QStringLiteral("bad.zip"), "PK\x03\x04 broken");
        r = listArchive(path(QStringLiteral("bad.zip")), QStringLiteral("application/zip"));
        QVERIFY(r.entries.isEmpty());
        QVERIFY(!r.error.isEmpty());
    }

    void sevenZipOutput()
    {
        const QString out = QStringLiteral(
            "7-Zip 23.01 (x64)\n\nScanning the drive for archives:\n1 file, 300 bytes\n\nListing archive: x.rar\n\n--\n"
            "Path = x.rar\nType = Rar5\nPhysical Size = 300\n\n----------\n"
            "Path = folder\nFolder = +\nSize = 0\nModified = 2024-05-01 10:11:12\nAttributes = D....\n\n"
            "Path = folder/file.txt\nFolder = -\nSize = 42\nPacked Size = 30\nModified = 2024-05-01 10:11:13.1234567\nAttributes = A\n\n");
        const QVector<ArchiveEntry> e = parse7zSlt(out);
        QCOMPARE(e.size(), 2);
        QCOMPARE(e[0].path, QStringLiteral("folder"));
        QVERIFY(e[0].isDir);
        QCOMPARE(e[1].path, QStringLiteral("folder/file.txt"));
        QCOMPARE(e[1].size, qint64(42));
        QVERIFY(!e[1].isDir);
        QCOMPARE(e[1].date, QDateTime(QDate(2024, 5, 1), QTime(10, 11, 13)));
    }

    void pdf()
    {
        if (!pdfSupported())
            QSKIP("built without Poppler");
        {
            QPdfWriter writer(path(QStringLiteral("doc.pdf")));
            writer.setTitle(QStringLiteral("Quick View test"));
            QPainter p(&writer);
            p.fillRect(QRect(0, 0, 2000, 2000), Qt::black);
            writer.newPage();
            p.fillRect(QRect(0, 0, 1000, 1000), Qt::blue);
        }
        PdfResult r = renderPdfPage(path(QStringLiteral("doc.pdf")), 0, 400);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.pageCount, 2);
        QCOMPARE(r.title, QStringLiteral("Quick View test"));
        QVERIFY(qAbs(r.page.width() - 400) <= 2);
        r = renderPdfPage(path(QStringLiteral("doc.pdf")), 7, 300);
        QCOMPARE(r.pageIndex, 1); // clamped to the last page
        write(QStringLiteral("bad.pdf"), "%PDF-1.4 nothing");
        r = renderPdfPage(path(QStringLiteral("bad.pdf")), 0, 300);
        QVERIFY(!r.error.isEmpty());
    }

    void hex()
    {
        const QString dump = hexDump(QByteArray("Hello, world!\n\0\x01\xff", 17));
        const QStringList lines = dump.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 2);
        QVERIFY(lines[0].startsWith(QStringLiteral("00000000  48 65 6c 6c 6f 2c 20 77  6f 72 6c 64 21 0a 00 01")));
        QVERIFY(lines[0].endsWith(QStringLiteral("|Hello, world!...|")));
        QVERIFY(lines[1].startsWith(QStringLiteral("00000010  ff")));
        QVERIFY(hexDump(QByteArray("x"), 0x100).startsWith(QStringLiteral("00000100  78")));
        write(QStringLiteral("head.bin"), QByteArray(100000, 'a'));
        QCOMPARE(readHead(path(QStringLiteral("head.bin")), HexLimit).size(), int(HexLimit));
        QString error;
        QVERIFY(readHead(path(QStringLiteral("nothing.bin")), 10, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
};

QTEST_MAIN(PaneroQuickViewTest)

#include "paneroquickviewtest.moc"
