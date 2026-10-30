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
// File Name:   mdiarchivecopy.h
// Purpose:     Copy of the archive data (fork only, owner's decision of
//              2026-10-30)
// Description: Titles, events, people and approved faces live only in
//              mdi_data.db; this writes a dated folder ("Teca copy
//              YYYY-MM-DD HH-MM") holding consistent copies of the
//              collection database (File: VACUUM INTO; Memory: its files;
//              Hosted: not copied, it lives on its server), mdi_data.db and
//              teca_history.db (VACUUM INTO: they run in WAL mode while the
//              AI runner writes), and the settings file. Laid out like the
//              collection folder, so the copy opens as a collection.
//              Runs on the caller's thread with its own connections: meant
//              for a worker thread. The folder only gets its final name once
//              every file is written; a failed copy stays in a hidden
//              ".…partial" folder and is never mistaken for a complete one.
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDIARCHIVECOPY_H
#define MDIARCHIVECOPY_H

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <functional>

class MdiArchiveCopy
{
public:
    struct Sources {
        QString collectionFolder;
        QString databaseMode;        // "File", "Memory" or "Hosted"
        QString databaseFilePath;    // File mode: the collection database
        QString settingsFilePath;    // the settings file
        bool    withHistory = true;  // teca_history.db (not for hosted collections)
    };

    enum class Error {
        None,
        NoDestination,      // destination missing or not a folder
        InsideCollection,   // destination inside the collection folder
        NotEnoughSpace,     // see needed / available
        CannotWrite,        // the folder could not be made or renamed
        CopyFailed,         // a file could not be copied (see detail)
        Stopped
    };

    struct Result {
        Error       error = Error::None;
        QString     detail;              // technical reason, for the log and a %1
        QString     folder;              // the complete copy (empty on failure)
        QStringList written;             // its files, relative to it
        bool        databaseSkipped = false;   // Hosted: the hosted database is not copied
        qint64      needed = 0;          // bytes the copy was estimated to need
        qint64      available = -1;      // bytes free at the destination
        bool ok() const { return error == Error::None && !folder.isEmpty(); }
    };

    // "Teca copy 2026-10-30 14-05"
    static QString folderName(const QDateTime &when);

    // Bytes the copy will take at most (sum of the sources, WAL files included)
    static qint64 estimateBytes(const Sources &sources);

    // progress(done, total, item) is called before each file; returning
    // false stops before the next one
    using Progress = std::function<bool(int done, int total, const QString &item)>;

    // Checks the destination and its free space first (the estimate plus
    // 10 %), then copies. destination: the folder chosen by the owner.
    static Result run(const Sources &sources, const QString &destination, const QDateTime &when,
                      const Progress &progress = nullptr);
};

#endif // MDIARCHIVECOPY_H
