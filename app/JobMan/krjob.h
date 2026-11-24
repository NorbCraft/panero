/*
    SPDX-FileCopyrightText: 2016-2022 Krusader Krew <https://krusader.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KRJOB_H
#define KRJOB_H

#include <KIO/Job>
#include <KIO/CopyJob>
#include <KIO/DropJob>

/**
 * Wrapper for KIO::Job cause it has limitations.
 *
 * KIO::Jobs cannot be started in pause mode (and pausing directly after creation is buggy).
 * Instead, a KrJob can be created which creates the KIO::Job on first start() call.
 *
 * Started jobs are recorded by KIO/FileUndoManager (if supported).
 *
 * KrJob is deleted after KIO::Job was deleted (which is after finished() call). Do not use
 * KIO::Job::finished() but KrJob::terminated() to be prepared for job deletion.
 */
class KrJob : public QObject
{
    Q_OBJECT
public:
    /** Supported job types. Add other types if needed.*/
    enum Type { Copy, Move, Link, Trash, Delete, Pack, Unpack, Test };

    /** Create a new copy, move, or link job. */
    static KrJob *createCopyJob(KIO::CopyJob::CopyMode mode, const QList<QUrl> &src,
                         const QUrl &destination, KIO::JobFlags flags);
    /** Create a new trash or delete job. */
    static KrJob *createDeleteJob(const QList<QUrl> &urls, bool moveToTrash);
    /** Create a drop job - the copy job is already started.*/
    static KrJob *createDropJob(KIO::DropJob *dropJob, KIO::CopyJob *job);
    /** Panero: wrap an already started job (pack, unpack, test archives) for the job manager. */
    static KrJob *createStartedJob(Type type, const QList<QUrl> &src, const QUrl &destination,
                                   const QString &description, KIO::Job *job);

    /** Start or resume this job. If job was started started() is emitted. */
    void start();
    /** Cancel this job and mark for deletion. terminated() will be emitted.*/
    void cancel();
    /** Suspend job (if started). */
    void pause();

    /** Return true if job was started and is not suspended(). */
    bool isRunning() const { return m_job && !m_job->isSuspended(); }
    /** Return true if job was started and then paused by user. */
    bool isPaused() const { return m_job && m_job->isSuspended(); }
    /** Return percent progress of job. */
    int percent() const { return m_job ? static_cast<int>(m_job->percent()) : 0; }

    /** Return (initial) job description.
     * The KIO::Job emits a more detailed description after start.
     */
    QString description() const { return m_description; }
    // Panero: for the progress windows and the jobs button
    Type type() const { return m_type; }
    QUrl destination() const { return m_dest; }
    QList<QUrl> sources() const { return m_urls; }
    KIO::Job *kioJob() const { return m_job; }

signals:
    /** Emitted if job was started. Parameter is the KIO::Job that was created. */
    void started(KIO::Job *job);
    /** Emitted if job is finished or was canceled. Job will be deleted afterwards. */
    void terminated(KrJob *krJob);

private:
    KrJob(Type type, const QList<QUrl> &urls, const QUrl &dest, KIO::JobFlags flags,
          const QString &description, KIO::Job *startedJob = nullptr,
          KIO::DropJob *dropJob = nullptr);
    static KrJob *createKrCopyJob(KIO::CopyJob::CopyMode mode, const QList<QUrl> &src,
                              const QUrl &destination, KIO::JobFlags flags,
                              KIO::CopyJob *job = nullptr, KIO::DropJob *dropJob = nullptr);
    void connectStartedJob();

    const Type m_type;
    const QList<QUrl> m_urls;
    const QUrl m_dest;
    const KIO::JobFlags m_flags;
    const QString m_description;

    KIO::Job *m_job;
    KIO::DropJob *m_dropJob;
};

#endif // KRJOB_H
