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
// File Name:   mdidiskspace.cpp
// Purpose:     Free space of the volumes Teca writes its AI data to (fork only)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdidiskspace.h"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

qint64 MdiDiskSpace::available(const QString &path)
{
    if (path.trimmed().isEmpty())
        return -1;
    // Same walk as the destination checks: up to the nearest existing folder
    QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    while (!info.exists()) {
        const QString parent = info.absolutePath();
        if (parent == info.absoluteFilePath())
            return -1;
        info = QFileInfo(parent);
    }
    const QStorageInfo storage(info.absoluteFilePath());
    if (!storage.isValid() || !storage.isReady())
        return -1;
    return storage.bytesAvailable();
}

bool MdiDiskSpace::isWritable(const QString &path)
{
    // A destination that does not exist yet is accepted when the nearest
    // existing folder above it can be written to: it is created when first used.
    if (path.trimmed().isEmpty())
        return false;
    QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    while (!info.exists()) {
        const QString parent = info.absolutePath();
        if (parent == info.absoluteFilePath())
            return false;
        info = QFileInfo(parent);
    }
    return info.isDir() && info.isWritable();
}

MdiDiskSpace::Shortage MdiDiskSpace::firstShortage(const QStringList &paths, qint64 needed)
{
    Shortage shortage;
    for (const QString &p : paths) {
        if (p.trimmed().isEmpty())
            continue;
        const qint64 free = available(p);
        if (free >= 0 && free < needed) {
            shortage.path      = p;
            shortage.available = free;
            shortage.needed    = needed;
            return shortage;
        }
    }
    return shortage;
}
