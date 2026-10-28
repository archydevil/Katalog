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
// File Name:   mdischema.cpp
// Purpose:     Media intelligence tables (fork only, SpecMediaIntelligence.md)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdischema.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDebug>

namespace {

QSqlError exec(const QString &connectionName, const QString &sql)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    if (!query.exec(sql))
        return query.lastError();
    return QSqlError();
}

} // namespace

QSqlError MdiSchema::ensure(const QString &connectionName)
{
    // Plain SQL types only (INTEGER, TEXT), valid for SQLite and MySQL/MariaDB.
    QSqlError err = exec(connectionName, QLatin1String(R"(
        CREATE TABLE IF NOT EXISTS mdi_schema (
            mdi_schema_version INTEGER NOT NULL
        )
    )"));
    if (err.type() != QSqlError::NoError)
        return err;

    // One enrichment profile per catalog device (MDI-F8). The choices made in
    // the wizard are kept as JSON so later phases can add options without
    // altering the table.
    err = exec(connectionName, QLatin1String(R"(
        CREATE TABLE IF NOT EXISTS mdi_profile (
            profile_device_id        INTEGER NOT NULL PRIMARY KEY,
            profile_destination_path TEXT    NOT NULL,
            profile_settings         TEXT    NOT NULL,
            profile_date_updated     TEXT
        )
    )"));
    if (err.type() != QSqlError::NoError)
        return err;

    const int stored = storedVersion(connectionName);
    if (stored == 0) {
        QSqlQuery insert(QSqlDatabase::database(connectionName));
        insert.prepare(QLatin1String("INSERT INTO mdi_schema (mdi_schema_version) VALUES (:version)"));
        insert.bindValue(":version", currentVersion);
        if (!insert.exec())
            return insert.lastError();
    }
    // Upgrade steps go here, each guarded by "stored < N" and harmless to repeat.

    return QSqlError();
}

int MdiSchema::storedVersion(const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    if (!query.exec(QLatin1String("SELECT MAX(mdi_schema_version) FROM mdi_schema")) || !query.next())
        return 0;
    return query.value(0).toInt();
}
