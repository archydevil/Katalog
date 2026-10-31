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
// File Name:   collectionaccess.h
// Purpose:     Who may change a collection (fork only, D43, owner's request
//              of 2026-10-31: a collection shared between Macs is read-only
//              on every Mac he has not authorised)
// Description: CollectionLock: the lock file of a File- or Memory-mode
//              collection folder (Collection::lockFilePath). One process
//              holds it; any other one, Teca or the command line, opens the
//              collection read-only. The holder refreshes it every
//              kRefreshSeconds, so a lock left by a crash on another computer
//              is stale after kStaleMinutes; on the same computer Qt sees at
//              once that the process is gone.
//              CollectionWriters: the computers allowed to change the
//              collection, kept in the collection itself (parameter rows, all
//              three modes, no schema change). Off by default: a collection
//              without the rows is open to every computer, as before.
//              A computer is known by its code, a short hash of
//              QSysInfo::machineUniqueId: the hardware id itself is never
//              written into the collection.
//              Plain Qt value types, no UI.
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef COLLECTIONACCESS_H
#define COLLECTIONACCESS_H

#include <QList>
#include <QString>
#include <QStringList>
#include <memory>

class Collection;
class QLockFile;

class CollectionLock
{
public:
    // Shared by every process that takes the lock (window and command line):
    // a lock older than kStaleMinutes counts as left behind; its holder
    // refreshes it every kRefreshSeconds.
    static constexpr int kStaleMinutes   = 5;
    static constexpr int kRefreshSeconds = 60;

    enum class Result {
        Acquired,        // this process holds the lock
        HeldElsewhere,   // another live process holds it: open read-only
        NotPossible      // no folder, or it cannot hold a lock file (read-only share)
    };

    CollectionLock();
    ~CollectionLock();   // releases the lock
    CollectionLock(const CollectionLock &) = delete;
    CollectionLock &operator=(const CollectionLock &) = delete;

    // Releases any lock held, then takes the one of folder. A lock left by a
    // process that ended is removed and taken.
    Result acquire(const QString &folder);
    void   release();
    bool   isHeld() const;
    QString folder() const { return m_folder; }

    // The holder when acquire() answered HeldElsewhere: its host name,
    // process id and program name, as written in the lock file
    QString holderHost() const { return m_holderHost; }
    qint64  holderPid()  const { return m_holderPid; }
    QString holderApp()  const { return m_holderApp; }

    // Keeps the lock young for the other computers. False when it is no
    // longer this process's (removed, or taken over while this computer
    // slept): the holder is then in holderHost() / holderPid() / holderApp().
    bool refresh();

private:
    std::unique_ptr<QLockFile> m_lock;
    QString m_folder;
    QString m_holderHost;
    qint64  m_holderPid = 0;
    QString m_holderApp;
};

class CollectionWriters
{
public:
    struct Writer {
        QString code;       // "XXXX-XXXX-XXXX"
        QString name;       // readable name given when added
        bool    thisComputer = false;
    };
    enum class Change {
        Ok,
        InvalidCode,     // not a code of 12 letters and digits
        AlreadyListed,
        NotListed,
        LastWriter,      // the last one cannot be removed while protection is on
        Failed           // database error
    };

    explicit CollectionWriters(Collection *collection);

    // This computer: its code (from QSysInfo::machineUniqueId, else from its
    // host name) and a readable name (its host name)
    static QString thisComputerCode();
    static QString thisComputerName();
    // Code of a machine id; a typed code in canonical form ("" when invalid):
    // case, spaces and dashes do not matter, O reads 0, I and L read 1
    static QString codeOf(const QByteArray &machineId);
    static QString normalizeCode(const QString &typed);

    bool          protectionOn() const;
    QList<Writer> writers() const;
    // True when protection is off, or this computer is in the list
    bool          mayThisComputerWrite() const;

    // The same answer before the collection is opened, read from its files
    // without writing anything (no -wal or -shm either): File mode from the
    // SQLite file, Memory mode from parameters.csv. True when unreadable.
    static bool mayThisComputerWriteFile(const QString &databaseFilePath);
    static bool mayThisComputerWriteFolder(const QString &memoryFolder);
    // SQLite database name that opens path read-only and immutable (nothing
    // written, no -wal or -shm file), with the connect options
    // kReadOnlyConnectOptions
    static QString readOnlyDatabaseName(const QString &path);
    static constexpr const char *kReadOnlyConnectOptions = "QSQLITE_OPEN_READONLY;QSQLITE_OPEN_URI";

    // Turning protection on adds this computer first; turning it off keeps
    // the list for the next time.
    Change setProtection(bool on);
    Change addWriter(const QString &typedCode, const QString &name);
    Change removeWriter(const QString &code);

private:
    Collection *m_collection;
    static bool allows(bool protection, const QStringList &codes);
    QString connectionName() const;
    bool    insertWriter(const QString &code, const QString &name);
    void    saveToFile() const;   // Memory mode: parameters.csv
};

#endif // COLLECTIONACCESS_H
