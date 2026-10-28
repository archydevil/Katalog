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
// File Name:   mdistore.cpp
// Purpose:     Derived data of the media intelligence runner (fork only)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdistore.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QDebug>

namespace {

QString toJson(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QJsonObject fromJson(const QString &s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

MdiDatum readDatum(const QSqlQuery &q)
{
    MdiDatum d;
    d.catalogId     = q.value(0).toInt();
    d.filePath      = q.value(1).toString();
    d.analysis      = q.value(2).toString();
    d.outcome       = q.value(3).toString();
    d.reason        = q.value(4).toString();
    d.value         = fromJson(q.value(5).toString());
    d.derivedFile   = q.value(6).toString();
    d.tool          = q.value(7).toString();
    d.toolVersion   = q.value(8).toString();
    d.model         = q.value(9).toString();
    d.provider      = q.value(10).toString();
    d.providerClass = q.value(11).toString();
    d.endpointHost  = q.value(12).toString();
    d.params        = fromJson(q.value(13).toString());
    d.fileSize      = q.value(14).toLongLong();
    d.fileDate      = q.value(15).toString();
    d.cost          = q.value(16).toDouble();
    d.currency      = q.value(17).toString();
    d.date          = q.value(18).toString();
    return d;
}

const char *kDatumColumns =
    "datum_catalog_id, datum_file_path, datum_analysis, datum_outcome, datum_reason, "
    "datum_value, datum_derived_file, datum_tool, datum_tool_version, datum_model, "
    "datum_provider, datum_provider_class, datum_endpoint_host, datum_params, "
    "datum_file_size, datum_file_date, datum_cost, datum_currency, datum_date";

} // namespace

QString MdiStore::filePath(const QString &collectionFolder)
{
    return collectionFolder + QStringLiteral("/mdi_data.db");
}

bool MdiStore::open(const QString &connectionName, const QString &dbFile)
{
    QSqlDatabase db = QSqlDatabase::contains(connectionName)
                      ? QSqlDatabase::database(connectionName, false)
                      : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    if (db.databaseName() != dbFile) {
        if (db.isOpen())
            db.close();
        db.setDatabaseName(dbFile);
    }
    if (!db.isOpen() && !db.open()) {
        qWarning() << "MdiStore::open failed:" << dbFile << db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    // WAL lets the main thread read while the runner writes (MDI-C4)
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));

    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS mdi_schema (mdi_schema_version INTEGER NOT NULL)")))
        return false;
    if (!q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS mdi_datum (
            datum_catalog_id     INTEGER NOT NULL,
            datum_file_path      TEXT    NOT NULL,
            datum_analysis       TEXT    NOT NULL,
            datum_outcome        TEXT    NOT NULL,
            datum_reason         TEXT,
            datum_value          TEXT,
            datum_derived_file   TEXT,
            datum_tool           TEXT,
            datum_tool_version   TEXT,
            datum_model          TEXT,
            datum_provider       TEXT,
            datum_provider_class TEXT,
            datum_endpoint_host  TEXT,
            datum_params         TEXT,
            datum_file_size      INTEGER,
            datum_file_date      TEXT,
            datum_cost           REAL,
            datum_currency       TEXT,
            datum_date           TEXT,
            PRIMARY KEY (datum_catalog_id, datum_file_path, datum_analysis)
        )
    )"))) {
        qWarning() << "MdiStore::open: mdi_datum:" << q.lastError().text();
        return false;
    }
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS mdi_datum_catalog ON mdi_datum (datum_catalog_id)"));

    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM mdi_schema")) && q.next() && q.value(0).toInt() == 0) {
        QSqlQuery insert(db);
        insert.prepare(QStringLiteral("INSERT INTO mdi_schema (mdi_schema_version) VALUES (:v)"));
        insert.bindValue(":v", currentVersion);
        insert.exec();
    }
    return true;
}

void MdiStore::close(const QString &connectionName)
{
    {
        QSqlDatabase db = QSqlDatabase::database(connectionName, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

bool MdiStore::upsert(const QString &connectionName, const MdiDatum &d)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO mdi_datum (%1) VALUES "
                             "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)").arg(QLatin1String(kDatumColumns)));
    q.addBindValue(d.catalogId);
    q.addBindValue(d.filePath);
    q.addBindValue(d.analysis);
    q.addBindValue(d.outcome);
    q.addBindValue(d.reason);
    q.addBindValue(toJson(d.value));
    q.addBindValue(d.derivedFile);
    q.addBindValue(d.tool);
    q.addBindValue(d.toolVersion);
    q.addBindValue(d.model);
    q.addBindValue(d.provider);
    q.addBindValue(d.providerClass);
    q.addBindValue(d.endpointHost);
    q.addBindValue(toJson(d.params));
    q.addBindValue(d.fileSize);
    q.addBindValue(d.fileDate);
    q.addBindValue(d.cost);
    q.addBindValue(d.currency);
    q.addBindValue(d.date);
    if (!q.exec()) {
        qWarning() << "MdiStore::upsert failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool MdiStore::isCurrent(const QString &connectionName, int catalogId, const QString &filePath,
                         const QString &analysis, qint64 fileSize, const QString &fileDate,
                         const QJsonObject &params)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral(R"(
        SELECT datum_outcome, datum_file_size, datum_file_date, datum_params
        FROM mdi_datum
        WHERE datum_catalog_id = ? AND datum_file_path = ? AND datum_analysis = ?
    )"));
    q.addBindValue(catalogId);
    q.addBindValue(filePath);
    q.addBindValue(analysis);
    if (!q.exec() || !q.next())
        return false;
    return q.value(0).toString() == QLatin1String("completed")
        && q.value(1).toLongLong() == fileSize
        && q.value(2).toString() == fileDate
        && fromJson(q.value(3).toString()) == params;
}

QList<MdiDatum> MdiStore::forFile(const QString &connectionName, int catalogId, const QString &filePath)
{
    QList<MdiDatum> list;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("SELECT %1 FROM mdi_datum WHERE datum_catalog_id = ? AND datum_file_path = ? "
                             "ORDER BY datum_analysis").arg(QLatin1String(kDatumColumns)));
    q.addBindValue(catalogId);
    q.addBindValue(filePath);
    if (q.exec())
        while (q.next())
            list.append(readDatum(q));
    return list;
}

QHash<QString, QString> MdiStore::previewImages(const QString &connectionName, int catalogId)
{
    // A thumbnail wins over a poster frame; both are small JPEG files
    QHash<QString, QString> map;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral(R"(
        SELECT datum_file_path, datum_derived_file, datum_analysis
        FROM mdi_datum
        WHERE datum_catalog_id = ? AND datum_outcome = 'completed'
          AND datum_analysis IN ('thumbnail', 'poster') AND datum_derived_file <> ''
    )"));
    q.addBindValue(catalogId);
    if (q.exec())
        while (q.next())
            if (!map.contains(q.value(0).toString()) || q.value(2).toString() == QLatin1String("thumbnail"))
                map.insert(q.value(0).toString(), q.value(1).toString());
    return map;
}

QHash<QString, int> MdiStore::completedCounts(const QString &connectionName, int catalogId)
{
    QHash<QString, int> map;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral(R"(
        SELECT datum_file_path, COUNT(*) FROM mdi_datum
        WHERE datum_catalog_id = ? AND datum_outcome = 'completed'
        GROUP BY datum_file_path
    )"));
    q.addBindValue(catalogId);
    if (q.exec())
        while (q.next())
            map.insert(q.value(0).toString(), q.value(1).toInt());
    return map;
}
