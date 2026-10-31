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
// File Name:   mackeychain_other.cpp
// Purpose:     The reader key on systems other than macOS (fork only, D44):
//              not available; every call says so. Built instead of
//              mackeychain.mm (core/CMakeLists.txt).
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/platform/mackeychain.h"

#include <QtGlobal>

#ifndef Q_OS_MACOS

namespace {

void notAvailable(MacKeychain::Status *status, QString *error)
{
    if (status)
        *status = MacKeychain::Status::NotAvailable;
    if (error)
        *error = QStringLiteral("the reader key is not available on this system");
}

} // namespace

bool MacKeychain::isAvailable()
{
    return false;
}

QByteArray MacKeychain::ensureReaderKey(const MacReaderKeyRef &, bool *created, Status *status, QString *error)
{
    if (created)
        *created = false;
    notAvailable(status, error);
    return QByteArray();
}

QByteArray MacKeychain::readerPublicKey(const MacReaderKeyRef &, Status *status, QString *error)
{
    notAvailable(status, error);
    return QByteArray();
}

QString MacKeychain::readerFingerprint(const MacReaderKeyRef &, Status *status, QString *error)
{
    notAvailable(status, error);
    return QString();
}

QByteArray MacKeychain::readerPrivateKey(const QString &, const MacReaderKeyRef &, Prompt,
                                         Status *status, QString *error)
{
    notAvailable(status, error);
    return QByteArray();
}

bool MacKeychain::removeReaderKey(const MacReaderKeyRef &, Status *status, QString *error)
{
    notAvailable(status, error);
    return false;
}

#endif // !Q_OS_MACOS
