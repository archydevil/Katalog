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
    return derived;
}
