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
// File Name:   cataloghistory.cpp
// Purpose:     Restore points and history of catalog changes (fork only)
// Description: See cataloghistory.h
/////////////////////////////////////////////////////////////////////////////
*/

#include "cataloghistory.h"
#include "collection.h"
#include "device.h"
#include "catalog.h"
#include "mdidata.h"
#include "mdiprofile.h"
#include "mdiarchivecopy.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QMutex>
#include <QAtomicInt>
#include <QDebug>

// One table of the collection kept in a restore point, and the scope list its
// rows are selected by
struct HistoryTable {
    const char *table;
    const char *key;
    enum Kind { DeviceKey, CatalogKey, StorageKey } kind;
    const char *skip;   // generated column, left to the database on insert
};

namespace {

const HistoryTable kTables[] = {
    { "device",         "device_id",         HistoryTable::DeviceKey,  nullptr     },
    { "catalog",        "catalog_id",        HistoryTable::CatalogKey, nullptr     },
    { "file",           "file_catalog_id",   HistoryTable::CatalogKey, nullptr     },
    { "folder",         "folder_catalog_id", HistoryTable::CatalogKey, nullptr     },
    { "catalog_filter", "filter_catalog_id", HistoryTable::CatalogKey, "filter_id" },
    { "mdi_profile",    "profile_device_id", HistoryTable::DeviceKey,  nullptr     },
    { "storage",        "storage_id",        HistoryTable::StorageKey, nullptr     },
};

// Name of the history file on the collection connection while rows are copied
const QLatin1String kAttachName("teca_hist");

// Lines of the history list kept once their rows are pruned (the newest)
constexpr int kMaxEntryRows = 1000;

QAtomicInt s_instanceCounter;

const HistoryTable &tableNamed(const char *name)
{
    for (const HistoryTable &t : kTables)
        if (qstrcmp(t.table, name) == 0)
            return t;
    return kTables[0];
}

QString idList(const QList<int> &ids)
{
    QStringList parts;
    parts.reserve(ids.size());
    for (int id : ids)
        parts << QString::number(id);
    return parts.join(QLatin1Char(','));
}

void appendUnique(QList<int> &list, int id)
{
    if (!list.contains(id))
        list.append(id);
}

QJsonArray toArray(const QList<int> &ids)
{
    QJsonArray array;
    for (int id : ids)
        array.append(id);
    return array;
}

QList<int> fromArray(const QJsonArray &array)
{
    QList<int> ids;
    for (const QJsonValue &v : array)
        ids.append(v.toInt());
    return ids;
}

QList<int> keysFor(const HistoryTable &t, const CatalogHistory::Scope &scope)
{
    switch (t.kind) {
    case HistoryTable::DeviceKey:  return scope.deviceIds;
    case HistoryTable::CatalogKey: return scope.catalogIds;
    case HistoryTable::StorageKey: return scope.storageIds;
    }
    return {};
}

QJsonObject factToJson(const CatalogHistory::CatalogFact &f)
{
    return QJsonObject{ { "deviceId",  f.deviceId },  { "catalogId", f.catalogId },
                        { "name",      f.name },      { "path",      f.path },
                        { "files",     double(f.files) }, { "size",  double(f.size) },
                        { "signature", f.signature } };
}

CatalogHistory::CatalogFact factFromJson(const QJsonObject &o)
{
    CatalogHistory::CatalogFact f;
    f.deviceId  = o.value("deviceId").toInt();
    f.catalogId = o.value("catalogId").toInt();
    f.name      = o.value("name").toString();
    f.path      = o.value("path").toString();
    f.files     = qint64(o.value("files").toDouble());
    f.size      = qint64(o.value("size").toDouble());
    f.signature = o.value("signature").toString();
    return f;
}

QString foldersFilePathOf(const QString &catalogFilePath)
{
    QString path = catalogFilePath;
    const int pos = path.lastIndexOf(QLatin1String(".idx"));
    if (pos < 0)
        return QString();
    path.replace(pos, 4, QLatin1String(".folders.idx"));
    return path;
}

} // namespace

//----------------------------------------------------------------------
// Scope
//----------------------------------------------------------------------
CatalogHistory::Scope CatalogHistory::Scope::united(const Scope &other) const
{
    Scope s = *this;
    for (int id : other.deviceIds)  appendUnique(s.deviceIds, id);
    for (int id : other.catalogIds) appendUnique(s.catalogIds, id);
    for (int id : other.storageIds) appendUnique(s.storageIds, id);
    return s;
}

QJsonObject CatalogHistory::Scope::toJson() const
{
    return QJsonObject{ { "devices",  toArray(deviceIds) },
                        { "catalogs", toArray(catalogIds) },
                        { "storages", toArray(storageIds) } };
}

CatalogHistory::Scope CatalogHistory::Scope::fromJson(const QJsonObject &json)
{
    Scope s;
    s.deviceIds  = fromArray(json.value("devices").toArray());
    s.catalogIds = fromArray(json.value("catalogs").toArray());
    s.storageIds = fromArray(json.value("storages").toArray());
    return s;
}

//----------------------------------------------------------------------
// Opening
//----------------------------------------------------------------------
CatalogHistory::CatalogHistory(Collection *collection)
    : m_collection(collection)
{
    m_mainConnection = collection->connectionName();
    m_databaseMode   = collection->databaseMode;
    m_folder         = collection->folder;
    // Own connection per instance: two instances never close each other's
    m_histConnection = QStringLiteral("teca_history_%1_%2")
                           .arg(m_mainConnection).arg(s_instanceCounter.fetchAndAddRelaxed(1));
    m_enabled        = isAvailable(m_databaseMode, m_folder);
}

CatalogHistory::~CatalogHistory()
{
    detach();
    if (QSqlDatabase::contains(m_histConnection)) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_histConnection, false);
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(m_histConnection);
    }
}

QString CatalogHistory::filePath(const QString &collectionFolder)
{
    return collectionFolder + QStringLiteral("/teca_history.db");
}

bool CatalogHistory::isAvailable(const QString &databaseMode, const QString &collectionFolder)
{
    // Hosted: the collection folder is not the collection's (it can be the one
    // of the collection opened before), so nothing is written there
    // A published copy (fork, D43): teca_history.db is not published, and
    // nothing is created in its folder (MDI-C64)
    return databaseMode != QLatin1String("Hosted")
        && !collectionFolder.isEmpty()
        && QDir(collectionFolder).exists()
        && !MdiPublish::isPublishedFolder(collectionFolder);
}

bool CatalogHistory::ensureOpen(bool create) const
{
    if (!m_enabled)
        return false;
    if (m_open)
        return true;

    const QString path = filePath(m_folder);
    const bool exists = QFileInfo::exists(path);
    if (!exists && !create)
        return false;

    QSqlDatabase db = QSqlDatabase::contains(m_histConnection)
                      ? QSqlDatabase::database(m_histConnection, false)
                      : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_histConnection);
    if (!db.isOpen()) {
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
        if (!db.open()) {
            qWarning() << "CatalogHistory: cannot open" << path << db.lastError().text();
            return false;
        }
    }

    QSqlQuery q(db);
    // Pruned rows give their pages back (set before the first table only)
    if (!exists)
        q.exec(QStringLiteral("PRAGMA auto_vacuum = INCREMENTAL"));
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS history_schema (history_schema_version INTEGER NOT NULL)"));
    q.exec(QStringLiteral("INSERT INTO history_schema (history_schema_version) "
                          "SELECT 1 WHERE NOT EXISTS (SELECT 1 FROM history_schema)"));
    const bool ok = q.exec(QLatin1String(R"(
        CREATE TABLE IF NOT EXISTS history_entry (
            entry_id       INTEGER PRIMARY KEY AUTOINCREMENT,
            entry_date     TEXT    NOT NULL,
            entry_action   TEXT    NOT NULL,
            entry_state    TEXT    NOT NULL,
            entry_before   TEXT,
            entry_after    TEXT,
            entry_facts    TEXT,
            entry_extra    TEXT,
            entry_snapshot INTEGER NOT NULL DEFAULT 0
        )
    )"));
    if (!ok) {
        qWarning() << "CatalogHistory: table creation failed:" << q.lastError().text();
        return false;
    }
    m_open = true;
    return true;
}

bool CatalogHistory::mainIsSqlite() const
{
    return QSqlDatabase::database(m_mainConnection).driverName() == QLatin1String("QSQLITE");
}

bool CatalogHistory::attach()
{
    // The history file on the collection connection: a catalog's rows are then
    // copied by one INSERT ... SELECT instead of row by row
    if (m_attached)
        return true;
    if (!mainIsSqlite() || !ensureOpen(true))
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_mainConnection);
    // Not inside a caller's transaction: the history file would stay locked
    // until that commits, and could not be detached. Probed by opening one.
    if (!db.transaction())
        return false;
    db.rollback();
    {
        QSqlQuery list(db);
        if (list.exec(QStringLiteral("PRAGMA database_list")))
            while (list.next())
                if (list.value(1).toString() == kAttachName)
                    m_attached = true;   // left by an interrupted call
    }
    if (m_attached)
        return true;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("ATTACH DATABASE :path AS %1").arg(kAttachName));
    q.bindValue(":path", filePath(m_folder));
    m_attached = q.exec();
    if (!m_attached)
        qWarning() << "CatalogHistory: attach failed, rows copied one by one:" << q.lastError().text();
    return m_attached;
}

void CatalogHistory::detach()
{
    if (!m_attached)
        return;
    QSqlQuery q(QSqlDatabase::database(m_mainConnection));
    if (!q.exec(QStringLiteral("DETACH DATABASE %1").arg(kAttachName)))
        qWarning() << "CatalogHistory: detach failed:" << q.lastError().text();
    m_attached = false;
}

//----------------------------------------------------------------------
// Collection rows
//----------------------------------------------------------------------
QList<int> CatalogHistory::catalogDeviceRows(int catalogId) const
{
    // The row in the Physical group first, then its Virtual copies
    QList<int> ids;
    QSqlQuery q(QSqlDatabase::database(m_mainConnection));
    q.prepare(QLatin1String(R"(
        SELECT d.device_id
        FROM device d
        LEFT JOIN device p ON p.device_id = d.device_parent_id
        WHERE d.device_type = 'Catalog' AND d.device_external_id = :id
        ORDER BY CASE WHEN p.device_type = 'Virtual' THEN 1 ELSE 0 END, d.device_id
    )"));
    q.bindValue(":id", catalogId);
    if (q.exec())
        while (q.next())
            ids.append(q.value(0).toInt());
    return ids;
}

bool CatalogHistory::rowExists(const QString &table, const QString &key, int id) const
{
    QSqlQuery q(QSqlDatabase::database(m_mainConnection));
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE %2 = :id").arg(table, key));
    q.bindValue(":id", id);
    return q.exec() && q.next() && q.value(0).toInt() > 0;
}

QList<int> CatalogHistory::parentsOf(const QList<int> &deviceIds) const
{
    QList<int> parents;
    if (deviceIds.isEmpty())
        return parents;
    QSqlQuery q(QSqlDatabase::database(m_mainConnection));
    if (q.exec(QStringLiteral("SELECT DISTINCT device_parent_id FROM device WHERE device_id IN (%1)")
                   .arg(idList(deviceIds))))
        while (q.next())
            if (q.value(0).toInt() > 0)
                appendUnique(parents, q.value(0).toInt());
    return parents;
}

void CatalogHistory::refreshParents(const QList<int> &parentIds) const
{
    // Totals of the storages and groups a catalog left or joined
    for (int parentId : parentIds) {
        if (!rowExists(QStringLiteral("device"), QStringLiteral("device_id"), parentId))
            continue;
        Device parent;
        parent.ID = parentId;
        parent.loadDevice(m_mainConnection);
        parent.updateNumbersFromChildren();
        parent.updateParentsNumbers();
    }
}

CatalogHistory::Scope CatalogHistory::scopeOf(const QList<int> &deviceIds, bool withDescendants) const
{
    Scope scope;
    QSqlDatabase db = QSqlDatabase::database(m_mainConnection);
    auto addCatalog = [&](int catalogId) {
        if (catalogId <= 0 || scope.catalogIds.contains(catalogId))
            return;
        scope.catalogIds.append(catalogId);
        for (int id : catalogDeviceRows(catalogId))
            appendUnique(scope.deviceIds, id);
    };
    auto addRow = [&](int id, const QString &type, int externalId) {
        if (type == QLatin1String("Catalog")) {
            appendUnique(scope.deviceIds, id);
            addCatalog(externalId);
            return;
        }
        appendUnique(scope.deviceIds, id);
        if (type == QLatin1String("Storage") && externalId > 0)
            appendUnique(scope.storageIds, externalId);
    };

    for (int deviceId : deviceIds) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT device_type, device_external_id FROM device WHERE device_id = :id"));
        q.bindValue(":id", deviceId);
        if (!q.exec() || !q.next())
            continue;
        const QString type = q.value(0).toString();
        addRow(deviceId, type, q.value(1).toInt());
        if (!withDescendants || type == QLatin1String("Catalog"))
            continue;

        QSqlQuery sub(db);
        sub.prepare(QLatin1String(R"(
            WITH RECURSIVE hierarchy AS (
                SELECT device_id FROM device WHERE device_parent_id = :id
                UNION ALL
                SELECT t.device_id FROM device t JOIN hierarchy h ON t.device_parent_id = h.device_id
            )
            SELECT d.device_id, d.device_type, d.device_external_id
            FROM device d
            WHERE d.device_id IN (SELECT device_id FROM hierarchy)
        )"));
        sub.bindValue(":id", deviceId);
        if (sub.exec())
            while (sub.next())
                addRow(sub.value(0).toInt(), sub.value(1).toString(), sub.value(2).toInt());
    }
    return scope;
}

QList<CatalogHistory::CatalogFact> CatalogHistory::catalogFacts(const QList<int> &catalogIds) const
{
    QList<CatalogFact> facts;
    QSqlDatabase db = QSqlDatabase::database(m_mainConnection);
    for (int catalogId : catalogIds) {
        CatalogFact f;
        f.catalogId = catalogId;
        QSqlQuery q(db);
        q.prepare(QLatin1String(R"(
            SELECT d.device_id, d.device_name, d.device_path,
                   d.device_total_file_count, d.device_total_file_size
            FROM device d
            LEFT JOIN device p ON p.device_id = d.device_parent_id
            WHERE d.device_type = 'Catalog' AND d.device_external_id = :id
            ORDER BY CASE WHEN p.device_type = 'Virtual' THEN 1 ELSE 0 END, d.device_id
        )"));
        q.bindValue(":id", catalogId);
        if (q.exec() && q.next()) {
            f.deviceId = q.value(0).toInt();
            f.name     = q.value(1).toString();
            f.path     = q.value(2).toString();
            f.files    = q.value(3).toLongLong();
            f.size     = q.value(4).toLongLong();
        }
        // Cheap fingerprint of the file list: an update that found nothing new
        // leaves it as it was
        QSqlQuery s(db);
        s.prepare(QLatin1String(R"(
            SELECT COUNT(*), SUM(file_size), SUM(LENGTH(file_full_path)),
                   MIN(file_full_path), MAX(file_full_path), MAX(file_date_updated)
            FROM file WHERE file_catalog_id = :id
        )"));
        s.bindValue(":id", catalogId);
        if (s.exec() && s.next()) {
            QStringList parts;
            for (int i = 0; i < 6; ++i)
                parts << s.value(i).toString();
            parts << QString::number(f.files) << QString::number(f.size) << f.name << f.path;
            f.signature = parts.join(QLatin1Char('|'));
        }
        facts.append(f);
    }
    return facts;
}

void CatalogHistory::loadMemoryCatalogs(const QList<int> &catalogIds) const
{
    // Memory mode: a catalog's files and folders are in the tables only once
    // loaded from its .idx files
    if (m_databaseMode != QLatin1String("Memory"))
        return;
    for (int catalogId : catalogIds) {
        const QList<int> rows = catalogDeviceRows(catalogId);
        if (rows.isEmpty())
            continue;
        Device device;
        device.ID = rows.first();
        device.loadDevice(m_mainConnection);
        device.catalog->loadFoldersToTable();
        QMutex mutex;
        bool stop = false;
        device.catalog->loadCatalogFileListToTable(mutex, stop);
    }
}

void CatalogHistory::writeMemoryCatalogs(const QList<int> &catalogIds, const QStringList &removedFiles) const
{
    // Memory mode: the .idx files and the collection files are the collection
    if (m_databaseMode != QLatin1String("Memory"))
        return;
    QStringList written;
    bool allWritten = true;
    for (int catalogId : catalogIds) {
        const QList<int> rows = catalogDeviceRows(catalogId);
        if (rows.isEmpty())
            continue;
        Device device;
        device.ID = rows.first();
        device.loadDevice(m_mainConnection);
        allWritten = device.catalog->saveCatalogToFile(m_databaseMode, m_folder) && allWritten;
        allWritten = device.catalog->saveFoldersToFile(m_databaseMode, m_folder) && allWritten;
        written << device.catalog->filePath;
        QSqlQuery q(QSqlDatabase::database(m_mainConnection));
        q.prepare(QStringLiteral("UPDATE catalog SET catalog_file_path = :path WHERE catalog_id = :id"));
        q.bindValue(":path", device.catalog->filePath);
        q.bindValue(":id",   catalogId);
        q.exec();
    }
    // The files of the catalogs that are gone, as a split removes the original's;
    // kept when a restored catalog could not be written (disk full...)
    for (const QString &path : allWritten ? removedFiles : QStringList()) {
        if (path.isEmpty() || written.contains(path))
            continue;
        QFile::remove(path);
        const QString folders = foldersFilePathOf(path);
        if (!folders.isEmpty())
            QFile::remove(folders);
    }
    m_collection->saveDeviceTableToFile();
    m_collection->saveStorageTableToFile();
    m_collection->saveCatalogFilterTableToFile();
    MdiProfile::saveTableToFile(m_mainConnection, m_folder);
}

//----------------------------------------------------------------------
// Copying rows
//----------------------------------------------------------------------
QStringList CatalogHistory::mainColumns(const HistoryTable &t) const
{
    QStringList columns;
    const QSqlRecord record = QSqlDatabase::database(m_mainConnection).record(QLatin1String(t.table));
    for (int i = 0; i < record.count(); ++i) {
        const QString name = record.fieldName(i);
        if (t.skip && name == QLatin1String(t.skip))
            continue;
        columns << name;
    }
    return columns;
}

bool CatalogHistory::ensureSnapTable(const HistoryTable &t, const QStringList &columns) const
{
    // Same columns as the collection table, without types: values are kept as
    // they were. Columns added to the collection later are added here too.
    QSqlDatabase db = QSqlDatabase::database(m_histConnection);
    const QString snap = QStringLiteral("snap_") + QLatin1String(t.table);
    const QSqlRecord existing = db.record(snap);
    QSqlQuery q(db);
    if (existing.isEmpty()) {
        const QString sql = QStringLiteral("CREATE TABLE IF NOT EXISTS %1 (snap_entry_id INTEGER NOT NULL, %2)")
                                .arg(snap, columns.join(QStringLiteral(", ")));
        if (!q.exec(sql)) {
            qWarning() << "CatalogHistory: cannot create" << snap << q.lastError().text();
            return false;
        }
        q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS %1_entry ON %1 (snap_entry_id)").arg(snap));
        return true;
    }
    for (const QString &column : columns)
        if (existing.indexOf(column) < 0
            && !q.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(snap, column))) {
            qWarning() << "CatalogHistory: cannot add" << column << "to" << snap << q.lastError().text();
            return false;
        }
    return true;
}

bool CatalogHistory::copyToSnapshot(int entryId, const HistoryTable &t, const QList<int> &keys)
{
    if (keys.isEmpty())
        return true;
    const QStringList columns = mainColumns(t);
    if (columns.isEmpty())
        return true;   // table absent from this collection (no AI schema yet)
    if (!ensureSnapTable(t, columns))
        return false;

    const QString table   = QLatin1String(t.table);
    const QString colList = columns.join(QStringLiteral(", "));
    const QString where   = QStringLiteral("%1 IN (%2)").arg(QLatin1String(t.key), idList(keys));
    QSqlDatabase main = QSqlDatabase::database(m_mainConnection);

    if (m_attached) {
        QSqlQuery q(main);
        const QString sql = QStringLiteral("INSERT INTO %1.snap_%2 (snap_entry_id, %3) SELECT %4, %3 FROM %2 WHERE %5")
                                .arg(kAttachName, table, colList, QString::number(entryId), where);
        if (!q.exec(sql)) {
            qWarning() << "CatalogHistory: copy of" << table << "failed:" << q.lastError().text();
            return false;
        }
        return true;
    }

    // Row by row: another database engine, or the file could not be attached
    QSqlQuery src(main);
    src.setForwardOnly(true);
    if (!src.exec(QStringLiteral("SELECT %1 FROM %2 WHERE %3").arg(colList, table, where))) {
        qWarning() << "CatalogHistory: read of" << table << "failed:" << src.lastError().text();
        return false;
    }
    QSqlDatabase hist = QSqlDatabase::database(m_histConnection);
    QStringList marks;
    for (int i = 0; i <= columns.size(); ++i)
        marks << QStringLiteral("?");
    hist.transaction();
    bool ok = true;
    {
        QSqlQuery ins(hist);
        ins.prepare(QStringLiteral("INSERT INTO snap_%1 (snap_entry_id, %2) VALUES (%3)")
                        .arg(table, colList, marks.join(QStringLiteral(", "))));
        while (ok && src.next()) {
            ins.bindValue(0, entryId);
            for (int i = 0; i < columns.size(); ++i)
                ins.bindValue(i + 1, src.value(i));
            ok = ins.exec();
            if (!ok)
                qWarning() << "CatalogHistory: copy of" << table << "failed:" << ins.lastError().text();
        }
    }
    if (!ok) {
        hist.rollback();
        return false;
    }
    return hist.commit();
}

bool CatalogHistory::copyFromSnapshot(int entryId, const HistoryTable &t, const QString &snapFilter,
                                      const QHash<QString, QVariant> &overrides)
{
    const QString table = QLatin1String(t.table);
    const QStringList mainCols = mainColumns(t);
    const QSqlRecord snapRecord = QSqlDatabase::database(m_histConnection).record(QStringLiteral("snap_") + table);
    if (mainCols.isEmpty() || snapRecord.isEmpty())
        return true;   // nothing kept for that table

    // The columns both have; a column the collection gained since keeps its default
    QStringList columns;
    for (const QString &c : mainCols)
        if (snapRecord.indexOf(c) >= 0 || overrides.contains(c))
            columns << c;
    const QString where = QStringLiteral("snap_entry_id = %1%2")
                              .arg(entryId)
                              .arg(snapFilter.isEmpty() ? QString() : QStringLiteral(" AND (%1)").arg(snapFilter));
    QSqlDatabase main = QSqlDatabase::database(m_mainConnection);

    if (m_attached) {
        QStringList select;
        QList<QVariant> values;
        for (const QString &c : columns) {
            if (overrides.contains(c)) {
                select << QStringLiteral(":o%1").arg(values.size());
                values << overrides.value(c);
            } else {
                select << c;
            }
        }
        QSqlQuery q(main);
        q.prepare(QStringLiteral("INSERT INTO %1 (%2) SELECT %3 FROM %4.snap_%1 WHERE %5")
                      .arg(table, columns.join(QStringLiteral(", ")), select.join(QStringLiteral(", ")),
                           kAttachName, where));
        for (int i = 0; i < values.size(); ++i)
            q.bindValue(QStringLiteral(":o%1").arg(i), values.at(i));
        if (!q.exec()) {
            qWarning() << "CatalogHistory: restore of" << table << "failed:" << q.lastError().text();
            return false;
        }
        return true;
    }

    QStringList read;
    for (const QString &c : columns)
        if (!overrides.contains(c))
            read << c;
    QSqlQuery src(QSqlDatabase::database(m_histConnection));
    src.setForwardOnly(true);
    if (!src.exec(QStringLiteral("SELECT %1 FROM snap_%2 WHERE %3")
                      .arg(read.isEmpty() ? QStringLiteral("snap_entry_id") : read.join(QStringLiteral(", ")),
                           table, where))) {
        qWarning() << "CatalogHistory: read of snap_" << table << "failed:" << src.lastError().text();
        return false;
    }
    QStringList marks;
    for (int i = 0; i < columns.size(); ++i)
        marks << QStringLiteral("?");
    QSqlQuery ins(main);
    ins.prepare(QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)")
                    .arg(table, columns.join(QStringLiteral(", ")), marks.join(QStringLiteral(", "))));
    while (src.next()) {
        int readIndex = 0;
        for (int i = 0; i < columns.size(); ++i) {
            const QString &c = columns.at(i);
            ins.bindValue(i, overrides.contains(c) ? overrides.value(c) : src.value(readIndex++));
        }
        if (!ins.exec()) {
            qWarning() << "CatalogHistory: restore of" << table << "failed:" << ins.lastError().text();
            return false;
        }
    }
    return true;
}

bool CatalogHistory::deleteMainRows(const HistoryTable &t, const QList<int> &keys)
{
    if (keys.isEmpty() || mainColumns(t).isEmpty())
        return true;
    QSqlQuery q(QSqlDatabase::database(m_mainConnection));
    if (!q.exec(QStringLiteral("DELETE FROM %1 WHERE %2 IN (%3)")
                    .arg(QLatin1String(t.table), QLatin1String(t.key), idList(keys)))) {
        qWarning() << "CatalogHistory: delete from" << t.table << "failed:" << q.lastError().text();
        return false;
    }
    return true;
}

void CatalogHistory::deleteSnapshotRows(int entryId, const QString &table, const QString &key,
                                        const QList<int> &keys) const
{
    QSqlDatabase db = QSqlDatabase::database(m_histConnection);
    for (const HistoryTable &t : kTables) {
        if (!table.isEmpty() && table != QLatin1String(t.table))
            continue;
        const QString snap = QStringLiteral("snap_") + QLatin1String(t.table);
        if (db.record(snap).isEmpty())
            continue;
        QSqlQuery q(db);
        QString sql = QStringLiteral("DELETE FROM %1 WHERE snap_entry_id = %2").arg(snap).arg(entryId);
        if (!key.isEmpty())
            sql += QStringLiteral(" AND %1 IN (%2)").arg(key, idList(keys));
        q.exec(sql);
    }
}

QHash<QString, int> CatalogHistory::snapshotPaths(int entryId) const
{
    QHash<QString, int> paths;
    QSqlDatabase db = QSqlDatabase::database(m_histConnection);
    if (db.record(QStringLiteral("snap_file")).isEmpty())
        return paths;
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (q.exec(QStringLiteral("SELECT file_full_path, file_catalog_id FROM snap_file WHERE snap_entry_id = %1").arg(entryId)))
        while (q.next())
            paths.insert(q.value(0).toString(), q.value(1).toInt());
    return paths;
}

int CatalogHistory::moveAiData(int fromCatalogId, const QHash<QString, int> &byPath) const
{
    // AI results are keyed by catalog and full path (mdi_data.db): they follow
    // the files to the catalog that holds them again. -1 when they could not.
    const QString mdiPath = MdiData::filePath(m_folder);
    if (byPath.isEmpty() || !QFileInfo::exists(mdiPath))
        return 0;
    const QString connection = m_histConnection + QStringLiteral("_mdi");
    int moved = -1;
    if (MdiData::open(connection, mdiPath)) {
        moved = MdiData::moveToCatalogs(connection, fromCatalogId, byPath);
        if (moved < 0)   // busy past the timeout: once more
            moved = MdiData::moveToCatalogs(connection, fromCatalogId, byPath);
    }
    MdiData::close(connection);
    return moved;
}

//----------------------------------------------------------------------
// Entries
//----------------------------------------------------------------------
bool CatalogHistory::writeEntry(Entry &e) const
{
    QJsonArray beforeFacts, afterFacts;
    for (const CatalogFact &f : e.beforeCatalogs) beforeFacts.append(factToJson(f));
    for (const CatalogFact &f : e.afterCatalogs)  afterFacts.append(factToJson(f));
    const QString facts = QString::fromUtf8(QJsonDocument(QJsonObject{ { "before", beforeFacts },
                                                                       { "after",  afterFacts } })
                                                .toJson(QJsonDocument::Compact));
    auto compact = [](const QJsonObject &o) {
        return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    };

    QSqlQuery q(QSqlDatabase::database(m_histConnection));
    if (e.id == 0) {
        q.prepare(QLatin1String(R"(
            INSERT INTO history_entry (entry_date, entry_action, entry_state, entry_before,
                                       entry_after, entry_facts, entry_extra, entry_snapshot)
            VALUES (:date, :action, :state, :before, :after, :facts, :extra, :snapshot)
        )"));
    } else {
        q.prepare(QLatin1String(R"(
            UPDATE history_entry
            SET entry_date = :date, entry_action = :action, entry_state = :state,
                entry_before = :before, entry_after = :after, entry_facts = :facts,
                entry_extra = :extra, entry_snapshot = :snapshot
            WHERE entry_id = :id
        )"));
        q.bindValue(":id", e.id);
    }
    q.bindValue(":date",     e.date);
    q.bindValue(":action",   e.action);
    q.bindValue(":state",    e.state);
    q.bindValue(":before",   compact(e.before.toJson()));
    q.bindValue(":after",    compact(e.after.toJson()));
    q.bindValue(":facts",    facts);
    q.bindValue(":extra",    compact(e.extra));
    q.bindValue(":snapshot", e.hasSnapshot ? 1 : 0);
    if (!q.exec()) {
        qWarning() << "CatalogHistory: cannot write entry:" << q.lastError().text();
        return false;
    }
    if (e.id == 0)
        e.id = q.lastInsertId().toInt();
    return e.id > 0;
}

CatalogHistory::Entry CatalogHistory::readEntry(const QSqlQuery &q) const
{
    Entry e;
    e.id          = q.value("entry_id").toInt();
    e.date        = q.value("entry_date").toString();
    e.action      = q.value("entry_action").toString();
    e.state       = q.value("entry_state").toString();
    e.before      = Scope::fromJson(QJsonDocument::fromJson(q.value("entry_before").toString().toUtf8()).object());
    e.after       = Scope::fromJson(QJsonDocument::fromJson(q.value("entry_after").toString().toUtf8()).object());
    e.extra       = QJsonDocument::fromJson(q.value("entry_extra").toString().toUtf8()).object();
    e.hasSnapshot = q.value("entry_snapshot").toInt() != 0;
    const QJsonObject facts = QJsonDocument::fromJson(q.value("entry_facts").toString().toUtf8()).object();
    for (const QJsonValue &v : facts.value("before").toArray()) e.beforeCatalogs.append(factFromJson(v.toObject()));
    for (const QJsonValue &v : facts.value("after").toArray())  e.afterCatalogs.append(factFromJson(v.toObject()));
    return e;
}

void CatalogHistory::setState(int entryId, const QString &state) const
{
    QSqlQuery q(QSqlDatabase::database(m_histConnection));
    q.prepare(QStringLiteral("UPDATE history_entry SET entry_state = :state WHERE entry_id = :id"));
    q.bindValue(":state", state);
    q.bindValue(":id",    entryId);
    q.exec();
}

CatalogHistory::Entry CatalogHistory::entry(int entryId) const
{
    if (!ensureOpen(false))
        return Entry();
    QSqlQuery q(QSqlDatabase::database(m_histConnection));
    q.prepare(QStringLiteral("SELECT * FROM history_entry WHERE entry_id = :id"));
    q.bindValue(":id", entryId);
    if (q.exec() && q.next())
        return readEntry(q);
    return Entry();
}

QList<CatalogHistory::Entry> CatalogHistory::entries(int deviceId) const
{
    QList<Entry> list;
    if (!ensureOpen(false))
        return list;

    int catalogId = 0;
    if (deviceId > 0) {
        QSqlQuery d(QSqlDatabase::database(m_mainConnection));
        d.prepare(QStringLiteral("SELECT device_type, device_external_id FROM device WHERE device_id = :id"));
        d.bindValue(":id", deviceId);
        if (d.exec() && d.next() && d.value(0).toString() == QLatin1String("Catalog"))
            catalogId = d.value(1).toInt();
    }

    QSqlQuery q(QSqlDatabase::database(m_histConnection));
    if (!q.exec(QStringLiteral("SELECT * FROM history_entry WHERE entry_state <> 'pending' ORDER BY entry_id DESC")))
        return list;
    while (q.next()) {
        const Entry e = readEntry(q);
        if (deviceId > 0) {
            const bool touches = e.before.deviceIds.contains(deviceId) || e.after.deviceIds.contains(deviceId)
                              || (catalogId > 0 && (e.before.catalogIds.contains(catalogId)
                                                    || e.after.catalogIds.contains(catalogId)));
            if (!touches)
                continue;
        }
        list.append(e);
    }
    return list;
}

//----------------------------------------------------------------------
// Restore points
//----------------------------------------------------------------------
int CatalogHistory::createRestorePoint(const QString &action, const Scope &scope, const QJsonObject &extra)
{
    if (!ensureOpen(true))
        return 0;
    loadMemoryCatalogs(scope.catalogIds);

    Entry e;
    e.date           = QDateTime::currentDateTime().toString(Qt::ISODate);
    e.action         = action;
    e.state          = QStringLiteral("pending");
    e.before         = scope;
    e.beforeCatalogs = catalogFacts(scope.catalogIds);
    e.extra          = extra;
    if (!writeEntry(e))
        return 0;

    // Tables first, on the history connection: once the collection connection
    // writes to the attached file, the history connection cannot change it
    // until the commit
    bool ok = true;
    for (const HistoryTable &t : kTables)
        if (ok && !keysFor(t, scope).isEmpty()) {
            const QStringList columns = mainColumns(t);
            ok = columns.isEmpty() || ensureSnapTable(t, columns);
        }
    if (!ok) {
        discardEntry(e.id);
        return 0;
    }

    // One transaction on the collection connection when the file is attached
    attach();
    QSqlDatabase main = QSqlDatabase::database(m_mainConnection);
    const bool transaction = m_attached && main.transaction();
    for (const HistoryTable &t : kTables)
        if (ok)
            ok = copyToSnapshot(e.id, t, keysFor(t, scope));
    if (transaction) {
        if (ok)
            ok = main.commit();
        if (!ok)
            main.rollback();
    }
    detach();
    if (!ok) {
        discardEntry(e.id);
        return 0;
    }
    e.hasSnapshot = true;
    writeEntry(e);
    return e.id;
}

bool CatalogHistory::completeEntry(int entryId, const Scope &result, bool dropUnchanged)
{
    Entry e = entry(entryId);
    if (e.id == 0)
        return false;
    loadMemoryCatalogs(result.catalogIds);
    e.after          = result;
    e.afterCatalogs  = catalogFacts(result.catalogIds);

    if (dropUnchanged) {
        // An update that found nothing new is no action to undo: its catalogs
        // leave the entry, and the entry goes when none is left
        QList<int> unchanged;
        for (const CatalogFact &b : std::as_const(e.beforeCatalogs))
            for (const CatalogFact &a : std::as_const(e.afterCatalogs))
                if (a.catalogId == b.catalogId && a.signature == b.signature)
                    unchanged.append(b.catalogId);
        if (unchanged.size() == e.beforeCatalogs.size()) {
            discardEntry(e.id);
            return false;
        }
        if (!unchanged.isEmpty()) {
            QList<int> rows;
            {
                QSqlQuery q(QSqlDatabase::database(m_histConnection));
                if (q.exec(QStringLiteral("SELECT device_id FROM snap_device WHERE snap_entry_id = %1 "
                                          "AND device_type = 'Catalog' AND device_external_id IN (%2)")
                               .arg(e.id).arg(idList(unchanged))))
                    while (q.next())
                        rows.append(q.value(0).toInt());
            }
            for (const HistoryTable &t : kTables) {
                if (t.kind == HistoryTable::CatalogKey)
                    deleteSnapshotRows(e.id, QLatin1String(t.table), QLatin1String(t.key), unchanged);
                else if (t.kind == HistoryTable::DeviceKey && !rows.isEmpty())
                    deleteSnapshotRows(e.id, QLatin1String(t.table), QLatin1String(t.key), rows);
            }
            for (Scope *s : { &e.before, &e.after }) {
                s->catalogIds.removeIf([&](int id) { return unchanged.contains(id); });
                s->deviceIds.removeIf([&](int id) { return rows.contains(id); });
            }
            e.beforeCatalogs.removeIf([&](const CatalogFact &f) { return unchanged.contains(f.catalogId); });
            e.afterCatalogs.removeIf([&](const CatalogFact &f) { return unchanged.contains(f.catalogId); });
        }
    }

    e.state = QStringLiteral("done");
    if (!writeEntry(e))
        return false;
    prune();
    return true;
}

void CatalogHistory::discardEntry(int entryId)
{
    if (entryId <= 0 || !ensureOpen(false))
        return;
    deleteSnapshotRows(entryId);
    QSqlQuery q(QSqlDatabase::database(m_histConnection));
    q.prepare(QStringLiteral("DELETE FROM history_entry WHERE entry_id = :id"));
    q.bindValue(":id", entryId);
    q.exec();
}

CatalogHistory::Error CatalogHistory::checkRestorable(const Entry &e) const
{
    if (!m_enabled)
        return Disabled;
    if (e.id == 0)
        return NotFound;
    if (e.state != QLatin1String("done") || !e.hasSnapshot)
        return NotRestorable;

    // What the action produced must still be there: it is what is replaced
    for (int catalogId : e.after.catalogIds)
        if (!rowExists(QStringLiteral("catalog"), QStringLiteral("catalog_id"), catalogId))
            return NotRestorable;

    // The numbers it gives back must be free, or be the ones it replaces
    for (int deviceId : e.before.deviceIds)
        if (!e.after.deviceIds.contains(deviceId)
            && rowExists(QStringLiteral("device"), QStringLiteral("device_id"), deviceId))
            return IdConflict;
    for (int catalogId : e.before.catalogIds)
        if (!e.after.catalogIds.contains(catalogId)
            && rowExists(QStringLiteral("catalog"), QStringLiteral("catalog_id"), catalogId))
            return IdConflict;

    // Every device put back needs its parent: kept with it, or still there
    QSqlQuery q(QSqlDatabase::database(m_histConnection));
    if (q.exec(QStringLiteral("SELECT DISTINCT device_parent_id FROM snap_device WHERE snap_entry_id = %1").arg(e.id)))
        while (q.next()) {
            const int parentId = q.value(0).toInt();
            if (parentId > 0 && !e.before.deviceIds.contains(parentId)
                && !rowExists(QStringLiteral("device"), QStringLiteral("device_id"), parentId))
                return NoParent;
        }
    return NoError;
}

CatalogHistory::Error CatalogHistory::restore(int entryId, int *restorePointId)
{
    const Entry e = entry(entryId);
    const Error check = checkRestorable(e);
    if (check != NoError)
        return check;

    // 1. The current state of what the entry produced becomes a restore point
    //    itself, so this restore can be undone too
    Scope current;
    current.catalogIds = e.after.catalogIds;
    for (int id : e.after.deviceIds)
        if (rowExists(QStringLiteral("device"), QStringLiteral("device_id"), id))
            appendUnique(current.deviceIds, id);
    for (int catalogId : e.after.catalogIds)
        for (int id : catalogDeviceRows(catalogId))   // Virtual copies made since
            appendUnique(current.deviceIds, id);
    for (int id : e.after.storageIds)
        if (rowExists(QStringLiteral("storage"), QStringLiteral("storage_id"), id))
            appendUnique(current.storageIds, id);
    const int rp = createRestorePoint(QStringLiteral("restore"), current,
                                      QJsonObject{ { "restoredEntryId", e.id },
                                                   { "restoredAction",  e.action } });
    if (rp <= 0)
        return DatabaseFailed;
    if (restorePointId)
        *restorePointId = rp;

    // 2. AI results back to the restored catalogs, by full path, before the
    //    collection changes: when mdi_data.db is busy nothing has changed yet
    if (m_databaseMode != QLatin1String("Hosted")) {
        const QHash<QString, int> back = snapshotPaths(e.id);
        for (int catalogId : e.after.catalogIds) {
            if (e.before.catalogIds.contains(catalogId))
                continue;   // same catalog before and after: nothing to move
            if (moveAiData(catalogId, back) < 0) {
                const QHash<QString, int> forth = snapshotPaths(rp);
                for (int from : e.before.catalogIds)
                    moveAiData(from, forth);
                discardEntry(rp);
                return AiMoveFailed;
            }
        }
    }

    // 3. In the collection: out what the action produced, in the rows kept
    QStringList removedFiles;
    {
        QSqlQuery f(QSqlDatabase::database(m_mainConnection));
        if (!e.after.catalogIds.isEmpty()
            && f.exec(QStringLiteral("SELECT catalog_file_path FROM catalog WHERE catalog_id IN (%1)")
                          .arg(idList(e.after.catalogIds))))
            while (f.next())
                removedFiles << f.value(0).toString();
    }
    QList<int> parents = parentsOf(current.deviceIds);

    attach();
    QSqlDatabase main = QSqlDatabase::database(m_mainConnection);
    const bool transaction = main.transaction();
    bool ok = true;
    // Through the core delete path: the device row, its Virtual copies, the
    // catalog, its files and folders. Nothing on disk.
    for (int catalogId : e.after.catalogIds) {
        const QList<int> rows = catalogDeviceRows(catalogId);
        if (rows.isEmpty())
            continue;
        Device device;
        device.ID = rows.first();
        device.loadDevice(m_mainConnection);
        if (device.deleteDevice(false).result != Device::DeleteSuccess)
            ok = false;
    }
    // What is left of both scopes: storages, groups, profiles, filters, orphans
    const Scope all = e.after.united(e.before);
    for (const HistoryTable &t : kTables)
        if (ok)
            ok = deleteMainRows(t, keysFor(t, all));
    for (const HistoryTable &t : kTables)
        if (ok)
            ok = copyFromSnapshot(e.id, t, QString(), {});
    if (transaction) {
        if (ok)
            ok = main.commit();
        if (!ok)
            main.rollback();
    }
    detach();
    if (!ok) {
        const QHash<QString, int> forth = snapshotPaths(rp);
        for (int from : e.before.catalogIds)
            moveAiData(from, forth);
        discardEntry(rp);
        return DatabaseFailed;
    }

    // 4. Totals of the parents left and joined, then Memory mode files
    for (int id : parentsOf(e.before.deviceIds))
        appendUnique(parents, id);
    refreshParents(parents);
    writeMemoryCatalogs(e.before.catalogIds, removedFiles);

    // 5. The history: this entry is undone; restoring a restore puts back the
    //    entry it had undone
    setState(e.id, QStringLiteral("undone"));
    if (e.action == QLatin1String("restore"))
        setState(e.extra.value("restoredEntryId").toInt(), QStringLiteral("done"));
    completeEntry(rp, e.before);
    return NoError;
}

CatalogHistory::Error CatalogHistory::openAsCatalog(int entryId, int catalogId, const QString &nameSuffix,
                                                    int *newDeviceId, int *openEntryId)
{
    if (!m_enabled)
        return Disabled;
    const Entry e = entry(entryId);
    if (e.id == 0)
        return NotFound;
    if (!e.hasSnapshot)
        return NotRestorable;
    if (!e.before.catalogIds.contains(catalogId))
        return NoSuchCatalog;

    // The kept device row of that catalog: the one in the Physical group,
    // under its parent when that is still there
    int snapDeviceId = 0;
    QString snapName;
    int parentId = 0;
    {
        QSqlQuery q(QSqlDatabase::database(m_histConnection));
        if (q.exec(QStringLiteral("SELECT device_id, device_name, device_parent_id FROM snap_device "
                                  "WHERE snap_entry_id = %1 AND device_type = 'Catalog' "
                                  "AND device_external_id = %2 ORDER BY device_id").arg(e.id).arg(catalogId)))
            while (q.next()) {
                const int parent = q.value(2).toInt();
                if (snapDeviceId == 0) {
                    snapDeviceId = q.value(0).toInt();
                    snapName     = q.value(1).toString();
                }
                if (!rowExists(QStringLiteral("device"), QStringLiteral("device_id"), parent))
                    continue;
                Device p;
                p.ID = parent;
                p.loadDevice(m_mainConnection);
                if (parentId == 0 || p.type != QLatin1String("Virtual")) {
                    snapDeviceId = q.value(0).toInt();
                    snapName     = q.value(1).toString();
                    parentId     = parent;
                }
                if (p.type != QLatin1String("Virtual"))
                    break;
            }
    }
    if (snapDeviceId == 0)
        return NoSuchCatalog;
    if (parentId == 0) {
        // Its storage is gone: beside the catalog it was, when that is still there
        const QList<int> rows = catalogDeviceRows(catalogId);
        if (!rows.isEmpty()) {
            Device current;
            current.ID = rows.first();
            current.loadDevice(m_mainConnection);
            parentId = current.parentID;
        }
    }
    if (parentId == 0)
        return NoParent;

    // New numbers and a free name
    Catalog numbering;
    numbering.setConnectionName(m_mainConnection);
    numbering.generateID();
    const int newCatalogId = numbering.ID;
    const int newDevice    = Device::generateNextDeviceID(m_mainConnection);
    QString name = snapName + nameSuffix;
    {
        const QString base = name;
        QSqlQuery chk(QSqlDatabase::database(m_mainConnection));
        chk.prepare(QStringLiteral("SELECT COUNT(*) FROM device WHERE device_type = 'Catalog' AND device_name = :n"));
        for (int suffix = 2; ; ++suffix) {
            chk.bindValue(":n", name);
            if (!chk.exec() || !chk.next() || chk.value(0).toInt() == 0)
                break;
            name = base + QStringLiteral("_") + QString::number(suffix);
        }
    }
    Device parent;
    parent.ID = parentId;
    parent.loadDevice(m_mainConnection);

    attach();
    QSqlDatabase main = QSqlDatabase::database(m_mainConnection);
    const bool transaction = main.transaction();
    const QString byCatalog = QStringLiteral("%1 = %2");
    bool ok = copyFromSnapshot(e.id, tableNamed("catalog"), byCatalog.arg(QStringLiteral("catalog_id")).arg(catalogId),
                               { { "catalog_id", newCatalogId },
                                 { "catalog_file_path", m_folder + QStringLiteral("/") + name + QStringLiteral(".idx") } })
           && copyFromSnapshot(e.id, tableNamed("file"), byCatalog.arg(QStringLiteral("file_catalog_id")).arg(catalogId),
                               { { "file_catalog_id", newCatalogId }, { "file_catalog", name } })
           && copyFromSnapshot(e.id, tableNamed("folder"), byCatalog.arg(QStringLiteral("folder_catalog_id")).arg(catalogId),
                               { { "folder_catalog_id", newCatalogId } })
           && copyFromSnapshot(e.id, tableNamed("catalog_filter"), byCatalog.arg(QStringLiteral("filter_catalog_id")).arg(catalogId),
                               { { "filter_catalog_id", newCatalogId } })
           && copyFromSnapshot(e.id, tableNamed("device"), byCatalog.arg(QStringLiteral("device_id")).arg(snapDeviceId),
                               { { "device_id", newDevice }, { "device_parent_id", parentId },
                                 { "device_name", name }, { "device_external_id", newCatalogId },
                                 { "device_group_id", parent.groupID } });
    if (ok) {
        // A version kept without a date would be indexed again at every launch
        QSqlQuery d(main);
        d.prepare(QStringLiteral("UPDATE catalog SET catalog_date_updated = :now WHERE catalog_id = :id "
                                 "AND (catalog_date_updated IS NULL OR catalog_date_updated = '')"));
        d.bindValue(":now", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
        d.bindValue(":id",  newCatalogId);
        d.exec();
    }
    if (transaction) {
        if (ok)
            ok = main.commit();
        if (!ok)
            main.rollback();
    }
    detach();
    if (!ok)
        return DatabaseFailed;

    refreshParents({ parentId });
    writeMemoryCatalogs({ newCatalogId }, {});

    // In the history as an action of its own: undoing it removes the copy
    Entry opened;
    opened.date           = QDateTime::currentDateTime().toString(Qt::ISODate);
    opened.action         = QStringLiteral("open");
    opened.state          = QStringLiteral("done");
    opened.after          = scopeOf({ newDevice }, false);
    opened.afterCatalogs  = catalogFacts(opened.after.catalogIds);
    opened.beforeCatalogs = {};
    opened.extra          = QJsonObject{ { "sourceEntryId", e.id }, { "sourceCatalogId", catalogId },
                                         { "sourceName", snapName } };
    opened.hasSnapshot    = true;   // nothing to put back: undoing removes the copy
    writeEntry(opened);
    if (newDeviceId)
        *newDeviceId = newDevice;
    if (openEntryId)
        *openEntryId = opened.id;
    prune();
    return NoError;
}

//----------------------------------------------------------------------
// Retention
//----------------------------------------------------------------------
void CatalogHistory::prune()
{
    if (!ensureOpen(false))
        return;
    QSqlDatabase db = QSqlDatabase::database(m_histConnection);

    // Newest first; an entry keeps its rows while it is among the last m_keep
    // entries of one of its catalogs (of one of its devices when it has none).
    // The newest entry of a catalog is always within them.
    QList<int> drop;
    {
        QHash<QString, int> seen;
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("SELECT * FROM history_entry WHERE entry_snapshot = 1 ORDER BY entry_id DESC")))
            return;
        while (q.next()) {
            const Entry e = readEntry(q);
            QStringList keys;
            for (int id : e.before.united(e.after).catalogIds)
                keys << QStringLiteral("c%1").arg(id);
            if (keys.isEmpty())
                for (int id : e.before.united(e.after).deviceIds)
                    keys << QStringLiteral("d%1").arg(id);
            bool keep = keys.isEmpty() || e.state == QLatin1String("pending");
            for (const QString &key : std::as_const(keys)) {
                if (seen.value(key) < m_keep)
                    keep = true;
                seen[key] = seen.value(key) + 1;
            }
            if (!keep)
                drop << e.id;
        }
    }
    for (int id : std::as_const(drop)) {
        deleteSnapshotRows(id);
        QSqlQuery u(db);
        u.exec(QStringLiteral("UPDATE history_entry SET entry_snapshot = 0 WHERE entry_id = %1").arg(id));
    }

    // The list itself stays readable: only very old lines go
    QSqlQuery old(db);
    old.exec(QStringLiteral("DELETE FROM history_entry WHERE entry_snapshot = 0 AND entry_id NOT IN "
                            "(SELECT entry_id FROM history_entry ORDER BY entry_id DESC LIMIT %1)").arg(kMaxEntryRows));
    if (!drop.isEmpty()) {
        QSqlQuery vacuum(db);
        vacuum.exec(QStringLiteral("PRAGMA incremental_vacuum"));
    }
}
