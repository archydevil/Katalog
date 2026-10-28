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
// File Name:   mdistore.h
// Purpose:     Derived data of the media intelligence runner (fork only,
//              SpecMediaIntelligence.md MDI-F52, MDI-C4, MDI-C5)
// Description: Derived data lives in its own SQLite file, mdi_data.db, in the
//              collection folder. The runner writes it from its own thread and
//              connection, so it never contends with the collection database,
//              and Memory mode keeps it on disk like File mode (decision Q6).
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDISTORE_H
#define MDISTORE_H

#include <QString>
#include <QList>
#include <QHash>
#include <QJsonObject>

struct MdiDatum
{
    int         catalogId = 0;
    QString     filePath;          // full path of the cataloged file
    QString     analysis;          // thumbnail, poster, keyframes, video_clip, audio_clip, waveform, caption, summary
    QString     outcome;           // completed / failed / skipped
    QString     reason;
    QJsonObject value;             // analysis result (caption, tags, summary, frames…)
    QString     derivedFile;       // relative to the destination path, empty when none
    QString     tool;
    QString     toolVersion;
    QString     model;
    QString     provider;
    QString     providerClass;     // local / local-network / remote
    QString     endpointHost;
    QJsonObject params;            // analysis parameters, prompt version included
    qint64      fileSize = 0;      // at processing time (MDI-F13)
    QString     fileDate;
    double      cost = 0.0;
    QString     currency;
    QString     date;
};

class MdiStore
{
public:
    static constexpr int currentVersion = 1;

    static QString filePath(const QString &collectionFolder);

    // Opens (or reuses) a connection to mdi_data.db and creates its tables
    static bool open(const QString &connectionName, const QString &dbFile);
    static void close(const QString &connectionName);

    static bool upsert(const QString &connectionName, const MdiDatum &datum);

    // True when a completed datum exists for the same file state and parameters
    static bool isCurrent(const QString &connectionName, int catalogId, const QString &filePath,
                          const QString &analysis, qint64 fileSize, const QString &fileDate,
                          const QJsonObject &params);

    static QList<MdiDatum> forFile(const QString &connectionName, int catalogId, const QString &filePath);

    // full path → derived preview image (thumbnail or poster frame), for list display
    static QHash<QString, QString> previewImages(const QString &connectionName, int catalogId);

    // full path → number of completed analyses, to mark files that have derived data
    static QHash<QString, int> completedCounts(const QString &connectionName, int catalogId);
};

#endif // MDISTORE_H
