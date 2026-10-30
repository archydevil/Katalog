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
// File Name:   cataloghistory.h
// Purpose:     Restore points and history of catalog changes (fork only,
//              SpecMediaIntelligence.md, owner's decision 2026-10-30)
// Description: Before an action changes catalogs (update, split, recompose,
//              delete, edit), the rows it will change are copied to their own
//              SQLite file in the collection folder, teca_history.db: device,
//              catalog, file, folder, catalog_filter, mdi_profile and storage
//              rows, column by column, so the collection schema never changes.
//              An entry can be restored ("as it was before"), which itself
//              leaves a restore point, or one of its catalogs opened as a
//              separate catalog. AI results follow by full path (MdiData).
//              The last 10 versions of each catalog keep their rows.
//              No collection folder (Hosted): no history.
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef CATALOGHISTORY_H
#define CATALOGHISTORY_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <QJsonObject>
#include <QVariant>

class Collection;
class QSqlQuery;
struct HistoryTable;   // one table kept in a restore point (cataloghistory.cpp)

class CatalogHistory
{
public:
    // What a restore point holds: device rows (every row that shows a catalog,
    // Virtual copies included), catalogs with their files, folders, exclude
    // filters and enrichment profiles, and storage rows
    struct Scope {
        QList<int> deviceIds;
        QList<int> catalogIds;
        QList<int> storageIds;
        bool        isEmpty() const { return deviceIds.isEmpty() && catalogIds.isEmpty() && storageIds.isEmpty(); }
        Scope       united(const Scope &other) const;
        QJsonObject toJson() const;
        static Scope fromJson(const QJsonObject &json);
    };

    // One catalog as the history shows it
    struct CatalogFact {
        int     deviceId  = 0;   // its device in the Physical group, else the first one
        int     catalogId = 0;
        QString name;
        QString path;
        qint64  files = 0;
        qint64  size  = 0;
        QString signature;       // files, sizes, paths and dates: tells whether it changed
    };

    struct Entry {
        int     id = 0;
        QString date;            // ISO 8601
        QString action;          // update, split, recompose, delete, edit, restore, open
        QString state;           // pending (action running), done, undone
        Scope   before;          // what the restore point holds
        Scope   after;           // what the action produced
        QList<CatalogFact> beforeCatalogs;
        QList<CatalogFact> afterCatalogs;
        QJsonObject extra;       // restore: restoredEntryId, restoredAction; open: sourceEntryId
        bool    hasSnapshot = false;   // false once pruned
    };

    enum Error {
        NoError = 0,
        Disabled,         // no collection folder (Hosted)
        NotFound,
        NotRestorable,    // undone, pruned, or a catalog it produced is gone
        IdConflict,       // a number it used now belongs to another device or catalog
        AiMoveFailed,     // mdi_data.db busy or unreadable: nothing was changed
        NoSuchCatalog,    // the catalog is not in this restore point
        NoParent,         // nowhere to put the catalog opened apart
        DatabaseFailed
    };

    static constexpr int kDefaultKeepPerCatalog = 10;   // owner's choice, setting later

    explicit CatalogHistory(Collection *collection);
    ~CatalogHistory();

    static QString filePath(const QString &collectionFolder);
    static bool    isAvailable(const QString &databaseMode, const QString &collectionFolder);
    bool isEnabled() const { return m_enabled; }
    void setKeepPerCatalog(int count) { m_keep = count > 0 ? count : 1; }

    // The rows an action on these devices changes. A catalog brings every
    // device row that shows it; withDescendants adds the devices and catalogs
    // under a storage or a group (an update of a storage updates its catalogs).
    Scope scopeOf(const QList<int> &deviceIds, bool withDescendants) const;

    // Copies the scope's rows before an action; returns the entry, 0 when
    // nothing could be kept (no folder, file not writable)
    int  createRestorePoint(const QString &action, const Scope &scope,
                            const QJsonObject &extra = QJsonObject());
    // After the action: what it produced. dropUnchanged (updates): catalogs
    // whose files did not change are left out, and the entry is discarded when
    // none changed. Returns false when the entry is not kept.
    bool completeEntry(int entryId, const Scope &result, bool dropUnchanged = false);
    void discardEntry(int entryId);

    // Newest first, without entries of actions still running. deviceId > 0:
    // the entries touching that device or its catalog.
    QList<Entry> entries(int deviceId = 0) const;
    Entry        entry(int entryId) const;
    Error        checkRestorable(const Entry &e) const;

    // Puts back the rows of the restore point, after keeping the current
    // state as a new restore point (action "restore") so this can be undone.
    Error restore(int entryId, int *restorePointId = nullptr);
    // Inserts one catalog of the restore point as a new catalog and device
    // (new numbers, name + nameSuffix), under the same parent. AI results are
    // not copied: they stay with the catalog they belong to.
    Error openAsCatalog(int entryId, int catalogId, const QString &nameSuffix,
                        int *newDeviceId = nullptr, int *openEntryId = nullptr);

    // Keeps the rows of the last m_keep entries of each catalog
    void prune();

private:
    Collection *m_collection = nullptr;
    QString m_mainConnection;
    QString m_histConnection;
    QString m_databaseMode;
    QString m_folder;
    bool    m_enabled  = false;
    mutable bool m_open = false;
    bool    m_attached = false;
    int     m_keep     = kDefaultKeepPerCatalog;

    bool ensureOpen(bool create) const;
    bool attach();
    void detach();
    bool mainIsSqlite() const;

    QList<CatalogFact> catalogFacts(const QList<int> &catalogIds) const;
    void loadMemoryCatalogs(const QList<int> &catalogIds) const;
    void writeMemoryCatalogs(const QList<int> &catalogIds, const QStringList &removedFiles) const;
    QList<int> catalogDeviceRows(int catalogId) const;
    QList<int> parentsOf(const QList<int> &deviceIds) const;
    void refreshParents(const QList<int> &parentIds) const;
    bool rowExists(const QString &table, const QString &key, int id) const;

    QStringList mainColumns(const HistoryTable &t) const;
    bool ensureSnapTable(const HistoryTable &t, const QStringList &columns) const;
    bool copyToSnapshot(int entryId, const HistoryTable &t, const QList<int> &keys);
    bool copyFromSnapshot(int entryId, const HistoryTable &t, const QString &snapFilter,
                          const QHash<QString, QVariant> &overrides);
    bool deleteMainRows(const HistoryTable &t, const QList<int> &keys);
    void deleteSnapshotRows(int entryId, const QString &table = QString(),
                            const QString &key = QString(), const QList<int> &keys = {}) const;
    QHash<QString, int> snapshotPaths(int entryId) const;
    int  moveAiData(int fromCatalogId, const QHash<QString, int> &byPath) const;

    bool  writeEntry(Entry &e) const;
    Entry readEntry(const QSqlQuery &q) const;
    void  setState(int entryId, const QString &state) const;
};

#endif // CATALOGHISTORY_H
