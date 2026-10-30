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
// File Name:   mdipeople.cpp
// Purpose:     File information, people and faces (fork only, SpecMediaIntelligence.md)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdipeople.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QRegularExpression>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

QString now()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

QByteArray toBlob(const QVector<float> &v)
{
    return QByteArray(reinterpret_cast<const char *>(v.constData()), v.size() * int(sizeof(float)));
}

QVector<float> fromBlob(const QByteArray &blob)
{
    QVector<float> v(blob.size() / int(sizeof(float)));
    if (!v.isEmpty())
        std::memcpy(v.data(), blob.constData(), size_t(v.size()) * sizeof(float));
    return v;
}

QString tagsToJson(const QStringList &tags)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(tags)).toJson(QJsonDocument::Compact));
}

QStringList tagsFromJson(const QString &json)
{
    QStringList tags;
    for (const QJsonValue &v : QJsonDocument::fromJson(json.toUtf8()).array())
        if (!v.toString().trimmed().isEmpty())
            tags << v.toString().trimmed();
    return tags;
}

// A list from QML (array of strings) or a text ("a, b; c")
QStringList toTagList(const QVariant &value)
{
    QStringList raw;
    if (value.canConvert<QVariantList>() && value.typeId() != QMetaType::QString)
        for (const QVariant &v : value.toList())
            raw << v.toString();
    else
        raw = value.toString().split(QRegularExpression(QStringLiteral("[,;\\n]")), Qt::SkipEmptyParts);
    QStringList tags;
    QSet<QString> seen;
    for (const QString &t : raw) {
        const QString tag = t.simplified();
        if (!tag.isEmpty() && !seen.contains(tag.toCaseFolded())) {
            seen.insert(tag.toCaseFolded());
            tags << tag;
        }
    }
    return tags;
}

QString idList(const QList<int> &ids)
{
    QStringList list;
    for (int id : ids)
        list << QString::number(id);
    return list.join(',');
}

QList<int> parseIds(const QString &text)
{
    QList<int> ids;
    for (const QString &s : text.split(',', Qt::SkipEmptyParts))
        if (s.toInt() > 0)
            ids << s.toInt();
    return ids;
}

// Lower case, without accents: "Rossi" finds "ROSSI", "Nicolo" finds "Nicolò"
QString fold(const QString &text)
{
    const QString d = text.normalized(QString::NormalizationForm_KD);
    QString out;
    out.reserve(d.size());
    for (const QChar c : d)
        if (c.category() != QChar::Mark_NonSpacing)
            out += c.toCaseFolded();
    return out;
}

MdiPeople::Face readFace(const QSqlQuery &q)
{
    MdiPeople::Face f;
    f.id                = q.value("face_id").toInt();
    f.catalogId         = q.value("face_catalog_id").toInt();
    f.fullPath          = q.value("face_full_path").toString();
    f.x                 = q.value("face_x").toDouble();
    f.y                 = q.value("face_y").toDouble();
    f.w                 = q.value("face_w").toDouble();
    f.h                 = q.value("face_h").toDouble();
    f.score             = q.value("face_score").toDouble();
    f.embedding         = fromBlob(q.value("face_embedding").toByteArray());
    f.personId          = q.value("face_person_id").toInt();
    f.status            = q.value("face_status").toString();
    f.suggestedPersonId = q.value("face_suggested_person_id").toInt();
    f.suggestionScore   = q.value("face_suggestion_score").toDouble();
    f.rejectedPersons   = parseIds(q.value("face_rejected").toString());
    f.cropRoot          = q.value("face_crop_root").toString();
    f.cropFile          = q.value("face_crop_file").toString();
    f.created           = q.value("face_created").toString();
    return f;
}

QList<MdiPeople::Face> selectFaces(const QString &connectionName, const QString &where,
                                   const QVariantMap &binds = {}, const QString &tail = QString())
{
    QList<MdiPeople::Face> faces;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT * FROM mdi_face WHERE %1 %2").arg(where, tail));
    for (auto it = binds.cbegin(); it != binds.cend(); ++it)
        q.bindValue(it.key(), it.value());
    if (!q.exec()) {
        qWarning() << "MdiPeople: faces not read:" << q.lastError().text();
        return faces;
    }
    while (q.next())
        faces.append(readFace(q));
    return faces;
}

// Confirmed faces of each person (or of one person)
QHash<int, QList<QVector<float>>> confirmedEmbeddings(const QString &connectionName, int personId = 0)
{
    QHash<int, QList<QVector<float>>> byPerson;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT face_person_id, face_embedding FROM mdi_face "
                             "WHERE face_status = 'confirmed' AND face_person_id IS NOT NULL%1")
                  .arg(personId > 0 ? QStringLiteral(" AND face_person_id = :person") : QString()));
    if (personId > 0)
        q.bindValue(":person", personId);
    if (q.exec())
        while (q.next()) {
            const QVector<float> v = fromBlob(q.value(1).toByteArray());
            if (!v.isEmpty())
                byPerson[q.value(0).toInt()].append(v);
        }
    return byPerson;
}

double bestMatch(const QVector<float> &face, const QList<QVector<float>> &references)
{
    double best = -1.0;
    for (const QVector<float> &r : references)
        best = std::max(best, MdiPeople::cosine(face, r));
    return best;
}

double iou(const MdiPeople::Face &a, const MdiPeople::Face &b)
{
    const double x1 = std::max(a.x, b.x), y1 = std::max(a.y, b.y);
    const double x2 = std::min(a.x + a.w, b.x + b.w), y2 = std::min(a.y + a.h, b.y + b.h);
    const double inter = std::max(0.0, x2 - x1) * std::max(0.0, y2 - y1);
    const double uni = a.w * a.h + b.w * b.h - inter;
    return uni > 0 ? inter / uni : 0.0;
}

// The file's link to a person follows its confirmed faces: a link made by a
// face is kept while one face of that person is confirmed in the file
bool relink(QSqlDatabase &db, int catalogId, const QString &fullPath, int personId)
{
    if (personId <= 0)
        return true;
    QSqlQuery face(db);
    face.prepare(QStringLiteral("SELECT face_id FROM mdi_face WHERE face_catalog_id = :c AND face_full_path = :p "
                                "AND face_person_id = :person AND face_status = 'confirmed' ORDER BY face_score DESC LIMIT 1"));
    face.bindValue(":c", catalogId);
    face.bindValue(":p", fullPath);
    face.bindValue(":person", personId);
    if (!face.exec())
        return false;
    QSqlQuery q(db);
    if (face.next()) {
        q.prepare(QStringLiteral(R"(
            INSERT INTO mdi_file_person (fp_catalog_id, fp_full_path, fp_person_id, fp_face_id, fp_source, fp_created)
            VALUES (:c, :p, :person, :face, 'face', :now)
            ON CONFLICT (fp_catalog_id, fp_full_path, fp_person_id)
            DO UPDATE SET fp_face_id = excluded.fp_face_id, fp_source = 'face'
        )"));
        q.bindValue(":face", face.value(0));
        q.bindValue(":now", now());
    } else {
        q.prepare(QStringLiteral("DELETE FROM mdi_file_person WHERE fp_catalog_id = :c AND fp_full_path = :p "
                                 "AND fp_person_id = :person AND fp_source = 'face'"));
    }
    q.bindValue(":c", catalogId);
    q.bindValue(":p", fullPath);
    q.bindValue(":person", personId);
    return q.exec();
}

bool exec(QSqlDatabase &db, const QString &sql, const QVariantMap &binds = {})
{
    QSqlQuery q(db);
    q.prepare(sql);
    for (auto it = binds.cbegin(); it != binds.cend(); ++it)
        q.bindValue(it.key(), it.value());
    if (!q.exec()) {
        qWarning() << "MdiPeople:" << q.lastError().text() << sql.simplified().left(120);
        return false;
    }
    return true;
}

} // namespace

QString MdiPeople::Person::displayName() const
{
    return (firstName.trimmed() + QLatin1Char(' ') + lastName.trimmed()).trimmed();
}

bool MdiPeople::clearAllFaces(const QString &connectionName, QList<QPair<int, QString>> *files,
                              QStringList *crops)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery q(db);
    if (files && q.exec(QStringLiteral("SELECT DISTINCT fp_catalog_id, fp_full_path FROM mdi_file_person WHERE fp_source = 'face'")))
        while (q.next())
            files->append({q.value(0).toInt(), q.value(1).toString()});
    if (crops && q.exec(QStringLiteral("SELECT face_crop_root, face_crop_file FROM mdi_face WHERE face_crop_file IS NOT NULL AND face_crop_file <> ''")))
        while (q.next())
            crops->append(QDir(q.value(0).toString()).filePath(q.value(1).toString()));
    db.transaction();
    const bool ok = q.exec(QStringLiteral("DELETE FROM mdi_file_person WHERE fp_source = 'face'"))
                 && q.exec(QStringLiteral("DELETE FROM mdi_face"))
                 && q.exec(QStringLiteral("DELETE FROM mdi_datum WHERE datum_analysis = 'faces'"));
    if (!ok) {
        qWarning() << "MdiPeople::clearAllFaces failed:" << q.lastError().text();
        db.rollback();
        return false;
    }
    return db.commit();
}

QString MdiPeople::Face::cropPath() const
{
    return cropRoot.isEmpty() || cropFile.isEmpty() ? QString() : QDir(cropRoot).filePath(cropFile);
}

double MdiPeople::cosine(const QVector<float> &a, const QVector<float> &b)
{
    if (a.size() != b.size() || a.isEmpty())
        return -1.0;
    double dot = 0, na = 0, nb = 0;
    for (int i = 0; i < a.size(); ++i) {
        dot += double(a[i]) * double(b[i]);
        na  += double(a[i]) * double(a[i]);
        nb  += double(b[i]) * double(b[i]);
    }
    return na > 0 && nb > 0 ? dot / std::sqrt(na * nb) : -1.0;
}

//----------------------------------------------------------------------
bool MdiPeople::ensureTables(const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery q(db);
    const bool ok =
        q.exec(QLatin1String(R"(
            CREATE TABLE IF NOT EXISTS mdi_annotation (
                ann_catalog_id  INTEGER NOT NULL,
                ann_full_path   TEXT    NOT NULL,
                ann_title       TEXT,
                ann_description TEXT,
                ann_event       TEXT,
                ann_place       TEXT,
                ann_date        TEXT,
                ann_author      TEXT,
                ann_notes       TEXT,
                ann_tags        TEXT,
                ann_updated     TEXT,
                PRIMARY KEY (ann_catalog_id, ann_full_path)
            )
        )"))
        && q.exec(QLatin1String(R"(
            CREATE TABLE IF NOT EXISTS mdi_person (
                person_id           INTEGER PRIMARY KEY AUTOINCREMENT,
                person_first_name   TEXT,
                person_last_name    TEXT,
                person_role         TEXT,
                person_organisation TEXT,
                person_notes        TEXT,
                person_created      TEXT,
                person_updated      TEXT
            )
        )"))
        && q.exec(QLatin1String(R"(
            CREATE TABLE IF NOT EXISTS mdi_file_person (
                fp_catalog_id INTEGER NOT NULL,
                fp_full_path  TEXT    NOT NULL,
                fp_person_id  INTEGER NOT NULL,
                fp_face_id    INTEGER,
                fp_source     TEXT    NOT NULL DEFAULT 'manual',
                fp_created    TEXT,
                PRIMARY KEY (fp_catalog_id, fp_full_path, fp_person_id)
            )
        )"))
        && q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS mdi_file_person_person ON mdi_file_person (fp_person_id)"))
        && q.exec(QLatin1String(R"(
            CREATE TABLE IF NOT EXISTS mdi_face (
                face_id                  INTEGER PRIMARY KEY AUTOINCREMENT,
                face_catalog_id          INTEGER NOT NULL,
                face_full_path           TEXT    NOT NULL,
                face_x                   REAL,
                face_y                   REAL,
                face_w                   REAL,
                face_h                   REAL,
                face_score               REAL,
                face_embedding           BLOB,
                face_person_id           INTEGER,
                face_status              TEXT    NOT NULL DEFAULT 'unknown',
                face_suggested_person_id INTEGER,
                face_suggestion_score    REAL,
                face_rejected            TEXT,
                face_crop_root           TEXT,
                face_crop_file           TEXT,
                face_created             TEXT
            )
        )"))
        && q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS mdi_face_file ON mdi_face (face_catalog_id, face_full_path)"))
        && q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS mdi_face_person ON mdi_face (face_person_id)"))
        && q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS mdi_face_status ON mdi_face (face_status)"));
    if (!ok) {
        qWarning() << "MdiPeople::ensureTables failed:" << q.lastError().text();
        return false;
    }
    q.exec(QStringLiteral("UPDATE mdi_schema SET mdi_schema_version = %1 WHERE mdi_schema_version < %1").arg(kStoreVersion));
    return true;
}

//----------------------------------------------------------------------
// File information
//----------------------------------------------------------------------
MdiPeople::Annotation MdiPeople::annotation(const QString &connectionName, int catalogId, const QString &fullPath)
{
    Annotation a;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("SELECT * FROM mdi_annotation WHERE ann_catalog_id = :c AND ann_full_path = :p"));
    q.bindValue(":c", catalogId);
    q.bindValue(":p", fullPath);
    if (!q.exec() || !q.next())
        return a;
    a.exists      = true;
    a.title       = q.value("ann_title").toString();
    a.description = q.value("ann_description").toString();
    a.event       = q.value("ann_event").toString();
    a.place       = q.value("ann_place").toString();
    a.date        = q.value("ann_date").toString();
    a.author      = q.value("ann_author").toString();
    a.notes       = q.value("ann_notes").toString();
    a.tags        = tagsFromJson(q.value("ann_tags").toString());
    a.updated     = q.value("ann_updated").toString();
    return a;
}

bool MdiPeople::updateAnnotations(const QString &connectionName, const QList<FileRef> &files,
                                  const QVariantMap &fields)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.transaction())
        return false;
    const QStringList tagsAdd    = fields.contains(QStringLiteral("tagsAdd")) ? toTagList(fields.value(QStringLiteral("tagsAdd"))) : QStringList();
    const QStringList tagsRemove = fields.contains(QStringLiteral("tagsRemove")) ? toTagList(fields.value(QStringLiteral("tagsRemove"))) : QStringList();
    for (const FileRef &file : files) {
        Annotation a = annotation(connectionName, file.catalogId, file.fullPath);
        auto text = [&fields](const char *key, QString &target) {
            const QString k = QString::fromLatin1(key);
            if (fields.contains(k))
                target = fields.value(k).toString().trimmed();
        };
        text("title", a.title);
        text("description", a.description);
        text("event", a.event);
        text("place", a.place);
        text("date", a.date);
        text("author", a.author);
        text("notes", a.notes);
        if (fields.contains(QStringLiteral("tags")))
            a.tags = toTagList(fields.value(QStringLiteral("tags")));
        for (const QString &t : tagsAdd)
            if (std::none_of(a.tags.cbegin(), a.tags.cend(), [&t](const QString &x) { return x.compare(t, Qt::CaseInsensitive) == 0; }))
                a.tags << t;
        for (const QString &t : tagsRemove)
            a.tags.removeIf([&t](const QString &x) { return x.compare(t, Qt::CaseInsensitive) == 0; });

        const bool empty = a.title.isEmpty() && a.description.isEmpty() && a.event.isEmpty() && a.place.isEmpty()
                        && a.date.isEmpty() && a.author.isEmpty() && a.notes.isEmpty() && a.tags.isEmpty();
        bool ok;
        if (empty) {
            ok = exec(db, QStringLiteral("DELETE FROM mdi_annotation WHERE ann_catalog_id = :c AND ann_full_path = :p"),
                      {{":c", file.catalogId}, {":p", file.fullPath}});
        } else {
            ok = exec(db, QStringLiteral(R"(
                INSERT OR REPLACE INTO mdi_annotation (ann_catalog_id, ann_full_path, ann_title, ann_description,
                    ann_event, ann_place, ann_date, ann_author, ann_notes, ann_tags, ann_updated)
                VALUES (:c, :p, :title, :description, :event, :place, :date, :author, :notes, :tags, :updated)
            )"), {{":c", file.catalogId}, {":p", file.fullPath}, {":title", a.title}, {":description", a.description},
                  {":event", a.event}, {":place", a.place}, {":date", a.date}, {":author", a.author},
                  {":notes", a.notes}, {":tags", tagsToJson(a.tags)}, {":updated", now()}});
        }
        if (!ok) {
            db.rollback();
            return false;
        }
    }
    return db.commit();
}

//----------------------------------------------------------------------
// People
//----------------------------------------------------------------------
QList<MdiPeople::Person> MdiPeople::persons(const QString &connectionName, const QString &filter)
{
    QList<Person> list;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.setForwardOnly(true);
    if (!q.exec(QLatin1String(R"(
            SELECT p.*,
              (SELECT COUNT(*) FROM mdi_file_person fp WHERE fp.fp_person_id = p.person_id) AS file_count,
              (SELECT COUNT(*) FROM mdi_face f WHERE f.face_person_id = p.person_id AND f.face_status = 'confirmed') AS face_count,
              (SELECT COUNT(*) FROM mdi_face f WHERE f.face_suggested_person_id = p.person_id AND f.face_status = 'suggested') AS pending_count
            FROM mdi_person p
        )")))
        return list;
    const QStringList words = fold(filter).split(' ', Qt::SkipEmptyParts);
    while (q.next()) {
        Person p;
        p.id           = q.value("person_id").toInt();
        p.firstName    = q.value("person_first_name").toString();
        p.lastName     = q.value("person_last_name").toString();
        p.role         = q.value("person_role").toString();
        p.organisation = q.value("person_organisation").toString();
        p.notes        = q.value("person_notes").toString();
        p.created      = q.value("person_created").toString();
        p.updated      = q.value("person_updated").toString();
        p.fileCount    = q.value("file_count").toInt();
        p.faceCount    = q.value("face_count").toInt();
        p.pendingCount = q.value("pending_count").toInt();
        const QString haystack = fold(p.firstName + ' ' + p.lastName + ' ' + p.role + ' ' + p.organisation);
        if (std::all_of(words.cbegin(), words.cend(), [&haystack](const QString &w) { return haystack.contains(w); }))
            list.append(p);
    }
    std::sort(list.begin(), list.end(), [](const Person &a, const Person &b) {
        const int byLast = QString::localeAwareCompare(a.lastName, b.lastName);
        return byLast != 0 ? byLast < 0 : QString::localeAwareCompare(a.firstName, b.firstName) < 0;
    });
    return list;
}

MdiPeople::Person MdiPeople::person(const QString &connectionName, int personId)
{
    for (const Person &p : persons(connectionName))
        if (p.id == personId)
            return p;
    return Person();
}

int MdiPeople::savePerson(const QString &connectionName, const Person &p)
{
    if (p.firstName.trimmed().isEmpty() && p.lastName.trimmed().isEmpty())
        return -1;
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery q(db);
    if (p.id > 0) {
        q.prepare(QLatin1String(R"(
            UPDATE mdi_person SET person_first_name = :first, person_last_name = :last, person_role = :role,
                   person_organisation = :org, person_notes = :notes, person_updated = :now
            WHERE person_id = :id
        )"));
        q.bindValue(":id", p.id);
    } else {
        q.prepare(QLatin1String(R"(
            INSERT INTO mdi_person (person_first_name, person_last_name, person_role, person_organisation,
                                    person_notes, person_created, person_updated)
            VALUES (:first, :last, :role, :org, :notes, :now, :now)
        )"));
    }
    q.bindValue(":first", p.firstName.trimmed());
    q.bindValue(":last",  p.lastName.trimmed());
    q.bindValue(":role",  p.role.trimmed());
    q.bindValue(":org",   p.organisation.trimmed());
    q.bindValue(":notes", p.notes.trimmed());
    q.bindValue(":now",   now());
    if (!q.exec()) {
        qWarning() << "MdiPeople::savePerson failed:" << q.lastError().text();
        return -1;
    }
    if (p.id > 0)
        return q.numRowsAffected() > 0 ? p.id : -1;
    return q.lastInsertId().toInt();
}

bool MdiPeople::deletePerson(const QString &connectionName, int personId)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.transaction())
        return false;
    const QVariantMap id{{":id", personId}};
    const bool ok =
        exec(db, QStringLiteral("UPDATE mdi_face SET face_person_id = NULL, face_status = 'unknown' "
                                "WHERE face_person_id = :id"), id)
        && exec(db, QStringLiteral("UPDATE mdi_face SET face_suggested_person_id = NULL, face_suggestion_score = NULL, "
                                   "face_status = CASE WHEN face_status = 'suggested' THEN 'unknown' ELSE face_status END "
                                   "WHERE face_suggested_person_id = :id"), id)
        && exec(db, QStringLiteral("DELETE FROM mdi_file_person WHERE fp_person_id = :id"), id)
        && exec(db, QStringLiteral("DELETE FROM mdi_person WHERE person_id = :id"), id);
    if (!ok) {
        db.rollback();
        return false;
    }
    return db.commit();
}

//----------------------------------------------------------------------
// Who is in which file
//----------------------------------------------------------------------
QList<MdiPeople::FilePerson> MdiPeople::peopleOfFile(const QString &connectionName, int catalogId, const QString &fullPath)
{
    QList<FilePerson> list;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QLatin1String(R"(
        SELECT fp.fp_person_id, fp.fp_face_id, fp.fp_source, p.person_first_name, p.person_last_name, p.person_role
        FROM mdi_file_person fp JOIN mdi_person p ON p.person_id = fp.fp_person_id
        WHERE fp.fp_catalog_id = :c AND fp.fp_full_path = :p
        ORDER BY p.person_last_name COLLATE NOCASE, p.person_first_name COLLATE NOCASE
    )"));
    q.bindValue(":c", catalogId);
    q.bindValue(":p", fullPath);
    if (!q.exec())
        return list;
    while (q.next()) {
        FilePerson fp;
        fp.personId = q.value(0).toInt();
        fp.faceId   = q.value(1).toInt();
        fp.source   = q.value(2).toString();
        fp.name     = (q.value(3).toString().trimmed() + QLatin1Char(' ') + q.value(4).toString().trimmed()).trimmed();
        fp.role     = q.value(5).toString();
        list.append(fp);
    }
    return list;
}

bool MdiPeople::addPeopleToFiles(const QString &connectionName, const QList<FileRef> &files,
                                 const QList<int> &personIds)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.transaction())
        return false;
    for (const FileRef &f : files)
        for (int personId : personIds) {
            // A link made by a face stays as it is
            if (!exec(db, QStringLiteral(R"(
                    INSERT OR IGNORE INTO mdi_file_person (fp_catalog_id, fp_full_path, fp_person_id, fp_face_id, fp_source, fp_created)
                    SELECT :c, :p, :person, NULL, 'manual', :now WHERE EXISTS (SELECT 1 FROM mdi_person WHERE person_id = :person)
                )"), {{":c", f.catalogId}, {":p", f.fullPath}, {":person", personId}, {":now", now()}})) {
                db.rollback();
                return false;
            }
        }
    return db.commit();
}

bool MdiPeople::setManualPeople(const QString &connectionName, const QList<FileRef> &files,
                                const QList<int> &personIds)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.transaction())
        return false;
    for (const FileRef &f : files) {
        QString sql = QStringLiteral("DELETE FROM mdi_file_person WHERE fp_catalog_id = :c AND fp_full_path = :p "
                                     "AND fp_source = 'manual'");
        if (!personIds.isEmpty())
            sql += QStringLiteral(" AND fp_person_id NOT IN (%1)").arg(idList(personIds));
        if (!exec(db, sql, {{":c", f.catalogId}, {":p", f.fullPath}})) {
            db.rollback();
            return false;
        }
    }
    if (!db.commit())
        return false;
    return personIds.isEmpty() || addPeopleToFiles(connectionName, files, personIds);
}

bool MdiPeople::removePersonFromFile(const QString &connectionName, int personId, int catalogId,
                                     const QString &fullPath)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.transaction())
        return false;
    // Faces of the file confirmed as this person: not this person after all
    bool ok = true;
    for (const Face &f : selectFaces(connectionName, QStringLiteral("face_catalog_id = :c AND face_full_path = :p AND face_person_id = :person"),
                                     {{":c", catalogId}, {":p", fullPath}, {":person", personId}})) {
        QList<int> rejected = f.rejectedPersons;
        if (!rejected.contains(personId))
            rejected << personId;
        ok = ok && exec(db, QStringLiteral("UPDATE mdi_face SET face_person_id = NULL, face_status = 'unknown', "
                                           "face_suggested_person_id = NULL, face_suggestion_score = NULL, face_rejected = :rejected "
                                           "WHERE face_id = :id"),
                        {{":rejected", idList(rejected)}, {":id", f.id}});
    }
    ok = ok && exec(db, QStringLiteral("DELETE FROM mdi_file_person WHERE fp_catalog_id = :c AND fp_full_path = :p AND fp_person_id = :person"),
                    {{":c", catalogId}, {":p", fullPath}, {":person", personId}});
    if (!ok) {
        db.rollback();
        return false;
    }
    return db.commit();
}

QList<MdiPeople::FileRef> MdiPeople::filesOfPerson(const QString &connectionName, int personId)
{
    QList<FileRef> files;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.prepare(QStringLiteral("SELECT fp_catalog_id, fp_full_path FROM mdi_file_person WHERE fp_person_id = :person "
                             "ORDER BY fp_full_path"));
    q.bindValue(":person", personId);
    if (q.exec())
        while (q.next())
            files.append({q.value(0).toInt(), q.value(1).toString()});
    return files;
}

//----------------------------------------------------------------------
// Search
//----------------------------------------------------------------------
QHash<QString, QString> MdiPeople::searchTextByFile(const QString &connectionName, int catalogId)
{
    QHash<QString, QString> text;
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery a(db);
    a.setForwardOnly(true);
    a.prepare(QStringLiteral("SELECT ann_full_path, ann_title, ann_description, ann_event, ann_place, ann_date, "
                             "ann_author, ann_notes, ann_tags FROM mdi_annotation WHERE ann_catalog_id = :c"));
    a.bindValue(":c", catalogId);
    if (a.exec())
        while (a.next()) {
            QStringList parts;
            for (int i = 1; i <= 7; ++i)
                parts << a.value(i).toString();
            parts << tagsFromJson(a.value(8).toString()).join(' ');
            parts.removeAll(QString());
            text[a.value(0).toString()] = parts.join(' ');
        }
    QSqlQuery p(db);
    p.setForwardOnly(true);
    p.prepare(QLatin1String(R"(
        SELECT fp.fp_full_path, p.person_first_name, p.person_last_name, p.person_role, p.person_organisation
        FROM mdi_file_person fp JOIN mdi_person p ON p.person_id = fp.fp_person_id
        WHERE fp.fp_catalog_id = :c
    )"));
    p.bindValue(":c", catalogId);
    if (p.exec())
        while (p.next()) {
            QStringList parts{p.value(1).toString(), p.value(2).toString(), p.value(3).toString(), p.value(4).toString()};
            parts.removeAll(QString());
            QString &t = text[p.value(0).toString()];
            t = (t + QLatin1Char(' ') + parts.join(' ')).trimmed();
        }
    return text;
}

QString MdiPeople::searchText(const QString &connectionName, int catalogId, const QString &fullPath)
{
    const Annotation a = annotation(connectionName, catalogId, fullPath);
    QStringList parts{a.title, a.description, a.event, a.place, a.date, a.author, a.notes, a.tags.join(' ')};
    QSqlQuery p(QSqlDatabase::database(connectionName));
    p.prepare(QLatin1String(R"(
        SELECT p.person_first_name, p.person_last_name, p.person_role, p.person_organisation
        FROM mdi_file_person fp JOIN mdi_person p ON p.person_id = fp.fp_person_id
        WHERE fp.fp_catalog_id = :c AND fp.fp_full_path = :path
    )"));
    p.bindValue(":c", catalogId);
    p.bindValue(":path", fullPath);
    if (p.exec())
        while (p.next())
            parts << p.value(0).toString() << p.value(1).toString() << p.value(2).toString() << p.value(3).toString();
    parts.removeAll(QString());
    return parts.join(' ');
}

QString MdiPeople::infoText(const QString &connectionName, int catalogId, const QString &fullPath)
{
    const Annotation a = annotation(connectionName, catalogId, fullPath);
    QStringList parts;
    auto add = [&parts](const QString &label, const QString &value) {
        if (!value.trimmed().isEmpty())
            parts << (label.isEmpty() ? value.trimmed() : label + QStringLiteral(": ") + value.trimmed());
    };
    add(QString(), a.title);
    add(QString(), a.description);
    add(QStringLiteral("Event"), a.event);
    add(QStringLiteral("Place"), a.place);
    add(QStringLiteral("Date"), a.date);
    add(QStringLiteral("Author"), a.author);
    add(QString(), a.notes);
    add(QStringLiteral("Tags"), a.tags.join(QStringLiteral(", ")));
    QStringList people;
    QSqlQuery p(QSqlDatabase::database(connectionName));
    p.prepare(QLatin1String(R"(
        SELECT p.person_first_name, p.person_last_name, p.person_role, p.person_organisation
        FROM mdi_file_person fp JOIN mdi_person p ON p.person_id = fp.fp_person_id
        WHERE fp.fp_catalog_id = :c AND fp.fp_full_path = :path
        ORDER BY p.person_last_name, p.person_first_name
    )"));
    p.bindValue(":c", catalogId);
    p.bindValue(":path", fullPath);
    if (p.exec())
        while (p.next()) {
            QString person = (p.value(0).toString().trimmed() + QLatin1Char(' ') + p.value(1).toString().trimmed()).trimmed();
            QStringList about{p.value(2).toString().trimmed(), p.value(3).toString().trimmed()};
            about.removeAll(QString());
            if (!about.isEmpty())
                person += QStringLiteral(" – ") + about.join(QStringLiteral(", "));
            people << person;
        }
    add(QStringLiteral("People"), people.join(QStringLiteral("; ")));
    if (parts.isEmpty())
        return QString();
    // The file's name helps queries naming a place or a person (as for summaries)
    return QFileInfo(fullPath).completeBaseName() + QStringLiteral(". ") + parts.join(QStringLiteral(". "));
}

QList<MdiPeople::FileRef> MdiPeople::findFiles(const QString &connectionName, const QString &text,
                                               const QList<int> &catalogIds, int limit)
{
    QList<FileRef> found;
    const QStringList words = fold(text).split(' ', Qt::SkipEmptyParts);
    if (words.isEmpty())
        return found;
    // Catalogs with information or people: their files' text, read once each
    QSet<int> catalogs;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    if (q.exec(QStringLiteral("SELECT DISTINCT ann_catalog_id FROM mdi_annotation "
                              "UNION SELECT DISTINCT fp_catalog_id FROM mdi_file_person")))
        while (q.next())
            if (catalogIds.isEmpty() || catalogIds.contains(q.value(0).toInt()))
                catalogs.insert(q.value(0).toInt());
    QList<int> sorted(catalogs.cbegin(), catalogs.cend());
    std::sort(sorted.begin(), sorted.end());
    for (int catalogId : std::as_const(sorted)) {
        const QHash<QString, QString> byFile = searchTextByFile(connectionName, catalogId);
        QStringList paths = byFile.keys();
        std::sort(paths.begin(), paths.end());
        for (const QString &path : std::as_const(paths)) {
            const QString haystack = fold(byFile.value(path));
            if (std::all_of(words.cbegin(), words.cend(), [&haystack](const QString &w) { return haystack.contains(w); })) {
                found.append({catalogId, path});
                if (found.size() >= limit)
                    return found;
            }
        }
    }
    return found;
}

//----------------------------------------------------------------------
// Faces
//----------------------------------------------------------------------
QList<MdiPeople::Face> MdiPeople::facesOfFile(const QString &connectionName, int catalogId, const QString &fullPath)
{
    return selectFaces(connectionName, QStringLiteral("face_catalog_id = :c AND face_full_path = :p"),
                       {{":c", catalogId}, {":p", fullPath}}, QStringLiteral("ORDER BY face_x"));
}

QHash<int, QString> MdiPeople::coverCrops(const QString &connectionName)
{
    QHash<int, QString> covers;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.setForwardOnly(true);
    if (q.exec(QStringLiteral("SELECT face_person_id, face_crop_root, face_crop_file FROM mdi_face "
                              "WHERE face_status = 'confirmed' AND face_person_id IS NOT NULL "
                              "AND face_crop_file IS NOT NULL ORDER BY face_score DESC")))
        while (q.next())
            if (!covers.contains(q.value(0).toInt()))
                covers.insert(q.value(0).toInt(), QDir(q.value(1).toString()).filePath(q.value(2).toString()));
    return covers;
}

MdiPeople::Face MdiPeople::face(const QString &connectionName, int faceId)
{
    const QList<Face> list = selectFaces(connectionName, QStringLiteral("face_id = :id"), {{":id", faceId}});
    return list.isEmpty() ? Face() : list.first();
}

bool MdiPeople::replaceFileFaces(const QString &connectionName, int catalogId, const QString &fullPath,
                                 const QList<Face> &detections, QList<int> *faceIds)
{
    QList<Face> known = facesOfFile(connectionName, catalogId, fullPath);
    QSet<int> matched;
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.transaction())
        return false;
    QList<int> ids;
    bool ok = true;
    for (const Face &d : detections) {
        int best = -1;
        double bestIou = 0.5;
        for (int k = 0; k < known.size(); ++k) {
            if (matched.contains(known.at(k).id))
                continue;
            const double v = iou(d, known.at(k));
            if (v >= bestIou) {
                bestIou = v;
                best = k;
            }
        }
        const QVariantMap common{{":x", d.x}, {":y", d.y}, {":w", d.w}, {":h", d.h}, {":score", d.score},
                                 {":emb", toBlob(d.embedding)}, {":root", d.cropRoot}, {":crop", d.cropFile}};
        if (best >= 0) {
            // The same face found again: it keeps its person and status
            matched.insert(known.at(best).id);
            QVariantMap b = common;
            b[":id"] = known.at(best).id;
            ok = ok && exec(db, QStringLiteral("UPDATE mdi_face SET face_x = :x, face_y = :y, face_w = :w, face_h = :h, "
                                               "face_score = :score, face_embedding = :emb, face_crop_root = :root, "
                                               "face_crop_file = :crop WHERE face_id = :id"), b);
            ids << known.at(best).id;
        } else {
            QSqlQuery ins(db);
            ins.prepare(QStringLiteral(R"(
                INSERT INTO mdi_face (face_catalog_id, face_full_path, face_x, face_y, face_w, face_h, face_score,
                                      face_embedding, face_status, face_crop_root, face_crop_file, face_created)
                VALUES (:c, :p, :x, :y, :w, :h, :score, :emb, 'unknown', :root, :crop, :now)
            )"));
            ins.bindValue(":c", catalogId);
            ins.bindValue(":p", fullPath);
            for (auto it = common.cbegin(); it != common.cend(); ++it)
                ins.bindValue(it.key(), it.value());
            ins.bindValue(":now", now());
            ok = ok && ins.exec();
            ids << (ok ? ins.lastInsertId().toInt() : 0);
        }
    }
    // Faces not found again: a confirmed one is kept (the owner named it)
    for (const Face &k : std::as_const(known)) {
        if (matched.contains(k.id) || k.status == QLatin1String("confirmed"))
            continue;
        ok = ok && exec(db, QStringLiteral("DELETE FROM mdi_face WHERE face_id = :id"), {{":id", k.id}});
    }
    if (!ok) {
        db.rollback();
        return false;
    }
    if (faceIds)
        *faceIds = ids;
    return db.commit();
}

bool MdiPeople::confirmFaces(const QString &connectionName, const QList<int> &faceIds, int personId)
{
    if (person(connectionName, personId).id != personId)
        return false;
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.transaction())
        return false;
    bool ok = true;
    for (int id : faceIds) {
        const Face f = face(connectionName, id);
        if (f.id != id)
            continue;
        QList<int> rejected = f.rejectedPersons;
        rejected.removeAll(personId);
        ok = ok && exec(db, QStringLiteral("UPDATE mdi_face SET face_person_id = :person, face_status = 'confirmed', "
                                           "face_suggested_person_id = NULL, face_suggestion_score = NULL, "
                                           "face_rejected = :rejected WHERE face_id = :id"),
                        {{":person", personId}, {":rejected", idList(rejected)}, {":id", id}})
                && relink(db, f.catalogId, f.fullPath, personId);
        if (f.personId > 0 && f.personId != personId)
            ok = ok && relink(db, f.catalogId, f.fullPath, f.personId);
    }
    if (!ok) {
        db.rollback();
        return false;
    }
    return db.commit();
}

bool MdiPeople::rejectFaces(const QString &connectionName, const QList<int> &faceIds)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.transaction())
        return false;
    bool ok = true;
    for (int id : faceIds) {
        const Face f = face(connectionName, id);
        if (f.id != id)
            continue;
        // The person suggested, or the one confirmed, is not this face's
        const int wrong = f.status == QLatin1String("confirmed") ? f.personId : f.suggestedPersonId;
        QList<int> rejected = f.rejectedPersons;
        if (wrong > 0 && !rejected.contains(wrong))
            rejected << wrong;
        ok = ok && exec(db, QStringLiteral("UPDATE mdi_face SET face_person_id = NULL, face_suggested_person_id = NULL, "
                                           "face_suggestion_score = NULL, face_rejected = :rejected, "
                                           "face_status = CASE WHEN face_status = 'rejected' THEN 'rejected' ELSE 'unknown' END "
                                           "WHERE face_id = :id"),
                        {{":rejected", idList(rejected)}, {":id", id}});
        if (f.personId > 0)
            ok = ok && relink(db, f.catalogId, f.fullPath, f.personId);
    }
    if (!ok) {
        db.rollback();
        return false;
    }
    return db.commit();
}

bool MdiPeople::ignoreFaces(const QString &connectionName, const QList<int> &faceIds)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.transaction())
        return false;
    bool ok = true;
    for (int id : faceIds) {
        const Face f = face(connectionName, id);
        if (f.id != id)
            continue;
        ok = ok && exec(db, QStringLiteral("UPDATE mdi_face SET face_person_id = NULL, face_suggested_person_id = NULL, "
                                           "face_suggestion_score = NULL, face_status = 'rejected' WHERE face_id = :id"),
                        {{":id", id}});
        if (f.personId > 0)
            ok = ok && relink(db, f.catalogId, f.fullPath, f.personId);
    }
    if (!ok) {
        db.rollback();
        return false;
    }
    return db.commit();
}

bool MdiPeople::clearFace(const QString &connectionName, int faceId)
{
    const Face f = face(connectionName, faceId);
    if (f.id != faceId)
        return false;
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.transaction())
        return false;
    const bool ok = exec(db, QStringLiteral("UPDATE mdi_face SET face_person_id = NULL, face_suggested_person_id = NULL, "
                                            "face_suggestion_score = NULL, face_status = 'unknown' WHERE face_id = :id"),
                         {{":id", faceId}})
                 && relink(db, f.catalogId, f.fullPath, f.personId);
    if (!ok) {
        db.rollback();
        return false;
    }
    return db.commit();
}

int MdiPeople::suggestForFaces(const QString &connectionName, const QList<int> &faceIds, double threshold)
{
    const QHash<int, QList<QVector<float>>> references = confirmedEmbeddings(connectionName);
    if (references.isEmpty() || faceIds.isEmpty())
        return 0;
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    db.transaction();
    int suggested = 0;
    for (const Face &f : selectFaces(connectionName, QStringLiteral("face_id IN (%1) AND face_person_id IS NULL "
                                                                    "AND face_status IN ('unknown', 'suggested')").arg(idList(faceIds)))) {
        int bestPerson = 0;
        double best = threshold;
        for (auto it = references.cbegin(); it != references.cend(); ++it) {
            if (f.rejectedPersons.contains(it.key()))
                continue;
            const double s = bestMatch(f.embedding, it.value());
            if (s >= best) {
                best = s;
                bestPerson = it.key();
            }
        }
        if (bestPerson > 0) {
            exec(db, QStringLiteral("UPDATE mdi_face SET face_status = 'suggested', face_suggested_person_id = :person, "
                                    "face_suggestion_score = :score WHERE face_id = :id"),
                 {{":person", bestPerson}, {":score", best}, {":id", f.id}});
            ++suggested;
        } else if (f.status == QLatin1String("suggested")) {
            exec(db, QStringLiteral("UPDATE mdi_face SET face_status = 'unknown', face_suggested_person_id = NULL, "
                                    "face_suggestion_score = NULL WHERE face_id = :id"), {{":id", f.id}});
        }
    }
    db.commit();
    return suggested;
}

int MdiPeople::suggestForPerson(const QString &connectionName, int personId, double threshold)
{
    const QList<QVector<float>> references = confirmedEmbeddings(connectionName, personId).value(personId);
    if (references.isEmpty())
        return 0;
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    db.transaction();
    int suggested = 0;
    for (const Face &f : selectFaces(connectionName, QStringLiteral("face_person_id IS NULL AND face_status IN ('unknown', 'suggested')"))) {
        if (f.rejectedPersons.contains(personId))
            continue;
        const double s = bestMatch(f.embedding, references);
        if (s < threshold)
            continue;
        if (f.status == QLatin1String("suggested") && f.suggestedPersonId != personId && f.suggestionScore >= s)
            continue;   // a better suggestion stays
        exec(db, QStringLiteral("UPDATE mdi_face SET face_status = 'suggested', face_suggested_person_id = :person, "
                                "face_suggestion_score = :score WHERE face_id = :id"),
             {{":person", personId}, {":score", s}, {":id", f.id}});
        ++suggested;
    }
    db.commit();
    return suggested;
}

QList<QPair<MdiPeople::Face, double>> MdiPeople::similarFaces(const QString &connectionName, int personId,
                                                               int limit, double minScore)
{
    QList<QPair<Face, double>> ranked;
    const QList<QVector<float>> references = confirmedEmbeddings(connectionName, personId).value(personId);
    if (references.isEmpty())
        return ranked;
    for (const Face &f : selectFaces(connectionName, QStringLiteral("face_person_id IS NULL AND face_status IN ('unknown', 'suggested')"))) {
        if (f.rejectedPersons.contains(personId))
            continue;
        const double s = bestMatch(f.embedding, references);
        if (s >= minScore)
            ranked.append({f, s});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    if (limit > 0 && ranked.size() > limit)
        ranked = ranked.mid(0, limit);
    for (auto &r : ranked)
        r.first.embedding.clear();   // not needed by the caller
    return ranked;
}

QList<MdiPeople::Face> MdiPeople::pendingFaces(const QString &connectionName, int limit)
{
    QList<Face> faces = selectFaces(connectionName, QStringLiteral("face_status = 'suggested' AND face_suggested_person_id IS NOT NULL"),
                                    {}, QStringLiteral("ORDER BY face_suggested_person_id, face_suggestion_score DESC%1")
                                            .arg(limit > 0 ? QStringLiteral(" LIMIT %1").arg(limit) : QString()));
    for (Face &f : faces)
        f.embedding.clear();
    return faces;
}

QList<QList<MdiPeople::Face>> MdiPeople::unnamedGroups(const QString &connectionName, int limit, double threshold)
{
    QList<Face> faces = selectFaces(connectionName, QStringLiteral("face_person_id IS NULL AND face_status = 'unknown'"),
                                    {}, QStringLiteral("ORDER BY face_score DESC LIMIT %1").arg(kGroupMaxFaces));
    QList<QList<Face>> groups;
    QVector<bool> used(faces.size(), false);
    for (int i = 0; i < faces.size(); ++i) {
        if (used[i] || faces[i].embedding.isEmpty())
            continue;
        used[i] = true;
        QList<Face> group{faces[i]};
        for (int j = i + 1; j < faces.size(); ++j)
            if (!used[j] && cosine(faces[i].embedding, faces[j].embedding) >= threshold) {
                used[j] = true;
                group.append(faces[j]);
            }
        if (group.size() >= 2)
            groups.append(group);
    }
    std::stable_sort(groups.begin(), groups.end(), [](const auto &a, const auto &b) { return a.size() > b.size(); });
    if (limit > 0 && groups.size() > limit)
        groups = groups.mid(0, limit);
    for (auto &g : groups)
        for (Face &f : g)
            f.embedding.clear();
    return groups;
}
