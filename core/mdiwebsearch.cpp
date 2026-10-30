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
// File Name:   mdiwebsearch.cpp
// Purpose:     Web lookups for dating (fork only)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdiwebsearch.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkCookieJar>
#include <QNetworkCookie>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>

namespace {

// Never keeps a cookie (owner: no tracking cookies)
class NoCookies : public QNetworkCookieJar
{
public:
    using QNetworkCookieJar::QNetworkCookieJar;
    bool setCookiesFromUrl(const QList<QNetworkCookie> &, const QUrl &) override { return false; }
    QList<QNetworkCookie> cookiesForUrl(const QUrl &) const override { return {}; }
};

QMutex        s_rateMutex;
QElapsedTimer s_lastRequest;

QString queryKey(const QString &query)
{
    return query.simplified().toLower();
}

QString jsonText(const QJsonValue &v)
{
    return v.isString() ? v.toString().trimmed() : QString();
}

} // namespace

QJsonObject MdiWebSearch::Result::toJson() const
{
    return QJsonObject{{"source", source}, {"title", title}, {"url", url}, {"text", text}};
}

MdiWebSearch::Result MdiWebSearch::Result::fromJson(const QJsonObject &o)
{
    Result r;
    r.source = o.value("source").toString();
    r.title  = o.value("title").toString();
    r.url    = o.value("url").toString();
    r.text   = o.value("text").toString();
    return r;
}

QString MdiWebSearch::userAgent()
{
    return QStringLiteral("Teca/3.0 (archive cataloguing; contact via app)");
}

QString MdiWebSearch::plainText(const QString &html)
{
    if (!html.contains(QLatin1Char('<')) && !html.contains(QLatin1Char('&')))
        return html.simplified();
    QString text = html;
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    text.remove(tags);
    // Entities: the named ones that occur in these answers, and numeric ones
    text.replace(QLatin1String("&quot;"), QLatin1String("\""));
    text.replace(QLatin1String("&#039;"), QLatin1String("'"));
    text.replace(QLatin1String("&#39;"), QLatin1String("'"));
    text.replace(QLatin1String("&apos;"), QLatin1String("'"));
    text.replace(QLatin1String("&lt;"), QLatin1String("<"));
    text.replace(QLatin1String("&gt;"), QLatin1String(">"));
    text.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    static const QRegularExpression numeric(QStringLiteral("&#(x?)([0-9a-fA-F]+);"));
    QRegularExpressionMatch m;
    int from = 0;
    while ((m = numeric.match(text, from)).hasMatch()) {
        bool ok = false;
        const uint code = m.captured(2).toUInt(&ok, m.captured(1).isEmpty() ? 10 : 16);
        const QString ch = ok && code > 0 ? QString::fromUcs4(reinterpret_cast<const char32_t *>(&code), 1) : QString();
        text.replace(m.capturedStart(0), m.capturedLength(0), ch);
        from = m.capturedStart(0) + ch.size();
    }
    text.replace(QLatin1String("&amp;"), QLatin1String("&"));
    return text.simplified();
}

QList<MdiWebSearch::Result> MdiWebSearch::parseDuckDuckGo(const QByteArray &json)
{
    QList<Result> list;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (root.isEmpty())
        return list;
    const QString heading = jsonText(root.value("Heading"));
    const QString abstract = plainText(jsonText(root.value("AbstractText")));
    const QString abstractUrl = jsonText(root.value("AbstractURL"));
    // The infobox: "Dates: 23 July – 8 August 2021", "Host city: Tokyo"
    QStringList facts;
    for (const QJsonValue &v : root.value("Infobox").toObject().value("content").toArray()) {
        const QJsonObject item = v.toObject();
        const QString label = jsonText(item.value("label"));
        const QString value = jsonText(item.value("value"));
        if (!label.isEmpty() && !value.isEmpty() && facts.size() < 12)
            facts << label + QStringLiteral(": ") + plainText(value);
    }
    if (!abstract.isEmpty() || !facts.isEmpty()) {
        Result r;
        r.source = QStringLiteral("duckduckgo");
        r.title = heading;
        r.url = abstractUrl;
        r.text = abstract;
        if (!facts.isEmpty())
            r.text += (r.text.isEmpty() ? QString() : QStringLiteral(" ")) + facts.join(QStringLiteral(". ")) + QLatin1Char('.');
        if (!r.url.isEmpty())
            list << r;
    }
    // Direct results, then a few related topics (not the disambiguation groups)
    auto addTopic = [&list](const QJsonObject &t) {
        const QString text = plainText(jsonText(t.value("Text")));
        const QString url = jsonText(t.value("FirstURL"));
        if (text.isEmpty() || url.isEmpty())
            return;
        Result r;
        r.source = QStringLiteral("duckduckgo");
        r.title = text.section(QStringLiteral(" - "), 0, 0).left(120);
        r.url = url;
        r.text = text;
        list << r;
    };
    for (const QJsonValue &v : root.value("Results").toArray())
        addTopic(v.toObject());
    int related = 0;
    for (const QJsonValue &v : root.value("RelatedTopics").toArray()) {
        const QJsonObject t = v.toObject();
        if (t.contains("Topics") || related >= 3)
            continue;
        addTopic(t);
        ++related;
    }
    return list;
}

QList<MdiWebSearch::Result> MdiWebSearch::parseWikipediaSearch(const QByteArray &json, const QString &language)
{
    QList<Result> list;
    const QJsonArray hits = QJsonDocument::fromJson(json).object().value("query").toObject().value("search").toArray();
    for (const QJsonValue &v : hits) {
        const QJsonObject hit = v.toObject();
        const QString title = jsonText(hit.value("title"));
        if (title.isEmpty())
            continue;
        Result r;
        r.source = QStringLiteral("wikipedia-") + language;
        r.title = title;
        r.url = wikipediaPageUrl(language, title);
        r.text = plainText(jsonText(hit.value("snippet")));
        list << r;
    }
    return list;
}

QString MdiWebSearch::parseWikipediaExtract(const QByteArray &json)
{
    const QJsonObject pages = QJsonDocument::fromJson(json).object().value("query").toObject().value("pages").toObject();
    for (auto it = pages.begin(); it != pages.end(); ++it) {
        const QString extract = it.value().toObject().value("extract").toString().simplified();
        if (!extract.isEmpty())
            return extract.left(2000);
    }
    return QString();
}

QUrl MdiWebSearch::duckDuckGoUrl(const QString &query)
{
    QUrl url(QStringLiteral("https://api.duckduckgo.com/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    q.addQueryItem(QStringLiteral("no_html"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("skip_disambig"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("no_redirect"), QStringLiteral("1"));
    url.setQuery(q);
    return url;
}

QUrl MdiWebSearch::wikipediaSearchUrl(const QString &language, const QString &query)
{
    QUrl url(QStringLiteral("https://%1.wikipedia.org/w/api.php").arg(language));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("action"), QStringLiteral("query"));
    q.addQueryItem(QStringLiteral("list"), QStringLiteral("search"));
    q.addQueryItem(QStringLiteral("srsearch"), query);
    q.addQueryItem(QStringLiteral("srlimit"), QStringLiteral("5"));
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    q.addQueryItem(QStringLiteral("utf8"), QStringLiteral("1"));
    url.setQuery(q);
    return url;
}

QUrl MdiWebSearch::wikipediaExtractUrl(const QString &language, const QString &title)
{
    QUrl url(QStringLiteral("https://%1.wikipedia.org/w/api.php").arg(language));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("action"), QStringLiteral("query"));
    q.addQueryItem(QStringLiteral("prop"), QStringLiteral("extracts"));
    q.addQueryItem(QStringLiteral("exintro"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("explaintext"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("redirects"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("titles"), title);
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(q);
    return url;
}

QString MdiWebSearch::wikipediaPageUrl(const QString &language, const QString &title)
{
    QString path = title;
    path.replace(QLatin1Char(' '), QLatin1Char('_'));
    return QStringLiteral("https://%1.wikipedia.org/wiki/").arg(language)
         + QString::fromUtf8(QUrl::toPercentEncoding(path, "_()',-.:"));
}

QByteArray MdiWebSearch::fetch(const QUrl &url, QString *error, const std::atomic<bool> *stop, int timeoutMs)
{
    // Polite rate: at most one request per second, all threads together
    {
        QMutexLocker lock(&s_rateMutex);
        while (s_lastRequest.isValid() && s_lastRequest.elapsed() < kMinIntervalMs) {
            if (stop && stop->load()) {
                *error = QStringLiteral("stopped");
                return {};
            }
            QThread::msleep(50);
        }
        s_lastRequest.start();
    }

    QNetworkAccessManager manager;
    manager.setCookieJar(new NoCookies(&manager));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = manager.get(request);

    QEventLoop loop;
    QTimer poll;
    QElapsedTimer clock;
    clock.start();
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if ((stop && stop->load()) || clock.elapsed() > timeoutMs)
            reply->abort();
    });
    poll.start(200);
    loop.exec();
    poll.stop();

    QByteArray body;
    if (reply->error() != QNetworkReply::NoError) {
        *error = (stop && stop->load()) ? QStringLiteral("stopped")
               : clock.elapsed() > timeoutMs ? QStringLiteral("time limit reached")
               : reply->errorString();
    } else {
        body = reply->readAll();
    }
    reply->deleteLater();
    return body;
}

QList<MdiWebSearch::Result> MdiWebSearch::search(const QString &query, QString *error,
                                                 const std::atomic<bool> *stop, const Fetcher &fetcher)
{
    QList<Result> results;
    QStringList errors;
    auto get = [&](const QUrl &url) {
        QString err;
        const QByteArray body = fetcher ? fetcher(url, &err) : fetch(url, &err, stop);
        if (!err.isEmpty())
            errors << url.host() + QStringLiteral(": ") + err;
        return body;
    };
    if (query.trimmed().isEmpty())
        return results;

    results << parseDuckDuckGo(get(duckDuckGoUrl(query)));
    for (const QString &language : {QStringLiteral("it"), QStringLiteral("en")}) {
        if (stop && stop->load())
            break;
        QList<Result> hits = parseWikipediaSearch(get(wikipediaSearchUrl(language, query)), language);
        if (hits.isEmpty())
            continue;
        // The best page's introduction says when and where
        const QString intro = parseWikipediaExtract(get(wikipediaExtractUrl(language, hits.first().title)));
        if (!intro.isEmpty())
            hits.first().text = intro;
        results << hits.mid(0, 5);
        break;   // English only when the Italian Wikipedia found nothing
    }
    if (results.isEmpty() && error)
        *error = errors.join(QStringLiteral("; "));
    return results;
}

bool MdiWebSearch::ensureTables(const QString &connectionName)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    const bool ok = q.exec(QLatin1String(R"(
        CREATE TABLE IF NOT EXISTS mdi_web_result (
            web_query_key TEXT NOT NULL PRIMARY KEY,
            web_query     TEXT NOT NULL,
            web_results   TEXT NOT NULL,
            web_date      TEXT
        )
    )"));
    if (!ok)
        qWarning() << "MdiWebSearch::ensureTables:" << q.lastError().text();
    return ok;
}

bool MdiWebSearch::storeResults(const QString &connectionName, const QString &query, const QList<Result> &results)
{
    QJsonArray array;
    for (const Result &r : results)
        array.append(r.toJson());
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QLatin1String(R"(
        INSERT OR REPLACE INTO mdi_web_result (web_query_key, web_query, web_results, web_date)
        VALUES (:key, :query, :results, :date)
    )"));
    q.bindValue(":key", queryKey(query));
    q.bindValue(":query", query.simplified());
    q.bindValue(":results", QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    q.bindValue(":date", QDateTime::currentDateTime().toString(Qt::ISODate));
    return q.exec();
}

QList<MdiWebSearch::Result> MdiWebSearch::cachedResults(const QString &connectionName, const QString &query)
{
    QList<Result> list;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("SELECT web_results FROM mdi_web_result WHERE web_query_key = :key"));
    q.bindValue(":key", queryKey(query));
    if (q.exec() && q.next())
        for (const QJsonValue &v : QJsonDocument::fromJson(q.value(0).toString().toUtf8()).array())
            list << Result::fromJson(v.toObject());
    return list;
}

bool MdiWebSearch::isCached(const QString &connectionName, const QString &query)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("SELECT 1 FROM mdi_web_result WHERE web_query_key = :key"));
    q.bindValue(":key", queryKey(query));
    return q.exec() && q.next();
}
