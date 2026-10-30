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
// File Name:   mdiproposals.h
// Purpose:     Inferred file information awaiting the owner's approval
//              (fork only, SpecMediaIntelligence.md, owner's decisions of
//              2026-10-30: "Proposte da approvare")
// Description: A proposal is a value Teca inferred for a field of a file's
//              information (date, event, place, author, title), with its
//              confidence and the sources it rests on. It never changes the
//              owner's information by itself: only accepting it writes the
//              value into the file's information (MdiPeople annotation), and
//              never over a value the owner wrote unless asked to.
//              Kept in mdi_data.db, keyed by catalog ID and full path like
//              the derived data, so it follows split and restored catalogs.
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDIPROPOSALS_H
#define MDIPROPOSALS_H

#include <QString>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QSet>
#include <QList>

class MdiProposals
{
public:
    // Fields a proposal can fill: the matching keys of the file's information
    static const QStringList &fields();

    struct Source {
        QString kind;     // "exif", "video", "filename", "folder", "folder-consistency",
                          // "caption", "classification", "context", "knowledge", "web",
                          // "model", "document"
        QString label;    // what it is, for the owner: "EXIF DateTimeOriginal", folder name...
        QString detail;   // the value or excerpt it rests on
        QString url;      // web page or document path, when any
        double  confidence = 0;
        QJsonObject toJson() const;
        static Source fromJson(const QJsonObject &o);
    };

    struct Proposal {
        int     id = 0;
        int     catalogId = 0;
        QString fullPath;
        QString field;        // one of fields()
        QString value;        // dates: "2019-07-12", "2019-07", "2019", "2013/2015"
        QString precision;    // dates: day, month, year, range; places: name, coordinates
        double  confidence = 0;
        QList<Source> sources;
        QString status = QStringLiteral("pending");   // pending, accepted, rejected, superseded
        QString origin;       // the analysis that made it: "dating", "document"
        QString created;
        QString decided;
        QVariantMap toVariant() const;
    };

    // Creates the table when absent. Idempotent.
    static bool ensureTables(const QString &connectionName);

    // Replaces the pending proposals of one origin for a file: a proposal
    // proposed again keeps its ID and status (accepted and rejected ones are
    // never proposed again), a pending one no longer proposed is removed.
    // Accepted and rejected proposals are never removed by this.
    static bool replaceForFile(const QString &connectionName, int catalogId, const QString &fullPath,
                               const QString &origin, const QList<Proposal> &proposals);
    // The same for many files in one transaction (a catalog pass); files
    // absent from `byFile` keep their proposals. Returns the pending count.
    static int  replaceForFiles(const QString &connectionName, int catalogId, const QString &origin,
                                const QHash<QString, QList<Proposal>> &byFile);

    // Every proposal of a file, best first within each field
    static QList<Proposal> forFile(const QString &connectionName, int catalogId, const QString &fullPath);
    static QList<Proposal> byIds(const QString &connectionName, const QList<int> &ids);
    // Pending proposals of the files of a folder (and its subfolders when
    // recursive), grouped by field and value for approval in bulk:
    // [{field, value, precision, confidence (mean), minConfidence, fileCount,
    //   proposalIds: [...], sources: [{kind, label, detail, url}] (distinct, at most 12)}]
    static QVariantList forFolder(const QString &connectionName, int catalogId, const QString &folderPath,
                                  bool recursive);
    // Files of a catalog with pending or accepted proposals (reports)
    static QSet<QString> filesWithProposals(const QString &connectionName, int catalogId);
    // Pending proposal counts per field of a catalog: {date: n, event: n...}
    static QVariantMap pendingCounts(const QString &connectionName, int catalogId);

    // Marks proposals accepted or rejected. Writing an accepted value into
    // the file's information is the caller's part (acceptAndApply).
    static bool setStatus(const QString &connectionName, const QList<int> &ids, const QString &status);

    // Accepts proposals and writes each value into its file's information
    // field (MdiPeople annotation). A field that already holds a different
    // value written by the owner is left untouched and its proposal stays
    // pending, unless replaceOwnValues. Two proposals for the same field of
    // the same file: the more confident one wins; the field's other pending
    // values become "superseded" (never proposed again).
    struct AcceptResult {
        int applied = 0;      // values written (or already equal)
        int kept = 0;         // left pending: the owner's own value differs
        int failed = 0;
        bool ok = true;
    };
    static AcceptResult acceptAndApply(const QString &connectionName, const QList<int> &ids,
                                       bool replaceOwnValues = false);

    // Catalog housekeeping, alongside MdiData::clearCatalog / moveToCatalogs
    static bool clearCatalog(const QString &connectionName, int catalogId);
    // SQL to move a file's proposals to a new catalog (same binds as
    // MdiData::moveToCatalogs: :new_id, :old_id, :full_path)
    static QString moveStatement();
};

#endif // MDIPROPOSALS_H
