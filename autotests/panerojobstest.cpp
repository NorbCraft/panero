/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Undo for file operations (D-022): FileUndoManager through PaneroUndo for move, rename,
// copy, new folder and Trash (Linux), the Windows Recycle Bin records and, on Windows, a
// real move to the Recycle Bin and its restore. Runs without D-Bus and with temporary
// HOME/XDG folders: never the user's Trash, settings or other applications' undo lists.

#include "panerorecyclebin.h"
#include "paneroundo.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <KIO/CopyJob>
#include <KIO/FileUndoManager>
#include <KIO/SimpleJob>

namespace
{
/// answers the "delete the copied files?" question of an undone copy with yes
class YesUi : public KIO::FileUndoManager::UiInterface
{
public:
    bool confirmDeletion(const QList<QUrl> &, KIO::FileUndoManager::UiInterface::ConfirmationType) override { return true; }
    void jobError(KIO::Job *job) override { qWarning() << "undo job error" << job->errorString(); }
};

void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

bool waitForUndo()
{
    QSignalSpy spy(PaneroUndo::self(), &PaneroUndo::undone);
    PaneroUndo::self()->undo();
    return spy.count() > 0 || spy.wait(15000);
}

bool runJob(KJob *job)
{
    QSignalSpy spy(job, &KJob::result);
    const bool ok = spy.wait(15000);
    // FileUndoManager adds the command right after the job's result
    QTest::qWait(50);
    return ok;
}
} // namespace

class PaneroJobsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void infoRecords();
    void infoPath();
    void undoMoveRenameCopyMkdir();
    void undoTrash();
    void recycleBinRestore();

private:
    QString m_dir;
};

static QTemporaryDir *s_home = nullptr;

void PaneroJobsTest::initTestCase()
{
    QVERIFY(s_home && s_home->isValid());
    m_dir = s_home->path() + QStringLiteral("/work");
    QVERIFY(QDir().mkpath(m_dir));
    KIO::FileUndoManager::self()->setUiInterface(new YesUi);
}

void PaneroJobsTest::infoRecords()
{
    PaneroRecycleBin::Info in;
    in.originalPath = QStringLiteral("C:\\Users\\Test\\Documents\\Bericht ü.txt");
    in.size = 12345;
    in.deletedAt = QDateTime(QDate(2026, 11, 23), QTime(10, 20, 30), Qt::UTC);
    const QByteArray v2 = PaneroRecycleBin::makeInfo(in);
    PaneroRecycleBin::Info out;
    QVERIFY(PaneroRecycleBin::parseInfo(v2, &out));
    QCOMPARE(out.originalPath, in.originalPath);
    QCOMPARE(out.size, in.size);
    QCOMPARE(out.deletedAt, in.deletedAt);

    // version 1 (Windows Vista to 8.1): fixed 260-character path
    QByteArray v1 = v2.left(24);
    v1[0] = 1;
    QString path = in.originalPath;
    path.resize(260, QChar(0));
    v1.append(reinterpret_cast<const char *>(path.utf16()), 520);
    QVERIFY(PaneroRecycleBin::parseInfo(v1, &out));
    QCOMPARE(out.originalPath, in.originalPath);

    // broken records
    QVERIFY(!PaneroRecycleBin::parseInfo(QByteArray(10, 'x'), &out));
    QByteArray bad = v2;
    bad[0] = 7;
    QVERIFY(!PaneroRecycleBin::parseInfo(bad, &out));
    QVERIFY(!PaneroRecycleBin::parseInfo(v2.left(v2.size() - 4), &out));

    // matching: same path (any case, separators), deletion time within minutes
    PaneroRecycleBin::Item item{QStringLiteral("c:/users/test/documents/bericht ü.txt"), QString(), in.deletedAt.addSecs(3)};
    QVERIFY(PaneroRecycleBin::matches(in, item));
    item.deletedAt = in.deletedAt.addSecs(3600);
    QVERIFY(!PaneroRecycleBin::matches(in, item));
    item.deletedAt = in.deletedAt;
    item.originalPath = QStringLiteral("C:\\Users\\Test\\Documents\\other.txt");
    QVERIFY(!PaneroRecycleBin::matches(in, item));
}

void PaneroJobsTest::infoPath()
{
    QCOMPARE(PaneroRecycleBin::infoPathFor(QStringLiteral("C:\\$Recycle.Bin\\S-1-5-21-1\\$RAB12CD.txt")),
             QStringLiteral("C:\\$Recycle.Bin\\S-1-5-21-1\\$IAB12CD.txt"));
    QCOMPARE(PaneroRecycleBin::infoPathFor(QStringLiteral("C:/$Recycle.Bin/S-1/$R0Q1W2E")),
             QStringLiteral("C:\\$Recycle.Bin\\S-1\\$I0Q1W2E"));
    QVERIFY(PaneroRecycleBin::infoPathFor(QStringLiteral("C:\\Users\\a.txt")).isEmpty());
}

void PaneroJobsTest::undoMoveRenameCopyMkdir()
{
    const QString a = m_dir + QStringLiteral("/a.txt");
    const QString sub = m_dir + QStringLiteral("/sub");
    QVERIFY(QDir().mkpath(sub));
    writeFile(a, "alpha");

    // move a.txt into sub, undo: back
    KIO::CopyJob *move = KIO::move(QUrl::fromLocalFile(a), QUrl::fromLocalFile(sub + QLatin1Char('/')), KIO::HideProgressInfo);
    KIO::FileUndoManager::self()->recordCopyJob(move);
    QVERIFY(runJob(move));
    QVERIFY(QFile::exists(sub + QStringLiteral("/a.txt")));
    QVERIFY(PaneroUndo::self()->isUndoAvailable());
    QVERIFY(PaneroUndo::self()->undoText().contains(QLatin1String("Move")));
    QVERIFY(waitForUndo());
    QTRY_VERIFY(QFile::exists(a));
    QVERIFY(!QFile::exists(sub + QStringLiteral("/a.txt")));

    // rename (F2 records it like this), undo: old name
    const QString b = m_dir + QStringLiteral("/b.txt");
    KIO::Job *rename = KIO::moveAs(QUrl::fromLocalFile(a), QUrl::fromLocalFile(b), KIO::HideProgressInfo);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Rename, {QUrl::fromLocalFile(a)}, QUrl::fromLocalFile(b), rename);
    QVERIFY(runJob(rename));
    QVERIFY(QFile::exists(b));
    QVERIFY(waitForUndo());
    QTRY_VERIFY(QFile::exists(a));
    QVERIFY(!QFile::exists(b));

    // copy, undo: the copy is deleted, the original stays
    KIO::CopyJob *copy = KIO::copy(QUrl::fromLocalFile(a), QUrl::fromLocalFile(sub + QLatin1Char('/')), KIO::HideProgressInfo);
    KIO::FileUndoManager::self()->recordCopyJob(copy);
    QVERIFY(runJob(copy));
    QVERIFY(QFile::exists(sub + QStringLiteral("/a.txt")));
    QVERIFY(waitForUndo());
    QTRY_VERIFY(!QFile::exists(sub + QStringLiteral("/a.txt")));
    QVERIFY(QFile::exists(a));

    // new folder (F7 records it like this), undo: removed
    const QString folder = m_dir + QStringLiteral("/new folder");
    KIO::SimpleJob *mkdir = KIO::mkdir(QUrl::fromLocalFile(folder));
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Mkdir, {}, QUrl::fromLocalFile(folder), mkdir);
    QVERIFY(runJob(mkdir));
    QVERIFY(QFileInfo(folder).isDir());
    QVERIFY(waitForUndo());
    QTRY_VERIFY(!QFileInfo::exists(folder));
}

void PaneroJobsTest::undoTrash()
{
#ifdef Q_OS_WIN
    QSKIP("Windows uses the Recycle Bin (recycleBinRestore)");
#else
    // KIO's trash in the temporary XDG_DATA_HOME (same file system as the test files)
    const QString c = m_dir + QStringLiteral("/c.txt");
    writeFile(c, "gamma");
    KIO::Job *trash = KIO::trash({QUrl::fromLocalFile(c)}, KIO::HideProgressInfo);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Trash, {QUrl::fromLocalFile(c)}, QUrl(QStringLiteral("trash:/")), trash);
    QVERIFY(runJob(trash));
    if (trash->error())
        QSKIP(qPrintable(QStringLiteral("no trash here: ") + trash->errorString()));
    QVERIFY(!QFile::exists(c));
    QVERIFY(QDir(s_home->path() + QStringLiteral("/data/Trash/files")).entryList(QDir::Files).contains(QStringLiteral("c.txt")));
    QVERIFY(waitForUndo());
    QTRY_VERIFY(QFile::exists(c));
#endif
}

void PaneroJobsTest::recycleBinRestore()
{
#ifndef Q_OS_WIN
    QSKIP("Windows only");
#else
    // a real move to the Recycle Bin of a file this test made, then Undo puts it back
    const QString d = QDir::toNativeSeparators(m_dir + QStringLiteral("/panero-recycle-test-%1.txt").arg(QDateTime::currentMSecsSinceEpoch()));
    writeFile(d, "delta");
    auto *job = new PaneroRecycleJob({QUrl::fromLocalFile(d)});
    QSignalSpy recorded(PaneroUndo::self(), &PaneroUndo::undoTextChanged);
    QSignalSpy result(job, &KJob::result);
    QVERIFY(result.wait(30000));
    if (job->error())
        QSKIP(qPrintable(QStringLiteral("no Recycle Bin here: ") + job->errorString()));
    QVERIFY(!QFile::exists(d));
    QTRY_VERIFY(PaneroUndo::self()->undoText().contains(QLatin1String("Recycle Bin")));
    // the item is found by its record (original path + time) also without the path Windows reported
    PaneroRecycleBin::Item item{d, QString(), QDateTime::currentDateTimeUtc()};
    const QString recycled = PaneroRecycleBin::findRecycled(item);
    QVERIFY2(!recycled.isEmpty(), "record not found");
    QVERIFY(QFile::exists(PaneroRecycleBin::infoPathFor(recycled)));
    QVERIFY(waitForUndo());
    QVERIFY(QFile::exists(d));
    QVERIFY(!QFile::exists(recycled));
    QVERIFY(!QFile::exists(PaneroRecycleBin::infoPathFor(recycled)));
    QFile::remove(d);
#endif
}

int main(int argc, char **argv)
{
    // no session bus: FileUndoManager would share its commands with the user's applications
    qputenv("DBUS_SESSION_BUS_ADDRESS", "disabled:");
    qputenv("KDE_FORK_SLAVES", "1");
    QTemporaryDir home;
    s_home = &home;
#ifndef Q_OS_WIN
    qputenv("HOME", home.path().toLocal8Bit());
    qputenv("XDG_DATA_HOME", (home.path() + QStringLiteral("/data")).toLocal8Bit());
    qputenv("XDG_CONFIG_HOME", (home.path() + QStringLiteral("/config")).toLocal8Bit());
    qputenv("XDG_CACHE_HOME", (home.path() + QStringLiteral("/cache")).toLocal8Bit());
#endif
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("panerojobstest"));
    PaneroJobsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "panerojobstest.moc"
