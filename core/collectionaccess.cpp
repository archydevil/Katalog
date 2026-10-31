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
// File Name:   collectionaccess.cpp
// Purpose:     Who may change a collection (fork only, D43)
// Description: See collectionaccess.h
/////////////////////////////////////////////////////////////////////////////
*/

#include "core/collectionaccess.h"
#include "core/collection.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <algorithm>
#include <cstring>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSysInfo>
#include <QTextStream>
#include <QUrl>
#include <QtEndian>
#include <chrono>

namespace {

// Crockford's base 32: no I, L, O or U, so a code read aloud or typed by
// hand is not misread
const char kCodeAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr int kCodeLength = 12;   // 60 bits

const QLatin1String kWriterType("authorised_writer");
const QLatin1String kProtectionName("writersProtection");

QString formatCode(const QString &raw)
{
    return raw.mid(0, 4) + QLatin1Char('-') + raw.mid(4, 4) + QLatin1Char('-') + raw.mid(8, 4);
}

// One line of parameters.csv per row (Memory mode): no tab or line break
QString cleanName(const QString &name)
{
    QString text = name;
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return text.simplified().left(80);
}

} // namespace

//----------------------------------------------------------------------
// CollectionLock
//----------------------------------------------------------------------
CollectionLock::CollectionLock() = default;

CollectionLock::~CollectionLock()
{
    release();
}
//----------------------------------------------------------------------
CollectionLock::Result CollectionLock::acquire(const QString &folder)
{
    release();
    m_folder = folder;
    m_holderHost.clear();
    m_holderPid = 0;
    m_holderApp.clear();
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return Result::NotPossible;

    auto lock = std::make_unique<QLockFile>(Collection::lockFilePath(folder));
    lock->setStaleLockTime(std::chrono::minutes(kStaleMinutes));
    // A lock left by a process that ended (same computer), or not refreshed
    // for kStaleMinutes (another one), is removed and taken here
    if (lock->tryLock(0)) {
        m_lock = std::move(lock);
        return Result::Acquired;
    }
    if (lock->error() == QLockFile::LockFailedError) {
        lock->getLockInfo(&m_holderPid, &m_holderHost, &m_holderApp);
        return Result::HeldElsewhere;
    }
    return Result::NotPossible;
}
//----------------------------------------------------------------------
void CollectionLock::release()
{
    if (!m_lock)
        return;
    // QLockFile::unlock() removes the file at its path: when another process
    // has taken the lock over meanwhile, that file is its own and must stay.
    // The object is then let go without unlocking (a rare case: a computer
    // that slept longer than kStaleMinutes).
    qint64 pid = 0;
    QString host, app;
    if (m_lock->getLockInfo(&pid, &host, &app) && pid != QCoreApplication::applicationPid()) {
        (void)m_lock.release();
        return;
    }
    m_lock->unlock();
    m_lock.reset();
}
//----------------------------------------------------------------------
bool CollectionLock::isHeld() const
{
    return m_lock && m_lock->isLocked();
}
//----------------------------------------------------------------------
bool CollectionLock::refresh()
{
    if (!isHeld())
        return false;
    qint64 pid = 0;
    QString host, app;
    if (!m_lock->getLockInfo(&pid, &host, &app)) {
        // Removed by someone else: nobody holds it
        m_holderPid = 0;
        m_holderHost.clear();
        m_holderApp.clear();
        (void)m_lock.release();   // its file is gone; unlocking would remove a new one
        return false;
    }
    if (pid != QCoreApplication::applicationPid()) {
        m_holderPid  = pid;
        m_holderHost = host;
        m_holderApp  = app;
        (void)m_lock.release();   // see release()
        return false;
    }
    // A new modification time, contents unchanged
    QFile file(Collection::lockFilePath(m_folder));
    if (file.open(QIODevice::ReadOnly))
        file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    return true;
}

//----------------------------------------------------------------------
// CollectionWriters
//----------------------------------------------------------------------
CollectionWriters::CollectionWriters(Collection *collection)
    : m_collection(collection)
{
}
//----------------------------------------------------------------------
QString CollectionWriters::connectionName() const
{
    return m_collection ? m_collection->connectionName() : QStringLiteral("defaultConnection");
}
//----------------------------------------------------------------------
QString CollectionWriters::codeOf(const QByteArray &machineId)
{
    const QByteArray digest = QCryptographicHash::hash("teca-writer\n" + machineId,
                                                       QCryptographicHash::Sha256);
    const quint64 value = qFromBigEndian<quint64>(digest.constData());
    QString raw;
    raw.reserve(kCodeLength);
    for (int i = 0; i < kCodeLength; ++i)
        raw += QLatin1Char(kCodeAlphabet[(value >> (64 - 5 * (i + 1))) & 31]);
    return formatCode(raw);
}
//----------------------------------------------------------------------
QString CollectionWriters::thisComputerCode()
{
    static const QString code = [] {
        QByteArray id = QSysInfo::machineUniqueId();
        if (id.isEmpty())
            id = "host:" + QSysInfo::machineHostName().toUtf8();
        return codeOf(id);
    }();
    return code;
}
//----------------------------------------------------------------------
QString CollectionWriters::thisComputerName()
{
    QString name = QSysInfo::machineHostName();
    if (name.endsWith(QLatin1String(".local"), Qt::CaseInsensitive))
        name.chop(6);
    return cleanName(name);
}
//----------------------------------------------------------------------
QString CollectionWriters::normalizeCode(const QString &typed)
{
    QString raw;
    for (QChar c : typed.toUpper()) {
        if (c.isSpace() || c == QLatin1Char('-') || c == QLatin1Char('_') || c == QLatin1Char('.'))
            continue;
        if (c == QLatin1Char('O'))
            c = QLatin1Char('0');
        else if (c == QLatin1Char('I') || c == QLatin1Char('L'))
            c = QLatin1Char('1');
        if (c.unicode() == 0 || c.unicode() > 127 || !std::strchr(kCodeAlphabet, char(c.unicode())))
            return QString();
        raw += c;
    }
    return raw.size() == kCodeLength ? formatCode(raw) : QString();
}
//----------------------------------------------------------------------
bool CollectionWriters::protectionOn() const
{
    QSqlQuery query(QSqlDatabase::database(connectionName()));
    query.prepare(QLatin1String(R"(
        SELECT parameter_value1
        FROM parameter
        WHERE parameter_type = 'collection'
        AND parameter_name = :name
    )"));
    query.bindValue(":name", kProtectionName);
    return query.exec() && query.next() && query.value(0).toString() == QLatin1String("true");
}
//----------------------------------------------------------------------
QList<CollectionWriters::Writer> CollectionWriters::writers() const
{
    QList<Writer> list;
    QSqlQuery query(QSqlDatabase::database(connectionName()));
    query.prepare(QLatin1String(R"(
        SELECT parameter_name, parameter_value1
        FROM parameter
        WHERE parameter_type = :type
        ORDER BY parameter_value1, parameter_name
    )"));
    query.bindValue(":type", kWriterType);
    if (!query.exec())
        return list;
    const QString own = thisComputerCode();
    while (query.next()) {
        const QString code = normalizeCode(query.value(0).toString());
        if (code.isEmpty())
            continue;
        if (std::any_of(list.cbegin(), list.cend(), [&code](const Writer &w) { return w.code == code; }))
            continue;
        Writer writer;
        writer.code = code;
        writer.name = query.value(1).toString();
        writer.thisComputer = (code == own);
        list.append(writer);
    }
    return list;
}
//----------------------------------------------------------------------
bool CollectionWriters::mayThisComputerWrite() const
{
    if (!protectionOn())
        return true;
    const QList<Writer> list = writers();
    return std::any_of(list.cbegin(), list.cend(), [](const Writer &w) { return w.thisComputer; });
}
//----------------------------------------------------------------------
bool CollectionWriters::allows(bool protection, const QStringList &codes)
{
    if (!protection)
        return true;
    const QString own = thisComputerCode();
    return std::any_of(codes.cbegin(), codes.cend(),
                       [&own](const QString &code) { return normalizeCode(code) == own; });
}
//----------------------------------------------------------------------
QString CollectionWriters::readOnlyDatabaseName(const QString &path)
{
    // immutable: SQLite neither locks nor reads or creates the -wal and -shm
    // files; what another computer has not yet checkpointed is not seen
    return QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toString(QUrl::FullyEncoded)
           + QStringLiteral("?mode=ro&immutable=1");
}
//----------------------------------------------------------------------
bool CollectionWriters::mayThisComputerWriteFile(const QString &databaseFilePath)
{
    if (databaseFilePath.isEmpty() || !QFileInfo::exists(databaseFilePath))
        return true;
    const QString conn = QStringLiteral("tecaWritersCheck");
    bool protection = false;
    QStringList codes;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setConnectOptions(QLatin1String(kReadOnlyConnectOptions));
        db.setDatabaseName(readOnlyDatabaseName(databaseFilePath));
        if (db.open()) {
            QSqlQuery query(db);
            if (query.exec(QLatin1String(R"(
                    SELECT parameter_name, parameter_type, parameter_value1
                    FROM parameter
                    WHERE parameter_type IN ('collection', 'authorised_writer')
                )"))) {
                while (query.next()) {
                    const QString type = query.value(1).toString();
                    if (type == kWriterType)
                        codes << query.value(0).toString();
                    else if (query.value(0).toString() == kProtectionName)
                        protection = query.value(2).toString() == QLatin1String("true");
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(conn);
    return allows(protection, codes);
}
//----------------------------------------------------------------------
bool CollectionWriters::mayThisComputerWriteFolder(const QString &memoryFolder)
{
    // parameters.csv: header line, then name, type, value1, value2 by tabs
    QFile file(memoryFolder + QStringLiteral("/parameters.csv"));
    if (memoryFolder.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return true;
    bool protection = false;
    QStringList codes;
    QTextStream in(&file);
    in.readLine();
    while (true) {
        const QString line = in.readLine();
        if (line.isNull())
            break;
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() < 3)
            continue;
        if (fields.at(1) == kWriterType)
            codes << fields.at(0);
        else if (fields.at(1) == QLatin1String("collection") && fields.at(0) == kProtectionName)
            protection = fields.at(2) == QLatin1String("true");
    }
    return allows(protection, codes);
}
//----------------------------------------------------------------------
bool CollectionWriters::insertWriter(const QString &code, const QString &name)
{
    QSqlQuery insert(QSqlDatabase::database(connectionName()));
    insert.prepare(QLatin1String(R"(
        INSERT INTO parameter (parameter_name, parameter_type, parameter_value1, parameter_value2)
        VALUES (:name, :type, :value1, '')
    )"));
    insert.bindValue(":name", code);
    insert.bindValue(":type", kWriterType);
    insert.bindValue(":value1", name.isEmpty() ? code : name);
    if (!insert.exec()) {
        qWarning() << "CollectionWriters: writer not added:" << insert.lastError().text();
        return false;
    }
    return true;
}
//----------------------------------------------------------------------
CollectionWriters::Change CollectionWriters::setProtection(bool on)
{
    if (on) {
        const QString own = thisComputerCode();
        const QList<Writer> list = writers();
        if (std::none_of(list.cbegin(), list.cend(), [&own](const Writer &w) { return w.code == own; })
            && !insertWriter(own, thisComputerName()))
            return Change::Failed;
    }

    // Updated when present, inserted otherwise (counted first: MySQL reports
    // no affected row for an UPDATE that changes nothing)
    const QSqlDatabase db = QSqlDatabase::database(connectionName());
    QSqlQuery count(db);
    count.prepare(QLatin1String(R"(
        SELECT COUNT(*) FROM parameter
        WHERE parameter_type = 'collection' AND parameter_name = :name
    )"));
    count.bindValue(":name", kProtectionName);
    const bool present = count.exec() && count.next() && count.value(0).toInt() > 0;

    QSqlQuery write(db);
    write.prepare(present
        ? QLatin1String(R"(
            UPDATE parameter SET parameter_value1 = :value
            WHERE parameter_type = 'collection' AND parameter_name = :name
          )")
        : QLatin1String(R"(
            INSERT INTO parameter (parameter_name, parameter_type, parameter_value1, parameter_value2)
            VALUES (:name, 'collection', :value, '')
          )"));
    write.bindValue(":name", kProtectionName);
    write.bindValue(":value", on ? QStringLiteral("true") : QStringLiteral("false"));
    if (!write.exec()) {
        qWarning() << "CollectionWriters: protection not saved:" << write.lastError().text();
        saveToFile();
        return Change::Failed;
    }
    saveToFile();
    return Change::Ok;
}
//----------------------------------------------------------------------
CollectionWriters::Change CollectionWriters::addWriter(const QString &typedCode, const QString &name)
{
    const QString code = normalizeCode(typedCode);
    if (code.isEmpty())
        return Change::InvalidCode;
    const QList<Writer> list = writers();
    if (std::any_of(list.cbegin(), list.cend(), [&code](const Writer &w) { return w.code == code; }))
        return Change::AlreadyListed;
    if (!insertWriter(code, cleanName(name)))
        return Change::Failed;
    saveToFile();
    return Change::Ok;
}
//----------------------------------------------------------------------
CollectionWriters::Change CollectionWriters::removeWriter(const QString &typedCode)
{
    const QString code = normalizeCode(typedCode);
    const QList<Writer> list = writers();
    if (code.isEmpty()
        || std::none_of(list.cbegin(), list.cend(), [&code](const Writer &w) { return w.code == code; }))
        return Change::NotListed;
    if (protectionOn() && list.size() <= 1)
        return Change::LastWriter;

    QSqlQuery remove(QSqlDatabase::database(connectionName()));
    remove.prepare(QLatin1String(R"(
        DELETE FROM parameter
        WHERE parameter_type = :type AND parameter_name = :name
    )"));
    remove.bindValue(":type", kWriterType);
    remove.bindValue(":name", code);
    if (!remove.exec()) {
        qWarning() << "CollectionWriters: writer not removed:" << remove.lastError().text();
        return Change::Failed;
    }
    saveToFile();
    return Change::Ok;
}
//----------------------------------------------------------------------
void CollectionWriters::saveToFile() const
{
    // No-op unless the collection is in Memory mode
    if (m_collection)
        m_collection->saveParameterTableToFile();
}
