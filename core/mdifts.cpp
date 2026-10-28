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
// File Name:   mdifts.cpp
// Purpose:     Instant search index (fork only, SpecMediaIntelligence.md, D5)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdifts.h"
#include "core/mdidata.h"
#include "core/search.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>

namespace {

constexpr int kBatch = 20000;   // rows per transaction while rebuilding

QString quoted(const QString &term)
{
    QString t = term;
    return QLatin1Char('"') + t.replace(QLatin1Char('"'), QLatin1String("\"\"")) + QLatin1Char('"');
}

QString joinTags(const QJsonValue &tags)
{
    QStringList list;
    for (const QJsonValue &t : tags.toArray())
        list << t.toString();
    return list.join(' ');
}

// One file's derived text from its data rows
QString composeText(const QHash<QString, QJsonObject> &byAnalysis)
{
    QStringList parts;
    if (byAnalysis.contains(QStringLiteral("snippet")))
        parts << byAnalysis.value(QStringLiteral("snippet")).value("text").toString();
    if (byAnalysis.contains(QStringLiteral("summary"))) {
        const QJsonObject s = byAnalysis.value(QStringLiteral("summary"));
        parts << s.value("summary").toString() << joinTags(s.value("tags"));
        for (const QJsonValue &e : s.value("excerpts").toArray())
            parts << e.toString();
    }
    if (byAnalysis.contains(QStringLiteral("caption"))) {
        const QJsonObject c = byAnalysis.value(QStringLiteral("caption"));
        parts << c.value("caption").toString() << joinTags(c.value("tags"));
    }
    parts.removeAll(QString());
    return parts.join(' ');
}

} // namespace

QString MdiFts::stamp(const QDateTime &catalogDateUpdated)
{
    return catalogDateUpdated.isValid() ? catalogDateUpdated.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")) : QString();
}

QString MdiFts::stamp(const QString &catalogDateUpdated)
{
    for (const QString &format : {QStringLiteral("yyyy-MM-dd hh:mm:ss"), QStringLiteral("yyyy/MM/dd hh:mm:ss")}) {
        const QDateTime d = QDateTime::fromString(catalogDateUpdated, format);
        if (d.isValid())
            return stamp(d);
    }
    const QDateTime iso = QDateTime::fromString(catalogDateUpdated, Qt::ISODateWithMs);
    return iso.isValid() ? stamp(iso) : catalogDateUpdated;
}

bool MdiFts::ensureTables(const QString &connectionName, const QString &schema)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    // Documents: one row per indexed file; its id is the FTS rowid, so a
    // file's entry is found through the unique key, never by scanning FTS.
    const bool ok =
        q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS %1.mdi_fts_doc ("
                              " doc_id INTEGER PRIMARY KEY,"
                              " doc_catalog_id INTEGER NOT NULL,"
                              " doc_full_path TEXT NOT NULL,"
                              " UNIQUE (doc_catalog_id, doc_full_path))").arg(schema))
        && q.exec(QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS %1.mdi_fts USING fts5("
                                 "name, folder, derived, tokenize = 'trigram')").arg(schema))
        && q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS %1.mdi_fts_state ("
                                 " state_catalog_id INTEGER PRIMARY KEY,"
                                 " state_stamp TEXT,"
                                 " state_file_count INTEGER,"
                                 " state_built_at TEXT)").arg(schema));
    if (!ok)
        qWarning() << "MdiFts::ensureTables failed:" << q.lastError().text();
    return ok;
}

bool MdiFts::attach(const QString &connectionName, const QString &mdiDbPath)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (db.driverName() != QLatin1String("QSQLITE") || mdiDbPath.isEmpty())
        return false;
    QSqlQuery list(db);
    if (list.exec(QStringLiteral("PRAGMA database_list")))
        while (list.next())
            if (list.value(1).toString() == QLatin1String("mdi"))
                return true;
    if (!QFileInfo::exists(mdiDbPath))
        return false;   // nothing indexed for this collection yet
    QSqlQuery q(db);
    q.prepare(QStringLiteral("ATTACH DATABASE :path AS mdi"));
    q.bindValue(":path", mdiDbPath);
    if (!q.exec()) {
        qWarning() << "MdiFts::attach failed:" << q.lastError().text();
        return false;
    }
    return ensureTables(connectionName, QStringLiteral("mdi"));
}

bool MdiFts::isCurrent(const QString &connectionName, const QString &schema, int catalogId, const QString &stamp)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("SELECT state_stamp FROM %1.mdi_fts_state WHERE state_catalog_id = :id").arg(schema));
    q.bindValue(":id", catalogId);
    return q.exec() && q.next() && !stamp.isEmpty() && q.value(0).toString() == stamp;
}

bool MdiFts::rebuildCatalog(const QString &mdiDbPath, int catalogId, const QString &stamp,
                            const QString &databaseMode, const QString &collectionDbPath,
                            const QString &catalogIdxPath, QString *error,
                            const std::atomic<bool> *stop)
{
    const QString mdiConn = QStringLiteral("mdi_fts_%1").arg(reinterpret_cast<quintptr>(&catalogId));
    const QString srcConn = mdiConn + QStringLiteral("_src");
    bool ok = false;
    {
        if (!MdiData::open(mdiConn, mdiDbPath) || !ensureTables(mdiConn)) {
            *error = QStringLiteral("mdi_data.db");
            MdiData::close(mdiConn);
            return false;
        }
        QSqlDatabase mdi = QSqlDatabase::database(mdiConn);

        // Derived text of the files that have some, read once
        QHash<QString, QHash<QString, QJsonObject>> derived;
        {
            QSqlQuery d(mdi);
            d.prepare(QLatin1String(R"(
                SELECT datum_full_path, datum_analysis, datum_value FROM mdi_datum
                WHERE datum_catalog_id = :id AND datum_outcome = 'completed'
                  AND datum_analysis IN ('snippet', 'summary', 'caption')
            )"));
            d.bindValue(":id", catalogId);
            if (d.exec())
                while (d.next())
                    derived[d.value(0).toString()][d.value(1).toString()] =
                        QJsonDocument::fromJson(d.value(2).toString().toUtf8()).object();
        }

        // Clear the catalog's previous entries
        mdi.transaction();
        QSqlQuery clear(mdi);
        clear.prepare(QStringLiteral("DELETE FROM mdi_fts WHERE rowid IN (SELECT doc_id FROM mdi_fts_doc WHERE doc_catalog_id = :id)"));
        clear.bindValue(":id", catalogId);
        clear.exec();
        clear.prepare(QStringLiteral("DELETE FROM mdi_fts_doc WHERE doc_catalog_id = :id"));
        clear.bindValue(":id", catalogId);
        clear.exec();
        clear.prepare(QStringLiteral("DELETE FROM mdi_fts_state WHERE state_catalog_id = :id"));
        clear.bindValue(":id", catalogId);
        clear.exec();
        mdi.commit();

        QSqlQuery insDoc(mdi);
        insDoc.prepare(QStringLiteral("INSERT INTO mdi_fts_doc (doc_catalog_id, doc_full_path) VALUES (:id, :path)"));
        QSqlQuery insFts(mdi);
        insFts.prepare(QStringLiteral("INSERT INTO mdi_fts (rowid, name, folder, derived) VALUES (:rowid, :name, :folder, :derived)"));

        int count = 0;
        bool stopped = false;
        auto add = [&](const QString &fullPath, const QString &name, const QString &folder) {
            insDoc.bindValue(":id", catalogId);
            insDoc.bindValue(":path", fullPath);
            if (!insDoc.exec())
                return;
            insFts.bindValue(":rowid", insDoc.lastInsertId());
            insFts.bindValue(":name", name);
            insFts.bindValue(":folder", folder);
            const auto it = derived.constFind(fullPath);
            insFts.bindValue(":derived", it == derived.constEnd() ? QString() : composeText(it.value()));
            insFts.exec();
            if (++count % kBatch == 0) {
                mdi.commit();
                mdi.transaction();
                stopped = stop && stop->load();
            }
        };

        mdi.transaction();
        if (databaseMode == QLatin1String("Memory")) {
            // Same layout as MdiEnrichmentJob::listCatalogFiles
            QFile idx(catalogIdxPath);
            if (idx.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&idx);
                while (!in.atEnd() && !stopped) {
                    const QString line = in.readLine();
                    if (line.isEmpty() || line.startsWith('<'))
                        continue;
                    const QString fullPath = line.section('\t', 0, 0);
                    const QFileInfo info(fullPath);
                    add(fullPath, info.fileName(), info.path());
                }
                ok = true;
            }
        } else {
            QSqlDatabase src = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), srcConn);
            src.setDatabaseName(collectionDbPath);
            src.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"));
            if (src.open()) {
                QSqlQuery files(src);
                files.setForwardOnly(true);
                files.prepare(QStringLiteral("SELECT file_full_path, file_name, file_folder_path FROM file WHERE file_catalog_id = :id"));
                files.bindValue(":id", catalogId);
                if (files.exec()) {
                    while (files.next() && !stopped)
                        add(files.value(0).toString(), files.value(1).toString(), files.value(2).toString());
                    ok = true;
                }
                src.close();
            }
        }

        if (ok && !stopped) {
            QSqlQuery state(mdi);
            state.prepare(QLatin1String(R"(
                INSERT OR REPLACE INTO mdi_fts_state (state_catalog_id, state_stamp, state_file_count, state_built_at)
                VALUES (:id, :stamp, :count, :at)
            )"));
            state.bindValue(":id", catalogId);
            state.bindValue(":stamp", stamp);
            state.bindValue(":count", count);
            state.bindValue(":at", QDateTime::currentDateTime().toString(Qt::ISODate));
            state.exec();
        }
        mdi.commit();
        if (stopped) {
            ok = false;
            *error = QStringLiteral("stopped");
        } else if (!ok) {
            *error = QStringLiteral("catalog files not readable");
        }
    }
    if (QSqlDatabase::contains(srcConn))
        QSqlDatabase::removeDatabase(srcConn);
    MdiData::close(mdiConn);
    return ok;
}

QList<QVariantMap> MdiFts::staleCatalogs(const QString &connectionName, const QString &mdiDbPath)
{
    QList<QVariantMap> list;
    const bool attached = attach(connectionName, mdiDbPath);
    QSqlQuery q(QSqlDatabase::database(connectionName));
    if (!q.exec(QLatin1String(R"(
            SELECT d.device_id, d.device_external_id, d.device_name, c.catalog_date_updated, c.catalog_file_path
            FROM device d JOIN catalog c ON c.catalog_id = d.device_external_id
            WHERE d.device_type = 'Catalog'
        )")))
        return list;
    while (q.next()) {
        const int catalogId = q.value(1).toInt();
        const QString s = stamp(q.value(3).toString());
        if (attached && isCurrent(connectionName, QStringLiteral("mdi"), catalogId, s))
            continue;
        QVariantMap c;
        c[QStringLiteral("deviceId")]  = q.value(0).toInt();
        c[QStringLiteral("catalogId")] = catalogId;
        c[QStringLiteral("name")]      = q.value(2).toString();
        c[QStringLiteral("stamp")]     = s;
        c[QStringLiteral("filePath")]  = q.value(4).toString();
        list.append(c);
    }
    return list;
}

QList<QVariantMap> MdiFts::catalogsNeedingPass(const QString &connectionName, const QString &mdiDbPath,
                                               const QString &pass)
{
    QList<QVariantMap> list;
    const bool attached = attach(connectionName, mdiDbPath);
    QSqlQuery q(QSqlDatabase::database(connectionName));
    if (!q.exec(QLatin1String(R"(
            SELECT d.device_id, d.device_external_id, d.device_name, c.catalog_date_updated, d.device_path
            FROM device d JOIN catalog c ON c.catalog_id = d.device_external_id
            WHERE d.device_type = 'Catalog'
        )")))
        return list;
    QSqlQuery state(QSqlDatabase::database(connectionName));
    if (attached)
        state.prepare(QStringLiteral("SELECT pass_stamp FROM mdi.mdi_pass WHERE pass_catalog_id = :id AND pass_name = :name"));
    while (q.next()) {
        const int catalogId = q.value(1).toInt();
        const QString s = stamp(q.value(3).toString());
        if (attached) {
            state.bindValue(":id", catalogId);
            state.bindValue(":name", pass);
            if (state.exec() && state.next() && state.value(0).toString() == s)
                continue;
        }
        QVariantMap c;
        c[QStringLiteral("deviceId")]   = q.value(0).toInt();
        c[QStringLiteral("catalogId")]  = catalogId;
        c[QStringLiteral("name")]       = q.value(2).toString();
        c[QStringLiteral("stamp")]      = s;
        c[QStringLiteral("sourcePath")] = q.value(4).toString();
        list.append(c);
    }
    return list;
}

QString MdiFts::derivedText(const QString &mdiConnectionName, int catalogId, const QString &fullPath)
{
    QHash<QString, QJsonObject> byAnalysis;
    for (const MdiData::Datum &d : MdiData::forFile(mdiConnectionName, catalogId, fullPath))
        if (d.outcome == QLatin1String("completed"))
            byAnalysis[d.analysis] = d.value;
    return composeText(byAnalysis);
}

void MdiFts::updateDerivedText(const QString &mdiConnectionName, int catalogId, const QString &fullPath)
{
    QSqlDatabase db = QSqlDatabase::database(mdiConnectionName);
    QSqlQuery find(db);
    find.prepare(QStringLiteral("SELECT doc_id FROM mdi_fts_doc WHERE doc_catalog_id = :id AND doc_full_path = :path"));
    find.bindValue(":id", catalogId);
    find.bindValue(":path", fullPath);
    if (!find.exec() || !find.next())
        return;
    QSqlQuery update(db);
    update.prepare(QStringLiteral("UPDATE mdi_fts SET derived = :derived WHERE rowid = :rowid"));
    update.bindValue(":derived", derivedText(mdiConnectionName, catalogId, fullPath));
    update.bindValue(":rowid", find.value(0));
    if (!update.exec())
        qWarning() << "MdiFts::updateDerivedText failed:" << update.lastError().text();
}

QString MdiFts::matchQuery(const QString &searchText, const QString &criteria, const QString &searchIn)
{
    if (criteria == Search::TEXT_CRITERIA_REGEX)
        return QString();
    const bool namesOnly = searchIn == Search::SEARCH_IN_FILE_NAMES;
    // Name and folder are indexed apart (the full path would store every name
    // twice). A term without '/' lies wholly in one of them, so matching either
    // is a superset of matching the full path; a term with '/' may straddle
    // the separator, so those searches run without the prefilter.
    if (!namesOnly && searchText.contains(QLatin1Char('/')))
        return QString();

    // Mirrors Search::prepareSearchPatterns: each line is an OR term
    QStringList lineExpressions;
    for (const QString &rawLine : searchText.split('\n', Qt::SkipEmptyParts)) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        if (criteria == Search::TEXT_CRITERIA_EXACT_PHRASE || criteria == Search::TEXT_CRITERIA_BEGINS_WITH) {
            if (line.size() < 3)
                return QString();
            lineExpressions << quoted(line);   // substring; "begins with" is checked by the regex
            continue;
        }
        QStringList words;
        for (const QString &w : line.split(' ', Qt::SkipEmptyParts)) {
            if (w.size() < 3)
                return QString();
            words << quoted(w);
        }
        if (words.isEmpty())
            continue;
        const QString op = criteria == Search::TEXT_CRITERIA_ANY_WORD ? QStringLiteral(" OR ") : QStringLiteral(" AND ");
        lineExpressions << QLatin1Char('(') + words.join(op) + QLatin1Char(')');
    }
    if (lineExpressions.isEmpty())
        return QString();
    const QString columns = namesOnly ? QStringLiteral("name")
                          : searchIn == Search::SEARCH_IN_FOLDER_PATH ? QStringLiteral("folder")
                                                                      : QStringLiteral("{name folder}");
    return QStringLiteral("%1 : (%2)").arg(columns, lineExpressions.join(QStringLiteral(" OR ")));
}
