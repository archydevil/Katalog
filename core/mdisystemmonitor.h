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
// File Name:   mdisystemmonitor.h
// Purpose:     Resource monitor for the Activity page (fork only, SpecMediaIntelligence.md)
// Description: Periodic snapshot of what the machine is doing while Katalog
//              enriches catalogs: CPU, GPU, memory, swap, memory pressure,
//              power, the programs Katalog started (ffmpeg, whisper...) and
//              Ollama with the models it holds in memory. Values come from
//              external programs (ps, vm_stat, sysctl, memory_pressure, ioreg,
//              pmset) on macOS and from /proc on Linux; a value that cannot
//              be read is absent from the snapshot. Nothing is stored.
//
// Usage:
//   QThread *thread = new QThread();
//   MdiSystemMonitor *monitor = new MdiSystemMonitor();
//   monitor->moveToThread(thread);
//   connect(thread, &QThread::finished, monitor, &QObject::deleteLater);
//   connect(monitor, &MdiSystemMonitor::snapshot, receiver, &Receiver::onSnapshot);
//   thread->start();
//   QMetaObject::invokeMethod(monitor, "start", Q_ARG(int, 2000));
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDISYSTEMMONITOR_H
#define MDISYSTEMMONITOR_H

#include <QObject>
#include <QString>
#include <QVariantMap>

class QTimer;

class MdiSystemMonitor : public QObject
{
    Q_OBJECT
public:
    explicit MdiSystemMonitor(QObject *parent = nullptr);

    // Tools the runner uses, with path and version (absent when not found):
    // {ffmpeg: {path, version}, ffprobe, pdftotext, whisper: {path, model, modelMb}}
    // Slower than a snapshot: call it when the page opens, not every tick.
    static QVariantMap toolsInfo(const QVariantMap &configuredPaths = QVariantMap());

public slots:
    void setOllamaUrl(const QString &url);
    void start(int intervalMs = 2000);
    void stop();
    void sampleNow();

signals:
    // Keys (each present only when readable):
    //   at (ISO time), machine, cores,
    //   cpu (0-100), gpu (0-100), memUsedGb, memTotalGb, swapUsedGb, memPressure (0-100),
    //   power ("ac" / "battery"), batteryPercent,
    //   processes: [{pid, name, role ("katalog" / "tool" / "ollama"), cpu, memMb}],
    //   ollama: {running, models: [{name, sizeGb, vramGb, contextLength}]}
    void snapshot(const QVariantMap &metrics);

private:
    QTimer *m_timer = nullptr;
    QString m_ollamaUrl = QStringLiteral("http://localhost:11434");
    qint64  m_appPid = 0;
    QString m_machine;
};

#endif // MDISYSTEMMONITOR_H
