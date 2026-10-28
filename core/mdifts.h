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
// File Name:   mdifts.h
// Purpose:     Instant search index (fork only, SpecMediaIntelligence.md, D5)
// Description: SQLite FTS5 trigram index of file names and full paths, plus
//              the text derived by the enrichment runner (snippet, summary,
//              caption), kept in mdi_data.db so the core schema stays 3.0.
//              The search uses it as a prefilter: a superset of what the
//              search regex accepts, so results are unchanged, only faster.
//              Synchronised per catalog after each creation or update (an
//              update rewrites all the catalog's rows, which is why there are
//              no triggers; SQLite triggers cannot write to another file).
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDIFTS_H
#define MDIFTS_H

#include <QString>
#include <QList>
#include <QVariantMap>
#include <atomic>

class MdiFts
{
public:
    // Catalog state the index was built from (catalog_date_updated format)
    static QString stamp(const QString &catalogDateUpdated) { return catalogDateUpdated; }

    // Creates the index tables in `schema` ("main" for a connection to
    // mdi_data.db, "mdi" when it is attached to the collection connection)
    static bool ensureTables(const QString &connectionName, const QString &schema = QStringLiteral("main"));

    // Attaches mdi_data.db to a collection connection as schema "mdi", once
    static bool attach(const QString &connectionName, const QString &mdiDbPath);

    static bool isCurrent(const QString &connectionName, const QString &schema, int catalogId,
                          const QString &stamp);

    // Rebuilds one catalog's index on connections of its own, so it can run on
    // any thread. File mode reads the collection database's file table;
    // Memory mode reads the catalog's .idx file. Commits in batches, so the
    // enrichment runner writing the same file is never held for long.
    static bool rebuildCatalog(const QString &mdiDbPath, int catalogId, const QString &stamp,
                               const QString &databaseMode, const QString &collectionDbPath,
                               const QString &catalogIdxPath, QString *error,
                               const std::atomic<bool> *stop = nullptr);

    // Catalogs of the collection whose index is missing or older than the
    // catalog: [{deviceId, catalogId, name, stamp, filePath}]. Collection
    // connection (main thread); mdi_data.db is attached to it.
    static QList<QVariantMap> staleCatalogs(const QString &connectionName, const QString &mdiDbPath);

    // Text derived for a file (snippet, summary, caption, tags), for the index
    static QString derivedText(const QString &mdiConnectionName, int catalogId, const QString &fullPath);

    // Refreshes one file's derived text in the index; nothing when the file
    // is not indexed yet (the next rebuild includes it)
    static void updateDerivedText(const QString &mdiConnectionName, int catalogId, const QString &fullPath);

    // FTS5 query for a search text and criteria (Search::TEXT_CRITERIA_*);
    // empty when the prefilter cannot be a superset of the regex: regex
    // criteria, or a term shorter than 3 characters (trigram minimum)
    static QString matchQuery(const QString &searchText, const QString &criteria, bool namesOnly);
};

#endif // MDIFTS_H
