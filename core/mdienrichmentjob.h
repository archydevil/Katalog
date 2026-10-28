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
#include <QVector>
#include <atomic>
#include <functional>

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
        QString     whisperPath;         // whisper-cli (whisper.cpp)
        QString     whisperModelPath;    // ggml model file
        QString     ollamaUrl = QStringLiteral("http://localhost:11434");
        QString     visionModel = QStringLiteral("gemma4");
        QString     textModel   = QStringLiteral("gemma4");
        QString     embedModel  = QStringLiteral("bge-m3");   // semantic search (MDI-F47)
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

    // A whisper.cpp ggml model: the configured file, else the largest
    // ggml-*.bin in ~/whisper-models or the Homebrew share folder; empty when none
    static QString findWhisperModel(const QString &configuredPath = QString());

    // Unit-length embeddings of `texts` from an Ollama endpoint, in order; empty
    // on error. Synchronous: usable on any thread (the search query is
    // embedded on the UI thread). Local endpoints only, as for the job.
    static QList<QVector<float>> embedTexts(const QString &ollamaUrl, const QString &model,
                                            const QStringList &texts, QString *error,
                                            int timeoutMs = 120 * 1000);

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

    // What runs, where, for the Activity page. One "start" and one "end" per
    // analysis that runs, "progress" in between when the tool reports it:
    //   event: "start" | "progress" | "end"
    //   analysis, filePath, fileIndex, fileCount (as progress())
    //   tool ("ffmpeg", "whisper.cpp", "ollama"), model, where ("local", "local-network", "remote")
    //   start:    startedAt (ISO time)
    //   progress: progress (0..1), detail (e.g. "9 / 15 min")
    //   end:      outcome, reason, durationMs, tokensPerSecond (AI analyses, when known)
    void activity(const QVariantMap &step);

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
    Result makeTranscript(const FileItem &file);
    Result makeSegments(const FileItem &file);
    // Catalog-level: groups the catalog's topic segments (MDI-F37, scope per catalog)
    Result makeClusters();
    Result makeEmbeddings(const FileItem &file);
    QString transcriptText(const FileItem &file);   // from this catalog's transcript datum

    QString extractText(const QString &path, QString *tool, QString *reason);
    QString findTranscript(const FileItem &file) const;
    double  probeDuration(const QString &path);
    bool    runTool(const QString &program, const QStringList &args, QByteArray *output, QString *error,
                    int timeoutMs = -1,
                    const std::function<void(const QString &)> &onErrorLine = {});
    QVariantMap stepInfo(const QString &analysis) const;   // tool, model, where
    QJsonObject askOllama(const QString &model, const QString &prompt, const QStringList &imagePaths, QString *error);
    QJsonObject askOllamaOnce(const QString &model, const QString &prompt, const QStringList &imagePaths, QString *error);

    QString derivedRelative(const FileItem &file, const QString &analysis, const QString &suffix) const;
    QString absoluteDerived(const QString &relative) const;
    bool    waitWhilePaused();   // false when stopped while paused

    Config          m_config;
    QList<FileItem> m_files;
    QString         m_connectionName;
    QString         m_ffmpegVersion;
    bool            m_segmentsChanged = false;
    // Current step, for progress reports from inside an analysis
    QString         m_stepAnalysis;
    QString         m_stepFile;
    int             m_stepIndex = 0;
    double          m_lastTokensPerSecond = -1;

    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_paused{false};
    QMutex            m_pauseMutex;
    QWaitCondition    m_pauseCondition;
};

#endif // MDIENRICHMENTJOB_H
