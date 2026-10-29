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
// File Name:   mdivolumes.h
// Purpose:     Connected drives, mounted or not (fork, owner's request 2026-10-29)
// Description: Katalog catalogs any drive the system has mounted, NTFS
//              included (reading is enough). A drive the system did not mount
//              cannot be cataloged; this lists external drives and their
//              partitions with their file system and state, and asks the
//              system to mount one read-only. External programs only:
//              diskutil (macOS), lsblk and udisksctl (Linux). On Windows NTFS
//              is native and the mounted volumes are listed.
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDIVOLUMES_H
#define MDIVOLUMES_H

#include <QString>
#include <QList>
#include <QVariantMap>

class MdiVolumes
{
public:
    // External drives' partitions that carry a file system:
    // [{device, name, fileSystem, driver, mounted, mountPoint, readOnly,
    //   size, protocol, uuid}]. driver names a third-party file-system
    // driver when one handles the volume ("Paragon NTFS", "Tuxera NTFS",
    // "macFUSE"), empty for the system's own.
    static QList<QVariantMap> list();

    // Asks the system to mount the partition read-only. On success fills
    // mountPoint; on failure, reason says why in plain words (English, for
    // the UI to show with its own text) and returns false.
    static bool mountReadOnly(const QString &device, QString *mountPoint, QString *reason);
};

#endif // MDIVOLUMES_H
