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
#include <QJsonArray>
#include <QList>
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

// Publishing for the read-only Macs (fork, D43 (4), D44; SpecMediaIntelligence.md
// MDI-F202..F207, MDI-F215, MDI-C65). On an authorised Mac the collection is
// written into a folder the owner syncs to his other Macs (Syncthing over
// Tailscale, receive-only there; Teca starts no sync program and opens no
// tunnel). The folder holds, each written under a hidden temporary name and
// then renamed, so a reader never sees half a file:
//   <collection>.db, or the Memory-mode files   consistent snapshot (VACUUM INTO / copies)
//   mdi_data.db                                  cleaned snapshot: no Notes, no people, no
//                                                faces (P2a), index rows rebuilt without them
//   katalog_mdi/<catalog>/<analysis>/<file>      derived files, mirrored incrementally;
//                                                face crops never
//   teca_published.json                          the marker, written last
// A folder with the marker opens read-only on every Mac (MDI-F204).
// Runs on the caller's thread with its own connections: meant for a worker.
class MdiPublish
{
public:
    static constexpr int kFormatVersion = 1;
    // Automatic publishing (MDI-F205): after this quiet time following the
    // last catalog update or AI job, and never sooner than kAutoMinimumSeconds
    // after the previous publish
    static constexpr int kAutoQuietSeconds   = 120;
    static constexpr int kAutoMinimumSeconds = 300;

    static QString markerFileName();      // "teca_published.json"
    static QString derivedFolderName();   // "katalog_mdi"
    static bool    isPublishedFolder(const QString &folder);

    // App-wide settings keys, per collection (Collection::settingsKey): the
    // folder chosen, and whether to publish again after updates and analyses
    static QString folderKey();           // "MediaIntelligence/PublishFolder"
    static QString autoKey();             // "MediaIntelligence/PublishAuto" (default true)

    struct FileEntry {
        QString name;      // relative to the published folder
        qint64  size = 0;
        QString sha256;    // hex
    };
    struct Marker {
        bool             valid = false;
        int              format = 0;
        QDateTime        publishedAt;
        QString          publisherCode;   // CollectionWriters::thisComputerCode of the publisher
        QString          publisherName;
        QString          collectionName;
        QString          databaseMode;    // "File" or "Memory"
        QString          databaseFile;    // File mode: the database's file name
        QList<FileEntry> files;           // the snapshots
        int              derivedFiles = 0;
        QJsonArray       layers;          // protected layers: written by P2b, empty in P2a
    };
    static Marker readMarker(const QString &folder);
    // Every file the marker lists is there with its size and, when
    // checkHashes, its SHA-256: a set Syncthing has not finished is not complete
    static bool isComplete(const QString &folder, const Marker &marker, bool checkHashes,
                           QString *missing = nullptr);

    struct Sources {
        QString     collectionFolder;
        QString     databaseMode;          // "File" or "Memory" (Hosted is refused)
        QString     databaseFilePath;      // File mode
        QString     collectionName;
        QStringList catalogSourcePaths;    // the publish folder may not be inside one (MDI-C10)
    };

    enum class Error {
        None,
        Hosted,             // a hosted collection has no files to publish
        PublishedCopy,      // the collection is itself a published copy
        NoDestination,      // folder missing or not a folder
        InsideCollection,   // folder inside the collection folder
        OnCatalogedSource,  // folder inside a catalog's source (detail: that path)
        NotEnoughSpace,
        CannotWrite,        // a file could not be written or renamed (detail)
        CopyFailed,         // a snapshot could not be made (detail)
        CleanFailed,        // mdi_data.db could not be cleaned or checked (detail)
        Stopped
    };

    // What the published mdi_data.db leaves out (MDI-F215, D44)
    struct CleanReport {
        int notes = 0;          // files whose Notes were removed
        int people = 0;         // people removed (Layer B)
        int links = 0;          // person <-> file links removed
        int faces = 0;          // face rows removed (Layer A)
        int faceData = 0;       // "faces" analysis records removed (their crop references)
        int crops = 0;          // face crops they listed, never published
        int infoVectors = 0;    // "info" meaning-search rows removed (built from Notes and names)
        int documentPeople = 0; // document studies whose "people of the archive" were removed
        int indexRows = 0;      // instant-index rows rebuilt from the cleaned information
    };

    struct Result {
        Error       error = Error::None;
        QString     detail;
        QDateTime   publishedAt;
        QStringList written;            // the snapshots, relative to the folder
        CleanReport cleaned;
        int         derivedCopied = 0;     // new or changed since the last publish
        int         derivedUnchanged = 0;
        int         derivedMissing = 0;    // recorded but not found at the source (drive away)
        int         cropsLeftOut = 0;      // face crops never published
        qint64      bytesCopied = 0;
        qint64      needed = 0;
        qint64      available = -1;
        bool ok() const { return error == Error::None; }
    };

    // The catalogs' source paths of a collection connection (device paths)
    static QStringList catalogSourcePaths(const QString &connectionName);

    static Error checkDestination(const Sources &sources, const QString &destination, QString *detail);

    // progress(done, total, item): returning false stops before the next step
    using Progress = MdiArchiveCopy::Progress;
    static Result run(const Sources &sources, const QString &destination, const QDateTime &when,
                      const Progress &progress = nullptr);

    // Cleans a copy of mdi_data.db in place for publishing, then checks it:
    // empty when clean, otherwise the reason. Never used on the collection's own file.
    static QString cleanMdiCopy(const QString &path, CleanReport *report);
};

#endif // MDIARCHIVECOPY_H
