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
// File Name:   mdiprofile.cpp
// Purpose:     Enrichment profile of a catalog (fork only, SpecMediaIntelligence.md)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdiprofile.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QDateTime>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <QDebug>

bool MdiProfile::load(const QString &connectionName, int catalogDeviceId)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    query.prepare(QLatin1String(R"(
        SELECT profile_destination_path, profile_settings, profile_date_updated
        FROM mdi_profile
        WHERE profile_device_id = :device_id
    )"));
    query.bindValue(":device_id", catalogDeviceId);
    if (!query.exec() || !query.next())
        return false;

    deviceId        = catalogDeviceId;
    destinationPath = query.value(0).toString();
    settings        = QJsonDocument::fromJson(query.value(1).toString().toUtf8()).object();
    dateUpdated     = query.value(2).toString();
    return true;
}

bool MdiProfile::save(const QString &connectionName)
{
    if (deviceId <= 0)
        return false;

    dateUpdated = QDateTime::currentDateTime().toString(Qt::ISODate);

    // Delete + insert in one transaction: portable across SQLite and MySQL,
    // where the upsert syntaxes differ.
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    db.transaction();

    QSqlQuery deleteQuery(db);
    deleteQuery.prepare(QLatin1String("DELETE FROM mdi_profile WHERE profile_device_id = :device_id"));
    deleteQuery.bindValue(":device_id", deviceId);

    QSqlQuery insertQuery(db);
    insertQuery.prepare(QLatin1String(R"(
        INSERT INTO mdi_profile (profile_device_id, profile_destination_path,
                                 profile_settings, profile_date_updated)
        VALUES (:device_id, :destination_path, :settings, :date_updated)
    )"));
    insertQuery.bindValue(":device_id",        deviceId);
    insertQuery.bindValue(":destination_path", destinationPath);
    insertQuery.bindValue(":settings",         QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
    insertQuery.bindValue(":date_updated",     dateUpdated);

    if (!deleteQuery.exec() || !insertQuery.exec()) {
        qWarning() << "MdiProfile::save failed:" << deleteQuery.lastError().text() << insertQuery.lastError().text();
        db.rollback();
        return false;
    }
    return db.commit();
}

bool MdiProfile::remove(const QString &connectionName, int catalogDeviceId)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    query.prepare(QLatin1String("DELETE FROM mdi_profile WHERE profile_device_id = :device_id"));
    query.bindValue(":device_id", catalogDeviceId);
    return query.exec();
}

QList<int> MdiProfile::deviceIds(const QString &connectionName)
{
    QList<int> ids;
    QSqlQuery query(QSqlDatabase::database(connectionName));
    if (!query.exec(QLatin1String("SELECT profile_device_id FROM mdi_profile ORDER BY profile_device_id")))
        return ids;
    while (query.next())
        ids.append(query.value(0).toInt());
    return ids;
}

QString MdiProfile::memoryFilePath(const QString &collectionFolder)
{
    return collectionFolder + QStringLiteral("/mdi_profile.csv");
}

void MdiProfile::loadFileToTable(const QString &connectionName, const QString &collectionFolder)
{
    QFile file(memoryFilePath(collectionFolder));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return; // no profile saved yet in this collection

    QSqlQuery deleteQuery(QSqlDatabase::database(connectionName));
    deleteQuery.exec(QLatin1String("DELETE FROM mdi_profile"));

    QTextStream in(&file);
    in.readLine(); // header

    while (true) {
        const QString line = in.readLine();
        if (line.isNull())
            break;
        const QStringList fields = line.split('\t');
        if (fields.size() < 4)
            continue;
        QSqlQuery insertQuery(QSqlDatabase::database(connectionName));
        insertQuery.prepare(QLatin1String(R"(
            INSERT INTO mdi_profile (profile_device_id, profile_destination_path,
                                     profile_settings, profile_date_updated)
            VALUES (:device_id, :destination_path, :settings, :date_updated)
        )"));
        insertQuery.bindValue(":device_id",        fields[0].toInt());
        insertQuery.bindValue(":destination_path", fields[1]);
        insertQuery.bindValue(":settings",         fields[2]);
        insertQuery.bindValue(":date_updated",     fields[3]);
        insertQuery.exec();
    }
}

void MdiProfile::saveTableToFile(const QString &connectionName, const QString &collectionFolder)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    if (!query.exec(QLatin1String(R"(
        SELECT profile_device_id, profile_destination_path, profile_settings, profile_date_updated
        FROM mdi_profile
        ORDER BY profile_device_id
    )")))
        return;

    // Compact JSON has no tab or newline, so it fits one tab-separated field.
    QSaveFile file(memoryFilePath(collectionFolder));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream out(&file);
    out << "device_id" << "\t" << "destination_path" << "\t" << "settings" << "\t" << "date_updated" << "\n";
    while (query.next()) {
        out << query.value(0).toString() << "\t"
            << query.value(1).toString() << "\t"
            << query.value(2).toString() << "\t"
            << query.value(3).toString() << "\n";
    }
    out.flush();
    file.commit();
}
