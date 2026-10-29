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
// File Name:   mdicontext.cpp
// Purpose:     Domain context of a collection (fork only, D33)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdicontext.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHostAddress>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QTimer>
#include <QUrl>
#include <QDebug>

namespace {

constexpr int kPreambleTerms   = 60;     // preferred terms in a perception prompt
constexpr int kPromptListChars = 6000;   // glossary or entities in a classification prompt

QStringList strings(const QJsonValue &value)
{
    QStringList list;
    for (const QJsonValue &v : value.toArray()) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty())
            list << s;
    }
    return list;
}

QJsonArray toArray(const QStringList &list)
{
    QJsonArray a;
    for (const QString &s : list)
        a.append(s);
    return a;
}

QString text(const QJsonObject &o, const char *key)
{
    return o.value(QLatin1String(key)).toString().trimmed();
}

// The brief: who, when, what, in which style
QString brief(const QJsonObject &context)
{
    QStringList lines;
    if (!text(context, "organisation").isEmpty()) lines << QStringLiteral("Organisation: ") + text(context, "organisation");
    if (!text(context, "period").isEmpty())       lines << QStringLiteral("Period: ") + text(context, "period");
    if (!text(context, "domain").isEmpty())       lines << QStringLiteral("Domain: ") + text(context, "domain");
    if (!text(context, "style").isEmpty())        lines << QStringLiteral("Narrative style and terminology: ") + text(context, "style");
    if (!text(context, "notes").isEmpty())        lines << QStringLiteral("Notes: ") + text(context, "notes");
    return lines.join('\n');
}

// "preferred (also: alias, alias) — definition"
QString glossaryLines(const QJsonObject &context, int maxChars, bool withDefinitions)
{
    QString out;
    for (const QJsonValue &v : context.value("glossary").toArray()) {
        const QJsonObject g = v.toObject();
        const QString preferred = text(g, "preferred").isEmpty() ? text(g, "term") : text(g, "preferred");
        if (preferred.isEmpty())
            continue;
        QStringList also = strings(g.value("aliases"));
        if (!text(g, "term").isEmpty() && text(g, "term") != preferred)
            also.prepend(text(g, "term"));
        QString line = QStringLiteral("- ") + preferred;
        if (!also.isEmpty())
            line += QStringLiteral(" (also: ") + also.join(QStringLiteral(", ")) + ')';
        if (withDefinitions && !text(g, "definition").isEmpty())
            line += QStringLiteral(" — ") + text(g, "definition");
        if (out.size() + line.size() > maxChars)
            break;
        out += line + '\n';
    }
    return out;
}

QString entityLines(const QJsonObject &context, int maxChars)
{
    QString out;
    for (const QJsonValue &v : context.value("entities").toArray()) {
        const QJsonObject e = v.toObject();
        if (text(e, "name").isEmpty())
            continue;
        QString line = QStringLiteral("- ") + text(e, "name");
        if (!text(e, "kind").isEmpty())
            line += QStringLiteral(" [") + text(e, "kind") + ']';
        const QStringList aliases = strings(e.value("aliases"));
        if (!aliases.isEmpty())
            line += QStringLiteral(" (also: ") + aliases.join(QStringLiteral(", ")) + ')';
        if (!text(e, "notes").isEmpty())
            line += QStringLiteral(" — ") + text(e, "notes");
        if (out.size() + line.size() > maxChars)
            break;
        out += line + '\n';
    }
    return out;
}

// Canonical form of a value: a closed category's allowed value, a glossary's
// preferred term or an entity's name, matched without case
QString canonical(const QString &value, const QHash<QString, QString> &forms)
{
    return forms.value(value.trimmed().toLower(), value.trimmed());
}

QHash<QString, QString> termForms(const QJsonObject &context)
{
    QHash<QString, QString> forms;
    for (const QJsonValue &v : context.value("glossary").toArray()) {
        const QJsonObject g = v.toObject();
        const QString preferred = text(g, "preferred").isEmpty() ? text(g, "term") : text(g, "preferred");
        if (preferred.isEmpty())
            continue;
        const QStringList names = strings(g.value("aliases")) + QStringList{text(g, "term"), preferred};
        for (const QString &alias : names)
            if (!alias.isEmpty())
                forms.insert(alias.toLower(), preferred);
    }
    for (const QJsonValue &v : context.value("entities").toArray()) {
        const QJsonObject e = v.toObject();
        if (text(e, "name").isEmpty())
            continue;
        const QStringList names = strings(e.value("aliases")) + QStringList{text(e, "name")};
        for (const QString &alias : names)
            forms.insert(alias.toLower(), text(e, "name"));
    }
    return forms;
}

bool isLocal(const QUrl &url)
{
    const QString host = url.host();
    return host == QLatin1String("localhost") || QHostAddress(host).isLoopback();
}

} // namespace

//----------------------------------------------------------------------
QJsonObject MdiContext::load(const QString &connectionName, int scope)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("SELECT context_json FROM mdi_context WHERE context_scope = :scope"));
    q.bindValue(":scope", scope);
    if (!q.exec() || !q.next())
        return QJsonObject();
    return QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object();
}

bool MdiContext::save(const QString &connectionName, int scope, const QJsonObject &context)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO mdi_context (context_scope, context_json, context_version, context_date) "
                             "VALUES (:scope, :json, :version, :date)"));
    q.bindValue(":scope",   scope);
    q.bindValue(":json",    QString::fromUtf8(QJsonDocument(context).toJson(QJsonDocument::Compact)));
    q.bindValue(":version", version(context));
    q.bindValue(":date",    QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) {
        qWarning() << "MdiContext::save failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool MdiContext::remove(const QString &connectionName, int scope)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("DELETE FROM mdi_context WHERE context_scope = :scope"));
    q.bindValue(":scope", scope);
    return q.exec();
}

QString MdiContext::version(const QJsonObject &context)
{
    if (context.isEmpty())
        return QString();
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(context).toJson(QJsonDocument::Compact),
                                                        QCryptographicHash::Sha1).toHex().left(12));
}

bool MdiContext::isEmpty(const QJsonObject &context)
{
    for (const char *key : {"organisation", "period", "domain", "notes"})
        if (!text(context, key).isEmpty())
            return false;
    for (const char *key : {"glossary", "categories", "entities"})
        if (!context.value(QLatin1String(key)).toArray().isEmpty())
            return false;
    return true;
}

QString MdiContext::language(const QJsonObject &context, const QString &fallback)
{
    const QString l = text(context, "language");
    return l.isEmpty() ? fallback : l;
}

QString MdiContext::preamble(const QJsonObject &context, const QString &catalogNotes)
{
    if (isEmpty(context))
        return QString();
    QString out = QStringLiteral("CONTEXT: the files belong to this archive.\n") + brief(context) + '\n';
    if (!catalogNotes.trimmed().isEmpty())
        out += QStringLiteral("This disk or catalog: ") + catalogNotes.trimmed() + '\n';
    // Preferred terms only: the definitions stay for the classification
    QString terms;
    int n = 0;
    for (const QJsonValue &v : context.value("glossary").toArray()) {
        const QJsonObject g = v.toObject();
        const QString preferred = text(g, "preferred").isEmpty() ? text(g, "term") : text(g, "preferred");
        if (preferred.isEmpty() || ++n > kPreambleTerms)
            continue;
        terms += (terms.isEmpty() ? QString() : QStringLiteral(", ")) + preferred;
    }
    if (!terms.isEmpty())
        out += QStringLiteral("Use these terms when they apply: ") + terms + '\n';
    return out + '\n';
}

QString MdiContext::whisperHint(const QJsonObject &context)
{
    if (!context.value("transcriptHint").toBool(false))
        return QString();
    QStringList words;
    if (!text(context, "organisation").isEmpty())
        words << text(context, "organisation");
    for (const QJsonValue &v : context.value("glossary").toArray()) {
        const QJsonObject g = v.toObject();
        const QString preferred = text(g, "preferred").isEmpty() ? text(g, "term") : text(g, "preferred");
        if (!preferred.isEmpty())
            words << preferred;
    }
    for (const QJsonValue &v : context.value("entities").toArray())
        if (!text(v.toObject(), "name").isEmpty())
            words << text(v.toObject(), "name");
    // whisper.cpp keeps about 220 tokens of prompt
    QString hint = words.join(QStringLiteral(", "));
    if (hint.size() > 600)
        hint = hint.left(600).section(QStringLiteral(", "), 0, -2);
    return hint;
}

QString MdiContext::classificationPrompt(const QJsonObject &context, const QString &catalogNotes,
                                         const QString &fileFacts, const QStringList &passages,
                                         const QString &language)
{
    QString categories;
    QJsonObject shape;
    for (const QJsonValue &v : context.value("categories").toArray()) {
        const QJsonObject c = v.toObject();
        const QString name = text(c, "name");
        if (name.isEmpty())
            continue;
        const QStringList values = strings(c.value("values"));
        const bool closed   = c.value("closed").toBool(false) && !values.isEmpty();
        const bool multiple = c.value("multiple").toBool(true);
        categories += QStringLiteral("- ") + name;
        if (!text(c, "description").isEmpty())
            categories += QStringLiteral(": ") + text(c, "description");
        if (!values.isEmpty())
            categories += (closed ? QStringLiteral(" ONLY one of: ") : QStringLiteral(" e.g. ")) + values.join(QStringLiteral(" | "));
        categories += multiple ? QStringLiteral(" (one or more values)") : QStringLiteral(" (a single value)");
        categories += '\n';
        shape.insert(name, QJsonArray{QStringLiteral("value")});
    }

    QString prompt = QStringLiteral(
        "You are the archivist of the archive described below. Interpret ONE file of the archive with the "
        "archive's own terminology and narrative style. Rely on the facts given; use the reference passages "
        "only when they clearly concern this file; when something is not supported, leave it out rather "
        "than guess.\n\nARCHIVE:\n") + brief(context) + '\n';
    if (!catalogNotes.trimmed().isEmpty())
        prompt += QStringLiteral("This disk or catalog: ") + catalogNotes.trimmed() + '\n';
    const QString glossary = glossaryLines(context, kPromptListChars, true);
    if (!glossary.isEmpty())
        prompt += QStringLiteral("\nGLOSSARY (write the first form):\n") + glossary;
    const QString entities = entityLines(context, kPromptListChars);
    if (!entities.isEmpty())
        prompt += QStringLiteral("\nKNOWN PEOPLE, PLACES, EVENTS AND BODIES (write the first form):\n") + entities;
    if (!categories.isEmpty())
        prompt += QStringLiteral("\nCATEGORIES to assign (leave a category empty when unknown):\n") + categories;
    if (!passages.isEmpty())
        prompt += QStringLiteral("\nREFERENCE PASSAGES:\n") + passages.join('\n') + '\n';
    prompt += QStringLiteral("\nFILE:\n") + fileFacts + '\n';

    const QString answerShape = QString::fromUtf8(QJsonDocument(QJsonObject{
        {"title",       QStringLiteral("short title")},
        {"description", QStringLiteral("2 to 4 sentences")},
        {"categories",  shape},
        {"tags",        QJsonArray{QStringLiteral("3 to 10 keywords")}},
        {"entities",    QJsonArray{QStringLiteral("names of people, places, events")}},
        {"sources",     QJsonArray{1}}}).toJson(QJsonDocument::Compact));
    prompt += QStringLiteral("\nWrite in %1. Answer as JSON only: %2 — \"sources\" lists the numbers of the "
                             "reference passages you used.").arg(language, answerShape);
    return prompt;
}

QJsonObject MdiContext::normaliseClassification(const QJsonObject &answer, const QJsonObject &context)
{
    const QHash<QString, QString> forms = termForms(context);
    QJsonObject out;
    out["title"]       = text(answer, "title");
    out["description"] = text(answer, "description");

    const QJsonObject given = answer.value("categories").toObject();
    QJsonObject categories;
    for (const QJsonValue &v : context.value("categories").toArray()) {
        const QJsonObject c = v.toObject();
        const QString name = text(c, "name");
        if (name.isEmpty())
            continue;
        // The model may change the key's case
        QJsonValue raw = given.value(name);
        for (auto it = given.begin(); raw.isUndefined() && it != given.end(); ++it)
            if (it.key().compare(name, Qt::CaseInsensitive) == 0)
                raw = it.value();
        QStringList values = raw.isString() ? QStringList{raw.toString()} : strings(raw);

        QHash<QString, QString> allowed;
        for (const QString &a : strings(c.value("values")))
            allowed.insert(a.toLower(), a);
        const bool closed = c.value("closed").toBool(false) && !allowed.isEmpty();
        QStringList kept;
        for (const QString &value : values) {
            const QString form = allowed.value(value.trimmed().toLower(), canonical(value, forms));
            if (form.isEmpty() || (closed && !allowed.contains(form.toLower())) || kept.contains(form))
                continue;
            kept << form;
        }
        if (!c.value("multiple").toBool(true) && kept.size() > 1)
            kept = kept.mid(0, 1);
        if (!kept.isEmpty())
            categories.insert(name, toArray(kept));
    }
    out["categories"] = categories;

    auto canonicalList = [&forms](const QJsonValue &value) {
        QStringList list;
        for (const QString &s : strings(value)) {
            const QString form = canonical(s, forms);
            if (!list.contains(form, Qt::CaseInsensitive))
                list << form;
        }
        return toArray(list);
    };
    out["tags"]     = canonicalList(answer.value("tags"));
    out["entities"] = canonicalList(answer.value("entities"));
    QJsonArray sources;
    for (const QJsonValue &v : answer.value("sources").toArray())
        if (v.toInt() > 0)
            sources.append(v.toInt());
    out["sources"] = sources;
    return out;
}

QString MdiContext::draftPrompt(const QStringList &folders, const QStringList &samples, const QString &language)
{
    const QString shape = QString::fromUtf8(QJsonDocument(QJsonObject{
        {"organisation", QStringLiteral("")}, {"period", QStringLiteral("")}, {"domain", QStringLiteral("")},
        {"style", QStringLiteral("")},
        {"glossary",   QJsonArray{QJsonObject{{"term", ""}, {"preferred", ""}, {"aliases", QJsonArray{}}, {"definition", ""}}}},
        {"categories", QJsonArray{QJsonObject{{"name", ""}, {"description", ""}, {"values", QJsonArray{}},
                                              {"closed", false}, {"multiple", true}}}},
        {"entities",   QJsonArray{QJsonObject{{"name", ""}, {"kind", "person|place|event|organisation|other"},
                                              {"aliases", QJsonArray{}}, {"notes", ""}}}}}).toJson(QJsonDocument::Compact));
    return QStringLiteral(
        "Below are the folder names of a disk and a sample of descriptions of its files. Propose the domain "
        "context an archivist would use to classify this archive: the organisation, the period, the domain, "
        "the narrative style and terminology, a glossary of the specialised terms and abbreviations you see, "
        "4 to 8 categories useful to classify the files (with their typical values), and the people, places, "
        "events and bodies that recur. Only propose what the names and descriptions support; leave a field "
        "empty when unknown and do not invent facts. Write in %1. Answer as JSON only: %2\n\nFOLDERS:\n%3"
        "\n\nDESCRIPTIONS:\n%4")
        .arg(language, shape, folders.join('\n'), samples.join('\n'));
}

QJsonObject MdiContext::parseDraft(const QJsonObject &answer)
{
    QJsonObject draft;
    for (const char *key : {"organisation", "period", "domain", "style"})
        draft.insert(QLatin1String(key), text(answer, key));

    QJsonArray glossary;
    for (const QJsonValue &v : answer.value("glossary").toArray()) {
        const QJsonObject g = v.toObject();
        if (text(g, "term").isEmpty() && text(g, "preferred").isEmpty())
            continue;
        glossary.append(QJsonObject{{"term", text(g, "term")}, {"preferred", text(g, "preferred")},
                                    {"aliases", toArray(strings(g.value("aliases")))},
                                    {"definition", text(g, "definition")}});
    }
    QJsonArray categories;
    for (const QJsonValue &v : answer.value("categories").toArray()) {
        const QJsonObject c = v.toObject();
        if (text(c, "name").isEmpty())
            continue;
        categories.append(QJsonObject{{"name", text(c, "name")}, {"description", text(c, "description")},
                                      {"values", toArray(strings(c.value("values")))},
                                      {"closed", c.value("closed").toBool(false)},
                                      {"multiple", c.value("multiple").toBool(true)}});
    }
    QJsonArray entities;
    static const QStringList kinds = {"person", "place", "event", "organisation", "other"};
    for (const QJsonValue &v : answer.value("entities").toArray()) {
        const QJsonObject e = v.toObject();
        if (text(e, "name").isEmpty())
            continue;
        const QString kind = text(e, "kind").toLower();
        entities.append(QJsonObject{{"name", text(e, "name")},
                                    {"kind", kinds.contains(kind) ? kind : QStringLiteral("other")},
                                    {"aliases", toArray(strings(e.value("aliases")))},
                                    {"notes", text(e, "notes")}});
    }
    draft["glossary"]   = glossary;
    draft["categories"] = categories;
    draft["entities"]   = entities;
    return draft;
}

QJsonObject MdiContext::askText(const QString &ollamaUrl, const QString &model, const QString &prompt,
                                QString *error, int timeoutMs, const std::atomic<bool> *stop)
{
    // Local endpoints only, as for the enrichment job (D3)
    if (!isLocal(QUrl(ollamaUrl))) {
        *error = QStringLiteral("endpoint is not local");
        return {};
    }
    QJsonObject body;
    body["model"]   = model;
    body["prompt"]  = prompt;
    body["format"]  = "json";
    body["stream"]  = false;
    body["think"]   = false;
    body["options"] = QJsonObject{{"temperature", 0.2}, {"num_ctx", 16384}, {"num_predict", 4096}};

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(ollamaUrl + QStringLiteral("/api/generate")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = manager.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

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

    QJsonObject result;
    if (reply->error() != QNetworkReply::NoError) {
        *error = (stop && stop->load()) ? QStringLiteral("stopped")
               : clock.elapsed() > timeoutMs ? QStringLiteral("time limit reached")
               : reply->errorString();
    } else {
        const QString answer = QJsonDocument::fromJson(reply->readAll()).object().value("response").toString();
        result = QJsonDocument::fromJson(answer.toUtf8()).object();
        if (result.isEmpty()) {
            const int first = answer.indexOf('{'), last = answer.lastIndexOf('}');
            if (first >= 0 && last > first)
                result = QJsonDocument::fromJson(answer.mid(first, last - first + 1).toUtf8()).object();
        }
        if (result.isEmpty())
            *error = QStringLiteral("answer is not JSON");
    }
    reply->deleteLater();
    return result;
}
