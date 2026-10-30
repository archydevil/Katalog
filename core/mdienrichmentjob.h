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
//              (F11), incremental (F13), waits for a disconnected drive (F91).
//              External tools through QProcess with an argument list (MDI-C7);
//              local AI over HTTP (Ollama).
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
#include <QHash>
#include <QMutex>
#include <QWaitCondition>
#include <QVector>
#include <atomic>
#include <functional>

#include "core/mdiknowledge.h"
#include "core/mdifaces.h"
#include <memory>

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
        QString metadata;      // extended metadata (JSON), read by the classification (MDI-C34)
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
        QString     whisperVadModelPath; // ggml-silero-*.bin: whisper skips what is not speech
        QString     faceDetectorModelPath;     // YuNet ONNX (faces, OpenCV)
        QString     faceRecognizerModelPath;   // SFace ONNX
        double      faceMatchThreshold = 0.363;   // cosine from which a face is suggested as a person
        QString     ollamaUrl = QStringLiteral("http://localhost:11434");
        QString     visionModel = QStringLiteral("gemma4");
        QString     textModel   = QStringLiteral("gemma4");
        QString     embedModel  = QStringLiteral("bge-m3");   // semantic search (MDI-F47)
        QString     uiLanguageName = QStringLiteral("English");   // captions of images
        int         toolTimeoutMs = 10 * 60 * 1000;               // MDI-F22
        int         httpTimeoutMs = 120 * 1000;                   // MDI-C20
        bool        quickHashOnly = false;   // a pass computing only the quick hash
        QString     catalogStamp;            // catalog state the passes complete on
        QString     sourceRoot;              // catalog's device path: waited for when away (MDI-F91)
        // Domain context (D33): the collection's context and the catalog's
        // notes; empty when none (no classification then)
        QJsonObject context;
        QString     catalogNotes;
        bool        reinterpret = false;     // classification only, forced, on a scope (D33)
        QString     scopeFolder;             // the scope's folder (full path), all when empty
        QString     scopeFileType;           // the scope's file type ("image"...), all when empty
        bool        knowledgeOnly = false;   // a pass indexing the knowledge base's pending documents
        bool        facesOnly = false;       // the "faces" analysis only, on the catalog's images
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
    // whisper.cpp's voice-activity model (ggml-silero-*.bin) in the same
    // places; empty when none (the transcript then runs without --vad)
    static QString findWhisperVadModel(const QString &configuredPath = QString());

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
    // and, apart from steps, "waiting" with waiting (bool) and drivePath when
    // the job starts and stops waiting for a drive (MDI-F91)
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
    Result makeSnippet(const FileItem &file);
    Result makeQuickHash(const FileItem &file);
    Result makeMediaInfo(const FileItem &file);
    Result makeArchiveListing(const FileItem &file);
    // Interpretation in the collection's context, text only (D33)
    Result makeClassification(const FileItem &file);
    // Faces of an image: found, embedded, cropped, suggested (never confirmed)
    Result makeFaces(const FileItem &file);
    std::unique_ptr<MdiFaces::Engine> m_faceEngine;   // loaded on the job's thread at first use
    QString                           m_faceEngineError;
    // Knowledge-base pass: text, passages and embeddings of pending documents
    void   indexKnowledge(int &completed, int &skipped, int &failed);
    MdiKnowledge::Index m_knowledge;    // loaded once per job for the classification
    QString m_contextVersion;           // the collection's context and the catalog's notes (MDI-F96)
    // Parallel image-thumbnail pre-pass over files [from, to) (QThreadPool)
    void   precomputeThumbnails(int from, int to);
    QHash<QString, Result> m_precomputed;   // full path -> thumbnail done by the pre-pass
    // ffmpeg writes a JPEG frame; this stores it as the thumbnail (WebP when possible)
    bool   finishThumbnail(const QString &jpegPath, const QString &targetPath, QString *error);
    QString transcriptText(const FileItem &file);   // from this catalog's transcript datum

    // whole: every page and character (knowledge base), not the summary's share
    QString extractText(const QString &path, QString *tool, QString *reason, bool whole = false);
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
    // The path of the drive the job waits for (MDI-F91, MDI-C29): the source,
    // or the destination of the derived files; empty when both are there
    QString driveAway() const;
    bool    waitForSource();     // false when stopped while waiting

    Config          m_config;
    QList<FileItem> m_files;
    QString         m_connectionName;
    QString         m_ffmpegVersion;
    bool            m_segmentsChanged = false;
    int             m_unreachable = 0;   // sources not reachable in this run
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
