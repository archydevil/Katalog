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
// File Name:   mdiproposals.cpp
// Purpose:     Inferred file information awaiting approval (fork only)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdiproposals.h"
#include "core/mdipeople.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QDateTime>
#include <QSet>
#include <QMap>
#include <algorithm>
#include <QDebug>

namespace {

QString nowIso()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

QString valueKey(const QString &field, const QString &value)
{
    return field + QLatin1Char('\x1f') + value.simplified().toLower();
}

QString sourcesJson(const QList<MdiProposals::Source> &sources)
{
    QJsonArray array;
    for (const MdiProposals::Source &s : sources)
        array.append(s.toJson());
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QList<MdiProposals::Source> sourcesFromJson(const QString &text)
{
    QList<MdiProposals::Source> list;
    for (const QJsonValue &v : QJsonDocument::fromJson(text.toUtf8()).array())
        list << MdiProposals::Source::fromJson(v.toObject());
    return list;
}

MdiProposals::Proposal readProposal(const QSqlQuery &q)
{
    MdiProposals::Proposal p;
    p.id         = q.value("proposal_id").toInt();
    p.catalogId  = q.value("proposal_catalog_id").toInt();
    p.fullPath   = q.value("proposal_full_path").toString();
    p.field      = q.value("proposal_field").toString();
    p.value      = q.value("proposal_value").toString();
    p.precision  = q.value("proposal_precision").toString();
    p.confidence = q.value("proposal_confidence").toDouble();
    p.sources    = sourcesFromJson(q.value("proposal_sources").toString());
    p.status     = q.value("proposal_status").toString();
    p.origin     = q.value("proposal_origin").toString();
    p.created    = q.value("proposal_created").toString();
    p.decided    = q.value("proposal_decided").toString();
    return p;
}

QString idList(const QList<int> &ids)
{
    QStringList parts;
    for (int id : ids)
        parts << QString::number(id);
    return parts.join(QLatin1Char(','));
}

// The owner's value of a field in the file's information
QString ownValue(const MdiPeople::Annotation &a, const QString &field)
{
    if (field == QLatin1String("date"))   return a.date;
    if (field == QLatin1String("event"))  return a.event;
    if (field == QLatin1String("place"))  return a.place;
    if (field == QLatin1String("author")) return a.author;
    if (field == QLatin1String("title"))  return a.title;
    return QString();
}

// Writes or refreshes one file's proposals of an origin on an open transaction
bool replaceOne(QSqlDatabase &db, int catalogId, const QString &fullPath, const QString &origin,
                const QList<MdiProposals::Proposal> &proposals, int *pending)
{
    QSqlQuery read(db);
    read.prepare(QLatin1String(R"(
        SELECT proposal_id, proposal_field, proposal_value, proposal_status, proposal_origin
        FROM mdi_proposal WHERE proposal_catalog_id = :catalog_id AND proposal_full_path = :full_path
    )"));
    read.bindValue(":catalog_id", catalogId);
    read.bindValue(":full_path", fullPath);
    if (!read.exec())
        return false;
    struct Existing { int id; QString status; QString origin; };
    QHash<QString, Existing> existing;
    while (read.next())
        existing.insert(valueKey(read.value(1).toString(), read.value(2).toString()),
                        Existing{read.value(0).toInt(), read.value(3).toString(), read.value(4).toString()});

    QSqlQuery update(db);
    update.prepare(QLatin1String(R"(
        UPDATE mdi_proposal SET proposal_precision = :precision, proposal_confidence = :confidence,
               proposal_sources = :sources, proposal_value = :value
        WHERE proposal_id = :id
    )"));
    QSqlQuery insert(db);
    insert.prepare(QLatin1String(R"(
        INSERT OR IGNORE INTO mdi_proposal (proposal_catalog_id, proposal_full_path, proposal_field,
            proposal_value, proposal_value_key, proposal_precision, proposal_confidence, proposal_sources,
            proposal_status, proposal_origin, proposal_created)
        VALUES (:catalog_id, :full_path, :field, :value, :value_key, :precision, :confidence, :sources,
                'pending', :origin, :created)
    )"));

    QSet<QString> kept;
    const QString created = nowIso();
    for (const MdiProposals::Proposal &p : proposals) {
        if (p.value.trimmed().isEmpty() || !MdiProposals::fields().contains(p.field))
            continue;
        const QString key = valueKey(p.field, p.value);
        if (kept.contains(key))
            continue;
        kept.insert(key);
        const auto found = existing.constFind(key);
        if (found != existing.constEnd()) {
            // Decided by the owner: never proposed again, never changed
            if (found->status != QLatin1String("pending"))
                continue;
            update.bindValue(":precision",  p.precision);
            update.bindValue(":confidence", p.confidence);
            update.bindValue(":sources",    sourcesJson(p.sources));
            update.bindValue(":value",      p.value.simplified());
            update.bindValue(":id",         found->id);
            if (!update.exec())
                return false;
        } else {
            insert.bindValue(":catalog_id", catalogId);
            insert.bindValue(":full_path",  fullPath);
            insert.bindValue(":field",      p.field);
            insert.bindValue(":value",      p.value.simplified());
            insert.bindValue(":value_key",  key);
            insert.bindValue(":precision",  p.precision);
            insert.bindValue(":confidence", p.confidence);
            insert.bindValue(":sources",    sourcesJson(p.sources));
            insert.bindValue(":origin",     origin);
            insert.bindValue(":created",    created);
            if (!insert.exec())
                return false;
        }
        if (pending)
            ++*pending;
    }

    // Pending proposals of this origin that the evidence no longer supports
    QList<int> stale;
    for (auto it = existing.cbegin(); it != existing.cend(); ++it)
        if (it->status == QLatin1String("pending") && it->origin == origin && !kept.contains(it.key()))
            stale << it->id;
    if (!stale.isEmpty()) {
        QSqlQuery del(db);
        if (!del.exec(QStringLiteral("DELETE FROM mdi_proposal WHERE proposal_id IN (%1)").arg(idList(stale))))
            return false;
    }
    return true;
}

} // namespace

const QStringList &MdiProposals::fields()
{
    static const QStringList list{QStringLiteral("date"), QStringLiteral("event"), QStringLiteral("place"),
                                  QStringLiteral("author"), QStringLiteral("title")};
    return list;
}

QJsonObject MdiProposals::Source::toJson() const
{
    QJsonObject o{{"kind", kind}, {"label", label}, {"detail", detail}};
    if (!url.isEmpty())
        o["url"] = url;
    if (confidence > 0)
        o["confidence"] = qRound(confidence * 100) / 100.0;
    return o;
}

MdiProposals::Source MdiProposals::Source::fromJson(const QJsonObject &o)
{
    Source s;
    s.kind       = o.value("kind").toString();
    s.label      = o.value("label").toString();
    s.detail     = o.value("detail").toString();
    s.url        = o.value("url").toString();
    s.confidence = o.value("confidence").toDouble();
    return s;
}

QVariantMap MdiProposals::Proposal::toVariant() const
{
    QVariantList list;
    for (const Source &s : sources)
        list << s.toJson().toVariantMap();
    return QVariantMap{{"proposalId", id}, {"catalogId", catalogId}, {"fullPath", fullPath},
                       {"field", field}, {"value", value}, {"precision", precision},
                       {"confidence", qRound(confidence * 100) / 100.0}, {"status", status},
                       {"origin", origin}, {"sources", list}};
}

bool MdiProposals::ensureTables(const QString &connectionName)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    // value_key: the value simplified and lower case, so the same value
    // proposed again is recognised (and a rejected one stays rejected)
    const bool ok = q.exec(QLatin1String(R"(
        CREATE TABLE IF NOT EXISTS mdi_proposal (
            proposal_id          INTEGER PRIMARY KEY AUTOINCREMENT,
            proposal_catalog_id  INTEGER NOT NULL,
            proposal_full_path   TEXT    NOT NULL,
            proposal_field       TEXT    NOT NULL,
            proposal_value       TEXT    NOT NULL,
            proposal_value_key   TEXT    NOT NULL,
            proposal_precision   TEXT,
            proposal_confidence  REAL    NOT NULL DEFAULT 0,
            proposal_sources     TEXT,
            proposal_status      TEXT    NOT NULL DEFAULT 'pending',
            proposal_origin      TEXT,
            proposal_created     TEXT,
            proposal_decided     TEXT,
            UNIQUE (proposal_catalog_id, proposal_full_path, proposal_value_key)
        )
    )"));
    if (!ok)
        qWarning() << "MdiProposals::ensureTables:" << q.lastError().text();
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS mdi_proposal_file ON mdi_proposal (proposal_catalog_id, proposal_full_path)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS mdi_proposal_status ON mdi_proposal (proposal_catalog_id, proposal_status)"));
    return ok;
}

bool MdiProposals::replaceForFile(const QString &connectionName, int catalogId, const QString &fullPath,
                                  const QString &origin, const QList<Proposal> &proposals)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.transaction())
        return false;
    if (!replaceOne(db, catalogId, fullPath, origin, proposals, nullptr)) {
        qWarning() << "MdiProposals::replaceForFile failed:" << db.lastError().text();
        db.rollback();
        return false;
    }
    return db.commit();
}

int MdiProposals::replaceForFiles(const QString &connectionName, int catalogId, const QString &origin,
                                  const QHash<QString, QList<Proposal>> &byFile)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.transaction())
        return -1;
    int pending = 0;
    for (auto it = byFile.cbegin(); it != byFile.cend(); ++it) {
        if (!replaceOne(db, catalogId, it.key(), origin, it.value(), &pending)) {
            qWarning() << "MdiProposals::replaceForFiles failed:" << it.key();
            db.rollback();
            return -1;
        }
    }
    if (!db.commit()) {
        db.rollback();
        return -1;
    }
    return pending;
}

QList<MdiProposals::Proposal> MdiProposals::forFile(const QString &connectionName, int catalogId,
                                                    const QString &fullPath)
{
    QList<Proposal> list;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QLatin1String(R"(
        SELECT * FROM mdi_proposal WHERE proposal_catalog_id = :catalog_id AND proposal_full_path = :full_path
        ORDER BY proposal_field, proposal_confidence DESC
    )"));
    q.bindValue(":catalog_id", catalogId);
    q.bindValue(":full_path", fullPath);
    if (q.exec())
        while (q.next())
            list << readProposal(q);
    // Field order of the information form
    std::stable_sort(list.begin(), list.end(), [](const Proposal &a, const Proposal &b) {
        return fields().indexOf(a.field) < fields().indexOf(b.field);
    });
    return list;
}

QList<MdiProposals::Proposal> MdiProposals::byIds(const QString &connectionName, const QList<int> &ids)
{
    QList<Proposal> list;
    if (ids.isEmpty())
        return list;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    if (q.exec(QStringLiteral("SELECT * FROM mdi_proposal WHERE proposal_id IN (%1)").arg(idList(ids))))
        while (q.next())
            list << readProposal(q);
    return list;
}

QVariantList MdiProposals::forFolder(const QString &connectionName, int catalogId, const QString &folderPath,
                                     bool recursive)
{
    QString folder = folderPath;
    while (folder.size() > 1 && folder.endsWith(QLatin1Char('/')))
        folder.chop(1);
    const QString prefix = folder.endsWith(QLatin1Char('/')) ? folder : folder + QLatin1Char('/');

    QSqlQuery q(QSqlDatabase::database(connectionName));
    // substr instead of LIKE: paths may hold % and _
    q.prepare(QLatin1String(R"(
        SELECT * FROM mdi_proposal
        WHERE proposal_catalog_id = :catalog_id AND proposal_status = 'pending'
          AND substr(proposal_full_path, 1, :len) = :prefix
    )"));
    q.bindValue(":catalog_id", catalogId);
    q.bindValue(":len", prefix.size());
    q.bindValue(":prefix", prefix);

    struct Group {
        QString field, value, precision;
        double  sum = 0, min = 1;
        int     files = 0;
        QVariantList ids;
        QVariantList sources;
        QSet<QString> sourceKeys;
    };
    QMap<QString, Group> groups;
    QStringList order;
    if (q.exec()) {
        while (q.next()) {
            const Proposal p = readProposal(q);
            if (!recursive && p.fullPath.mid(prefix.size()).contains(QLatin1Char('/')))
                continue;
            const QString key = valueKey(p.field, p.value) + QLatin1Char('\x1f') + p.precision;
            Group &g = groups[key];
            if (g.field.isEmpty()) {
                g.field = p.field;
                g.value = p.value;
                g.precision = p.precision;
                order << key;
            }
            g.sum += p.confidence;
            g.min = std::min(g.min, p.confidence);
            ++g.files;
            g.ids << p.id;
            for (const Source &s : p.sources) {
                // The same folder or document for many files: listed once
                const QString sk = s.kind + QLatin1Char('\x1f') + s.label;
                if (g.sourceKeys.contains(sk) || g.sources.size() >= 12)
                    continue;
                g.sourceKeys.insert(sk);
                g.sources << s.toJson().toVariantMap();
            }
        }
    }
    QList<Group> sorted;
    for (const QString &key : order)
        sorted << groups.value(key);
    // Field order, then the values that cover the most files
    std::stable_sort(sorted.begin(), sorted.end(), [](const Group &a, const Group &b) {
        const int fa = fields().indexOf(a.field), fb = fields().indexOf(b.field);
        if (fa != fb)
            return fa < fb;
        if (a.files != b.files)
            return a.files > b.files;
        return a.sum / a.files > b.sum / b.files;
    });
    QVariantList out;
    for (const Group &g : sorted)
        out << QVariantMap{{"field", g.field}, {"value", g.value}, {"precision", g.precision},
                           {"confidence", qRound(g.sum / g.files * 100) / 100.0},
                           {"minConfidence", qRound(g.min * 100) / 100.0},
                           {"fileCount", g.files}, {"proposalIds", g.ids}, {"sources", g.sources}};
    return out;
}

QVariantMap MdiProposals::pendingCounts(const QString &connectionName, int catalogId)
{
    QVariantMap counts;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QLatin1String(R"(
        SELECT proposal_field, COUNT(*) FROM mdi_proposal
        WHERE proposal_catalog_id = :catalog_id AND proposal_status = 'pending'
        GROUP BY proposal_field
    )"));
    q.bindValue(":catalog_id", catalogId);
    if (q.exec())
        while (q.next())
            counts.insert(q.value(0).toString(), q.value(1).toInt());
    return counts;
}

bool MdiProposals::setStatus(const QString &connectionName, const QList<int> &ids, const QString &status)
{
    if (ids.isEmpty())
        return true;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("UPDATE mdi_proposal SET proposal_status = :status, proposal_decided = :decided "
                             "WHERE proposal_id IN (%1)").arg(idList(ids)));
    q.bindValue(":status", status);
    q.bindValue(":decided", status == QLatin1String("pending") ? QString() : nowIso());
    return q.exec();
}

MdiProposals::AcceptResult MdiProposals::acceptAndApply(const QString &connectionName, const QList<int> &ids,
                                                        bool replaceOwnValues)
{
    AcceptResult result;
    QList<Proposal> chosen = byIds(connectionName, ids);
    // Most confident first: of two values for one field of one file, the first wins
    std::stable_sort(chosen.begin(), chosen.end(), [](const Proposal &a, const Proposal &b) {
        return a.confidence > b.confidence;
    });

    QSet<QString> filled;           // catalog/path/field written in this call
    QList<int> accepted, superseded;
    for (const Proposal &p : std::as_const(chosen)) {
        if (p.status == QLatin1String("accepted"))
            continue;
        const QString slot = QString::number(p.catalogId) + QLatin1Char('\x1f') + p.fullPath
                           + QLatin1Char('\x1f') + p.field;
        if (filled.contains(slot)) {
            superseded << p.id;    // another value was just accepted for this field
            continue;
        }
        const MdiPeople::Annotation own = MdiPeople::annotation(connectionName, p.catalogId, p.fullPath);
        const QString current = ownValue(own, p.field).trimmed();
        if (!current.isEmpty() && current.compare(p.value.trimmed(), Qt::CaseInsensitive) != 0
            && !replaceOwnValues) {
            ++result.kept;          // the owner's value stays; the proposal waits
            continue;
        }
        if (current.compare(p.value.trimmed(), Qt::CaseInsensitive) != 0) {
            const MdiPeople::FileRef ref{p.catalogId, p.fullPath};
            if (!MdiPeople::updateAnnotations(connectionName, {ref}, QVariantMap{{p.field, p.value}})) {
                ++result.failed;
                result.ok = false;
                continue;
            }
        }
        filled.insert(slot);
        accepted << p.id;
        ++result.applied;
    }
    if (!setStatus(connectionName, accepted, QStringLiteral("accepted"))
        || !setStatus(connectionName, superseded, QStringLiteral("superseded")))
        result.ok = false;
    // The other pending values of a field just filled are settled too
    // ("superseded": not proposed again, told apart from a rejection)
    if (!filled.isEmpty()) {
        QSqlQuery q(QSqlDatabase::database(connectionName));
        q.prepare(QLatin1String(R"(
            UPDATE mdi_proposal SET proposal_status = 'superseded', proposal_decided = :decided
            WHERE proposal_catalog_id = :catalog_id AND proposal_full_path = :full_path
              AND proposal_field = :field AND proposal_status = 'pending'
        )"));
        for (const QString &slot : std::as_const(filled)) {
            const QStringList parts = slot.split(QLatin1Char('\x1f'));
            q.bindValue(":decided", nowIso());
            q.bindValue(":catalog_id", parts.value(0).toInt());
            q.bindValue(":full_path", parts.value(1));
            q.bindValue(":field", parts.value(2));
            q.exec();
        }
    }
    return result;
}

bool MdiProposals::clearCatalog(const QString &connectionName, int catalogId)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("DELETE FROM mdi_proposal WHERE proposal_catalog_id = :catalog_id"));
    q.bindValue(":catalog_id", catalogId);
    return q.exec();
}

QString MdiProposals::moveStatement()
{
    return QStringLiteral("UPDATE OR IGNORE mdi_proposal SET proposal_catalog_id = :new_id "
                          "WHERE proposal_catalog_id = :old_id AND proposal_full_path = :full_path");
}
