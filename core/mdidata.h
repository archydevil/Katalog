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
// File Name:   mdidata.h
// Purpose:     Derived data store (fork only, SpecMediaIntelligence.md)
// Description: Every derived datum (preview, caption, summary...) with its
//              provenance (MDI-F52). Kept in its own SQLite file in the
//              collection folder, mdi_data.db, so the enrichment runner never
//              writes to the collection database and Memory mode needs no
//              shared in-memory connection (Q6, MDI-C4, MDI-C5, MDI-F55).
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDIDATA_H
#define MDIDATA_H

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QVariantMap>
#include <QHash>
#include <QList>
#include <QVector>
#include <QVariantList>

class MdiData
{
public:
    struct Datum {
        int         catalogId = 0;
        QString     fullPath;          // source file, as cataloged
        QString     analysis;          // "thumbnail", "poster", "caption", "summary"...
        QString     outcome;           // "completed", "failed", "skipped"
        QString     reason;            // why it failed or was skipped
        QJsonObject value;             // analysis result (caption, tags, summary...)
        QString     derivedRoot;       // destination path the derived file lives under
        QStringList derivedFiles;      // relative to derivedRoot
        QString     tool;              // "ffmpeg 9.0.2", "pdftotext 26.10.0"...
        QString     model;             // "gemma4" for AI analyses
        QString     provider;          // "ollama"
        QString     providerClass;     // "local", "local-network", "remote"
        QString     endpointHost;
        QJsonObject params;            // analysis parameters, incl. prompt version
        qint64      fileSize = 0;      // source file at processing time (MDI-F13)
        QString     fileDate;
        QString     date;              // processing date, ISO 8601
    };

    // One indexed text of a file for semantic search (MDI-F47): a summary,
    // a caption, a topic segment or a transcript window
    struct EmbeddingRow {
        QString        kind;          // "summary", "excerpt", "caption", "segment", "passage"
        int            index = 0;     // segment or window number within the file
        double         start = -1;    // seconds, for timecoded kinds; -1 otherwise
        double         end   = -1;
        QString        text;
        QVector<float> vector;        // unit length
    };

    // One enrichment job not finished, in run order (MDI-F12, MDI-C28)
    struct PendingJob {
        int     deviceId = 0;
        bool    paused   = false;   // paused when it was last written
        QString kind     = QStringLiteral("enrich");   // or "reinterpret" (D33)
        QString folder;             // re-interpretation scope: a folder, all when empty
        QString fileType;           // and a file type ("image"...), all when empty
        bool sameWork(const PendingJob &o) const
        { return deviceId == o.deviceId && kind == o.kind && folder == o.folder && fileType == o.fileType; }
    };

    static QString filePath(const QString &collectionFolder);

    // Opens (creating when absent) the store on a connection of the caller's
    // thread, and ensures its tables. False when the file cannot be opened.
    static bool open(const QString &connectionName, const QString &dbPath);
    static void close(const QString &connectionName);

    static bool upsert(const QString &connectionName, const Datum &datum);

    // True when a completed datum exists for the same file state and parameters
    static bool isCurrent(const QString &connectionName, int catalogId, const QString &fullPath,
                          const QString &analysis, qint64 fileSize, const QString &fileDate,
                          const QJsonObject &params);

    // Per file: {thumbnail: absolute path, hasData: true, caption: text}, keyed by full path
    static QHash<QString, QVariantMap> catalogIndex(const QString &connectionName, int catalogId);

    // Every datum of one file, for the details view
    static QList<Datum> forFile(const QString &connectionName, int catalogId, const QString &fullPath);

    // Completed data of one analysis across a catalog (e.g. every "segments")
    static QList<Datum> forCatalog(const QString &connectionName, int catalogId, const QString &analysis);

    // Replaces the file's indexed texts (one model at a time per file)
    static bool replaceEmbeddings(const QString &connectionName, int catalogId, const QString &fullPath,
                                  const QString &model, const QList<EmbeddingRow> &rows);

    // Semantic search over the given catalogs (all when empty), with vectors of
    // the same model only (MDI-F49). Files by best score, each with its best
    // passages: [{catalogId, fullPath, score, hits: [{kind, text, start, end, score}]}]
    static QVariantList semanticSearch(const QString &connectionName, const QList<int> &catalogIds,
                                       const QVector<float> &query, const QString &model,
                                       int maxFiles = 30, int hitsPerFile = 3, double minScore = 0.35);

    // Catalog-wide passes (e.g. "quickhash"): the catalog state they completed on
    static QString passStamp(const QString &connectionName, int catalogId, const QString &pass);
    static void    setPassStamp(const QString &connectionName, int catalogId, const QString &pass,
                                const QString &stamp);

    // Removes the catalog's data and returns the derived files it recorded (absolute)
    static QStringList clearCatalog(const QString &connectionName, int catalogId);

    // The enrichment list (running job first, then the waiting ones), kept
    // across restarts and collection switches (MDI-C28). No file position:
    // resuming relies on the incremental check (MDI-F13).
    static QList<PendingJob> pendingJobs(const QString &connectionName);
    static bool              savePendingJobs(const QString &connectionName, const QList<PendingJob> &jobs);
};

#endif // MDIDATA_H
