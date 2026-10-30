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
// File Name:   mdiwebsearch.h
// Purpose:     Web lookups for dating (fork only, owner's decision of
//              2026-10-30: "Sì, su richiesta e con avviso")
// Description: Off by default per collection. The owner sees the queries
//              before anything is sent; only the query words leave the
//              computer, never a file or a person's name. Two open,
//              keyless services that permit automated use:
//              - DuckDuckGo's Instant Answer API (abstracts and infoboxes);
//              - Wikipedia's MediaWiki API (search, then the introduction of
//                the best page), Italian first, then English.
//              A descriptive User-Agent, at most one request per second, no
//              cookies kept. Results are cached in mdi_data.db with the
//              query, so the dating pass reads them without going online.
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDIWEBSEARCH_H
#define MDIWEBSEARCH_H

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QUrl>
#include <QList>
#include <QJsonObject>
#include <atomic>
#include <functional>

class MdiWebSearch
{
public:
    struct Result {
        QString source;    // "duckduckgo", "wikipedia-it", "wikipedia-en"
        QString title;
        QString url;       // cited with the proposal
        QString text;      // abstract, snippet or page introduction (plain text)
        QJsonObject toJson() const;
        static Result fromJson(const QJsonObject &o);
    };

    static QString userAgent();
    // Minimum time between two requests, all queries together
    static constexpr int kMinIntervalMs = 1100;

    // ---- Parsers (pure, unit-tested on saved answers)
    static QList<Result> parseDuckDuckGo(const QByteArray &json);
    static QList<Result> parseWikipediaSearch(const QByteArray &json, const QString &language);
    static QString       parseWikipediaExtract(const QByteArray &json);
    // "<span class=\"searchmatch\">Tokyo</span> &amp; ..." -> "Tokyo & ..."
    static QString       plainText(const QString &html);

    static QUrl duckDuckGoUrl(const QString &query);
    static QUrl wikipediaSearchUrl(const QString &language, const QString &query);
    static QUrl wikipediaExtractUrl(const QString &language, const QString &title);
    static QString wikipediaPageUrl(const QString &language, const QString &title);

    // ---- Network
    // GET with the User-Agent, no cookies kept, the rate limit applied;
    // synchronous, for a worker thread. Empty and *error set on failure.
    static QByteArray fetch(const QUrl &url, QString *error, const std::atomic<bool> *stop = nullptr,
                            int timeoutMs = 20000);
    using Fetcher = std::function<QByteArray(const QUrl &url, QString *error)>;
    // One query: DuckDuckGo, then Wikipedia (it, then en when it finds
    // nothing). `fetcher` replaces the network in tests.
    static QList<Result> search(const QString &query, QString *error, const std::atomic<bool> *stop = nullptr,
                                const Fetcher &fetcher = {});

    // ---- Cache (mdi_data.db)
    static bool ensureTables(const QString &connectionName);
    static bool storeResults(const QString &connectionName, const QString &query, const QList<Result> &results);
    static QList<Result> cachedResults(const QString &connectionName, const QString &query);
    static bool isCached(const QString &connectionName, const QString &query);
};

#endif // MDIWEBSEARCH_H
