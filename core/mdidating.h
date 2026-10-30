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
// File Name:   mdidating.h
// Purpose:     Dating photos and videos (fork only, SpecMediaIntelligence.md,
//              owner's decisions of 2026-10-30)
// Description: Gathers the evidence of when, at which event and where a photo
//              or a video was taken — its embedded metadata (EXIF, QuickTime),
//              its file name, the names of its folders, the files around it,
//              its caption and classification, the collection's context, the
//              knowledge base and, when the owner asked for them, web
//              lookups — and turns it into proposals (MdiProposals) with a
//              confidence and their sources. Nothing is written into the
//              owner's information: the owner approves.
//
//              Two steps:
//              - analyseFile(): the "dating" analysis of the enrichment
//                runner, per file, while its drive is connected: reads the
//                embedded metadata and the file name. Stored as a datum, so
//                the next step works with the drive away.
//              - deriveCatalog(): a catalog pass, after the runner's files
//                and after web lookups: adds what the folders, the
//                neighbouring files, the context, the knowledge base and the
//                web say, and replaces the catalog's pending proposals.
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDIDATING_H
#define MDIDATING_H

#include <QString>
#include <QStringList>
#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantList>
#include <QHash>
#include <QList>
#include <atomic>
#include <functional>

#include "core/mdiproposals.h"

class MdiDating
{
public:
    // Version of the "dating" analysis (its parameters, MDI-F13)
    static constexpr int kVersion = 1;
    // Proposals below this confidence are not shown
    static constexpr double kMinConfidence = 0.30;

    // A date, or a span of dates, as precise as the evidence allows
    struct DateValue {
        QDate   from, to;
        QString precision;      // "day", "month", "year", "range"
        bool    isValid() const { return from.isValid() && to.isValid() && from <= to; }
        qint64  span() const { return isValid() ? from.daysTo(to) : -1; }
        // "2019-07-12", "2019-07", "2019", "2013/2015", "2015-06-12/2015-06-15"
        QString text() const;
        bool    contains(const DateValue &o) const { return isValid() && o.isValid() && from <= o.from && o.to <= to; }
        bool    overlaps(const DateValue &o) const { return isValid() && o.isValid() && from <= o.to && o.from <= to; }
        // Days between the two spans, 0 when they overlap
        qint64  gap(const DateValue &o) const;
        static DateValue day(int y, int m, int d);
        static DateValue month(int y, int m);
        static DateValue year(int y);
        static DateValue range(const QDate &from, const QDate &to);
        static DateValue fromText(const QString &text);
    };

    // One piece of evidence for one field
    struct Evidence {
        QString field;          // "date", "event", "place"
        QString value;          // DateValue::text() for dates
        QString precision;      // dates: day/month/year/range; places: name/coordinates
        double  confidence = 0;
        QString kind;           // MdiProposals::Source::kind
        QString label;          // "EXIF DateTimeOriginal", "Folder: Munich 2015"...
        QString detail;         // raw value or excerpt
        QString url;            // web page or document path
        QJsonObject toJson() const;
        static Evidence fromJson(const QJsonObject &o);
    };

    // A date found in free text, with where it is
    struct FoundDate {
        DateValue date;
        int       position = 0;
        int       length = 0;
    };

    // What a folder name says
    struct FolderReading {
        QString     name;
        DateValue   date;           // invalid when none
        QString     event;          // "Winter Cup 2015", "Europei Osijek"; empty when none
        double      eventConfidence = 0;
        bool        specificEvent = false;   // a named event (championship, cup...), not "trip"
        QStringList places;         // gazetteer or context places named
        QStringList unknownWords;   // capitalised words not understood
        QStringList eventWords;     // the event words found
        bool        generic = false;   // "DCIM", "100CANON", "Foto"...: says nothing
        bool        fromModel = false; // read by the text model
    };

    // ---- Readers (pure, unit-tested)
    static QList<FoundDate> findDates(const QString &text, int minYear = 1900, int maxYear = 0);
    static QList<Evidence>  readFileName(const QString &fileName);
    static FolderReading    readFolderName(const QString &name, const QJsonObject &context = {});
    // Evidence of the folders of a path relative to the catalog root, the
    // nearest folder first; readings already made are reused
    static QList<Evidence>  readFolders(const QString &relativeFolder, const QJsonObject &context,
                                        QHash<QString, FolderReading> *cache = nullptr);
    // Evidence in a caption or a classification datum
    static QList<Evidence>  readDerived(const QString &analysis, const QJsonObject &value,
                                        const QJsonObject &context);
    // Dates and places that the context's known events state (notes and aliases)
    static QList<Evidence>  readContext(const QString &eventOrName, int folderYear, const QJsonObject &context);
    // Known city within 40 km of a position, else empty
    static QString          nearestPlace(double latitude, double longitude, double *distanceKm = nullptr);
    // A metadata date (EXIF "2019:07:12 10:22:33", ISO 8601, QuickTime);
    // invalid for camera defaults (1904, 1970, 2000-01-01 00:00) and future dates
    static QDateTime        parseMetadataDate(const QString &text);
    // A place name of the built-in list (Italian and English names) or of the
    // context's places; empty when unknown
    static QString          knownPlace(const QString &words, const QJsonObject &context = {});

    // ---- Per file: the "dating" analysis of the runner
    struct Tools {
        QString exiv2;            // exiv2 CLI, when KFileMetaData gives no date
        QString ffprobe;          // videos
        bool    useKFileMetaData = true;
        int     timeoutMs = 30 * 1000;
        const std::atomic<bool> *stop = nullptr;
    };
    // Embedded metadata of a photo or video, from the file:
    // {dateTimeOriginal, createDate, modifyDate, latitude, longitude, make,
    //  model, software, source ("kfilemetadata", "exiv2", "ffprobe")}
    static QJsonObject readEmbedded(const QString &path, const QString &fileType, const Tools &tools,
                                    QString *error);
    static QList<Evidence> embeddedEvidence(const QJsonObject &embedded);
    // The datum value: {version, embedded: {...}, evidence: [file name and
    // embedded evidence]}; outcome "skipped" only for unreadable files
    static QJsonObject analyseFile(const QString &fullPath, const QString &fileType, const Tools &tools,
                                   QString *outcome, QString *reason);

    // ---- Combination
    static QList<MdiProposals::Proposal> combine(const QList<Evidence> &evidence, const QJsonObject &context,
                                                 double minConfidence = kMinConfidence, int alternatives = 1);

    // ---- Catalog pass
    struct DeriveOptions {
        QString     sourceRoot;     // catalog's device path, stripped from full paths
        QJsonObject context;
        // Local text model for folder names the rules cannot read; null: rules only
        std::function<QJsonObject(const QString &prompt, QString *error)> ask;
        QString     language;       // of the model's answers (MdiContext::language)
        const std::atomic<bool> *stop = nullptr;
        std::function<void(int done, int total)> progress;
        int         maxModelFolders = 400;
    };
    struct DeriveSummary {
        int files = 0;
        int proposals = 0;          // pending after the pass
        int modelCalls = 0;
        bool stopped = false;
        QString error;
    };
    static DeriveSummary deriveCatalog(const QString &connectionName, int catalogId, const DeriveOptions &options);

    // ---- Web lookups (only when the owner enabled them, MdiWebSearch)
    // Queries worth asking for the catalog's folders, built only from event,
    // place and year words — never people's names — and not asked yet:
    // [{query, reason, folder, fileCount}]
    static QVariantList webQueries(const QString &connectionName, int catalogId, const QString &sourceRoot,
                                   const QJsonObject &context, const QStringList &personNames = {},
                                   int max = 40);
    // The query for one folder reading; empty when it would not be specific
    static QString webQuery(const FolderReading &reading, const QJsonObject &context,
                            const QStringList &personNames = {});

    // Table of folder readings made by the text model (cache), in mdi_data.db
    static bool ensureTables(const QString &connectionName);
    static bool clearCatalog(const QString &connectionName, int catalogId);
};

#endif // MDIDATING_H
