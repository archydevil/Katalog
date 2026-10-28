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
// File Name:   mdiprofile.h
// Purpose:     Enrichment profile of a catalog (fork only, SpecMediaIntelligence.md)
// Description: Destination path and wizard choices saved per catalog
//              (MDI-F7, MDI-F8). Memory mode keeps them in mdi_profile.csv in
//              the collection folder (MDI-F55).
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDIPROFILE_H
#define MDIPROFILE_H

#include <QString>
#include <QJsonObject>

class MdiProfile
{
public:
    int         deviceId = 0;
    QString     destinationPath;
    QJsonObject settings;      // file types and preview options chosen in the wizard
    QString     dateUpdated;   // ISO 8601, set by save()

    bool load(const QString &connectionName, int catalogDeviceId);
    bool save(const QString &connectionName);   // insert or replace the catalog's row
    static bool remove(const QString &connectionName, int catalogDeviceId);

    // Memory mode persistence (tab-separated, like catalog_filter.csv)
    static QString memoryFilePath(const QString &collectionFolder);
    static void loadFileToTable(const QString &connectionName, const QString &collectionFolder);
    static void saveTableToFile(const QString &connectionName, const QString &collectionFolder);
};

#endif // MDIPROFILE_H
