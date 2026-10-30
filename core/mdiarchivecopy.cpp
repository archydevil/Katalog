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
#include "core/cataloghistory.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

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
