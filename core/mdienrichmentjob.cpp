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
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QUrl>
#include <QThread>
#include <QDebug>

#include <KFileMetaData/ExtractorCollection>
#include <KFileMetaData/Extractor>
#include <KFileMetaData/SimpleExtractionResult>

namespace {

// Prompt versions take part in the parameters, so a changed prompt re-runs
// its analysis on the next job (MDI-F54).
const QString kCaptionPrompt = QStringLiteral("caption-v1");
const QString kSummaryPrompt = QStringLiteral("summary-v2");

// Text sent to the model is cut to this length: long documents are summarised
// from their beginning. The context window is sized for it: with Ollama's
// default window a long transcript is cut and the answer is no longer JSON.
constexpr int kMaxTextChars  = 24000;
constexpr int kContextTokens = 16384;
constexpr int kAnswerTokens  = 1024;

constexpr int kThumbnailSize = 320;   // longest side, MDI-F29

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

    for (int i = 0; i < fileCount && !m_stop.load(); ++i) {
        const FileItem &file = m_files.at(i);
        const QStringList analyses = plannedAnalyses(file);
        if (analyses.isEmpty())
            continue;

        // A source that cannot be reached (disconnected drive) keeps its data:
        // nothing is overwritten, so it stays browsable offline (MDI-F44).
        if (!QFileInfo::exists(file.fullPath)) {
            skipped += analyses.size();
            continue;
        }

        for (const QString &analysis : analyses) {
            if (!waitWhilePaused())
                break;

            const QJsonObject params = analysisParams(analysis);
            // Incremental (MDI-F13): same file state and parameters, and the
            // derived file still present, means nothing to do.
            if (MdiData::isCurrent(m_connectionName, m_config.catalogId, file.fullPath, analysis,
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
            Result r = runAnalysis(analysis, file);
            if (m_stop.load())
                break;   // the abandoned step is not recorded (MDI-F10)

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
            MdiData::upsert(m_connectionName, d);
            emit datumCommitted(file.fullPath, analysis);

            if (r.outcome == QLatin1String("completed"))      ++completed;
            else if (r.outcome == QLatin1String("skipped"))   ++skipped;
            else                                              ++failed;
        }
    }

    MdiData::close(m_connectionName);

    summary[QStringLiteral("completed")] = completed;
    summary[QStringLiteral("unchanged")] = unchanged;
    summary[QStringLiteral("skipped")]   = skipped;
    summary[QStringLiteral("failed")]    = failed;
    summary[QStringLiteral("stopped")]   = m_stop.load();
    emit finished(summary);
}

//----------------------------------------------------------------------
QStringList MdiEnrichmentJob::plannedAnalyses(const FileItem &file) const
{
    const QJsonObject s = m_config.settings;
    bool typeSelected = false;
    for (const QJsonValue &t : s.value("fileTypes").toArray())
        typeSelected = typeSelected || lowerType(t.toString()) == file.fileType;
    if (!typeSelected)
        return {};

    const QJsonObject image = s.value("image").toObject();
    const QJsonObject video = s.value("video").toObject();
    const QJsonObject audio = s.value("audio").toObject();
    // Profiles saved before the AI options existed have no "ai" key: both
    // analyses are on for them, as they are by default in the form.
    const QJsonObject ai    = s.value("ai").toObject();
    // The K3 form writes "caption"; "imageCaption" is accepted as well
    const bool caption = ai.value("imageCaption").toBool(ai.value("caption").toBool(true));
    const bool summary = ai.value("summary").toBool(true);

    QStringList list;
    if (file.fileType == QLatin1String("image")) {
        if (image.value("thumbnail").toBool()) list << QStringLiteral("thumbnail");
        if (caption)                           list << QStringLiteral("caption");
    } else if (file.fileType == QLatin1String("video")) {
        if (video.value("posterFrame").toBool())   list << QStringLiteral("poster");
        if (video.value("keyframeStrip").toBool()) list << QStringLiteral("keyframes");
        if (video.value("clip").toString(QStringLiteral("off")) != QLatin1String("off"))
            list << QStringLiteral("clip_video");
        if (caption) list << QStringLiteral("caption");
        if (summary) list << QStringLiteral("summary");
    } else if (file.fileType == QLatin1String("audio")) {
        if (audio.value("clip").toString(QStringLiteral("off")) != QLatin1String("off"))
            list << QStringLiteral("clip_audio");
        if (audio.value("waveform").toBool()) list << QStringLiteral("waveform");
        if (summary)                          list << QStringLiteral("summary");
    } else if (file.fileType == QLatin1String("text")) {
        if (summary) list << QStringLiteral("summary");
    }
    return list;
}

QJsonObject MdiEnrichmentJob::analysisParams(const QString &analysis) const
{
    const QJsonObject s = m_config.settings;
    QJsonObject p;
    if (analysis == QLatin1String("thumbnail") || analysis == QLatin1String("poster")) {
        p["size"] = kThumbnailSize;
    } else if (analysis == QLatin1String("keyframes")) {
        p["count"] = s.value("video").toObject().value("keyframeCount").toInt(8);
        p["size"]  = kThumbnailSize;
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
    }
    return p;
}

MdiEnrichmentJob::Result MdiEnrichmentJob::runAnalysis(const QString &analysis, const FileItem &file)
{
    // Previews need ffmpeg; caption and summary check their own inputs (MDI-F21)
    const bool isPreview = analysis != QLatin1String("summary") && analysis != QLatin1String("caption");
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

MdiEnrichmentJob::Result MdiEnrichmentJob::makeThumbnail(const FileItem &file)
{
    Result r;
    r.tool = m_ffmpegVersion;
    const QString rel = derivedRelative(file, QStringLiteral("thumbnail"), QStringLiteral(".jpg"));
    QString err;
    if (!runTool(m_config.ffmpegPath, {"-nostdin", "-y", "-loglevel", "error", "-i", file.fullPath,
                                       "-vf", scaleFilter(kThumbnailSize), "-frames:v", "1", "-q:v", "4",
                                       absoluteDerived(rel)}, nullptr, &err)) {
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
    const QString rel = derivedRelative(file, QStringLiteral("poster"), QStringLiteral(".jpg"));
    QString err;
    if (!runTool(m_config.ffmpegPath, {"-nostdin", "-y", "-loglevel", "error",
                                       "-ss", QString::number(at, 'f', 2), "-i", file.fullPath,
                                       "-vf", scaleFilter(kThumbnailSize), "-frames:v", "1", "-q:v", "4",
                                       absoluteDerived(rel)}, nullptr, &err)) {
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
    Result r;
    r.tool = m_ffmpegVersion;
    const int count = qBound(2, m_config.settings.value("video").toObject().value("keyframeCount").toInt(8), 32);
    const double duration = probeDuration(file.fullPath);
    if (duration <= 0) {
        r.outcome = QStringLiteral("failed");
        r.reason  = QStringLiteral("duration unknown");
        return r;
    }
    QJsonArray timecodes;
    for (int k = 0; k < count && !m_stop.load(); ++k) {
        const double at = duration * (k + 0.5) / count;   // evenly spaced (MDI-F31)
        const QString rel = derivedRelative(file, QStringLiteral("keyframes"),
                                            QStringLiteral("_%1.jpg").arg(k, 2, 10, QLatin1Char('0')));
        QString err;
        if (!runTool(m_config.ffmpegPath, {"-nostdin", "-y", "-loglevel", "error",
                                           "-ss", QString::number(at, 'f', 2), "-i", file.fullPath,
                                           "-vf", scaleFilter(kThumbnailSize), "-frames:v", "1", "-q:v", "5",
                                           absoluteDerived(rel)}, nullptr, &err)) {
            r.outcome = QStringLiteral("failed");
            r.reason  = err;
            return r;
        }
        r.derivedFiles << rel;
        timecodes.append(at);
    }
    r.value["timecodes"] = timecodes;
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

    static const QStringList plain = {"txt", "md", "markdown", "srt", "vtt", "csv", "log", "rtf", "html", "htm"};
    if (plain.contains(suffix)) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            *reason = f.errorString();
            return QString();
        }
        return QString::fromUtf8(f.read(kMaxTextChars * 4));
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
    if (file.fileType == QLatin1String("audio") || file.fileType == QLatin1String("video")) {
        source = findTranscript(file);
        if (source.isEmpty()) {
            r.outcome = QStringLiteral("skipped");
            r.reason  = QStringLiteral("no transcript");
            return r;
        }
        isTranscript = true;
    }

    QString tool, reason;
    QString text = extractText(source, &tool, &reason).simplified();
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
        "\"excerpts\": [\"up to 4 short key passages quoted verbatim\"], "
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
        r.value["source"] = source;
    return r;
}

//----------------------------------------------------------------------
bool MdiEnrichmentJob::runTool(const QString &program, const QStringList &args, QByteArray *output, QString *error)
{
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
    while (!process.waitForFinished(200)) {
        out += process.readAllStandardOutput();
        if (m_stop.load() || clock.elapsed() > m_config.toolTimeoutMs) {
            process.kill();
            process.waitForFinished(2000);
            *error = m_stop.load() ? QStringLiteral("stopped") : QStringLiteral("time limit reached");
            return false;
        }
    }
    out += process.readAllStandardOutput();
    if (output)
        *output = out;

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
        *error = stderrText.isEmpty() ? QStringLiteral("exit code %1").arg(process.exitCode())
                                      : stderrText.section('\n', -1);
        return false;
    }
    return true;
}

QJsonObject MdiEnrichmentJob::askOllama(const QString &model, const QString &prompt,
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
        const QJsonObject envelope = QJsonDocument::fromJson(reply->readAll()).object();
        const QString text = envelope.value("response").toString();
        result = QJsonDocument::fromJson(text.toUtf8()).object();
        if (result.isEmpty()) {
            // Some answers wrap the object in prose or a code fence
            const int first = text.indexOf('{'), last = text.lastIndexOf('}');
            if (first >= 0 && last > first)
                result = QJsonDocument::fromJson(text.mid(first, last - first + 1).toUtf8()).object();
        }
        if (result.isEmpty())
            *error = envelope.contains("error") ? envelope.value("error").toString()
                                                : QStringLiteral("answer is not JSON");
    }
    reply->deleteLater();
    return result;
}
