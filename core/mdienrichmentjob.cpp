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
// File Name:   mdienrichmentjob.cpp
// Purpose:     Enrichment runner (fork only, SpecMediaIntelligence.md)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdienrichmentjob.h"
#include "core/mdidata.h"
#include "core/mdifts.h"
#include "core/filechecksum.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMimeDatabase>
#include <QTemporaryDir>
#include <QImage>
#include <QImageWriter>
#include <QImageReader>
#include <QPainter>
#include <QThreadPool>
#include <QRunnable>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QUrl>
#include <QThread>
#include <QSet>
#include <QRegularExpression>
#include <QDebug>
#include <algorithm>
#include <memory>
#include <cmath>

#include <KFileMetaData/ExtractorCollection>
#include <KFileMetaData/Extractor>
#include <KFileMetaData/SimpleExtractionResult>

#ifdef KATALOG_HAS_KARCHIVE
#include <KZip>
#include <KTar>
#include <K7Zip>
#include <KArchiveDirectory>
#include <KArchiveFile>
#include <KCompressionDevice>
#endif

namespace {

// Prompt versions take part in the parameters, so a changed prompt re-runs
// its analysis on the next job (MDI-F54).
const QString kCaptionPrompt = QStringLiteral("caption-v1");
const QString kSummaryPrompt = QStringLiteral("summary-v3");
const QString kSegmentsPrompt = QStringLiteral("segments-v1");
const QString kClustersPrompt = QStringLiteral("clusters-v1");
const QString kEmbedVersion   = QStringLiteral("embed-v1");

// Transcript windows indexed for search: about this long, in seconds or words
constexpr double kWindowSeconds = 45.0;
constexpr int    kWindowWords   = 120;
constexpr int    kEmbedBatch    = 32;

// Transcript text sent per segmentation request
constexpr int kSegmentChunkChars = 20000;
constexpr int kMinSegmentWords   = 150;

// Text sent to the model is cut to this length: long documents are summarised
// from their beginning. The context window is sized for it: with Ollama's
// default window a long transcript is cut and the answer is no longer JSON.
constexpr int kMaxTextChars  = 24000;
constexpr int kContextTokens = 16384;
constexpr int kAnswerTokens  = 2048;

constexpr int kThumbnailSize = 256;   // longest side (owner's choice; MDI-F29 range 128-1024)
constexpr int kThumbQuality  = 75;    // WebP quality: ~5 KB per thumbnail
constexpr int kSheetHeight   = 144;   // height of each frame in a video contact sheet
constexpr int kParallelChunk = 64;    // files per parallel thumbnail pre-pass
constexpr int kSnippetChars  = 800;   // text kept for offline full-text search

// Archive listing: entries stored per archive (all are counted), and the
// largest compressed tar listed (a tar has no index: listing reads it whole)
constexpr int    kArchiveMaxEntries = 20000;
constexpr qint64 kArchiveMaxTarSize = qint64(2) * 1024 * 1024 * 1024;

// "zip", "tar", "7z" or empty, from the file name (".tar.gz" has suffix "gz")
QString archiveKind(const QString &fullPath)
{
    const QString name = QFileInfo(fullPath).fileName().toLower();
    if (name.endsWith(QLatin1String(".zip")) || name.endsWith(QLatin1String(".jar")))
        return QStringLiteral("zip");
    if (name.endsWith(QLatin1String(".7z")))
        return QStringLiteral("7z");
    for (const char *t : {".tar", ".tar.gz", ".tgz", ".tar.bz2", ".tbz", ".tbz2", ".tar.xz", ".txz", ".tar.zst"})
        if (name.endsWith(QLatin1String(t)))
            return QStringLiteral("tar");
    return QString();
}

// Files whose first characters are kept as a snippet, whatever their catalog type
const QStringList kSnippetSuffixes = {
    "txt", "md", "markdown", "csv", "tsv", "json", "xml", "log", "srt", "vtt", "rtf", "html", "htm",
    "pdf", "docx", "odt", "doc", "pptx", "odp", "xlsx", "ods",
};

// Offline thumbnails are WebP when Qt can write it (owner's choice), JPEG otherwise
QString thumbnailSuffix()
{
    static const QString suffix = QImageWriter::supportedImageFormats().contains("webp")
                                  ? QStringLiteral(".webp") : QStringLiteral(".jpg");
    return suffix;
}

QString lowerType(const QString &profileType)
{
    return profileType.toLower();
}

QString providerClass(const QUrl &url)
{
    const QString host = url.host();
    if (host == QLatin1String("localhost"))
        return QStringLiteral("local");
    QHostAddress address(host);
    if (address.isLoopback())
        return QStringLiteral("local");
    if (!address.isNull()
        && (address.isInSubnet(QHostAddress(QStringLiteral("10.0.0.0")), 8)
            || address.isInSubnet(QHostAddress(QStringLiteral("172.16.0.0")), 12)
            || address.isInSubnet(QHostAddress(QStringLiteral("192.168.0.0")), 16)))
        return QStringLiteral("local-network");
    return QStringLiteral("remote");
}

// "h:mm:ss", "mm:ss", "ss" or a number of seconds
double parseTimecode(const QJsonValue &value)
{
    if (value.isDouble())
        return value.toDouble();
    const QStringList parts = value.toString().trimmed().split(':');
    double seconds = 0.0;
    for (const QString &part : parts)
        seconds = seconds * 60.0 + part.toDouble();
    return seconds;
}

QString formatTimecode(double seconds)
{
    const qint64 s = qint64(seconds);
    return QStringLiteral("%1:%2:%3").arg(s / 3600)
        .arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString srtTime(double seconds)
{
    const qint64 ms = qint64(seconds * 1000.0);
    return QStringLiteral("%1:%2:%3,%4").arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg((ms / 60000) % 60, 2, 10, QLatin1Char('0'))
        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QJsonArray stringArray(const QJsonValue &value)
{
    QJsonArray out;
    for (const QJsonValue &v : value.toArray())
        if (v.isString() && !v.toString().trimmed().isEmpty())
            out.append(v.toString().trimmed());
    return out;
}

} // namespace

MdiEnrichmentJob::MdiEnrichmentJob(const Config &config, const QList<FileItem> &files, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_files(files)
{
    m_connectionName = QStringLiteral("mdi_job_%1").arg(reinterpret_cast<quintptr>(this));
}

//----------------------------------------------------------------------
QList<MdiEnrichmentJob::FileItem> MdiEnrichmentJob::listCatalogFiles(const QString &connectionName, int catalogId,
                                                                      const QString &databaseMode,
                                                                      const QString &catalogFilePath)
{
    QList<FileItem> files;

    if (databaseMode == QLatin1String("Memory")) {
        // Same layout Catalog::loadCatalogFileListToTable reads: header lines
        // start with '<', then full path, size, date, extension, type.
        QFile idx(catalogFilePath);
        if (!idx.open(QIODevice::ReadOnly | QIODevice::Text))
            return files;
        QTextStream in(&idx);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.isEmpty() || line.startsWith('<'))
                continue;
            const QStringList f = line.split('\t');
            if (f.size() < 5)
                continue;
            FileItem item;
            item.fullPath    = f[0];
            item.size        = f[1].toLongLong();
            item.dateUpdated = f[2];
            item.extension   = f[3].toLower();
            item.fileType    = f[4].toLower();
            files.append(item);
        }
        return files;
    }

    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QLatin1String(R"(
        SELECT file_full_path, file_size, file_date_updated, file_extension, file_type
        FROM file
        WHERE file_catalog_id = :catalog_id
        ORDER BY file_full_path
    )"));
    q.bindValue(":catalog_id", catalogId);
    if (!q.exec())
        return files;
    while (q.next()) {
        FileItem item;
        item.fullPath    = q.value(0).toString();
        item.size        = q.value(1).toLongLong();
        item.dateUpdated = q.value(2).toString();
        item.extension   = q.value(3).toString().toLower();
        item.fileType    = q.value(4).toString().toLower();
        files.append(item);
    }
    return files;
}

//----------------------------------------------------------------------
QString MdiEnrichmentJob::findTool(const QString &name, const QString &configuredPath)
{
    if (!configuredPath.isEmpty() && QFileInfo(configuredPath).isExecutable())
        return configuredPath;
    // GUI applications on macOS do not inherit the shell's PATH, so the
    // package-manager locations are tried before the search path.
    const QStringList extraDirs = {
        QStringLiteral("/opt/homebrew/bin"),
        QStringLiteral("/usr/local/bin"),
        QStringLiteral("/usr/bin"),
    };
    QString found = QStandardPaths::findExecutable(name, extraDirs);
    if (found.isEmpty())
        found = QStandardPaths::findExecutable(name);
    return found;
}

//----------------------------------------------------------------------
QString MdiEnrichmentJob::findWhisperModel(const QString &configuredPath)
{
    if (!configuredPath.isEmpty() && QFileInfo::exists(configuredPath))
        return configuredPath;
    const QStringList dirs = {
        QDir::homePath() + QStringLiteral("/whisper-models"),
        QStringLiteral("/opt/homebrew/share/whisper-cpp"),
        QStringLiteral("/usr/local/share/whisper-cpp"),
    };
    QFileInfo best;
    for (const QString &d : dirs)
        for (const QFileInfo &f : QDir(d).entryInfoList({QStringLiteral("ggml-*.bin")}, QDir::Files))
            if (!best.exists() || f.size() > best.size())
                best = f;
    return best.exists() ? best.absoluteFilePath() : QString();
}

//----------------------------------------------------------------------
void MdiEnrichmentJob::requestStop()
{
    m_stop = true;
    QMutexLocker locker(&m_pauseMutex);
    m_pauseCondition.wakeAll();
}

void MdiEnrichmentJob::setPaused(bool paused)
{
    m_paused = paused;
    QMutexLocker locker(&m_pauseMutex);
    m_pauseCondition.wakeAll();
}

bool MdiEnrichmentJob::waitWhilePaused()
{
    QMutexLocker locker(&m_pauseMutex);
    while (m_paused.load() && !m_stop.load())
        m_pauseCondition.wait(&m_pauseMutex, 200);
    return !m_stop.load();
}

QString MdiEnrichmentJob::driveAway() const
{
    // The source drive; a quick-hash pass never waits, it is scheduled again
    // (MDI-F84). A fixed mount point stays as an empty folder when its drive
    // is gone.
    if (!m_config.quickHashOnly && !m_config.sourceRoot.isEmpty()
        && (!QFileInfo::exists(m_config.sourceRoot) || QDir(m_config.sourceRoot).isEmpty()))
        return m_config.sourceRoot;
    // The drive of the derived files, waited for as well (D31). The folder
    // itself is created when first used: only a folder that cannot be created
    // (its drive is away) is waited for.
    if (!m_config.destinationPath.isEmpty() && !QFileInfo::exists(m_config.destinationPath)
        && !QDir().mkpath(m_config.destinationPath))
        return m_config.destinationPath;
    return QString();
}

bool MdiEnrichmentJob::waitForSource()
{
    const QString path = driveAway();
    if (path.isEmpty())
        return !m_stop.load();
    // Nothing runs, nothing is read but the path itself, every 2 s;
    // Pause and Stop are seen in between (MDI-F91, MDI-C29)
    QVariantMap waiting;
    waiting[QStringLiteral("event")]      = QStringLiteral("waiting");
    waiting[QStringLiteral("waiting")]    = true;
    waiting[QStringLiteral("drivePath")]  = path;
    emit activity(waiting);
    while (!m_stop.load() && !driveAway().isEmpty()) {
        QMutexLocker locker(&m_pauseMutex);
        if (!m_stop.load())
            m_pauseCondition.wait(&m_pauseMutex, 2000);
    }
    waiting[QStringLiteral("waiting")] = false;
    emit activity(waiting);
    return !m_stop.load();
}

//----------------------------------------------------------------------
void MdiEnrichmentJob::run()
{
    QVariantMap summary;
    int completed = 0, unchanged = 0, skipped = 0, failed = 0;

    if (!MdiData::open(m_connectionName, m_config.dataDbPath)) {
        summary[QStringLiteral("error")] = QStringLiteral("mdi_data.db");
        emit finished(summary);
        return;
    }

    if (!m_config.ffmpegPath.isEmpty()) {
        QByteArray out;
        QString err;
        if (runTool(m_config.ffmpegPath, {QStringLiteral("-version")}, &out, &err))
            m_ffmpegVersion = QString::fromUtf8(out).section('\n', 0, 0).section(' ', 0, 2);
    }

    const QString derivedRoot = QDir(m_config.destinationPath).filePath(QStringLiteral("katalog_mdi"));
    const int fileCount = m_files.size();

    // Clusters are computed when segments changed, or when segments exist
    // without clusters (first run after an upgrade, or a stopped run)
    const bool hasClusters = !MdiData::forCatalog(m_connectionName, m_config.catalogId,
                                                  QStringLiteral("clusters")).isEmpty();
    m_segmentsChanged = !hasClusters && !MdiData::forCatalog(m_connectionName, m_config.catalogId,
                                                             QStringLiteral("segments")).isEmpty();

    int precomputedChunk = -1;
    for (int i = 0; i < fileCount && !m_stop.load(); ++i) {
        // Nothing is read from the source while paused or while its drive is
        // away, the thumbnail pre-pass included (MDI-F11, MDI-F91)
        if (!waitWhilePaused())
            break;
        if (i / kParallelChunk != precomputedChunk) {
            // Paused while waiting: still paused when the drive is back
            if (!waitForSource() || !waitWhilePaused())
                break;
            precomputedChunk = i / kParallelChunk;
            precomputeThumbnails(i, qMin(fileCount, (precomputedChunk + 1) * kParallelChunk));
        }
        const FileItem &file = m_files.at(i);
        const QStringList analyses = plannedAnalyses(file);
        if (analyses.isEmpty())
            continue;

        // A source that cannot be reached keeps its data: nothing is
        // overwritten, so it stays browsable offline (MDI-F44). A file gone
        // while its drive is there is skipped; a drive gone is waited for,
        // and the job goes on with this same file once it is back (MDI-F91).
        if (!QFileInfo::exists(file.fullPath)) {
            if (!driveAway().isEmpty()) {
                if (!waitForSource())
                    break;
                --i;
                continue;
            }
            skipped += analyses.size();
            ++m_unreachable;
            continue;
        }

        // Embeddings index the file's other data: redone whenever one of them
        // was produced in this run
        bool fileChanged = false;
        for (int a = 0; a < analyses.size(); ++a) {
            const QString &analysis = analyses.at(a);
            if (!waitWhilePaused())
                break;

            const QJsonObject params = analysisParams(analysis);
            // Incremental (MDI-F13): same file state and parameters, and the
            // derived file still present, means nothing to do.
            const bool forced = analysis == QLatin1String("embeddings") && fileChanged;
            if (!forced && MdiData::isCurrent(m_connectionName, m_config.catalogId, file.fullPath, analysis,
                                              file.size, file.dateUpdated, params)) {
                bool filesPresent = true;
                for (const MdiData::Datum &d : MdiData::forFile(m_connectionName, m_config.catalogId, file.fullPath))
                    if (d.analysis == analysis)
                        for (const QString &rel : d.derivedFiles)
                            filesPresent = filesPresent && QFileInfo::exists(QDir(d.derivedRoot).filePath(rel));
                if (filesPresent) {
                    ++unchanged;
                    continue;
                }
            }

            emit progress(i + 1, fileCount, analysis, file.fullPath);
            m_stepAnalysis = analysis;
            m_stepFile     = file.fullPath;
            m_stepIndex    = i + 1;
            m_lastTokensPerSecond = -1;
            QVariantMap step = stepInfo(analysis);
            step[QStringLiteral("analysis")]  = analysis;
            step[QStringLiteral("filePath")]  = file.fullPath;
            step[QStringLiteral("fileIndex")] = i + 1;
            step[QStringLiteral("fileCount")] = fileCount;
            QVariantMap started = step;
            started[QStringLiteral("event")]     = QStringLiteral("start");
            started[QStringLiteral("startedAt")] = QDateTime::currentDateTime().toString(Qt::ISODate);
            emit activity(started);
            QElapsedTimer stepClock;
            stepClock.start();

            Result r = runAnalysis(analysis, file);
            // The drive went away during the step (MDI-F91)
            const bool sourceLost = r.outcome != QLatin1String("completed") && !driveAway().isEmpty();

            QVariantMap ended = step;
            ended[QStringLiteral("event")]      = QStringLiteral("end");
            ended[QStringLiteral("outcome")]    = m_stop.load() || sourceLost ? QStringLiteral("stopped") : r.outcome;
            ended[QStringLiteral("reason")]     = r.reason;
            ended[QStringLiteral("durationMs")] = stepClock.elapsed();
            if (m_lastTokensPerSecond > 0)
                ended[QStringLiteral("tokensPerSecond")] = m_lastTokensPerSecond;
            emit activity(ended);
            if (m_stop.load())
                break;   // the abandoned step is not recorded (MDI-F10)
            if (sourceLost) {
                // Not recorded either: the file's datum and derived files stay,
                // and the step runs again once the drive is back
                if (!waitForSource())
                    break;
                --a;
                continue;
            }

            // Derived files of the datum being replaced (e.g. JPEG thumbnails
            // becoming WebP): removed once the new datum is committed
            QStringList previousFiles;
            QString previousRoot;
            for (const MdiData::Datum &old : MdiData::forFile(m_connectionName, m_config.catalogId, file.fullPath))
                if (old.analysis == analysis) {
                    previousFiles = old.derivedFiles;
                    previousRoot  = old.derivedRoot;
                }

            MdiData::Datum d;
            d.catalogId     = m_config.catalogId;
            d.fullPath      = file.fullPath;
            d.analysis      = analysis;
            d.outcome       = r.outcome;
            d.reason        = r.reason;
            d.value         = r.value;
            d.derivedRoot   = derivedRoot;
            d.derivedFiles  = r.derivedFiles;
            d.tool          = r.tool;
            d.model         = r.model;
            if (!r.model.isEmpty()) {
                const QUrl url(m_config.ollamaUrl);
                d.provider      = QStringLiteral("ollama");
                d.providerClass = providerClass(url);
                d.endpointHost  = url.host();
            }
            d.params        = params;
            d.fileSize      = file.size;
            d.fileDate      = file.dateUpdated;
            d.date          = QDateTime::currentDateTime().toString(Qt::ISODate);
            if (MdiData::upsert(m_connectionName, d)) {
                // Only files this runner recorded, under its own folder (MDI-F14)
                for (const QString &rel : previousFiles)
                    if (!r.derivedFiles.contains(rel) && previousRoot.endsWith(QLatin1String("katalog_mdi")))
                        QFile::remove(QDir(previousRoot).filePath(rel));
            }
            emit datumCommitted(file.fullPath, analysis);
            // Instant-search index: the file's derived text changed
            if (r.outcome == QLatin1String("completed")
                && (analysis == QLatin1String("snippet") || analysis == QLatin1String("summary")
                    || analysis == QLatin1String("caption") || analysis == QLatin1String("archive")))
                MdiFts::updateDerivedText(m_connectionName, m_config.catalogId, file.fullPath);
            if (analysis == QLatin1String("segments") && r.outcome == QLatin1String("completed"))
                m_segmentsChanged = true;
            if (r.outcome == QLatin1String("completed"))
                fileChanged = true;

            if (r.outcome == QLatin1String("completed"))      ++completed;
            else if (r.outcome == QLatin1String("skipped"))   ++skipped;
            else                                              ++failed;
        }
    }

    // Catalog-level clusters, when this run produced new topic segments or
    // none exist yet (MDI-F37; scope per catalog, D-Q3). Never in a quick-hash
    // pass: it computes the quick hash only and asks no AI (MDI-F84).
    if (!m_stop.load() && !m_config.quickHashOnly && m_segmentsChanged) {
        emit progress(fileCount, fileCount, QStringLiteral("clusters"), m_config.catalogName);
        QVariantMap step = stepInfo(QStringLiteral("clusters"));
        step[QStringLiteral("analysis")]  = QStringLiteral("clusters");
        step[QStringLiteral("filePath")]  = m_config.catalogName;
        step[QStringLiteral("fileIndex")] = fileCount;
        step[QStringLiteral("fileCount")] = fileCount;
        QVariantMap started = step;
        started[QStringLiteral("event")]     = QStringLiteral("start");
        started[QStringLiteral("startedAt")] = QDateTime::currentDateTime().toString(Qt::ISODate);
        emit activity(started);
        QElapsedTimer stepClock;
        stepClock.start();
        m_lastTokensPerSecond = -1;
        Result r = makeClusters();
        QVariantMap ended = step;
        ended[QStringLiteral("event")]      = QStringLiteral("end");
        ended[QStringLiteral("outcome")]    = m_stop.load() ? QStringLiteral("stopped") : r.outcome;
        ended[QStringLiteral("reason")]     = r.reason;
        ended[QStringLiteral("durationMs")] = stepClock.elapsed();
        if (m_lastTokensPerSecond > 0)
            ended[QStringLiteral("tokensPerSecond")] = m_lastTokensPerSecond;
        emit activity(ended);
        if (!m_stop.load() && r.outcome != QLatin1String("none")) {
            MdiData::Datum d;
            d.catalogId    = m_config.catalogId;
            d.fullPath     = QStringLiteral("");   // the catalog itself (empty, not NULL)
            d.analysis     = QStringLiteral("clusters");
            d.outcome      = r.outcome;
            d.reason       = r.reason;
            d.value        = r.value;
            d.model        = r.model;
            const QUrl url(m_config.ollamaUrl);
            d.provider      = QStringLiteral("ollama");
            d.providerClass = providerClass(url);
            d.endpointHost  = url.host();
            d.params       = analysisParams(QStringLiteral("clusters"));
            d.date         = QDateTime::currentDateTime().toString(Qt::ISODate);
            MdiData::upsert(m_connectionName, d);
            emit datumCommitted(QString(), d.analysis);
            if (r.outcome == QLatin1String("completed")) ++completed;
            else if (r.outcome == QLatin1String("skipped")) ++skipped;
            else ++failed;
        }
    }

    // The quick-hash pass is complete for this catalog state when every file
    // was reached (a disconnected drive leaves it pending for next time)
    const bool hashesWanted = m_config.quickHashOnly
                              || m_config.settings.value("checksum").toObject().value("quick").toBool(true);
    if (!m_stop.load() && m_unreachable == 0 && hashesWanted && !m_config.catalogStamp.isEmpty())
        MdiData::setPassStamp(m_connectionName, m_config.catalogId, QStringLiteral("quickhash"), m_config.catalogStamp);

    MdiData::close(m_connectionName);

    summary[QStringLiteral("completed")] = completed;
    summary[QStringLiteral("unchanged")] = unchanged;
    summary[QStringLiteral("skipped")]   = skipped;
    summary[QStringLiteral("failed")]    = failed;
    summary[QStringLiteral("stopped")]   = m_stop.load();
    emit finished(summary);
}

//----------------------------------------------------------------------
QVariantMap MdiEnrichmentJob::stepInfo(const QString &analysis) const
{
    QVariantMap info;
    const QString ollamaWhere = providerClass(QUrl(m_config.ollamaUrl));
    if (analysis == QLatin1String("caption")) {
        info[QStringLiteral("tool")]  = QStringLiteral("ollama");
        info[QStringLiteral("model")] = m_config.visionModel;
        info[QStringLiteral("where")] = ollamaWhere;
    } else if (analysis == QLatin1String("summary") || analysis == QLatin1String("segments")
               || analysis == QLatin1String("clusters")) {
        info[QStringLiteral("tool")]  = QStringLiteral("ollama");
        info[QStringLiteral("model")] = m_config.textModel;
        info[QStringLiteral("where")] = ollamaWhere;
    } else if (analysis == QLatin1String("embeddings")) {
        info[QStringLiteral("tool")]  = QStringLiteral("ollama");
        info[QStringLiteral("model")] = m_config.embedModel;
        info[QStringLiteral("where")] = ollamaWhere;
    } else if (analysis == QLatin1String("transcript")) {
        info[QStringLiteral("tool")]  = QStringLiteral("whisper.cpp");
        info[QStringLiteral("model")] = QFileInfo(m_config.whisperModelPath).fileName();
        info[QStringLiteral("where")] = QStringLiteral("local");
    } else if (analysis == QLatin1String("mediainfo")) {
        info[QStringLiteral("tool")]  = QStringLiteral("ffprobe");
        info[QStringLiteral("where")] = QStringLiteral("local");
    } else if (analysis == QLatin1String("thumbnail")) {
        info[QStringLiteral("tool")]  = QStringLiteral("qt / ffmpeg");
        info[QStringLiteral("where")] = QStringLiteral("local");
    } else if (analysis == QLatin1String("archive")) {
        info[QStringLiteral("tool")]  = QStringLiteral("karchive");
        info[QStringLiteral("where")] = QStringLiteral("local");
    } else if (analysis == QLatin1String("quickhash") || analysis == QLatin1String("snippet")) {
        info[QStringLiteral("tool")]  = QStringLiteral("katalog");
        info[QStringLiteral("where")] = QStringLiteral("local");
    } else {
        info[QStringLiteral("tool")]  = QStringLiteral("ffmpeg");
        info[QStringLiteral("where")] = QStringLiteral("local");
    }
    return info;
}

//----------------------------------------------------------------------
QStringList MdiEnrichmentJob::plannedAnalyses(const FileItem &file) const
{
    const QJsonObject s = m_config.settings;

    // Quick hash: every file, whatever its type (duplicates across disks)
    QStringList hashes;
    if (m_config.quickHashOnly || s.value("checksum").toObject().value("quick").toBool(true))
        hashes << QStringLiteral("quickhash");
    if (m_config.quickHashOnly)
        return hashes;
    // Contents of archives, listed without extracting (whatever the catalog type)
    if (s.value("archive").toObject().value("list").toBool(true) && !archiveKind(file.fullPath).isEmpty())
        hashes << QStringLiteral("archive");
    // First characters of documents, no AI: searchable offline (instant search)
    if (s.value("text").toObject().value("snippet").toBool(true) && kSnippetSuffixes.contains(file.extension))
        hashes << QStringLiteral("snippet");

    bool typeSelected = false;
    for (const QJsonValue &t : s.value("fileTypes").toArray())
        typeSelected = typeSelected || lowerType(t.toString()) == file.fileType;
    if (!typeSelected)
        return hashes;

    const QJsonObject image = s.value("image").toObject();
    const QJsonObject video = s.value("video").toObject();
    const QJsonObject audio = s.value("audio").toObject();
    // Profiles saved before the AI options existed have no "ai" key: both
    // analyses are on for them, as they are by default in the form.
    const QJsonObject ai    = s.value("ai").toObject();
    // The K3 form writes "caption"; "imageCaption" is accepted as well
    const bool caption = ai.value("imageCaption").toBool(ai.value("caption").toBool(true));
    const bool summary = ai.value("summary").toBool(true);
    // Topic segments need the transcript, which is selected with them (MDI-F5)
    const bool segments   = ai.value("segments").toBool(true);
    const bool transcript = segments || ai.value("transcript").toBool(true);
    const bool embeddings = ai.value("embeddings").toBool(true);

    QStringList list;
    if (file.fileType == QLatin1String("image")) {
        if (image.value("thumbnail").toBool()) list << QStringLiteral("thumbnail");
        if (caption)                           list << QStringLiteral("caption");
    } else if (file.fileType == QLatin1String("video")) {
        list << QStringLiteral("mediainfo");
        if (video.value("posterFrame").toBool())   list << QStringLiteral("poster");
        if (video.value("keyframeStrip").toBool()) list << QStringLiteral("keyframes");
        if (video.value("clip").toString(QStringLiteral("off")) != QLatin1String("off"))
            list << QStringLiteral("clip_video");
        if (transcript) list << QStringLiteral("transcript");
        if (caption)    list << QStringLiteral("caption");
        if (summary)    list << QStringLiteral("summary");
        if (segments)   list << QStringLiteral("segments");
    } else if (file.fileType == QLatin1String("audio")) {
        list << QStringLiteral("mediainfo");
        if (audio.value("clip").toString(QStringLiteral("off")) != QLatin1String("off"))
            list << QStringLiteral("clip_audio");
        if (audio.value("waveform").toBool()) list << QStringLiteral("waveform");
        if (transcript)                       list << QStringLiteral("transcript");
        if (summary)                          list << QStringLiteral("summary");
        if (segments)                         list << QStringLiteral("segments");
    } else if (file.fileType == QLatin1String("text")) {
        if (summary) list << QStringLiteral("summary");
    }
    // Last: indexes what the analyses above produced (MDI-F47)
    if (embeddings && !list.isEmpty())
        list << QStringLiteral("embeddings");
    return hashes + list;
}

QJsonObject MdiEnrichmentJob::analysisParams(const QString &analysis) const
{
    const QJsonObject s = m_config.settings;
    QJsonObject p;
    if (analysis == QLatin1String("thumbnail") || analysis == QLatin1String("poster")) {
        p["size"]    = kThumbnailSize;
        p["format"]  = thumbnailSuffix().mid(1);
        p["quality"] = kThumbQuality;
    } else if (analysis == QLatin1String("keyframes")) {
        p["count"]   = s.value("video").toObject().value("keyframeCount").toInt(4);
        p["height"]  = kSheetHeight;
        p["format"]  = thumbnailSuffix().mid(1);
        p["quality"] = kThumbQuality;
        p["layout"]  = QStringLiteral("sheet");
    } else if (analysis == QLatin1String("snippet")) {
        p["chars"] = kSnippetChars;
    } else if (analysis == QLatin1String("mediainfo")) {
        p["tool"] = QStringLiteral("ffprobe");
    } else if (analysis == QLatin1String("archive")) {
        p["maxEntries"] = kArchiveMaxEntries;
    } else if (analysis == QLatin1String("quickhash")) {
        p["method"] = QStringLiteral("blake2b-256 size+3x64KiB");
    } else if (analysis == QLatin1String("clip_video") || analysis == QLatin1String("clip_audio")) {
        const QJsonObject o = s.value(analysis == QLatin1String("clip_video") ? "video" : "audio").toObject();
        p["mode"]     = o.value("clip").toString();
        p["start"]    = o.value("clipStart").toInt(0);
        p["duration"] = o.value("clipDuration").toInt(30);
    } else if (analysis == QLatin1String("waveform")) {
        p["width"]  = 1200;
        p["height"] = 200;
    } else if (analysis == QLatin1String("caption")) {
        p["model"]    = m_config.visionModel;
        p["prompt"]   = kCaptionPrompt;
        p["language"] = m_config.uiLanguageName;
    } else if (analysis == QLatin1String("summary")) {
        p["model"]  = m_config.textModel;
        p["prompt"] = kSummaryPrompt;
    } else if (analysis == QLatin1String("transcript")) {
        p["model"]    = QFileInfo(m_config.whisperModelPath).fileName();
        p["language"] = QStringLiteral("auto");
    } else if (analysis == QLatin1String("segments")) {
        p["model"]  = m_config.textModel;
        p["prompt"] = kSegmentsPrompt;
    } else if (analysis == QLatin1String("clusters")) {
        p["model"]  = m_config.textModel;
        p["prompt"] = kClustersPrompt;
    } else if (analysis == QLatin1String("embeddings")) {
        p["model"]   = m_config.embedModel;
        p["version"] = kEmbedVersion;
    }
    return p;
}

MdiEnrichmentJob::Result MdiEnrichmentJob::runAnalysis(const QString &analysis, const FileItem &file)
{
    // Previews need ffmpeg; caption and summary check their own inputs (MDI-F21)
    const bool isPreview = analysis != QLatin1String("summary") && analysis != QLatin1String("caption")
                        && analysis != QLatin1String("segments") && analysis != QLatin1String("embeddings")
                        && analysis != QLatin1String("snippet") && analysis != QLatin1String("quickhash")
                        && analysis != QLatin1String("mediainfo") && analysis != QLatin1String("thumbnail")
                        && analysis != QLatin1String("archive");
    if (isPreview && m_config.ffmpegPath.isEmpty()) {
        Result r;
        r.outcome = QStringLiteral("skipped");
        r.reason  = QStringLiteral("ffmpeg not found");
        return r;
    }
    if (analysis == QLatin1String("thumbnail"))  return makeThumbnail(file);
    if (analysis == QLatin1String("poster"))     return makePosterFrame(file);
    if (analysis == QLatin1String("keyframes"))  return makeKeyframes(file);
    if (analysis == QLatin1String("clip_video")) return makeClip(file, true);
    if (analysis == QLatin1String("clip_audio")) return makeClip(file, false);
    if (analysis == QLatin1String("waveform"))   return makeWaveform(file);
    if (analysis == QLatin1String("caption"))    return makeCaption(file);
    if (analysis == QLatin1String("summary"))    return makeSummary(file);
    if (analysis == QLatin1String("transcript")) return makeTranscript(file);
    if (analysis == QLatin1String("segments"))   return makeSegments(file);
    if (analysis == QLatin1String("embeddings")) return makeEmbeddings(file);
    if (analysis == QLatin1String("snippet"))    return makeSnippet(file);
    if (analysis == QLatin1String("quickhash"))  return makeQuickHash(file);
    if (analysis == QLatin1String("mediainfo"))  return makeMediaInfo(file);
    if (analysis == QLatin1String("archive"))    return makeArchiveListing(file);
    Result r;
    r.outcome = QStringLiteral("skipped");
    r.reason  = QStringLiteral("unknown analysis");
    return r;
}

//----------------------------------------------------------------------
// Derived files: <destination>/katalog_mdi/<catalog id>/<analysis>/<hash>.<suffix>
// The name is a hash of the source path, never the source name (MDI-C19).
QString MdiEnrichmentJob::derivedRelative(const FileItem &file, const QString &analysis, const QString &suffix) const
{
    const QByteArray hash = QCryptographicHash::hash(file.fullPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20);
    return QStringLiteral("%1/%2/%3%4").arg(m_config.catalogId).arg(analysis, QString::fromLatin1(hash), suffix);
}

QString MdiEnrichmentJob::absoluteDerived(const QString &relative) const
{
    const QString path = QDir(QDir(m_config.destinationPath).filePath(QStringLiteral("katalog_mdi"))).filePath(relative);
    QDir().mkpath(QFileInfo(path).absolutePath());
    return path;
}

static QString scaleFilter(int size)
{
    // Longest side `size`, aspect ratio kept, never enlarged
    return QStringLiteral("scale='if(gt(iw,ih),min(%1,iw),-2)':'if(gt(iw,ih),-2,min(%1,ih))'").arg(size);
}

bool MdiEnrichmentJob::finishThumbnail(const QString &jpegPath, const QString &targetPath, QString *error)
{
    QFile::remove(targetPath);
    if (!targetPath.endsWith(QLatin1String(".webp"))) {
        if (QFile::rename(jpegPath, targetPath) || QFile::copy(jpegPath, targetPath))
            return true;
        *error = QStringLiteral("thumbnail not written");
        return false;
    }
    const QImage image(jpegPath);
    if (image.isNull()) {
        *error = QStringLiteral("frame not readable");
        return false;
    }
    QImageWriter writer(targetPath, "webp");
    writer.setQuality(kThumbQuality);
    if (!writer.write(image)) {
        *error = writer.errorString();
        return false;
    }
    return true;
}

MdiEnrichmentJob::Result MdiEnrichmentJob::makeQuickHash(const FileItem &file)
{
    Result r;
    r.tool = QStringLiteral("katalog");
    const QString hash = FileChecksum::computeQuickHash(file.fullPath);
    if (hash.isEmpty()) {
        r.outcome = QStringLiteral("failed");
        r.reason  = QStringLiteral("file not readable");
        return r;
    }
    r.value["hash"] = hash;
    return r;
}

MdiEnrichmentJob::Result MdiEnrichmentJob::makeSnippet(const FileItem &file)
{
    Result r;
    QString tool, reason;
    QString text = extractText(file.fullPath, &tool, &reason).simplified();
    r.tool = tool;
    if (text.isEmpty()) {
        r.outcome = QStringLiteral("skipped");
        r.reason  = reason.isEmpty() ? QStringLiteral("no text") : reason;
        return r;
    }
    r.value["text"]      = text.left(kSnippetChars);
    r.value["truncated"] = text.size() > kSnippetChars;
    return r;
}

// Image thumbnail decoded by Qt itself: JPEG is decoded already reduced, the
// EXIF orientation is applied, and the result is written straight as WebP.
// Thread-safe (no member state): the parallel pre-pass runs it on a pool.
static bool qtImageThumbnail(const QString &source, const QString &target, QString *error)
{
    QImageReader reader(source);
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        *error = reader.errorString();
        return false;
    }
    const QSize original = reader.size();
    if (original.isValid() && (original.width() > kThumbnailSize || original.height() > kThumbnailSize))
        reader.setScaledSize(original.scaled(kThumbnailSize, kThumbnailSize, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull()) {
        *error = reader.errorString();
        return false;
    }
    if (image.width() > kThumbnailSize || image.height() > kThumbnailSize)
        image = image.scaled(kThumbnailSize, kThumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QDir().mkpath(QFileInfo(target).absolutePath());
    QImageWriter writer(target, target.endsWith(QLatin1String(".webp")) ? "webp" : "jpeg");
    writer.setQuality(kThumbQuality);
    if (!writer.write(image)) {
        *error = writer.errorString();
        return false;
    }
    return true;
}

void MdiEnrichmentJob::precomputeThumbnails(int from, int to)
{
    // Image thumbnails of the next files, decoded in parallel; the job thread
    // still commits every datum in order (MDI-F9). Only files whose thumbnail
    // is due; any image Qt cannot read is left to the ffmpeg path.
    struct Task { QString source, target, rel; };
    QList<Task> tasks;
    for (int i = from; i < to; ++i) {
        const FileItem &file = m_files.at(i);
        if (file.fileType != QLatin1String("image") || !plannedAnalyses(file).contains(QStringLiteral("thumbnail")))
            continue;
        if (MdiData::isCurrent(m_connectionName, m_config.catalogId, file.fullPath, QStringLiteral("thumbnail"),
                               file.size, file.dateUpdated, analysisParams(QStringLiteral("thumbnail")))
            || !QFileInfo::exists(file.fullPath))
            continue;
        const QString rel = derivedRelative(file, QStringLiteral("thumbnail"), thumbnailSuffix());
        tasks.append({file.fullPath, absoluteDerived(rel), rel});
    }
    if (tasks.isEmpty())
        return;

    QThreadPool pool;
    pool.setMaxThreadCount(qBound(1, QThread::idealThreadCount() / 2, 4));
    QMutex mutex;
    for (const Task &task : tasks) {
        pool.start([this, task, &mutex]() {
            // Decodes not started yet do not start after Pause (MDI-F11); the
            // file then takes the sequential path after Resume
            if (m_stop.load() || m_paused.load())
                return;
            QString error;
            if (qtImageThumbnail(task.source, task.target, &error)) {
                Result r;
                r.tool = QStringLiteral("qt %1").arg(QString::fromLatin1(qVersion()));
                r.derivedFiles << task.rel;
                QMutexLocker locker(&mutex);
                m_precomputed.insert(task.source, r);
            }
        });
    }
    pool.waitForDone();
}

MdiEnrichmentJob::Result MdiEnrichmentJob::makeThumbnail(const FileItem &file)
{
    // Done by the parallel pre-pass
    const auto pre = m_precomputed.find(file.fullPath);
    if (pre != m_precomputed.end()) {
        const Result r = pre.value();
        m_precomputed.erase(pre);
        return r;
    }

    Result r;
    const QString target = absoluteDerived(derivedRelative(file, QStringLiteral("thumbnail"), thumbnailSuffix()));
    QString qtError;
    if (qtImageThumbnail(file.fullPath, target, &qtError)) {
        r.tool = QStringLiteral("qt %1").arg(QString::fromLatin1(qVersion()));
        r.derivedFiles << derivedRelative(file, QStringLiteral("thumbnail"), thumbnailSuffix());
        return r;
    }
    if (m_config.ffmpegPath.isEmpty()) {
        r.outcome = QStringLiteral("failed");
        r.reason  = qtError;
        return r;
    }
    // Formats Qt does not read (PSD, RAW...): ffmpeg
    r.tool = m_ffmpegVersion;
    const QString rel = derivedRelative(file, QStringLiteral("thumbnail"), thumbnailSuffix());
    QTemporaryDir tmp;
    const QString jpeg = tmp.filePath(QStringLiteral("frame.jpg"));
    QString err;
    if (!runTool(m_config.ffmpegPath, {"-nostdin", "-y", "-loglevel", "error", "-i", file.fullPath,
                                       "-vf", scaleFilter(kThumbnailSize), "-frames:v", "1", "-q:v", "3",
                                       jpeg}, nullptr, &err)
        || !finishThumbnail(jpeg, absoluteDerived(rel), &err)) {
        r.outcome = QStringLiteral("failed");
        r.reason  = err;
        return r;
    }
    r.derivedFiles << rel;
    return r;
}

double MdiEnrichmentJob::probeDuration(const QString &path)
{
    if (m_config.ffprobePath.isEmpty())
        return 0.0;
    QByteArray out;
    QString err;
    if (!runTool(m_config.ffprobePath, {"-v", "error", "-show_entries", "format=duration",
                                        "-of", "default=nw=1:nk=1", path}, &out, &err))
        return 0.0;
    return QString::fromUtf8(out).trimmed().toDouble();
}

MdiEnrichmentJob::Result MdiEnrichmentJob::makePosterFrame(const FileItem &file)
{
    Result r;
    r.tool = m_ffmpegVersion;
    const double duration = probeDuration(file.fullPath);
    const double at = duration > 0 ? duration * 0.10 : 0.0;   // MDI-F30
    const QString rel = derivedRelative(file, QStringLiteral("poster"), thumbnailSuffix());
    QTemporaryDir tmp;
    const QString jpeg = tmp.filePath(QStringLiteral("frame.jpg"));
    QString err;
    if (!runTool(m_config.ffmpegPath, {"-nostdin", "-y", "-loglevel", "error",
                                       "-ss", QString::number(at, 'f', 2), "-i", file.fullPath,
                                       "-vf", scaleFilter(kThumbnailSize), "-frames:v", "1", "-q:v", "3",
                                       jpeg}, nullptr, &err)
        || !finishThumbnail(jpeg, absoluteDerived(rel), &err)) {
        r.outcome = QStringLiteral("failed");
        r.reason  = err;
        return r;
    }
    r.derivedFiles << rel;
    r.value["timecode"] = at;
    r.value["duration"] = duration;
    return r;
}

MdiEnrichmentJob::Result MdiEnrichmentJob::makeKeyframes(const FileItem &file)
{
    // Video contact sheet: N frames evenly spaced (MDI-F31) side by side in
    // one image, each kSheetHeight px high; value.frames maps each frame to
    // its timecode and horizontal position in the sheet
    Result r;
    r.tool = m_ffmpegVersion;
    const int count = qBound(2, m_config.settings.value("video").toObject().value("keyframeCount").toInt(4), 32);
    const double duration = probeDuration(file.fullPath);
    if (duration <= 0) {
        r.outcome = QStringLiteral("failed");
        r.reason  = QStringLiteral("duration unknown");
        return r;
    }
    QTemporaryDir tmp;
    QList<QImage> frames;
    QJsonArray timecodes;
    for (int k = 0; k < count && !m_stop.load(); ++k) {
        const double at = duration * (k + 0.5) / count;
        const QString jpeg = tmp.filePath(QStringLiteral("frame_%1.jpg").arg(k));
        QString err;
        if (!runTool(m_config.ffmpegPath, {"-nostdin", "-y", "-loglevel", "error",
                                           "-ss", QString::number(at, 'f', 2), "-i", file.fullPath,
                                           "-vf", QStringLiteral("scale=-2:%1").arg(kSheetHeight),
                                           "-frames:v", "1", "-q:v", "3", jpeg}, nullptr, &err)) {
            r.outcome = QStringLiteral("failed");
            r.reason  = err;
            return r;
        }
        const QImage frame(jpeg);
        if (frame.isNull()) {
            r.outcome = QStringLiteral("failed");
            r.reason  = QStringLiteral("frame not readable");
            return r;
        }
        frames.append(frame);
        timecodes.append(at);
    }
    if (frames.isEmpty())
        return r;

    int width = 0;
    for (const QImage &f : frames)
        width += f.width();
    QImage sheet(width, kSheetHeight, QImage::Format_RGB32);
    sheet.fill(Qt::black);
    QJsonArray layout;
    {
        QPainter painter(&sheet);
        int x = 0;
        for (int k = 0; k < frames.size(); ++k) {
            painter.drawImage(x, 0, frames.at(k));
            layout.append(QJsonObject{{"t", timecodes.at(k)}, {"x", x}, {"w", frames.at(k).width()}});
            x += frames.at(k).width();
        }
    }
    const QString rel = derivedRelative(file, QStringLiteral("keyframes"), QStringLiteral("_sheet") + thumbnailSuffix());
    const QString target = absoluteDerived(rel);
    QFile::remove(target);
    QImageWriter writer(target, target.endsWith(QLatin1String(".webp")) ? "webp" : "jpeg");
    writer.setQuality(kThumbQuality);
    if (!writer.write(sheet)) {
        r.outcome = QStringLiteral("failed");
        r.reason  = writer.errorString();
        return r;
    }
    r.derivedFiles << rel;
    r.value["timecodes"] = timecodes;
    r.value["frames"]    = layout;
    r.value["sheet"]     = true;
    return r;
}

#ifdef KATALOG_HAS_KARCHIVE
namespace {
void walkArchive(const KArchiveDirectory *dir, const QString &prefix, QJsonArray &entries,
                 int &count, qint64 &total, const std::atomic<bool> &stop)
{
    const QStringList names = dir->entries();
    for (const QString &name : names) {
        if (stop.load())
            return;
        const KArchiveEntry *e = dir->entry(name);
        if (!e)
            continue;
        const QString path = prefix.isEmpty() ? name : prefix + QLatin1Char('/') + name;
        ++count;
        qint64 size = 0;
        if (e->isFile()) {
            size = static_cast<const KArchiveFile *>(e)->size();
            total += size;
        }
        if (entries.size() < kArchiveMaxEntries) {
            QJsonObject o{{"path", path}, {"dir", e->isDirectory()},
                          {"date", e->date().toString(Qt::ISODate)}};
            if (e->isFile())
                o["size"] = size;
            entries.append(o);
        }
        if (e->isDirectory())
            walkArchive(static_cast<const KArchiveDirectory *>(e), path, entries, count, total, stop);
    }
}
} // namespace
#endif

MdiEnrichmentJob::Result MdiEnrichmentJob::makeArchiveListing(const FileItem &file)
{
    Result r;
    r.tool = QStringLiteral("karchive");
#ifdef KATALOG_HAS_KARCHIVE
    const QString kind = archiveKind(file.fullPath);
    if (kind == QLatin1String("tar") && !file.fullPath.toLower().endsWith(QLatin1String(".tar"))
        && file.size > kArchiveMaxTarSize) {
        r.outcome = QStringLiteral("skipped");
        r.reason  = QStringLiteral("compressed tar too large to list");
        return r;
    }
    std::unique_ptr<QIODevice> device;   // outlives the archive reading it
    std::unique_ptr<KArchive> archive;
    if (kind == QLatin1String("zip"))
        archive = std::make_unique<KZip>(file.fullPath);
    else if (kind == QLatin1String("7z"))
        archive = std::make_unique<K7Zip>(file.fullPath);
    else
        archive = std::make_unique<KTar>(file.fullPath);   // compression detected by KArchive
    bool opened = archive->open(QIODevice::ReadOnly);
    if (!opened && kind == QLatin1String("tar")) {
        // Second attempt with the decompressor named by the suffix
        const QString lower = file.fullPath.toLower();
        KCompressionDevice::CompressionType type = KCompressionDevice::None;
        if (lower.endsWith(QLatin1String(".gz")) || lower.endsWith(QLatin1String(".tgz")))
            type = KCompressionDevice::GZip;
        else if (lower.endsWith(QLatin1String(".bz2")) || lower.endsWith(QLatin1String(".tbz")) || lower.endsWith(QLatin1String(".tbz2")))
            type = KCompressionDevice::BZip2;
        else if (lower.endsWith(QLatin1String(".xz")) || lower.endsWith(QLatin1String(".txz")))
            type = KCompressionDevice::Xz;
        else if (lower.endsWith(QLatin1String(".zst")))
            type = KCompressionDevice::Zstd;
        if (type != KCompressionDevice::None) {
            archive.reset();
            device = std::make_unique<KCompressionDevice>(file.fullPath, type);
            archive = std::make_unique<KTar>(device.get());
            opened = archive->open(QIODevice::ReadOnly);
        }
    }
    if (!opened) {
        r.outcome = QStringLiteral("failed");
        r.reason  = archive->errorString().isEmpty() ? QStringLiteral("archive not readable") : archive->errorString();
        return r;
    }
    QJsonArray entries;
    int count = 0;
    qint64 total = 0;
    walkArchive(archive->directory(), QString(), entries, count, total, m_stop);
    archive->close();
    r.value["format"]    = kind;
    r.value["count"]     = count;
    r.value["totalSize"] = total;
    r.value["truncated"] = count > entries.size();
    r.value["entries"]   = entries;
#else
    Q_UNUSED(file);
    r.outcome = QStringLiteral("skipped");
    r.reason  = QStringLiteral("archive support not built");
#endif
    return r;
}

MdiEnrichmentJob::Result MdiEnrichmentJob::makeMediaInfo(const FileItem &file)
{
    // Technical data by ffprobe, for catalogs scanned without metadata
    Result r;
    if (m_config.ffprobePath.isEmpty()) {
        r.outcome = QStringLiteral("skipped");
        r.reason  = QStringLiteral("ffprobe not found");
        return r;
    }
    r.tool = QStringLiteral("ffprobe");
    QByteArray out;
    QString err;
    if (!runTool(m_config.ffprobePath, {"-v", "error", "-show_entries",
                                        "format=duration,bit_rate:stream=codec_type,codec_name,width,height,"
                                        "avg_frame_rate,sample_rate,channels",
                                        "-of", "json", file.fullPath}, &out, &err)) {
        r.outcome = QStringLiteral("failed");
        r.reason  = err;
        return r;
    }
    const QJsonObject doc = QJsonDocument::fromJson(out).object();
    const QJsonObject format = doc.value("format").toObject();
    r.value["duration"] = format.value("duration").toString().toDouble();
    r.value["bitrate"]  = format.value("bit_rate").toString().toLongLong();
    for (const QJsonValue &v : doc.value("streams").toArray()) {
        const QJsonObject st = v.toObject();
        const QString type = st.value("codec_type").toString();
        if (type == QLatin1String("video") && !r.value.contains("video")) {
            const QStringList rate = st.value("avg_frame_rate").toString().split('/');
            const double fps = rate.size() == 2 && rate[1].toDouble() > 0 ? rate[0].toDouble() / rate[1].toDouble() : 0.0;
            r.value["video"] = QJsonObject{{"codec", st.value("codec_name")}, {"width", st.value("width")},
                                           {"height", st.value("height")}, {"fps", qRound(fps * 100) / 100.0}};
        } else if (type == QLatin1String("audio") && !r.value.contains("audio")) {
            r.value["audio"] = QJsonObject{{"codec", st.value("codec_name")},
                                           {"sampleRate", st.value("sample_rate").toString().toInt()},
                                           {"channels", st.value("channels")}};
        }
    }
    return r;
}

MdiEnrichmentJob::Result MdiEnrichmentJob::makeClip(const FileItem &file, bool video)
{
    Result r;
    r.tool = m_ffmpegVersion;
    const QJsonObject o = m_config.settings.value(video ? "video" : "audio").toObject();
    const bool partial = o.value("clip").toString() == QLatin1String("partial");
    const QString analysis = video ? QStringLiteral("clip_video") : QStringLiteral("clip_audio");
    const QString rel = derivedRelative(file, analysis, video ? QStringLiteral(".mp4") : QStringLiteral(".m4a"));

    QStringList args = {"-nostdin", "-y", "-loglevel", "error"};
    if (partial)
        args << "-ss" << QString::number(o.value("clipStart").toInt(0));
    args << "-i" << file.fullPath;
    if (partial)
        args << "-t" << QString::number(qMax(1, o.value("clipDuration").toInt(30)));
    if (video)   // H.264 no taller than 360 px, AAC (MDI-F32)
        args << "-vf" << "scale=-2:'min(360,ih)'" << "-c:v" << "libx264" << "-preset" << "veryfast"
             << "-crf" << "30" << "-c:a" << "aac" << "-b:a" << "96k" << "-movflags" << "+faststart";
    else         // AAC (MDI-F33)
        args << "-vn" << "-c:a" << "aac" << "-b:a" << "96k";
    args << absoluteDerived(rel);

    QString err;
    if (!runTool(m_config.ffmpegPath, args, nullptr, &err)) {
        r.outcome = QStringLiteral("failed");
        r.reason  = err;
        return r;
    }
    r.derivedFiles << rel;
    if (partial) {
        r.value["start"]    = o.value("clipStart").toInt(0);
        r.value["duration"] = o.value("clipDuration").toInt(30);
    }
    return r;
}

MdiEnrichmentJob::Result MdiEnrichmentJob::makeWaveform(const FileItem &file)
{
    Result r;
    r.tool = m_ffmpegVersion;
    const QString rel = derivedRelative(file, QStringLiteral("waveform"), QStringLiteral(".png"));
    QString err;
    if (!runTool(m_config.ffmpegPath, {"-nostdin", "-y", "-loglevel", "error", "-i", file.fullPath,
                                       "-filter_complex", "showwavespic=s=1200x200:colors=0x3daee9",
                                       "-frames:v", "1", absoluteDerived(rel)}, nullptr, &err)) {
        r.outcome = QStringLiteral("failed");
        r.reason  = err;
        return r;
    }
    r.derivedFiles << rel;
    return r;
}

//----------------------------------------------------------------------
MdiEnrichmentJob::Result MdiEnrichmentJob::makeCaption(const FileItem &file)
{
    Result r;
    r.model = m_config.visionModel;

    // The model sees a small frame or thumbnail, never the source file (MDI-F39).
    QTemporaryDir tmp;
    QString frame;
    const QString existing = absoluteDerived(derivedRelative(file,
        file.fileType == QLatin1String("video") ? QStringLiteral("poster") : QStringLiteral("thumbnail"),
        QStringLiteral(".jpg")));
    if (QFileInfo::exists(existing)) {
        frame = existing;
    } else if (!m_config.ffmpegPath.isEmpty() && tmp.isValid()) {
        frame = tmp.filePath(QStringLiteral("frame.jpg"));
        QStringList args = {"-nostdin", "-y", "-loglevel", "error"};
        if (file.fileType == QLatin1String("video")) {
            const double d = probeDuration(file.fullPath);
            args << "-ss" << QString::number(d > 0 ? d * 0.10 : 0.0, 'f', 2);
        }
        args << "-i" << file.fullPath << "-vf" << scaleFilter(768) << "-frames:v" << "1" << frame;
        QString err;
        if (!runTool(m_config.ffmpegPath, args, nullptr, &err)) {
            r.outcome = QStringLiteral("failed");
            r.reason  = err;
            return r;
        }
    } else {
        r.outcome = QStringLiteral("skipped");
        r.reason  = QStringLiteral("ffmpeg not found");
        return r;
    }

    const QString prompt = QStringLiteral(
        "You describe images for a personal media catalog. Answer in %1, as JSON only: "
        "{\"caption\": \"one or two sentences describing what is shown\", "
        "\"tags\": [\"3 to 8 short lowercase keywords\"]}.").arg(m_config.uiLanguageName);
    QString err;
    const QJsonObject answer = askOllama(m_config.visionModel, prompt, {frame}, &err);
    const QString caption = answer.value("caption").toString().trimmed();
    if (caption.isEmpty()) {   // unparsable answer: recorded as failed (MDI-C20)
        r.outcome = QStringLiteral("failed");
        r.reason  = err.isEmpty() ? QStringLiteral("no caption in the model answer") : err;
        return r;
    }
    r.value["caption"] = caption;
    r.value["tags"]    = stringArray(answer.value("tags"));
    return r;
}

QString MdiEnrichmentJob::findTranscript(const FileItem &file) const
{
    // A recording's transcript sits next to it: "talk.m4a.txt", "talk.m4a.pdf",
    // "talk.txt", "talk.pdf" or "talk.docx".
    const QFileInfo info(file.fullPath);
    const QString base = info.dir().filePath(info.completeBaseName());
    const QStringList candidates = {
        file.fullPath + ".txt", file.fullPath + ".pdf", file.fullPath + ".docx",
        base + ".txt", base + ".pdf", base + ".docx", base + ".srt", base + ".vtt",
    };
    for (const QString &c : candidates)
        if (QFileInfo::exists(c))
            return c;
    return QString();
}

QString MdiEnrichmentJob::extractText(const QString &path, QString *tool, QString *reason)
{
    const QString suffix = QFileInfo(path).suffix().toLower();

    if (suffix == QLatin1String("pdf")) {
        if (m_config.pdftotextPath.isEmpty()) {
            *reason = QStringLiteral("pdftotext not found");
            return QString();
        }
        QByteArray out;
        QString err;
        // First 30 pages are plenty for a summary
        if (!runTool(m_config.pdftotextPath, {"-l", "30", "-enc", "UTF-8", path, "-"}, &out, &err)) {
            *reason = err;
            return QString();
        }
        *tool = QStringLiteral("pdftotext");
        return QString::fromUtf8(out);
    }

    static const QStringList plain = {"txt", "md", "markdown", "srt", "vtt", "csv", "tsv", "log", "rtf",
                                      "html", "htm", "json", "xml"};
    if (plain.contains(suffix)) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            *reason = f.errorString();
            return QString();
        }
        // Bounded read; a NUL byte means a binary file named like a text one
        const QByteArray bytes = f.read(kMaxTextChars * 4);
        if (bytes.left(4096).contains('\0')) {
            *reason = QStringLiteral("binary content");
            return QString();
        }
        QString text = QString::fromUtf8(bytes);
        // Control characters out (keeps tabs and line breaks for simplified())
        text.removeIf([](QChar c) { return c.category() == QChar::Other_Control && c != '\n' && c != '\t'; });
        return text;
    }

    // Office and OpenDocument files: KFileMetaData's plain-text extractors
    const QString mime = QMimeDatabase().mimeTypeForFile(path).name();
    KFileMetaData::ExtractorCollection collection;
    const QList<KFileMetaData::Extractor *> extractors = collection.fetchExtractors(mime);
    for (KFileMetaData::Extractor *extractor : extractors) {
        KFileMetaData::SimpleExtractionResult result(path, mime, KFileMetaData::ExtractionResult::ExtractPlainText);
        extractor->extract(&result);
        if (!result.text().trimmed().isEmpty()) {
            *tool = QStringLiteral("kfilemetadata");
            return result.text();
        }
    }
    *reason = QStringLiteral("no text extracted (%1)").arg(mime);
    return QString();
}

MdiEnrichmentJob::Result MdiEnrichmentJob::makeSummary(const FileItem &file)
{
    Result r;
    r.model = m_config.textModel;

    QString source = file.fullPath;
    bool isTranscript = false;
    QString text, tool, reason;
    if (file.fileType == QLatin1String("audio") || file.fileType == QLatin1String("video")) {
        // A transcript next to the recording first (often revised by hand),
        // else the one whisper made in this catalog
        isTranscript = true;
        source = findTranscript(file);
        if (source.isEmpty()) {
            text = transcriptText(file);
            if (text.isEmpty()) {
                r.outcome = QStringLiteral("skipped");
                r.reason  = QStringLiteral("no transcript");
                return r;
            }
            tool = QStringLiteral("whisper");
        }
    }

    if (text.isEmpty())
        text = extractText(source, &tool, &reason);
    text = text.simplified();
    r.tool = tool;
    if (text.isEmpty()) {
        r.outcome = QStringLiteral("skipped");
        r.reason  = reason.isEmpty() ? QStringLiteral("no text") : reason;
        return r;
    }
    const bool truncated = text.size() > kMaxTextChars;
    text.truncate(kMaxTextChars);

    // Output language = the source's language (D-Q4); the model follows the
    // prompt's language unless told twice.
    const QString prompt = QStringLiteral(
        "You summarise %1 for a personal media catalog. Write the summary, the excerpts and the tags "
        "in the SAME LANGUAGE AS THE TEXT (an Italian text gets an Italian summary). "
        "Answer as JSON only: {\"summary\": \"3 to 5 sentences\", "
        "\"excerpts\": [\"up to 4 key passages quoted verbatim, each under 30 words\"], "
        "\"tags\": [\"3 to 10 lowercase keywords\"], \"language\": \"ISO 639-1 code\"}.\n\nTEXT:\n%2"
        "\n\nRemember: same language as the TEXT, JSON only.")
        .arg(isTranscript ? QStringLiteral("the transcript of a recording") : QStringLiteral("a document"), text);

    QString err;
    const QJsonObject answer = askOllama(m_config.textModel, prompt, {}, &err);
    const QString summary = answer.value("summary").toString().trimmed();
    if (summary.isEmpty()) {
        r.outcome = QStringLiteral("failed");
        r.reason  = err.isEmpty() ? QStringLiteral("no summary in the model answer") : err;
        return r;
    }
    r.value["summary"]  = summary;
    r.value["excerpts"] = stringArray(answer.value("excerpts"));
    r.value["tags"]     = stringArray(answer.value("tags"));
    r.value["language"] = answer.value("language").toString();
    r.value["truncated"] = truncated;
    if (isTranscript)
        r.value["source"] = source.isEmpty() ? QStringLiteral("whisper") : source;
    return r;
}

//----------------------------------------------------------------------
MdiEnrichmentJob::Result MdiEnrichmentJob::makeTranscript(const FileItem &file)
{
    Result r;
    if (m_config.whisperPath.isEmpty() || m_config.whisperModelPath.isEmpty()) {
        r.outcome = QStringLiteral("skipped");
        r.reason  = m_config.whisperPath.isEmpty() ? QStringLiteral("whisper not found")
                                                   : QStringLiteral("whisper model not found");
        return r;
    }
    if (m_config.ffmpegPath.isEmpty()) {
        r.outcome = QStringLiteral("skipped");
        r.reason  = QStringLiteral("ffmpeg not found");
        return r;
    }
    r.tool = QStringLiteral("whisper.cpp");
    r.model = QString();   // local program, not an AI provider over HTTP

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        r.outcome = QStringLiteral("failed");
        r.reason  = QStringLiteral("no temporary folder");
        return r;
    }
    // whisper.cpp reads 16 kHz mono PCM
    const QString wav = tmp.filePath(QStringLiteral("audio.wav"));
    const double duration = probeDuration(file.fullPath);
    QString err;
    if (!runTool(m_config.ffmpegPath, {"-nostdin", "-y", "-loglevel", "error", "-i", file.fullPath,
                                       "-vn", "-ac", "1", "-ar", "16000", "-c:a", "pcm_s16le", wav},
                 nullptr, &err)) {
        r.outcome = QStringLiteral("failed");
        r.reason  = err;
        return r;
    }

    // A long recording takes longer than the default limit: allow twice its length
    const int limit = qMax(m_config.toolTimeoutMs, int(qMin(duration * 2000.0, 6.0 * 3600 * 1000)));
    const QString outBase = tmp.filePath(QStringLiteral("transcript"));
    const QString threads = QString::number(qMax(1, QThread::idealThreadCount() - 1));
    // whisper reports "progress = NN%" on its error output (-pp)
    auto onLine = [this, duration](const QString &line) {
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("progress\\s*=\\s*(\\d+)%")).match(line);
        if (!m.hasMatch())
            return;
        const double fraction = m.captured(1).toDouble() / 100.0;
        QVariantMap step = stepInfo(QStringLiteral("transcript"));
        step[QStringLiteral("event")]     = QStringLiteral("progress");
        step[QStringLiteral("analysis")]  = QStringLiteral("transcript");
        step[QStringLiteral("filePath")]  = m_stepFile;
        step[QStringLiteral("fileIndex")] = m_stepIndex;
        step[QStringLiteral("fileCount")] = m_files.size();
        step[QStringLiteral("progress")]  = fraction;
        if (duration > 0)
            step[QStringLiteral("detail")] = QStringLiteral("%1 / %2 min").arg(qRound(duration * fraction / 60.0))
                                                                          .arg(qRound(duration / 60.0));
        emit activity(step);
    };
    if (!runTool(m_config.whisperPath, {"-m", m_config.whisperModelPath, "-f", wav, "-l", "auto",
                                        "-t", threads, "-oj", "-of", outBase, "-np", "-pp"},
                 nullptr, &err, limit, onLine)) {
        r.outcome = QStringLiteral("failed");
        r.reason  = err;
        return r;
    }

    QFile json(outBase + QStringLiteral(".json"));
    if (!json.open(QIODevice::ReadOnly)) {
        r.outcome = QStringLiteral("failed");
        r.reason  = QStringLiteral("whisper wrote no output");
        return r;
    }
    const QJsonObject doc = QJsonDocument::fromJson(json.readAll()).object();
    QJsonArray passages;
    QString srt;
    int n = 0;
    for (const QJsonValue &v : doc.value("transcription").toArray()) {
        const QJsonObject o = v.toObject();
        const QString text = o.value("text").toString().trimmed();
        if (text.isEmpty())
            continue;
        const double start = o.value("offsets").toObject().value("from").toDouble() / 1000.0;
        const double end   = o.value("offsets").toObject().value("to").toDouble() / 1000.0;
        passages.append(QJsonObject{{"start", start}, {"end", end}, {"text", text}});
        srt += QStringLiteral("%1\n%2 --> %3\n%4\n\n").arg(++n).arg(srtTime(start), srtTime(end), text);
    }
    if (passages.isEmpty()) {
        r.outcome = QStringLiteral("skipped");
        r.reason  = QStringLiteral("no speech found");
        return r;
    }

    // Subtitles file, so the transcript opens in any player at its timecodes
    const QString rel = derivedRelative(file, QStringLiteral("transcript"), QStringLiteral(".srt"));
    QFile out(absoluteDerived(rel));
    if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        out.write(srt.toUtf8());
        out.close();
        r.derivedFiles << rel;
    }
    r.value["language"] = doc.value("result").toObject().value("language").toString();
    r.value["passages"] = passages;
    r.value["duration"] = duration;
    return r;
}

QString MdiEnrichmentJob::transcriptText(const FileItem &file)
{
    for (const MdiData::Datum &d : MdiData::forFile(m_connectionName, m_config.catalogId, file.fullPath)) {
        if (d.analysis != QLatin1String("transcript") || d.outcome != QLatin1String("completed"))
            continue;
        QStringList parts;
        for (const QJsonValue &p : d.value.value("passages").toArray())
            parts << p.toObject().value("text").toString();
        return parts.join(' ');
    }
    return QString();
}

MdiEnrichmentJob::Result MdiEnrichmentJob::makeSegments(const FileItem &file)
{
    Result r;
    r.model = m_config.textModel;

    QJsonArray passages;
    double duration = 0.0;
    for (const MdiData::Datum &d : MdiData::forFile(m_connectionName, m_config.catalogId, file.fullPath))
        if (d.analysis == QLatin1String("transcript") && d.outcome == QLatin1String("completed")) {
            passages = d.value.value("passages").toArray();
            duration = d.value.value("duration").toDouble();
        }
    if (passages.isEmpty()) {
        r.outcome = QStringLiteral("skipped");
        r.reason  = QStringLiteral("no transcript");
        return r;
    }
    if (duration <= 0)
        duration = passages.last().toObject().value("end").toDouble();

    // A few words (ambient sound, a short clip) make no topics
    int words = 0;
    for (const QJsonValue &v : passages)
        words += v.toObject().value("text").toString().split(' ', Qt::SkipEmptyParts).size();
    if (words < kMinSegmentWords) {
        r.outcome = QStringLiteral("skipped");
        r.reason  = QStringLiteral("transcript too short");
        return r;
    }

    // Timecoded lines, split into chunks the model can take in one request
    QStringList chunks;
    QString current;
    for (const QJsonValue &v : passages) {
        const QJsonObject p = v.toObject();
        const QString line = QStringLiteral("[%1] %2\n").arg(formatTimecode(p.value("start").toDouble()),
                                                             p.value("text").toString());
        if (current.size() + line.size() > kSegmentChunkChars && !current.isEmpty()) {
            chunks << current;
            current.clear();
        }
        current += line;
    }
    if (!current.isEmpty())
        chunks << current;

    QList<QJsonObject> segments;
    for (const QString &chunk : chunks) {
        if (m_stop.load())
            break;
        const QString prompt = QStringLiteral(
            "You split the timecoded transcript of a recording into topic segments for a personal media catalog. "
            "Each line starts with its timecode [h:mm:ss]. Make consecutive segments, each covering one topic, "
            "typically 1 to 10 minutes long, that together cover the whole transcript below. "
            "Write titles, summaries and topics in the SAME LANGUAGE AS THE TRANSCRIPT. "
            "Answer as JSON only: {\"segments\": [{\"start\": \"h:mm:ss\", \"end\": \"h:mm:ss\", "
            "\"title\": \"short title\", \"summary\": \"1 or 2 sentences\", "
            "\"topics\": [\"2 to 5 lowercase keywords\"]}]}.\n\nTRANSCRIPT:\n%1"
            "\n\nRemember: same language as the TRANSCRIPT, JSON only.").arg(chunk);
        QString err;
        const QJsonObject answer = askOllama(m_config.textModel, prompt, {}, &err);
        const QJsonArray found = answer.value("segments").toArray();
        if (found.isEmpty()) {
            r.outcome = QStringLiteral("failed");
            r.reason  = err.isEmpty() ? QStringLiteral("no segments in the model answer") : err;
            return r;
        }
        for (const QJsonValue &v : found) {
            const QJsonObject o = v.toObject();
            const QString title = o.value("title").toString().trimmed();
            if (title.isEmpty())
                continue;
            segments.append(QJsonObject{
                {"start",   parseTimecode(o.value("start"))},
                {"end",     parseTimecode(o.value("end"))},
                {"title",   title},
                {"summary", o.value("summary").toString().trimmed()},
                {"topics",  stringArray(o.value("topics"))},
            });
        }
    }
    if (segments.isEmpty()) {
        r.outcome = QStringLiteral("failed");
        r.reason  = QStringLiteral("no segments in the model answer");
        return r;
    }

    // Contiguous ranges (MDI-F38): sorted, each ends where the next starts
    std::sort(segments.begin(), segments.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value("start").toDouble() < b.value("start").toDouble();
    });
    QJsonArray out;
    for (int i = 0; i < segments.size(); ++i) {
        QJsonObject s = segments[i];
        const double start = qBound(0.0, s.value("start").toDouble(), duration);
        const double end   = i + 1 < segments.size() ? segments[i + 1].value("start").toDouble() : duration;
        if (end <= start)
            continue;
        s["start"] = start;
        s["end"]   = end;
        out.append(s);
    }
    r.value["segments"] = out;
    return r;
}

MdiEnrichmentJob::Result MdiEnrichmentJob::makeClusters()
{
    Result r;
    r.model = m_config.textModel;

    // Every segment of the catalog, with an id the model refers to
    struct Ref { QString fullPath; int index; QJsonObject segment; };
    QList<Ref> refs;
    QString lines;
    for (const MdiData::Datum &d : MdiData::forCatalog(m_connectionName, m_config.catalogId, QStringLiteral("segments"))) {
        const QJsonArray segs = d.value.value("segments").toArray();
        for (int i = 0; i < segs.size(); ++i) {
            const QJsonObject s = segs.at(i).toObject();
            const QString id = QStringLiteral("S%1").arg(refs.size() + 1);
            QStringList topics;
            for (const QJsonValue &t : s.value("topics").toArray())
                topics << t.toString();
            lines += QStringLiteral("%1 | %2 | %3 — %4 [%5]\n").arg(id, QFileInfo(d.fullPath).fileName(),
                         s.value("title").toString(), s.value("summary").toString(), topics.join(", "));
            refs.append({d.fullPath, i, s});
        }
    }
    if (refs.size() < 2) {
        r.outcome = QStringLiteral("none");   // nothing to group: no datum written
        return r;
    }
    lines.truncate(kMaxTextChars);

    const QString prompt = QStringLiteral(
        "Below are topic segments (id | file | title — summary [topics]) from recordings of one catalog. "
        "Group segments that deal with the same subject into detailed thematic clusters, across files. "
        "Each segment belongs to at most one cluster; a cluster has at least 2 segments. "
        "Write labels and descriptions in the SAME LANGUAGE AS THE SEGMENTS. "
        "Answer as JSON only: {\"clusters\": [{\"label\": \"short label\", "
        "\"description\": \"1 or 2 sentences\", \"members\": [\"S1\", \"S7\"]}]}.\n\nSEGMENTS:\n%1").arg(lines);
    QString err;
    const QJsonObject answer = askOllama(m_config.textModel, prompt, {}, &err);
    const QJsonArray found = answer.value("clusters").toArray();
    if (found.isEmpty()) {
        r.outcome = QStringLiteral("failed");
        r.reason  = err.isEmpty() ? QStringLiteral("no clusters in the model answer") : err;
        return r;
    }

    QSet<int> used;
    QJsonArray clusters;
    for (const QJsonValue &v : found) {
        const QJsonObject c = v.toObject();
        QJsonArray members;
        for (const QJsonValue &m : c.value("members").toArray()) {
            const int k = m.toString().mid(1).toInt() - 1;
            if (k < 0 || k >= refs.size() || used.contains(k))
                continue;   // unknown id, or already in a cluster (MDI-F37)
            used.insert(k);
            const Ref &ref = refs.at(k);
            members.append(QJsonObject{
                {"fullPath", ref.fullPath},
                {"segment",  ref.index},
                {"start",    ref.segment.value("start")},
                {"end",      ref.segment.value("end")},
                {"title",    ref.segment.value("title")},
            });
        }
        if (members.size() < 2 || c.value("label").toString().trimmed().isEmpty())
            continue;
        clusters.append(QJsonObject{
            {"label",       c.value("label").toString().trimmed()},
            {"description", c.value("description").toString().trimmed()},
            {"members",     members},
        });
    }
    if (clusters.isEmpty()) {
        r.outcome = QStringLiteral("failed");
        r.reason  = QStringLiteral("no usable cluster in the model answer");
        return r;
    }
    r.value["clusters"] = clusters;
    r.value["segmentCount"] = refs.size();
    return r;
}

//----------------------------------------------------------------------
QList<QVector<float>> MdiEnrichmentJob::embedTexts(const QString &ollamaUrl, const QString &model,
                                                   const QStringList &texts, QString *error, int timeoutMs)
{
    QList<QVector<float>> vectors;
    if (providerClass(QUrl(ollamaUrl)) != QLatin1String("local")) {
        *error = QStringLiteral("endpoint is not local");
        return vectors;
    }
    QJsonArray input;
    for (const QString &t : texts)
        input.append(t);
    const QJsonObject body{{"model", model}, {"input", input}};

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(ollamaUrl + QStringLiteral("/api/embed")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(timeoutMs);
    QNetworkReply *reply = manager.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        *error = reply->errorString();
    } else {
        const QJsonObject doc = QJsonDocument::fromJson(reply->readAll()).object();
        for (const QJsonValue &e : doc.value("embeddings").toArray()) {
            const QJsonArray a = e.toArray();
            QVector<float> v(a.size());
            double norm = 0.0;
            for (int i = 0; i < a.size(); ++i) {
                v[i] = float(a.at(i).toDouble());
                norm += double(v[i]) * double(v[i]);
            }
            norm = std::sqrt(norm);
            if (norm > 0)
                for (float &x : v)
                    x = float(x / norm);
            vectors.append(v);
        }
        if (vectors.size() != texts.size()) {
            *error = doc.contains("error") ? doc.value("error").toString()
                                           : QStringLiteral("unexpected embeddings answer");
            vectors.clear();
        }
    }
    reply->deleteLater();
    return vectors;
}

MdiEnrichmentJob::Result MdiEnrichmentJob::makeEmbeddings(const FileItem &file)
{
    Result r;
    r.model = m_config.embedModel;

    QList<MdiData::EmbeddingRow> rows;
    auto add = [&rows](const QString &kind, int index, double start, double end, const QString &text) {
        if (text.trimmed().isEmpty())
            return;
        MdiData::EmbeddingRow row;
        row.kind  = kind;
        row.index = index;
        row.start = start;
        row.end   = end;
        row.text  = text.trimmed();
        rows.append(row);
    };
    auto joinTags = [](const QJsonValue &tags) {
        QStringList list;
        for (const QJsonValue &t : tags.toArray())
            list << t.toString();
        return list.join(", ");
    };

    const QString name = QFileInfo(file.fullPath).completeBaseName();
    for (const MdiData::Datum &d : MdiData::forFile(m_connectionName, m_config.catalogId, file.fullPath)) {
        if (d.outcome != QLatin1String("completed"))
            continue;
        if (d.analysis == QLatin1String("summary")) {
            // The file's name helps queries naming a place or a person
            add(QStringLiteral("summary"), 0, -1, -1,
                name + QStringLiteral(". ") + d.value.value("summary").toString()
                + QStringLiteral(" (") + joinTags(d.value.value("tags")) + QStringLiteral(")"));
            int k = 0;
            for (const QJsonValue &e : d.value.value("excerpts").toArray())
                add(QStringLiteral("excerpt"), k++, -1, -1, e.toString());
        } else if (d.analysis == QLatin1String("caption")) {
            add(QStringLiteral("caption"), 0, -1, -1,
                d.value.value("caption").toString() + QStringLiteral(" (") + joinTags(d.value.value("tags")) + QStringLiteral(")"));
        } else if (d.analysis == QLatin1String("segments")) {
            const QJsonArray segs = d.value.value("segments").toArray();
            for (int i = 0; i < segs.size(); ++i) {
                const QJsonObject s = segs.at(i).toObject();
                add(QStringLiteral("segment"), i, s.value("start").toDouble(), s.value("end").toDouble(),
                    s.value("title").toString() + QStringLiteral(". ") + s.value("summary").toString()
                    + QStringLiteral(" (") + joinTags(s.value("topics")) + QStringLiteral(")"));
            }
        } else if (d.analysis == QLatin1String("transcript")) {
            // Windows of consecutive passages, so a hit lands on a timecode
            QString text;
            double start = -1, end = -1;
            int words = 0, k = 0;
            for (const QJsonValue &v : d.value.value("passages").toArray()) {
                const QJsonObject p = v.toObject();
                if (start < 0)
                    start = p.value("start").toDouble();
                end = p.value("end").toDouble();
                text += p.value("text").toString() + ' ';
                words += p.value("text").toString().split(' ', Qt::SkipEmptyParts).size();
                if (end - start >= kWindowSeconds || words >= kWindowWords) {
                    add(QStringLiteral("passage"), k++, start, end, text);
                    text.clear();
                    start = -1;
                    words = 0;
                }
            }
            if (!text.isEmpty())
                add(QStringLiteral("passage"), k, start, end, text);
        }
    }
    if (rows.isEmpty()) {
        r.outcome = QStringLiteral("skipped");
        r.reason  = QStringLiteral("nothing to index");
        return r;
    }

    for (int from = 0; from < rows.size() && !m_stop.load(); from += kEmbedBatch) {
        QStringList texts;
        for (int i = from; i < qMin(rows.size(), from + kEmbedBatch); ++i)
            texts << rows[i].text;
        QString err;
        const QList<QVector<float>> vectors = embedTexts(m_config.ollamaUrl, m_config.embedModel, texts, &err,
                                                         m_config.httpTimeoutMs);
        if (vectors.isEmpty()) {
            r.outcome = QStringLiteral("failed");
            r.reason  = err;
            return r;
        }
        for (int i = 0; i < vectors.size(); ++i)
            rows[from + i].vector = vectors[i];
    }
    if (m_stop.load())
        return r;
    if (!MdiData::replaceEmbeddings(m_connectionName, m_config.catalogId, file.fullPath, m_config.embedModel, rows)) {
        r.outcome = QStringLiteral("failed");
        r.reason  = QStringLiteral("index not written");
        return r;
    }
    r.value["count"] = rows.size();
    return r;
}

//----------------------------------------------------------------------
bool MdiEnrichmentJob::runTool(const QString &program, const QStringList &args, QByteArray *output, QString *error,
                               int timeoutMs, const std::function<void(const QString &)> &onErrorLine)
{
    const int limit = timeoutMs > 0 ? timeoutMs : m_config.toolTimeoutMs;
    // Program path + argument list, never a shell (MDI-C7)
    QProcess process;
    process.setProgram(program);
    process.setArguments(args);
    process.start();
    if (!process.waitForStarted(10000)) {
        *error = QStringLiteral("%1 did not start").arg(QFileInfo(program).fileName());
        return false;
    }

    QElapsedTimer clock;
    clock.start();
    QByteArray out;
    QByteArray errTail;   // kept for the failure reason when lines are consumed
    auto drainErrors = [&]() {
        if (!onErrorLine)
            return;
        errTail += process.readAllStandardError();
        int nl;
        while ((nl = errTail.indexOf('\n')) >= 0) {
            onErrorLine(QString::fromUtf8(errTail.left(nl)));
            errTail.remove(0, nl + 1);
        }
    };
    while (!process.waitForFinished(200)) {
        out += process.readAllStandardOutput();
        drainErrors();
        if (m_stop.load() || clock.elapsed() > limit) {
            process.kill();
            process.waitForFinished(2000);
            *error = m_stop.load() ? QStringLiteral("stopped") : QStringLiteral("time limit reached");
            return false;
        }
    }
    out += process.readAllStandardOutput();
    drainErrors();
    if (output)
        *output = out;

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(errTail + process.readAllStandardError()).trimmed();
        *error = stderrText.isEmpty() ? QStringLiteral("exit code %1").arg(process.exitCode())
                                      : stderrText.section('\n', -1);
        return false;
    }
    return true;
}

QJsonObject MdiEnrichmentJob::askOllama(const QString &model, const QString &prompt,
                                        const QStringList &imagePaths, QString *error)
{
    // A long answer occasionally comes back as broken JSON: one more try, then
    // the datum is recorded as failed (MDI-C20).
    QJsonObject result = askOllamaOnce(model, prompt, imagePaths, error);
    if (result.isEmpty() && !m_stop.load() && error->startsWith(QLatin1String("answer is not JSON"))) {
        error->clear();
        result = askOllamaOnce(model, prompt, imagePaths, error);
    }
    return result;
}

QJsonObject MdiEnrichmentJob::askOllamaOnce(const QString &model, const QString &prompt,
                                            const QStringList &imagePaths, QString *error)
{
    const QUrl base(m_config.ollamaUrl);
    // Content goes only to a local endpoint until the egress confirmation of
    // MDI-F24 exists.
    if (providerClass(base) != QLatin1String("local")) {
        *error = QStringLiteral("endpoint is not local");
        return {};
    }

    QJsonObject body;
    body["model"]  = model;
    body["prompt"] = prompt;
    body["format"] = "json";
    body["stream"] = false;
    body["think"]  = false;
    body["options"] = QJsonObject{{"temperature", 0.2},
                                  {"num_ctx", kContextTokens},
                                  {"num_predict", kAnswerTokens}};
    if (!imagePaths.isEmpty()) {
        QJsonArray images;
        for (const QString &p : imagePaths) {
            QFile f(p);
            if (f.open(QIODevice::ReadOnly))
                images.append(QString::fromLatin1(f.readAll().toBase64()));
        }
        body["images"] = images;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(m_config.ollamaUrl + QStringLiteral("/api/generate")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = manager.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    QTimer poll;
    QElapsedTimer clock;
    clock.start();
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (m_stop.load() || clock.elapsed() > m_config.httpTimeoutMs)
            reply->abort();   // finished() follows
    });
    poll.start(200);
    loop.exec();
    poll.stop();

    QJsonObject result;
    if (reply->error() != QNetworkReply::NoError) {
        *error = m_stop.load() ? QStringLiteral("stopped")
               : clock.elapsed() > m_config.httpTimeoutMs ? QStringLiteral("time limit reached")
               : reply->errorString();
    } else {
        const QByteArray raw = reply->readAll();
        QJsonParseError envelopeError;
        const QJsonObject envelope = QJsonDocument::fromJson(raw, &envelopeError).object();
        const double evalNs = envelope.value("eval_duration").toDouble();
        if (evalNs > 0)
            m_lastTokensPerSecond = qRound(envelope.value("eval_count").toDouble() * 10e9 / evalNs) / 10.0;
        const QString text = envelope.value("response").toString();
        QJsonParseError answerError;
        result = QJsonDocument::fromJson(text.toUtf8(), &answerError).object();
        if (result.isEmpty()) {
            // Some answers wrap the object in prose or a code fence
            const int first = text.indexOf('{'), last = text.lastIndexOf('}');
            if (first >= 0 && last > first)
                result = QJsonDocument::fromJson(text.mid(first, last - first + 1).toUtf8()).object();
        }
        if (result.isEmpty() && answerError.error == QJsonParseError::UnterminatedObject) {
            // The model sometimes stops before closing the object: close it
            for (const char *tail : {"}", "\"}", "]}", "\"]}"}) {
                result = QJsonDocument::fromJson((text.trimmed() + QLatin1String(tail)).toUtf8()).object();
                if (!result.isEmpty())
                    break;
            }
        }
        if (result.isEmpty())
            *error = envelope.contains("error") ? envelope.value("error").toString()
                   : envelope.isEmpty()
                     ? QStringLiteral("answer is not JSON (reply of %1 bytes: %2)")
                           .arg(raw.size()).arg(envelopeError.errorString())
                     : QStringLiteral("answer is not JSON (%1 at %2 of %3 chars)")
                           .arg(answerError.errorString()).arg(answerError.offset).arg(text.size());
    }
    reply->deleteLater();
    return result;
}
