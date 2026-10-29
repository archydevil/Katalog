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
// File Name:   mdivolumes.cpp
// Purpose:     Connected drives, mounted or not (fork, owner's request 2026-10-29)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdivolumes.h"

#include <QProcess>
#include <QStorageInfo>
#include <QXmlStreamReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <functional>

namespace {

QByteArray run(const QString &program, const QStringList &args, int *exitCode = nullptr, int timeoutMs = 20000)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(5000) || !p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        if (exitCode) *exitCode = -1;
        return QByteArray();
    }
    if (exitCode) *exitCode = p.exitCode();
    return p.readAllStandardOutput() + p.readAllStandardError();
}

#if defined(Q_OS_MACOS)
// Minimal XML property-list reader: dict, array, string, integer, real, true, false
QVariant readPlistValue(QXmlStreamReader &xml)
{
    const QString tag = xml.name().toString();
    if (tag == QLatin1String("dict")) {
        QVariantMap map;
        QString key;
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("key"))
                key = xml.readElementText();
            else
                map.insert(key, readPlistValue(xml));
        }
        return map;
    }
    if (tag == QLatin1String("array")) {
        QVariantList list;
        while (xml.readNextStartElement())
            list.append(readPlistValue(xml));
        return list;
    }
    if (tag == QLatin1String("true"))  { xml.skipCurrentElement(); return true; }
    if (tag == QLatin1String("false")) { xml.skipCurrentElement(); return false; }
    if (tag == QLatin1String("integer")) return xml.readElementText().toLongLong();
    if (tag == QLatin1String("real"))    return xml.readElementText().toDouble();
    return xml.readElementText();   // string, date, data
}

QVariantMap plist(const QByteArray &bytes)
{
    QXmlStreamReader xml(bytes);
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("plist"))
            continue;
        return readPlistValue(xml).toMap();
    }
    return {};
}

QString thirdPartyDriver(const QString &fileSystemName)
{
    const QString fs = fileSystemName.toLower();
    if (fs.contains(QLatin1String("ufsd")))   return QStringLiteral("Paragon NTFS");
    if (fs.contains(QLatin1String("tuxera"))) return QStringLiteral("Tuxera NTFS");
    if (fs.contains(QLatin1String("fuse")))   return QStringLiteral("macFUSE");
    return QString();
}

QVariantMap macVolume(const QString &device)
{
    const QVariantMap info = plist(run(QStringLiteral("/usr/sbin/diskutil"), {"info", "-plist", device}));
    QVariantMap v;
    if (info.isEmpty())
        return v;
    const QString mountPoint = info.value("MountPoint").toString();
    v["device"]     = device;
    v["name"]       = info.value("VolumeName").toString();
    v["fileSystem"] = info.value("FilesystemUserVisibleName").toString();
    v["driver"]     = thirdPartyDriver(info.value("FilesystemName").toString());
    v["mounted"]    = !mountPoint.isEmpty();
    v["mountPoint"] = mountPoint;
    v["readOnly"]   = !info.value("WritableVolume").toBool();
    v["size"]       = info.value("TotalSize").toLongLong();
    v["protocol"]   = info.value("BusProtocol").toString();
    v["uuid"]       = info.value("VolumeUUID").toString();
    return v;
}
#endif

} // namespace

QList<QVariantMap> MdiVolumes::list()
{
    QList<QVariantMap> volumes;
#if defined(Q_OS_MACOS)
    const QVariantMap all = plist(run(QStringLiteral("/usr/sbin/diskutil"), {"list", "-plist", "external"}));
    static const QStringList skipped = {"EFI", "Apple_Boot", "Apple_APFS_Recovery", "Apple_APFS_ISC", "Apple_KernelCoreDump"};
    for (const QVariant &d : all.value("AllDisksAndPartitions").toList()) {
        const QVariantMap disk = d.toMap();
        QVariantList parts = disk.value("Partitions").toList();
        parts += disk.value("APFSVolumes").toList();   // volumes of an external APFS container
        for (const QVariant &p : parts) {
            const QVariantMap part = p.toMap();
            if (skipped.contains(part.value("Content").toString()) || part.value("Content").toString() == QLatin1String("Apple_APFS"))
                continue;
            QVariantMap v = macVolume(part.value("DeviceIdentifier").toString());
            if (v.isEmpty() || v.value("fileSystem").toString().isEmpty())
                continue;
            if (v.value("protocol").toString() == QLatin1String("Disk Image"))
                continue;   // mounted installers and images are not drives
            volumes.append(v);
        }
    }
#elif defined(Q_OS_LINUX)
    const QJsonObject doc = QJsonDocument::fromJson(run(QStringLiteral("lsblk"),
        {"-J", "-b", "-o", "PATH,LABEL,FSTYPE,MOUNTPOINT,SIZE,UUID,HOTPLUG,TRAN,TYPE,RO"})).object();
    std::function<void(const QJsonArray &, const QString &)> walk = [&](const QJsonArray &devices, const QString &tran) {
        for (const QJsonValue &value : devices) {
            const QJsonObject o = value.toObject();
            const QString transport = o.value("tran").toString(tran);
            if (!o.value("fstype").toString().isEmpty()
                && (o.value("hotplug").toBool() || transport == QLatin1String("usb"))) {
                QVariantMap v;
                v["device"]     = o.value("path").toString();
                v["name"]       = o.value("label").toString();
                v["fileSystem"] = o.value("fstype").toString().toUpper();
                v["driver"]     = QString();
                v["mounted"]    = !o.value("mountpoint").toString().isEmpty();
                v["mountPoint"] = o.value("mountpoint").toString();
                v["readOnly"]   = o.value("ro").toBool();
                v["size"]       = o.value("size").toVariant().toLongLong();
                v["protocol"]   = transport.toUpper();
                v["uuid"]       = o.value("uuid").toString();
                volumes.append(v);
            }
            walk(o.value("children").toArray(), transport);
        }
    };
    walk(doc.value("blockdevices").toArray(), QString());
#else
    // Windows: NTFS is native; every mounted volume can be cataloged
    for (const QStorageInfo &s : QStorageInfo::mountedVolumes()) {
        if (!s.isValid() || !s.isReady())
            continue;
        QVariantMap v;
        v["device"]     = QString::fromUtf8(s.device());
        v["name"]       = s.name();
        v["fileSystem"] = QString::fromUtf8(s.fileSystemType());
        v["driver"]     = QString();
        v["mounted"]    = true;
        v["mountPoint"] = s.rootPath();
        v["readOnly"]   = s.isReadOnly();
        v["size"]       = s.bytesTotal();
        v["protocol"]   = QString();
        v["uuid"]       = QString();
        volumes.append(v);
    }
#endif
    return volumes;
}

bool MdiVolumes::mountReadOnly(const QString &device, QString *mountPoint, QString *reason)
{
#if defined(Q_OS_MACOS)
    int code = 0;
    const QString out = QString::fromUtf8(run(QStringLiteral("/usr/sbin/diskutil"), {"mount", "readOnly", device}, &code, 60000)).trimmed();
    const QVariantMap v = macVolume(device);
    if (code == 0 && v.value("mounted").toBool()) {
        *mountPoint = v.value("mountPoint").toString();
        return true;
    }
    const QString driver = v.value("driver").toString();
    *reason = driver.isEmpty()
              ? (out.isEmpty() ? QStringLiteral("the system could not mount this drive") : out.section('\n', -1))
              : QStringLiteral("the %1 driver installed on this computer could not mount this drive").arg(driver);
    return false;
#elif defined(Q_OS_LINUX)
    int code = 0;
    const QString out = QString::fromUtf8(run(QStringLiteral("udisksctl"),
        {"mount", "-b", device, "-o", "ro", "--no-user-interaction"}, &code, 60000)).trimmed();
    if (code == 0) {
        // "Mounted /dev/sdb1 at /media/user/Label"
        *mountPoint = out.section(QStringLiteral(" at "), 1).trimmed();
        if (mountPoint->endsWith('.'))
            mountPoint->chop(1);
        return true;
    }
    *reason = out.isEmpty() ? QStringLiteral("the system could not mount this drive") : out.section('\n', -1);
    return false;
#else
    Q_UNUSED(device);
    Q_UNUSED(mountPoint);
    *reason = QStringLiteral("drives are mounted by Windows itself");
    return false;
#endif
}
