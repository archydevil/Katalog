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
// File Name:   mdidiskspace.h
// Purpose:     Free space of the volumes Teca writes its AI data to (fork
//              only, owner's decision of 2026-10-30)
// Description: Used before an AI job starts (MDI-F19) and before a copy of
//              the archive data. A path that does not exist yet is measured
//              on its nearest existing folder, where it would be created.
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDIDISKSPACE_H
#define MDIDISKSPACE_H

#include <QString>
#include <QStringList>

class MdiDiskSpace
{
public:
    // Least free space an AI job needs on the volume of the collection's AI
    // data (mdi_data.db) and on the one of its derived files: 1 GiB. A floor,
    // not an estimate: SQLite needs room for its journal and checkpoints, and
    // a full disk would stop every write half-way.
    static constexpr qint64 kJobMinimumFree = 1024LL * 1024 * 1024;

    // Free bytes for this user on the volume of path (or of its nearest
    // existing folder); -1 when unknown
    static qint64 available(const QString &path);

    struct Shortage {
        QString path;             // the path short of space; empty when none is
        qint64  available = -1;   // its free bytes
        qint64  needed    = 0;
        bool isShort() const { return !path.isEmpty(); }
    };

    // The first of paths whose volume has less than `needed` bytes free.
    // Empty paths are skipped; a volume whose space is unknown is not short.
    static Shortage firstShortage(const QStringList &paths, qint64 needed);

    // A folder that can be written to, or that can be created in the nearest
    // existing folder above it (the destination of the derived files);
    // shared by the app and the command line (D43)
    static bool isWritable(const QString &path);
};

#endif // MDIDISKSPACE_H
