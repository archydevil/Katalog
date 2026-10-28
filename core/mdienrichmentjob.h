/*LICENCE
    This file is part of Katalog

    Copyright (C) 2020, the Katalog Development team

    Author: Stephane Couturier (Symbioxy)

    Katalog is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Katalog is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Katalog; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
/*FILE DESCRIPTION
/////////////////////////////////////////////////////////////////////////////
// Application: Katalog
// File Name:   mdienrichmentjob.h
// Purpose:     Enrichment runner (fork only, SpecMediaIntelligence.md)
// Description: Worker for one catalog's enrichment, run on its own QThread like
//              BackupJobStoppable (MDI-C3). One file at a time, each analysis
//              committed before the next (MDI-F9); stoppable (F10), pausable
//              (F11), incremental (F13). External tools through QProcess with
//              an argument list (MDI-C7); local AI over HTTP (Ollama).
//
// Usage:
//   QThread *thread = new QThread();
//   MdiEnrichmentJob *job = new MdiEnrichmentJob(config, files);
//   job->moveToThread(thread);
//   connect(thread, &QThread::started,  job,    &MdiEnrichmentJob::run);
//   connect(job,    &MdiEnrichmentJob::finished, thread, &QThread::quit);
//   connect(thread, &QThread::finished, job,    &QObject::deleteLater);
//   connect(thread, &QThread::finished, thread, &QObject::deleteLater);
//   thread->start();
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDIENRICHMENTJOB_H
#define MDIENRICHMENTJOB_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QVariantMap>
#include <QList>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>

class QProcess;

class MdiEnrichmentJob : public QObject
{
    Q_OBJECT
public:
    struct FileItem {
        QString fullPath;
        QString fileType;      // "image", "video", "audio", "text", "other"
        QString extension;
        qint64  size = 0;
        QString dateUpdated;
    };

    struct Config {
        int         catalogId = 0;
        QString     catalogName;
        QString     dataDbPath;          // mdi_data.db of the collection
        QString     destinationPath;     // derived files root (profile)
        QJsonObject settings;            // profile settings (wizard choices)
        QString     ffmpegPath;
        QString     ffprobePath;
        QString     pdftotextPath;
        QString     ollamaUrl = QStringLiteral("http://localhost:11434");
        QString     visionModel = QStringLiteral("gemma4");
        QString     textModel   = QStringLiteral("gemma4");
        QString     uiLanguageName = QStringLiteral("English");   // captions of images
        int         toolTimeoutMs = 10 * 60 * 1000;               // MDI-F22
        int         httpTimeoutMs = 120 * 1000;                   // MDI-C20
    };

    MdiEnrichmentJob(const Config &config, const QList<FileItem> &files, QObject *parent = nullptr);

    // Files of one catalog, read on the caller's thread: from the catalog's
    // .idx file in Memory mode, from the file table otherwise
    static QList<FileItem> listCatalogFiles(const QString &connectionName, int catalogId,
                                            const QString &databaseMode, const QString &catalogFilePath);

    // Finds a tool on the usual locations and the search path; empty when absent (MDI-F20)
    static QString findTool(const QString &name, const QString &configuredPath = QString());

    void requestStop();
    void setPaused(bool paused);
    bool isPaused() const { return m_paused.load(); }

public slots:
    void run();

signals:
    // fileIndex is 1-based; analysis is the analysis key being run
    void progress(int fileIndex, int fileCount, const QString &analysis, const QString &filePath);
    void datumCommitted(const QString &fullPath, const QString &analysis);
    // Counts per outcome: completed, unchanged, skipped, failed; "stopped": bool; "error": text
    void finished(const QVariantMap &summary);

private:
    struct Result {
        QString     outcome = QStringLiteral("completed");
        QString     reason;
        QJsonObject value;
        QStringList derivedFiles;   // relative to the catalog's derived folder root
        QString     tool;
        QString     model;
    };

    QStringList plannedAnalyses(const FileItem &file) const;
    QJsonObject analysisParams(const QString &analysis) const;
    Result runAnalysis(const QString &analysis, const FileItem &file);

    Result makeThumbnail(const FileItem &file);
    Result makePosterFrame(const FileItem &file);
    Result makeKeyframes(const FileItem &file);
    Result makeClip(const FileItem &file, bool video);
    Result makeWaveform(const FileItem &file);
    Result makeCaption(const FileItem &file);
    Result makeSummary(const FileItem &file);

    QString extractText(const QString &path, QString *tool, QString *reason);
    QString findTranscript(const FileItem &file) const;
    double  probeDuration(const QString &path);
    bool    runTool(const QString &program, const QStringList &args, QByteArray *output, QString *error);
    QJsonObject askOllama(const QString &model, const QString &prompt, const QStringList &imagePaths, QString *error);
    QJsonObject askOllamaOnce(const QString &model, const QString &prompt, const QStringList &imagePaths, QString *error);

    QString derivedRelative(const FileItem &file, const QString &analysis, const QString &suffix) const;
    QString absoluteDerived(const QString &relative) const;
    bool    waitWhilePaused();   // false when stopped while paused

    Config          m_config;
    QList<FileItem> m_files;
    QString         m_connectionName;
    QString         m_ffmpegVersion;

    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_paused{false};
    QMutex            m_pauseMutex;
    QWaitCondition    m_pauseCondition;
};

#endif // MDIENRICHMENTJOB_H
