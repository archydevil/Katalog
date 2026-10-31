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
// File Name:   mdiarchivecopy.cpp
// Purpose:     Copy of the archive data (fork only)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdiarchivecopy.h"
#include "core/mdidata.h"
#include "core/mdidiskspace.h"
#include "core/mdifts.h"
#include "core/cataloghistory.h"
#include "core/collectionaccess.h"
#include "core/device.h"   // Device::resolvePath (D43 P3)

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace {

struct Item {
    bool    vacuum = false;   // a database: VACUUM INTO; otherwise a plain file copy
    QString source;           // absolute
    QString relative;         // in the copy
};

QString canonical(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() ? info.canonicalFilePath() : QDir::cleanPath(info.absoluteFilePath());
}

bool isInside(const QString &path, const QString &folder)
{
    const QString p = canonical(path);
    const QString f = canonical(folder);
    return !f.isEmpty() && (p == f || p.startsWith(f.endsWith(QLatin1Char('/')) ? f : f + QLatin1Char('/')));
}

// The files of a Memory collection: its tables (.csv) and catalogs (.idx,
// .folders.idx), found in sub-folders too as the collection loader does
QList<Item> memoryFiles(const QString &folder)
{
    QList<Item> list;
    const QDir root(folder);
    QDirIterator it(folder, {QStringLiteral("*.csv"), QStringLiteral("*.idx")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString relative = root.relativeFilePath(path);
        // Hidden folders (an unfinished copy among them) are not the collection's
        bool hidden = false;
        for (const QString &part : relative.split(QLatin1Char('/')))
            hidden = hidden || part.startsWith(QLatin1Char('.'));
        if (!hidden)
            list.append(Item{false, path, relative});
    }
    std::sort(list.begin(), list.end(), [](const Item &a, const Item &b) { return a.relative < b.relative; });
    return list;
}

QList<Item> items(const MdiArchiveCopy::Sources &s)
{
    QList<Item> list;
    const QString folder = QDir::cleanPath(s.collectionFolder);
    if (s.databaseMode == QLatin1String("File")) {
        if (QFileInfo(s.databaseFilePath).isFile())
            list.append(Item{true, s.databaseFilePath, QFileInfo(s.databaseFilePath).fileName()});
    } else if (s.databaseMode == QLatin1String("Memory") && !folder.isEmpty()) {
        list += memoryFiles(folder);
    }
    if (!folder.isEmpty()) {
        const QString mdi = MdiData::filePath(folder);
        if (QFileInfo(mdi).isFile())
            list.append(Item{true, mdi, QFileInfo(mdi).fileName()});
        const QString history = CatalogHistory::filePath(folder);
        if (s.withHistory && QFileInfo(history).isFile())
            list.append(Item{true, history, QFileInfo(history).fileName()});
    }
    if (QFileInfo(s.settingsFilePath).isFile())
        list.append(Item{false, s.settingsFilePath, QFileInfo(s.settingsFilePath).fileName()});
    return list;
}

qint64 fileSize(const QString &path)
{
    const QFileInfo info(path);
    return info.isFile() ? info.size() : 0;
}

// A consistent copy of a database another connection may be writing (WAL):
// VACUUM INTO reads one snapshot. Empty on success, otherwise the reason.
QString vacuumInto(const QString &source, const QString &target)
{
    const QString conn = QStringLiteral("mdi_archive_copy_%1").arg(quintptr(QThread::currentThreadId()));
    QString error;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(source);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=10000"));
        if (!db.open()) {
            error = db.lastError().text();
        } else {
            QSqlQuery q(db);
            if (!q.prepare(QStringLiteral("VACUUM INTO ?"))) {
                error = q.lastError().text();
            } else {
                q.addBindValue(target);
                if (!q.exec())
                    error = q.lastError().text();
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
    return error;
}

} // namespace

QString MdiArchiveCopy::folderName(const QDateTime &when)
{
    return QStringLiteral("Teca copy ") + when.toString(QStringLiteral("yyyy-MM-dd HH-mm"));
}

qint64 MdiArchiveCopy::estimateBytes(const Sources &sources)
{
    qint64 total = 0;
    for (const Item &i : items(sources)) {
        total += fileSize(i.source);
        if (i.vacuum)   // what the WAL holds is part of the copy too
            total += fileSize(i.source + QStringLiteral("-wal"));
    }
    return total;
}

MdiArchiveCopy::Result MdiArchiveCopy::run(const Sources &sources, const QString &destination,
                                           const QDateTime &when, const Progress &progress)
{
    Result r;
    const QFileInfo dest(destination);
    if (destination.trimmed().isEmpty() || !dest.isDir()) {
        r.error = Error::NoDestination;
        return r;
    }
    if (!sources.collectionFolder.isEmpty() && isInside(destination, sources.collectionFolder)) {
        r.error = Error::InsideCollection;
        return r;
    }

    const QList<Item> list = items(sources);
    r.databaseSkipped = sources.databaseMode == QLatin1String("Hosted");
    qint64 total = 0;
    for (const Item &i : list)
        total += fileSize(i.source) + (i.vacuum ? fileSize(i.source + QStringLiteral("-wal")) : 0);
    r.needed    = total + total / 10;
    r.available = MdiDiskSpace::available(destination);
    if (r.available >= 0 && r.available < r.needed) {
        r.error = Error::NotEnoughSpace;
        return r;
    }

    // The final name, unique; written under a hidden name first
    QDir parent(dest.absoluteFilePath());
    const QString base = folderName(when);
    QString name = base;
    for (int n = 2; parent.exists(name); ++n)
        name = base + QStringLiteral(" (%1)").arg(n);
    QString partial = QStringLiteral(".") + name + QStringLiteral(".partial");
    for (int n = 2; parent.exists(partial); ++n)
        partial = QStringLiteral(".") + name + QStringLiteral(".partial-%1").arg(n);
    if (!parent.mkdir(partial)) {
        r.error  = Error::CannotWrite;
        r.detail = parent.filePath(partial);
        return r;
    }
    const QDir work(parent.filePath(partial));

    for (int k = 0; k < list.size(); ++k) {
        const Item &i = list.at(k);
        if (progress && !progress(k, int(list.size()), i.relative)) {
            r.error = Error::Stopped;
            return r;
        }
        const QString target = work.filePath(i.relative);
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            r.error  = Error::CannotWrite;
            r.detail = QFileInfo(target).absolutePath();
            return r;
        }
        if (i.vacuum) {
            const QString error = vacuumInto(i.source, target);
            if (!error.isEmpty()) {
                r.error  = Error::CopyFailed;
                r.detail = i.relative + QStringLiteral(": ") + error;
                return r;
            }
        } else {
            QFile source(i.source);
            if (!source.copy(target)) {
                r.error  = Error::CopyFailed;
                r.detail = i.relative + QStringLiteral(": ") + source.errorString();
                return r;
            }
        }
        r.written << i.relative;
    }
    if (progress)
        progress(int(list.size()), int(list.size()), QString());

    if (!parent.rename(partial, name)) {
        r.error  = Error::CannotWrite;
        r.detail = parent.filePath(name);
        return r;
    }
    r.folder = parent.filePath(name);
    return r;
}

//----------------------------------------------------------------------
// Publishing for the read-only Macs (fork, D43 (4), D44)
//----------------------------------------------------------------------
namespace {

const QLatin1String kPartialSuffix(".teca-partial");

// <folder>/.<name>.teca-partial: hidden, beside the file it becomes
QString partialPath(const QString &target)
{
    const QFileInfo info(target);
    return info.absoluteDir().filePath(QLatin1Char('.') + info.fileName() + kPartialSuffix);
}

// Replaces target by temp in one step where the system allows it, so a
// reader opening target never finds it missing
bool replaceFile(const QString &temp, const QString &target, QString *error)
{
#ifdef Q_OS_WIN
    QFile::remove(target);
    QFile file(temp);
    if (!file.rename(target)) {
        *error = file.errorString();
        return false;
    }
#else
    if (std::rename(QFile::encodeName(temp).constData(), QFile::encodeName(target).constData()) != 0) {
        *error = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
#endif
    return true;
}

QString sha256Of(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();
    return QString::fromLatin1(hash.result().toHex());
}

bool copyWithTime(const QString &source, const QString &partial, QString *error)
{
    QFile::remove(partial);   // a temporary file left by an interrupted publish
    QFile in(source);
    if (!in.copy(partial)) {
        *error = in.errorString();
        return false;
    }
    // The source's date, so the next publish sees the file unchanged
    QFile out(partial);
    if (out.open(QIODevice::Append))
        out.setFileTime(QFileInfo(source).lastModified(), QFileDevice::FileModificationTime);
    return true;
}

bool sameFile(const QFileInfo &a, const QFileInfo &b)
{
    // Two seconds: the resolution of some drives' dates (exFAT, FAT)
    return a.size() == b.size()
        && qAbs(a.lastModified().toMSecsSinceEpoch() - b.lastModified().toMSecsSinceEpoch()) <= 2000;
}

bool hasTable(const QSqlDatabase &db, const QString &table)
{
    return db.tables().contains(table);
}

int exec(QSqlDatabase &db, const QString &sql, QString *error)
{
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        if (error && error->isEmpty())
            *error = sql.section(QLatin1Char(' '), 0, 3) + QStringLiteral(": ") + q.lastError().text();
        return -1;
    }
    return q.numRowsAffected();
}

int count(QSqlDatabase &db, const QString &sql)
{
    QSqlQuery q(db);
    return q.exec(sql) && q.next() ? q.value(0).toInt() : -1;
}

// A document study's "people of the archive" (names taken from the people,
// Layer B); true when it had some
bool dropDocumentPeople(QJsonObject *value)
{
    QJsonObject related = value->value(QStringLiteral("related")).toObject();
    if (!related.contains(QStringLiteral("people")))
        return false;
    related.remove(QStringLiteral("people"));
    value->insert(QStringLiteral("related"), related);
    return true;
}

} // namespace

QString MdiPublish::markerFileName()    { return QStringLiteral("teca_published.json"); }
QString MdiPublish::derivedFolderName() { return QStringLiteral("katalog_mdi"); }
QString MdiPublish::folderKey()         { return QStringLiteral("MediaIntelligence/PublishFolder"); }
QString MdiPublish::autoKey()           { return QStringLiteral("MediaIntelligence/PublishAuto"); }

bool MdiPublish::isPublishedFolder(const QString &folder)
{
    return !folder.isEmpty() && QFileInfo(QDir(folder).filePath(markerFileName())).isFile();
}
//----------------------------------------------------------------------
MdiPublish::Marker MdiPublish::readMarker(const QString &folder)
{
    Marker m;
    QFile file(QDir(folder).filePath(markerFileName()));
    if (folder.isEmpty() || !file.open(QIODevice::ReadOnly))
        return m;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    m.format        = o.value(QStringLiteral("format")).toInt();
    m.publishedAt   = QDateTime::fromString(o.value(QStringLiteral("publishedAt")).toString(), Qt::ISODate);
    const QJsonObject publisher  = o.value(QStringLiteral("publisher")).toObject();
    m.publisherCode = publisher.value(QStringLiteral("code")).toString();
    m.publisherName = publisher.value(QStringLiteral("name")).toString();
    const QJsonObject collection = o.value(QStringLiteral("collection")).toObject();
    m.collectionName = collection.value(QStringLiteral("name")).toString();
    m.databaseMode   = collection.value(QStringLiteral("mode")).toString();
    m.databaseFile   = collection.value(QStringLiteral("database")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("files")).toArray()) {
        const QJsonObject f = v.toObject();
        m.files.append(FileEntry{ f.value(QStringLiteral("name")).toString(),
                                  qint64(f.value(QStringLiteral("size")).toDouble()),
                                  f.value(QStringLiteral("sha256")).toString() });
    }
    m.derivedFiles = o.value(QStringLiteral("derived")).toObject().value(QStringLiteral("files")).toInt();
    m.layers       = o.value(QStringLiteral("layers")).toArray();
    m.valid = m.format >= 1 && m.publishedAt.isValid() && !m.files.isEmpty();
    return m;
}
//----------------------------------------------------------------------
bool MdiPublish::isComplete(const QString &folder, const Marker &marker, bool checkHashes, QString *missing)
{
    if (!marker.valid)
        return false;
    const QDir dir(folder);
    for (const FileEntry &f : marker.files) {
        const QFileInfo info(dir.filePath(f.name));
        if (!info.isFile() || info.size() != f.size
            || (checkHashes && sha256Of(info.absoluteFilePath()) != f.sha256)) {
            if (missing)
                *missing = f.name;
            return false;
        }
    }
    return true;
}
//----------------------------------------------------------------------
QStringList MdiPublish::catalogSourcePaths(const QString &connectionName)
{
    QStringList paths;
    QSqlQuery q(QSqlDatabase::database(connectionName));
    if (q.exec(QStringLiteral("SELECT device_path FROM device WHERE device_type = 'Catalog'")))
        while (q.next())
            if (!q.value(0).toString().trimmed().isEmpty()) {
                paths << q.value(0).toString();
                // And where the drive is on this Mac (per-Mac remap, D43 P3)
                const QString local = Device::resolvePath(q.value(0).toString());
                if (local != q.value(0).toString())
                    paths << local;
            }
    return paths;
}
//----------------------------------------------------------------------
MdiPublish::Error MdiPublish::checkDestination(const Sources &s, const QString &destination, QString *detail)
{
    if (s.databaseMode != QLatin1String("File") && s.databaseMode != QLatin1String("Memory"))
        return Error::Hosted;
    if (isPublishedFolder(s.collectionFolder))
        return Error::PublishedCopy;
    if (destination.trimmed().isEmpty() || !QFileInfo(destination).isDir())
        return Error::NoDestination;
    if (!s.collectionFolder.isEmpty() && isInside(destination, s.collectionFolder))
        return Error::InsideCollection;
    for (const QString &source : s.catalogSourcePaths)
        if (isInside(destination, source)) {
            if (detail)
                *detail = source;
            return Error::OnCatalogedSource;
        }
    return Error::None;
}
//----------------------------------------------------------------------
QString MdiPublish::cleanMdiCopy(const QString &path, CleanReport *report)
{
    CleanReport local;
    CleanReport &r = report ? *report : local;
    const QString conn = QStringLiteral("mdi_publish_clean_%1").arg(quintptr(QThread::currentThreadId()));
    QString error;
    {
        // Every table a reader may read exists, so its read-only open finds them
        if (!MdiData::open(conn, path) || !MdiFts::ensureTables(conn)) {
            MdiData::close(conn);
            return QStringLiteral("mdi_data.db copy not opened");
        }
        QSqlDatabase db = QSqlDatabase::database(conn);
        // A plain file again (MdiData::open sets WAL), and what is deleted is
        // overwritten, not only unlinked
        exec(db, QStringLiteral("PRAGMA journal_mode=DELETE"), nullptr);
        exec(db, QStringLiteral("PRAGMA secure_delete=ON"), nullptr);

        // Files whose instant-index text was drawn from their information or
        // their people: rebuilt below from the cleaned information
        QList<QPair<int, QString>> affected;
        {
            QSet<QString> seen;
            QSqlQuery q(db);
            q.setForwardOnly(true);
            if (q.exec(QLatin1String(R"(
                    SELECT ann_catalog_id, ann_full_path FROM mdi_annotation
                    UNION SELECT fp_catalog_id, fp_full_path FROM mdi_file_person
                    UNION SELECT DISTINCT emb_catalog_id, emb_full_path FROM mdi_embedding WHERE emb_kind = 'info'
                )")))
                while (q.next()) {
                    const QString key = q.value(0).toString() + QLatin1Char('\n') + q.value(1).toString();
                    if (!seen.contains(key)) {
                        seen.insert(key);
                        affected.append({q.value(0).toInt(), q.value(1).toString()});
                    }
                }
        }

        db.transaction();
        // The Notes, never published (MDI-F215)
        r.notes = count(db, QStringLiteral("SELECT COUNT(*) FROM mdi_annotation WHERE ann_notes IS NOT NULL AND ann_notes <> ''"));
        exec(db, QStringLiteral("UPDATE mdi_annotation SET ann_notes = NULL WHERE ann_notes IS NOT NULL"), &error);
        // Layer B: people and who is in which file (by hand or by a face)
        r.links  = exec(db, QStringLiteral("DELETE FROM mdi_file_person"), &error);
        r.people = exec(db, QStringLiteral("DELETE FROM mdi_person"), &error);
        // Layer A: faces with their boxes, vectors and crop references, and
        // the analysis records listing the crops
        r.faces    = exec(db, QStringLiteral("DELETE FROM mdi_face"), &error);
        r.faceData = exec(db, QStringLiteral("DELETE FROM mdi_datum WHERE datum_analysis = 'faces'"), &error);
        if (hasTable(db, QStringLiteral("sqlite_sequence")))
            exec(db, QStringLiteral("DELETE FROM sqlite_sequence WHERE name IN ('mdi_person', 'mdi_face')"), &error);
        // Meaning search over the information: built from the Notes and the
        // names, left out (rebuilding it needs the embedding model)
        r.infoVectors = exec(db, QStringLiteral("DELETE FROM mdi_embedding WHERE emb_kind = 'info'"), &error);
        // A document study names the people of the archive it found
        {
            QSqlQuery read(db);
            QList<QPair<qint64, QString>> changed;
            if (read.exec(QStringLiteral("SELECT rowid, datum_value FROM mdi_datum "
                                         "WHERE datum_analysis = 'document' AND datum_value LIKE '%\"related\"%'")))
                while (read.next()) {
                    QJsonObject value = QJsonDocument::fromJson(read.value(1).toString().toUtf8()).object();
                    if (dropDocumentPeople(&value))
                        changed.append({read.value(0).toLongLong(),
                                        QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact))});
                }
            QSqlQuery write(db);
            write.prepare(QStringLiteral("UPDATE mdi_datum SET datum_value = :v WHERE rowid = :id"));
            for (const auto &c : std::as_const(changed)) {
                write.bindValue(":v", c.second);
                write.bindValue(":id", c.first);
                if (!write.exec() && error.isEmpty())
                    error = QStringLiteral("document study: ") + write.lastError().text();
            }
            r.documentPeople = int(changed.size());
        }
        if (!error.isEmpty()) {
            db.rollback();
        } else {
            db.commit();
            // The instant index: the affected files' text again, from the
            // cleaned information only (title, description, event, place,
            // date, author, tags) and the AI results; then the whole index is
            // rebuilt from its rows, so no removed word stays in its segments
            db.transaction();
            for (const auto &f : std::as_const(affected)) {
                MdiFts::updateDerivedText(conn, f.first, f.second);
                ++r.indexRows;
            }
            db.commit();
            exec(db, QStringLiteral("INSERT INTO mdi_fts(mdi_fts) VALUES('rebuild')"), &error);
            // Nothing deleted survives in free pages
            if (error.isEmpty())
                exec(db, QStringLiteral("VACUUM"), &error);
        }

        // The check: what must be gone is gone
        if (error.isEmpty()) {
            const QList<QPair<QString, QString>> checks = {
                { QStringLiteral("notes"),      QStringLiteral("SELECT COUNT(*) FROM mdi_annotation WHERE ann_notes IS NOT NULL") },
                { QStringLiteral("people"),     QStringLiteral("SELECT COUNT(*) FROM mdi_person") },
                { QStringLiteral("links"),      QStringLiteral("SELECT COUNT(*) FROM mdi_file_person") },
                { QStringLiteral("faces"),      QStringLiteral("SELECT COUNT(*) FROM mdi_face") },
                { QStringLiteral("face data"),  QStringLiteral("SELECT COUNT(*) FROM mdi_datum WHERE datum_analysis = 'faces'") },
                { QStringLiteral("info rows"),  QStringLiteral("SELECT COUNT(*) FROM mdi_embedding WHERE emb_kind = 'info'") },
                { QStringLiteral("free pages"), QStringLiteral("PRAGMA freelist_count") } };
            for (const auto &c : checks)
                if (count(db, c.second) != 0) {
                    error = QStringLiteral("check failed: ") + c.first;
                    break;
                }
        }
        if (error.isEmpty()) {
            QSqlQuery q(db);
            q.setForwardOnly(true);
            if (q.exec(QStringLiteral("SELECT datum_value FROM mdi_datum WHERE datum_analysis = 'document' "
                                      "AND datum_value LIKE '%\"related\"%'")))
                while (q.next() && error.isEmpty()) {
                    QJsonObject value = QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object();
                    if (dropDocumentPeople(&value))
                        error = QStringLiteral("check failed: document people");
                }
        }
        if (error.isEmpty()) {
            // Each rebuilt row holds exactly the text of the cleaned information
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT f.derived FROM mdi_fts f JOIN mdi_fts_doc d ON d.doc_id = f.rowid "
                                     "WHERE d.doc_catalog_id = :c AND d.doc_full_path = :p"));
            for (const auto &f : std::as_const(affected)) {
                q.bindValue(":c", f.first);
                q.bindValue(":p", f.second);
                if (q.exec() && q.next() && q.value(0).toString() != MdiFts::derivedText(conn, f.first, f.second)) {
                    error = QStringLiteral("check failed: index text");
                    break;
                }
            }
        }
        if (error.isEmpty()) {
            QSqlQuery mode(db);
            if (!mode.exec(QStringLiteral("PRAGMA journal_mode")) || !mode.next()
                || mode.value(0).toString().compare(QLatin1String("delete"), Qt::CaseInsensitive) != 0)
                error = QStringLiteral("check failed: journal mode");
        }
    }
    MdiData::close(conn);
    return error;
}
//----------------------------------------------------------------------
MdiPublish::Result MdiPublish::run(const Sources &s, const QString &destination, const QDateTime &when,
                                   const Progress &progress)
{
    Result r;
    r.publishedAt = when;
    r.error = checkDestination(s, destination, &r.detail);
    if (r.error != Error::None)
        return r;
    const QDir dest(QFileInfo(destination).absoluteFilePath());
    const QString folder = QDir::cleanPath(s.collectionFolder);

    // The snapshots: the collection, then mdi_data.db
    QList<Item> snapshots;
    if (s.databaseMode == QLatin1String("File")) {
        if (!QFileInfo(s.databaseFilePath).isFile()) {
            r.error  = Error::CopyFailed;
            r.detail = s.databaseFilePath;
            return r;
        }
        snapshots.append(Item{true, s.databaseFilePath, QFileInfo(s.databaseFilePath).fileName()});
    } else {
        snapshots += memoryFiles(folder);
    }
    const QString mdiSource = MdiData::filePath(folder);
    const bool withMdi = QFileInfo(mdiSource).isFile();

    qint64 total = 0;
    for (const Item &i : std::as_const(snapshots))
        total += fileSize(i.source) + (i.vacuum ? fileSize(i.source + QStringLiteral("-wal")) : 0);
    if (withMdi)
        total += fileSize(mdiSource) + fileSize(mdiSource + QStringLiteral("-wal"));
    r.needed    = total + total / 10;
    r.available = MdiDiskSpace::available(dest.absolutePath());
    if (r.available >= 0 && r.available < r.needed) {
        r.error = Error::NotEnoughSpace;
        return r;
    }

    const int steps = int(snapshots.size()) + (withMdi ? 2 : 0) + 2;   // + derived files, marker
    int step = 0;
    auto stopped = [&](const QString &item) {
        return progress && !progress(step, steps, item);
    };

    // 1. Every snapshot under its temporary name; the published set changes
    //    only once they are all written (MDI-F205: a failure keeps the previous one)
    struct Written { QString partial, target, relative; };
    QList<Written> written;
    auto fail = [&](Error e, const QString &detail) {
        for (const Written &w : std::as_const(written))
            QFile::remove(w.partial);   // this run's own temporary files
        r.error  = e;
        r.detail = detail;
        return r;
    };
    for (const Item &i : std::as_const(snapshots)) {
        if (stopped(i.relative))
            return fail(Error::Stopped, QString());
        ++step;
        const QString target  = dest.filePath(i.relative);
        const QString partial = partialPath(target);
        if (!QDir().mkpath(QFileInfo(target).absolutePath()))
            return fail(Error::CannotWrite, QFileInfo(target).absolutePath());
        QFile::remove(partial);   // left by an interrupted publish
        QString error;
        if (i.vacuum) {
            // Not in WAL mode: the copy opens without -wal or -shm (MDI-C65)
            error = vacuumInto(i.source, partial);
        } else {
            QFile source(i.source);
            if (!source.copy(partial))
                error = source.errorString();
        }
        if (!error.isEmpty())
            return fail(Error::CopyFailed, i.relative + QStringLiteral(": ") + error);
        written.append(Written{partial, target, i.relative});
    }
    if (withMdi) {
        if (stopped(QFileInfo(mdiSource).fileName()))
            return fail(Error::Stopped, QString());
        ++step;
        // Cleaned away from the published folder: its journal, its deleted
        // rows and the VACUUM work file never reach the synced folder
        QTemporaryDir work;
        if (!work.isValid())
            return fail(Error::CannotWrite, work.errorString());
        const QString clean = QDir(work.path()).filePath(QStringLiteral("mdi_data.db"));
        QString error = vacuumInto(mdiSource, clean);
        if (!error.isEmpty())
            return fail(Error::CopyFailed, QStringLiteral("mdi_data.db: ") + error);
        if (stopped(QFileInfo(mdiSource).fileName()))
            return fail(Error::Stopped, QString());
        ++step;
        error = cleanMdiCopy(clean, &r.cleaned);
        if (!error.isEmpty())
            return fail(Error::CleanFailed, error);
        const QString target  = dest.filePath(QStringLiteral("mdi_data.db"));
        const QString partial = partialPath(target);
        written.append(Written{partial, target, QStringLiteral("mdi_data.db")});
        if (!copyWithTime(clean, partial, &error))
            return fail(Error::CannotWrite, QStringLiteral("mdi_data.db: ") + error);

        // 2. The derived files, from the cleaned copy: faces are gone from it
        if (stopped(QStringLiteral("katalog_mdi")))
            return fail(Error::Stopped, QString());
        ++step;
        const QString conn = QStringLiteral("mdi_publish_list_%1").arg(quintptr(QThread::currentThreadId()));
        QList<QPair<QString, QString>> files;   // source, relative
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(clean);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (db.open()) {
                QSet<QString> seen;
                QSqlQuery q(db);
                q.setForwardOnly(true);
                if (q.exec(QStringLiteral("SELECT datum_derived_root, datum_derived_files FROM mdi_datum "
                                          "WHERE datum_derived_files IS NOT NULL AND datum_derived_files <> ''")))
                    while (q.next()) {
                        const QString root = MdiData::resolveDerivedRoot(q.value(0).toString());
                        for (const QString &rel : q.value(1).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                            const QStringList parts = QDir::cleanPath(rel).split(QLatin1Char('/'));
                            // <catalog>/<analysis>/<file>; never outside katalog_mdi, never a face crop
                            if (QDir::isAbsolutePath(rel) || parts.contains(QStringLiteral("..")))
                                continue;
                            if (parts.size() > 1 && parts.at(1) == QLatin1String("faces")) {
                                ++r.cropsLeftOut;
                                continue;
                            }
                            if (seen.contains(rel))
                                continue;
                            seen.insert(rel);
                            files.append({QDir(root).filePath(rel), rel});
                        }
                    }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(conn);
        const QDir derived(dest.filePath(derivedFolderName()));
        for (int k = 0; k < files.size(); ++k) {
            if (progress && k % 50 == 0 && !progress(step, steps, files.at(k).second))
                return fail(Error::Stopped, QString());
            const QFileInfo source(files.at(k).first);
            const QString target = derived.filePath(files.at(k).second);
            if (!source.isFile()) {
                ++r.derivedMissing;   // its drive away: the copy published before stays
                continue;
            }
            const QFileInfo existing(target);
            if (existing.isFile() && sameFile(source, existing)) {
                ++r.derivedUnchanged;
                continue;
            }
            const QString partialFile = partialPath(target);
            QString copyError;
            if (!QDir().mkpath(QFileInfo(target).absolutePath())
                || !copyWithTime(source.absoluteFilePath(), partialFile, &copyError)
                || !replaceFile(partialFile, target, &copyError)) {
                QFile::remove(partialFile);
                return fail(Error::CannotWrite, files.at(k).second + QStringLiteral(": ") + copyError);
            }
            ++r.derivedCopied;
            r.bytesCopied += source.size();
        }
    }

    // 3. Protected layers: written by P2b (Layers A and B, encrypted, D44).
    //    P2a writes none, and the marker says so with an empty list.
    const QJsonArray layers;

    // 4. The snapshots replace the previous ones, then the marker, last
    if (stopped(markerFileName()))
        return fail(Error::Stopped, QString());
    ++step;
    QJsonArray listed;
    for (const Written &w : std::as_const(written)) {
        const QString hash = sha256Of(w.partial);
        if (hash.isEmpty())
            return fail(Error::CannotWrite, w.relative);
        listed.append(QJsonObject{ { QStringLiteral("name"),   w.relative },
                                   { QStringLiteral("size"),   double(QFileInfo(w.partial).size()) },
                                   { QStringLiteral("sha256"), hash } });
    }
    for (int k = 0; k < written.size(); ++k) {
        QString error;
        if (!replaceFile(written.at(k).partial, written.at(k).target, &error)) {
            // Those not replaced yet are removed; the marker still lists the
            // previous set, so readers do not reload a mixed one
            written = written.mid(k);
            return fail(Error::CannotWrite, written.first().relative + QStringLiteral(": ") + error);
        }
        r.written << written.at(k).relative;
    }
    written.clear();
    const QJsonObject marker{
        { QStringLiteral("format"),      kFormatVersion },
        { QStringLiteral("publishedAt"), when.toOffsetFromUtc(when.offsetFromUtc()).toString(Qt::ISODate) },
        { QStringLiteral("publisher"),   QJsonObject{ { QStringLiteral("code"), CollectionWriters::thisComputerCode() },
                                                      { QStringLiteral("name"), CollectionWriters::thisComputerName() } } },
        { QStringLiteral("collection"),  QJsonObject{ { QStringLiteral("name"), s.collectionName },
                                                      { QStringLiteral("mode"), s.databaseMode },
                                                      { QStringLiteral("database"), s.databaseMode == QLatin1String("File")
                                                                                    ? QFileInfo(s.databaseFilePath).fileName() : QString() } } },
        { QStringLiteral("files"),       listed },
        { QStringLiteral("derived"),     QJsonObject{ { QStringLiteral("folder"), derivedFolderName() },
                                                      { QStringLiteral("files"), r.derivedCopied + r.derivedUnchanged } } },
        { QStringLiteral("layers"),      layers } };
    const QString markerPath = dest.filePath(markerFileName());
    const QString markerTemp = partialPath(markerPath);
    {
        QFile out(markerTemp);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || out.write(QJsonDocument(marker).toJson(QJsonDocument::Indented)) < 0 || !out.flush()) {
            r.error  = Error::CannotWrite;
            r.detail = markerFileName() + QStringLiteral(": ") + out.errorString();
            out.close();
            QFile::remove(markerTemp);
            return r;
        }
    }
    QString error;
    if (!replaceFile(markerTemp, markerPath, &error)) {
        QFile::remove(markerTemp);
        r.error  = Error::CannotWrite;
        r.detail = markerFileName() + QStringLiteral(": ") + error;
        return r;
    }
    if (progress)
        progress(steps, steps, QString());
    return r;
}
