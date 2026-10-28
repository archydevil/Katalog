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
// File Name:   mdischema.h
// Purpose:     Media intelligence tables (fork only, SpecMediaIntelligence.md)
// Description: Creates and upgrades the mdi_ tables. They live outside the core
//              schema numbering (MDI-C15): the core schema stays 3.0 and no core
//              table is touched. Safe to run on every open.
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDISCHEMA_H
#define MDISCHEMA_H

#include <QString>
#include <QSqlError>

class MdiSchema
{
public:
    // "media intelligence schema version", stored in mdi_schema
    static constexpr int currentVersion = 1;

    // Creates the mdi_ tables when absent and upgrades them when older.
    // Idempotent; touches only mdi_ tables.
    static QSqlError ensure(const QString &connectionName);

    // 0 when the mdi_schema table holds no version yet
    static int storedVersion(const QString &connectionName);
};

#endif // MDISCHEMA_H
