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
// File Name:   mdidata.cpp
// Purpose:     Derived data store (fork only, SpecMediaIntelligence.md)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdidata.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QDateTime>
#include <QMap>
#include <algorithm>
#include <cstring>
#include <QDebug>

namespace {

QString toJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonObject fromJson(const QString &text)
{
    return QJsonDocument::fromJson(text.toUtf8()).object();
}

MdiData::Datum readDatum(const QSqlQuery &q)
{
    MdiData::Datum d;
    d.catalogId     = q.value("datum_catalog_id").toInt();
    d.fullPath      = q.value("datum_full_path").toString();
    d.analysis      = q.value("datum_analysis").toString();
    d.outcome       = q.value("datum_outcome").toString();
    d.reason        = q.value("datum_reason").toString();
    d.value         = fromJson(q.value("datum_value").toString());
    d.derivedRoot   = q.value("datum_derived_root").toString();
    const QString files = q.value("datum_derived_files").toString();
    d.derivedFiles  = files.isEmpty() ? QStringList() : files.split('\n');
    d.tool          = q.value("datum_tool").toString();
    d.model         = q.value("datum_model").toString();
    d.provider      = q.value("datum_provider").toString();
    d.providerClass = q.value("datum_provider_class").toString();
    d.endpointHost  = q.value("datum_endpoint_host").toString();
    d.params        = fromJson(q.value("datum_params").toString());
    d.fileSize      = q.value("datum_file_size").toLongLong();
    d.fileDate      = q.value("datum_file_date").toString();
    d.date          = q.value("datum_date").toString();
    return d;
}

} // namespace

QString MdiData::filePath(const QString &collectionFolder)
{
    return collectionFolder + QStringLiteral("/mdi_data.db");
}

bool MdiData::open(const QString &connectionName, const QString &dbPath)
{
    QSqlDatabase db = QSqlDatabase::contains(connectionName)
                      ? QSqlDatabase::database(connectionName, false)
                      : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    if (!db.isOpen()) {
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
        if (!db.open()) {
            qWarning() << "MdiData::open failed:" << dbPath << db.lastError().text();
            return false;
        }
    }

    QSqlQuery q(db);
    // WAL: the details view reads while the runner writes (MDI-C4)
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QLatin1String(R"(
        CREATE TABLE IF NOT EXISTS mdi_schema (
            mdi_schema_version INTEGER NOT NULL
        )
    )"));
    q.exec(QStringLiteral("INSERT INTO mdi_schema (mdi_schema_version) SELECT 1 WHERE NOT EXISTS (SELECT 1 FROM mdi_schema)"));
    const bool ok = q.exec(QLatin1String(R"(
        CREATE TABLE IF NOT EXISTS mdi_datum (
            datum_catalog_id     INTEGER NOT NULL,
            datum_full_path      TEXT    NOT NULL,
            datum_analysis       TEXT    NOT NULL,
            datum_outcome        TEXT    NOT NULL,
            datum_reason         TEXT,
            datum_value          TEXT,
            datum_derived_root   TEXT,
            datum_derived_files  TEXT,
            datum_tool           TEXT,
            datum_model          TEXT,
            datum_provider       TEXT,
            datum_provider_class TEXT,
            datum_endpoint_host  TEXT,
            datum_params         TEXT,
            datum_file_size      INTEGER,
            datum_file_date      TEXT,
            datum_date           TEXT,
            PRIMARY KEY (datum_catalog_id, datum_full_path, datum_analysis)
        )
    )"));
    if (!ok)
        qWarning() << "MdiData::open: table creation failed:" << q.lastError().text();

    // Catalog-wide passes and the catalog state they completed on
    q.exec(QLatin1String(R"(
        CREATE TABLE IF NOT EXISTS mdi_pass (
            pass_catalog_id INTEGER NOT NULL,
            pass_name       TEXT    NOT NULL,
            pass_stamp      TEXT,
            pass_date       TEXT,
            PRIMARY KEY (pass_catalog_id, pass_name)
        )
    )"));

    // Semantic search index (MDI-F47): float32 vectors as BLOBs
    q.exec(QLatin1String(R"(
        CREATE TABLE IF NOT EXISTS mdi_embedding (
            emb_catalog_id INTEGER NOT NULL,
            emb_full_path  TEXT    NOT NULL,
            emb_kind       TEXT    NOT NULL,
            emb_index      INTEGER NOT NULL,
            emb_start      REAL,
            emb_end        REAL,
            emb_text       TEXT,
            emb_model      TEXT    NOT NULL,
            emb_vector     BLOB    NOT NULL
        )
    )"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS mdi_embedding_file ON mdi_embedding (emb_catalog_id, emb_full_path)"));

    // Enrichment jobs not finished, in run order (MDI-C28)
    q.exec(QLatin1String(R"(
        CREATE TABLE IF NOT EXISTS mdi_job (
            job_order     INTEGER NOT NULL PRIMARY KEY,
            job_device_id INTEGER NOT NULL,
            job_paused    INTEGER NOT NULL DEFAULT 0
        )
    )"));
    return ok;
}

void MdiData::close(const QString &connectionName)
{
    {
        QSqlDatabase db = QSqlDatabase::database(connectionName, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

bool MdiData::upsert(const QString &connectionName, const Datum &d)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QLatin1String(R"(
        INSERT OR REPLACE INTO mdi_datum (
            datum_catalog_id, datum_full_path, datum_analysis, datum_outcome, datum_reason,
            datum_value, datum_derived_root, datum_derived_files, datum_tool, datum_model,
            datum_provider, datum_provider_class, datum_endpoint_host, datum_params,
            datum_file_size, datum_file_date, datum_date)
        VALUES (:catalog_id, :full_path, :analysis, :outcome, :reason,
                :value, :derived_root, :derived_files, :tool, :model,
                :provider, :provider_class, :endpoint_host, :params,
                :file_size, :file_date, :date)
    )"));
    q.bindValue(":catalog_id",     d.catalogId);
    q.bindValue(":full_path",      d.fullPath);
    q.bindValue(":analysis",       d.analysis);
    q.bindValue(":outcome",        d.outcome);
    q.bindValue(":reason",         d.reason);
    q.bindValue(":value",          toJson(d.value));
    q.bindValue(":derived_root",   d.derivedRoot);
    q.bindValue(":derived_files",  d.derivedFiles.join('\n'));
    q.bindValue(":tool",           d.tool);
    q.bindValue(":model",          d.model);
    q.bindValue(":provider",       d.provider);
    q.bindValue(":provider_class", d.providerClass);
    q.bindValue(":endpoint_host",  d.endpointHost);
    q.bindValue(":params",         toJson(d.params));
    q.bindValue(":file_size",      d.fileSize);
    q.bindValue(":file_date",      d.fileDate);
    q.bindValue(":date",           d.date);
    if (!q.exec()) {
        qWarning() << "MdiData::upsert failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool MdiData::isCurrent(const QString &connectionName, int catalogId, const QString &fullPath,
                        const QString &analysis, qint64 fileSize, const QString &fileDate,
                        const QJsonObject &params)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QLatin1String(R"(
        SELECT datum_outcome, datum_file_size, datum_file_date, datum_params
        FROM mdi_datum
        WHERE datum_catalog_id = :catalog_id AND datum_full_path = :full_path
          AND datum_analysis = :analysis
    )"));
    q.bindValue(":catalog_id", catalogId);
    q.bindValue(":full_path",  fullPath);
    q.bindValue(":analysis",   analysis);
    if (!q.exec() || !q.next())
        return false;
    return q.value(0).toString() == QLatin1String("completed")
        && q.value(1).toLongLong() == fileSize
        && q.value(2).toString() == fileDate
        && q.value(3).toString() == toJson(params);
}

QHash<QString, QVariantMap> MdiData::catalogIndex(const QString &connectionName, int catalogId)
{
    QHash<QString, QVariantMap> index;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QLatin1String(R"(
        SELECT datum_full_path, datum_analysis, datum_value, datum_derived_root, datum_derived_files
        FROM mdi_datum
        WHERE datum_catalog_id = :catalog_id AND datum_outcome = 'completed'
    )"));
    q.bindValue(":catalog_id", catalogId);
    if (!q.exec())
        return index;

    while (q.next()) {
        QVariantMap &entry = index[q.value(0).toString()];
        entry[QStringLiteral("hasData")] = true;
        const QString analysis = q.value(1).toString();
        const QString root     = q.value(3).toString();
        const QString files    = q.value(4).toString();
        // Thumbnail first, poster frame for videos
        if ((analysis == QLatin1String("thumbnail") || analysis == QLatin1String("poster")) && !files.isEmpty())
            entry[QStringLiteral("thumbnail")] = QDir(root).filePath(files.section('\n', 0, 0));
        if (analysis == QLatin1String("caption"))
            entry[QStringLiteral("caption")] = fromJson(q.value(2).toString()).value("caption").toString();
        if (analysis == QLatin1String("summary"))
            entry[QStringLiteral("summary")] = fromJson(q.value(2).toString()).value("summary").toString();
    }
    return index;
}

QList<MdiData::Datum> MdiData::forFile(const QString &connectionName, int catalogId, const QString &fullPath)
{
    QList<Datum> list;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QLatin1String(R"(
        SELECT * FROM mdi_datum
        WHERE datum_catalog_id = :catalog_id AND datum_full_path = :full_path
        ORDER BY datum_analysis
    )"));
    q.bindValue(":catalog_id", catalogId);
    q.bindValue(":full_path",  fullPath);
    if (!q.exec())
        return list;
    while (q.next())
        list.append(readDatum(q));
    return list;
}

QList<MdiData::Datum> MdiData::forCatalog(const QString &connectionName, int catalogId, const QString &analysis)
{
    QList<Datum> list;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QLatin1String(R"(
        SELECT * FROM mdi_datum
        WHERE datum_catalog_id = :catalog_id AND datum_analysis = :analysis
          AND datum_outcome = 'completed'
        ORDER BY datum_full_path
    )"));
    q.bindValue(":catalog_id", catalogId);
    q.bindValue(":analysis",   analysis);
    if (!q.exec())
        return list;
    while (q.next())
        list.append(readDatum(q));
    return list;
}

bool MdiData::replaceEmbeddings(const QString &connectionName, int catalogId, const QString &fullPath,
                                const QString &model, const QList<EmbeddingRow> &rows)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    db.transaction();
    QSqlQuery del(db);
    del.prepare(QLatin1String("DELETE FROM mdi_embedding WHERE emb_catalog_id = :catalog_id AND emb_full_path = :full_path"));
    del.bindValue(":catalog_id", catalogId);
    del.bindValue(":full_path",  fullPath);
    if (!del.exec()) {
        db.rollback();
        return false;
    }
    QSqlQuery ins(db);
    ins.prepare(QLatin1String(R"(
        INSERT INTO mdi_embedding (emb_catalog_id, emb_full_path, emb_kind, emb_index,
                                   emb_start, emb_end, emb_text, emb_model, emb_vector)
        VALUES (:catalog_id, :full_path, :kind, :index, :start, :end, :text, :model, :vector)
    )"));
    for (const EmbeddingRow &r : rows) {
        ins.bindValue(":catalog_id", catalogId);
        ins.bindValue(":full_path",  fullPath);
        ins.bindValue(":kind",       r.kind);
        ins.bindValue(":index",      r.index);
        ins.bindValue(":start",      r.start);
        ins.bindValue(":end",        r.end);
        ins.bindValue(":text",       r.text);
        ins.bindValue(":model",      model);
        ins.bindValue(":vector",     QByteArray(reinterpret_cast<const char *>(r.vector.constData()),
                                                r.vector.size() * int(sizeof(float))));
        if (!ins.exec()) {
            qWarning() << "MdiData::replaceEmbeddings failed:" << ins.lastError().text();
            db.rollback();
            return false;
        }
    }
    return db.commit();
}

QVariantList MdiData::semanticSearch(const QString &connectionName, const QList<int> &catalogIds,
                                     const QVector<float> &query, const QString &model,
                                     int maxFiles, int hitsPerFile, double minScore)
{
    struct Hit { int catalogId; QString fullPath, kind, text; double start, end, score; };
    QList<Hit> hits;

    QString sql = QStringLiteral("SELECT emb_catalog_id, emb_full_path, emb_kind, emb_text, emb_start, emb_end, emb_vector "
                                 "FROM mdi_embedding WHERE emb_model = :model");
    if (!catalogIds.isEmpty()) {
        QStringList ids;
        for (int id : catalogIds)
            ids << QString::number(id);
        sql += QStringLiteral(" AND emb_catalog_id IN (%1)").arg(ids.join(','));
    }
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.setForwardOnly(true);
    q.prepare(sql);
    q.bindValue(":model", model);
    if (!q.exec() || query.isEmpty())
        return {};

    // Brute-force cosine: vectors are unit length, so a dot product
    while (q.next()) {
        const QByteArray blob = q.value(6).toByteArray();
        const int n = blob.size() / int(sizeof(float));
        if (n != query.size())
            continue;
        const float *v = reinterpret_cast<const float *>(blob.constData());
        double dot = 0.0;
        for (int i = 0; i < n; ++i)
            dot += double(v[i]) * double(query[i]);
        if (dot < minScore)
            continue;
        hits.append({q.value(0).toInt(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
                     q.value(4).toDouble(), q.value(5).toDouble(), dot});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.score > b.score; });

    // Group by file, in the order of each file's best hit
    QVariantList files;
    QMap<QString, int> position;   // "catalogId|fullPath" -> index in files
    for (const Hit &h : hits) {
        const QString key = QString::number(h.catalogId) + '|' + h.fullPath;
        auto it = position.find(key);
        if (it == position.end()) {
            if (files.size() >= maxFiles)
                continue;
            QVariantMap f;
            f[QStringLiteral("catalogId")] = h.catalogId;
            f[QStringLiteral("fullPath")]  = h.fullPath;
            f[QStringLiteral("score")]     = h.score;
            f[QStringLiteral("hits")]      = QVariantList();
            it = position.insert(key, files.size());
            files.append(f);
        }
        QVariantMap f = files[it.value()].toMap();
        QVariantList list = f.value(QStringLiteral("hits")).toList();
        if (list.size() >= hitsPerFile)
            continue;
        QVariantMap hit;
        hit[QStringLiteral("kind")]  = h.kind;
        hit[QStringLiteral("text")]  = h.text;
        hit[QStringLiteral("start")] = h.start;
        hit[QStringLiteral("end")]   = h.end;
        hit[QStringLiteral("score")] = h.score;
        list.append(hit);
        f[QStringLiteral("hits")] = list;
        files[it.value()] = f;
    }
    return files;
}

QString MdiData::passStamp(const QString &connectionName, int catalogId, const QString &pass)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("SELECT pass_stamp FROM mdi_pass WHERE pass_catalog_id = :id AND pass_name = :name"));
    q.bindValue(":id", catalogId);
    q.bindValue(":name", pass);
    return q.exec() && q.next() ? q.value(0).toString() : QString();
}

void MdiData::setPassStamp(const QString &connectionName, int catalogId, const QString &pass, const QString &stamp)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO mdi_pass (pass_catalog_id, pass_name, pass_stamp, pass_date) "
                             "VALUES (:id, :name, :stamp, :date)"));
    q.bindValue(":id", catalogId);
    q.bindValue(":name", pass);
    q.bindValue(":stamp", stamp);
    q.bindValue(":date", QDateTime::currentDateTime().toString(Qt::ISODate));
    q.exec();
}

QStringList MdiData::clearCatalog(const QString &connectionName, int catalogId)
{
    QStringList derived;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QLatin1String("SELECT datum_derived_root, datum_derived_files FROM mdi_datum WHERE datum_catalog_id = :catalog_id"));
    q.bindValue(":catalog_id", catalogId);
    if (q.exec()) {
        while (q.next()) {
            const QString root = q.value(0).toString();
            const QString files = q.value(1).toString();
            if (files.isEmpty())
                continue;
            for (const QString &f : files.split('\n'))
                derived << QDir(root).filePath(f);
        }
    }
    QSqlQuery del(QSqlDatabase::database(connectionName));
    del.prepare(QLatin1String("DELETE FROM mdi_datum WHERE datum_catalog_id = :catalog_id"));
    del.bindValue(":catalog_id", catalogId);
    del.exec();
    QSqlQuery delEmb(QSqlDatabase::database(connectionName));
    delEmb.prepare(QLatin1String("DELETE FROM mdi_embedding WHERE emb_catalog_id = :catalog_id"));
    delEmb.bindValue(":catalog_id", catalogId);
    delEmb.exec();
    return derived;
}

QList<MdiData::PendingJob> MdiData::pendingJobs(const QString &connectionName)
{
    QList<PendingJob> jobs;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    if (!q.exec(QStringLiteral("SELECT job_device_id, job_paused FROM mdi_job ORDER BY job_order")))
        return jobs;
    while (q.next())
        jobs.append({ q.value(0).toInt(), q.value(1).toBool() });
    return jobs;
}

bool MdiData::savePendingJobs(const QString &connectionName, const QList<PendingJob> &jobs)
{
    // One short transaction on the main-thread connection (MDI-C4)
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.transaction())
        return false;
    QSqlQuery q(db);
    bool ok = q.exec(QStringLiteral("DELETE FROM mdi_job"));
    q.prepare(QStringLiteral("INSERT INTO mdi_job (job_order, job_device_id, job_paused) VALUES (:order, :device, :paused)"));
    for (int i = 0; ok && i < jobs.size(); ++i) {
        q.bindValue(":order",  i);
        q.bindValue(":device", jobs.at(i).deviceId);
        q.bindValue(":paused", jobs.at(i).paused ? 1 : 0);
        ok = q.exec();
    }
    if (!ok) {
        qWarning() << "MdiData::savePendingJobs failed:" << q.lastError().text();
        db.rollback();
        return false;
    }
    return db.commit();
}
