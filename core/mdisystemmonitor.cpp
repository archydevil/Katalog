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
// File Name:   mdisystemmonitor.cpp
// Purpose:     Resource monitor for the Activity page (fork only, SpecMediaIntelligence.md)
/////////////////////////////////////////////////////////////////////////////
*/
#include "core/mdisystemmonitor.h"
#include "core/mdienrichmentjob.h"

#include <QCoreApplication>
#include <QProcess>
#include <QTimer>
#include <QThread>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

namespace {

// Short-lived system program; its output, empty when it fails or is absent
QString run(const QString &program, const QStringList &args, int timeoutMs = 3000)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(timeoutMs) || !p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(500);
        return QString();
    }
    return QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
}

double gb(double bytes)
{
    return qRound(bytes / 1e8) / 10.0;
}

qint64 number(const QString &text, const QString &pattern)
{
    const QRegularExpressionMatch m = QRegularExpression(pattern).match(text);
    return m.hasMatch() ? m.captured(1).toLongLong() : -1;
}

QString toolVersion(const QString &program, const QStringList &args)
{
    const QString out = run(program, args);
    // First line holding a version number
    for (const QString &line : out.split('\n')) {
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("(\\d+\\.\\d+(?:\\.\\d+)?)")).match(line);
        if (m.hasMatch())
            return m.captured(1);
    }
    return QString();
}

} // namespace

MdiSystemMonitor::MdiSystemMonitor(QObject *parent)
    : QObject(parent)
    , m_appPid(QCoreApplication::applicationPid())
{
}

void MdiSystemMonitor::setOllamaUrl(const QString &url)
{
    m_ollamaUrl = url;
}

void MdiSystemMonitor::start(int intervalMs)
{
    if (!m_timer) {
        m_timer = new QTimer(this);   // lives on the monitor's thread
        connect(m_timer, &QTimer::timeout, this, &MdiSystemMonitor::sampleNow);
    }
    m_timer->start(intervalMs);
    sampleNow();
}

void MdiSystemMonitor::stop()
{
    if (m_timer)
        m_timer->stop();
}

void MdiSystemMonitor::sampleNow()
{
    QVariantMap s;
    s[QStringLiteral("at")]    = QDateTime::currentDateTime().toString(Qt::ISODate);
    const int cores = QThread::idealThreadCount();
    s[QStringLiteral("cores")] = cores;

#if defined(Q_OS_MACOS)
    if (m_machine.isEmpty())
        m_machine = run(QStringLiteral("/usr/sbin/sysctl"), {"-n", "machdep.cpu.brand_string"}).trimmed();

    // Memory: active + wired + compressed pages, as Activity Monitor counts it
    const QString vm = run(QStringLiteral("/usr/bin/vm_stat"), {});
    const qint64 page = number(vm, QStringLiteral("page size of (\\d+)"));
    const qint64 active = number(vm, QStringLiteral("Pages active:\\s+(\\d+)"));
    const qint64 wired  = number(vm, QStringLiteral("Pages wired down:\\s+(\\d+)"));
    const qint64 compr  = number(vm, QStringLiteral("Pages occupied by compressor:\\s+(\\d+)"));
    const qint64 total  = run(QStringLiteral("/usr/sbin/sysctl"), {"-n", "hw.memsize"}).trimmed().toLongLong();
    if (page > 0 && active >= 0 && wired >= 0 && total > 0) {
        s[QStringLiteral("memUsedGb")]  = gb(double(page) * double(active + wired + qMax<qint64>(0, compr)));
        s[QStringLiteral("memTotalGb")] = gb(double(total));
    }
    const QRegularExpressionMatch swap = QRegularExpression(QStringLiteral("used = ([\\d.]+)M"))
        .match(run(QStringLiteral("/usr/sbin/sysctl"), {"-n", "vm.swapusage"}));
    if (swap.hasMatch())
        s[QStringLiteral("swapUsedGb")] = qRound(swap.captured(1).toDouble() / 102.4) / 10.0;
    const qint64 freePercent = number(run(QStringLiteral("/usr/bin/memory_pressure"), {"-Q"}),
                                      QStringLiteral("free percentage: (\\d+)%"));
    if (freePercent >= 0)
        s[QStringLiteral("memPressure")] = int(100 - freePercent);

    const qint64 gpu = number(run(QStringLiteral("/usr/sbin/ioreg"), {"-r", "-d", "1", "-w", "0", "-c", "IOAccelerator"}),
                              QStringLiteral("\"Device Utilization %\"=(\\d+)"));
    if (gpu >= 0)
        s[QStringLiteral("gpu")] = int(gpu);

    const QString batt = run(QStringLiteral("/usr/bin/pmset"), {"-g", "batt"});
    if (!batt.isEmpty()) {
        s[QStringLiteral("power")] = batt.contains(QLatin1String("'AC Power'")) ? QStringLiteral("ac")
                                                                               : QStringLiteral("battery");
        const qint64 pct = number(batt, QStringLiteral("(\\d+)%;"));
        if (pct >= 0)
            s[QStringLiteral("batteryPercent")] = int(pct);
    }
#elif defined(Q_OS_LINUX)
    if (m_machine.isEmpty()) {
        QFile cpuinfo(QStringLiteral("/proc/cpuinfo"));
        if (cpuinfo.open(QIODevice::ReadOnly))
            m_machine = QRegularExpression(QStringLiteral("model name\\s*:\\s*(.+)"))
                            .match(QString::fromUtf8(cpuinfo.readAll())).captured(1).trimmed();
    }
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (meminfo.open(QIODevice::ReadOnly)) {
        const QString m = QString::fromUtf8(meminfo.readAll());
        const qint64 total = number(m, QStringLiteral("MemTotal:\\s+(\\d+)"));
        const qint64 avail = number(m, QStringLiteral("MemAvailable:\\s+(\\d+)"));
        const qint64 swapT = number(m, QStringLiteral("SwapTotal:\\s+(\\d+)"));
        const qint64 swapF = number(m, QStringLiteral("SwapFree:\\s+(\\d+)"));
        if (total > 0 && avail >= 0) {
            s[QStringLiteral("memUsedGb")]   = gb((total - avail) * 1024.0);
            s[QStringLiteral("memTotalGb")]  = gb(total * 1024.0);
            s[QStringLiteral("memPressure")] = int(100 * (total - avail) / total);
        }
        if (swapT >= 0 && swapF >= 0)
            s[QStringLiteral("swapUsedGb")] = gb((swapT - swapF) * 1024.0);
    }
#endif
    if (!m_machine.isEmpty())
        s[QStringLiteral("machine")] = m_machine;

#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    // Processes: overall CPU, Katalog and the programs it started, Ollama
    const QString ps = run(QStringLiteral("/bin/ps"), {"-A", "-o", "pid=,ppid=,%cpu=,rss=,comm="});
    double cpuSum = 0.0;
    double ollamaCpu = 0.0, ollamaMem = 0.0;
    bool ollamaSeen = false;
    QVariantList processes;
    for (const QString &line : ps.split('\n', Qt::SkipEmptyParts)) {
        const QStringList f = line.simplified().split(' ');
        if (f.size() < 5)
            continue;
        const qint64 pid  = f[0].toLongLong();
        const qint64 ppid = f[1].toLongLong();
        const double cpu  = f[2].toDouble();
        const double memMb = f[3].toDouble() / 1024.0;
        const QString command = f.mid(4).join(' ');
        const QString name = QFileInfo(command).fileName();
        cpuSum += cpu;
        // The monitor's own sampling programs are not part of the work
        static const QStringList samplers = {"ps", "vm_stat", "sysctl", "memory_pressure", "ioreg", "pmset"};
        if (ppid == m_appPid && samplers.contains(name))
            continue;
        if (pid == m_appPid || ppid == m_appPid) {
            QVariantMap p;
            p[QStringLiteral("pid")]   = pid;
            p[QStringLiteral("name")]  = pid == m_appPid ? QStringLiteral("Katalog") : name;
            p[QStringLiteral("role")]  = pid == m_appPid ? QStringLiteral("katalog") : QStringLiteral("tool");
            p[QStringLiteral("cpu")]   = qRound(cpu);
            p[QStringLiteral("memMb")] = qRound(memMb);
            processes.append(p);
        } else if (name == QLatin1String("ollama") || name == QLatin1String("llama-server")
                   || name.startsWith(QLatin1String("ollama_"))) {
            ollamaSeen = true;
            ollamaCpu += cpu;
            ollamaMem += memMb;
        }
    }
    if (ollamaSeen) {
        QVariantMap p;
        p[QStringLiteral("name")]  = QStringLiteral("Ollama");
        p[QStringLiteral("role")]  = QStringLiteral("ollama");
        p[QStringLiteral("cpu")]   = qRound(ollamaCpu);
        p[QStringLiteral("memMb")] = qRound(ollamaMem);
        processes.append(p);
    }
    s[QStringLiteral("processes")] = processes;
    if (cores > 0)
        s[QStringLiteral("cpu")] = qMin(100, qRound(cpuSum / cores));
#endif

    // Ollama: running, and the models it holds in memory
    QVariantMap ollama;
    {
        QNetworkAccessManager manager;
        QNetworkRequest request(QUrl(m_ollamaUrl + QStringLiteral("/api/ps")));
        request.setTransferTimeout(1500);
        QNetworkReply *reply = manager.get(request);
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        ollama[QStringLiteral("running")] = reply->error() == QNetworkReply::NoError;
        if (reply->error() == QNetworkReply::NoError) {
            QVariantList models;
            for (const QJsonValue &v : QJsonDocument::fromJson(reply->readAll()).object().value("models").toArray()) {
                const QJsonObject o = v.toObject();
                QVariantMap m;
                m[QStringLiteral("name")]          = o.value("name").toString();
                m[QStringLiteral("sizeGb")]        = gb(o.value("size").toDouble());
                m[QStringLiteral("vramGb")]        = gb(o.value("size_vram").toDouble());
                m[QStringLiteral("contextLength")] = o.value("context_length").toInt();
                models.append(m);
            }
            ollama[QStringLiteral("models")] = models;
        }
        reply->deleteLater();
    }
    s[QStringLiteral("ollama")] = ollama;

    emit snapshot(s);
}

QVariantMap MdiSystemMonitor::toolsInfo(const QVariantMap &configuredPaths)
{
    QVariantMap tools;
    auto add = [&](const QString &key, const QString &program, const QStringList &versionArgs) {
        const QString path = MdiEnrichmentJob::findTool(program, configuredPaths.value(key).toString());
        if (path.isEmpty())
            return;
        QVariantMap t;
        t[QStringLiteral("path")] = path;
        const QString version = versionArgs.isEmpty() ? QString() : toolVersion(path, versionArgs);
        if (!version.isEmpty())
            t[QStringLiteral("version")] = version;
        tools[key] = t;
    };
    add(QStringLiteral("ffmpeg"),    QStringLiteral("ffmpeg"),    {"-version"});
    add(QStringLiteral("ffprobe"),   QStringLiteral("ffprobe"),   {"-version"});
    add(QStringLiteral("pdftotext"), QStringLiteral("pdftotext"), {"-v"});
    add(QStringLiteral("whisper"),   QStringLiteral("whisper-cli"), {});
    const QString model = MdiEnrichmentJob::findWhisperModel(configuredPaths.value(QStringLiteral("whisperModel")).toString());
    if (!model.isEmpty()) {
        QVariantMap w = tools.value(QStringLiteral("whisper")).toMap();
        w[QStringLiteral("model")]   = QFileInfo(model).fileName();
        w[QStringLiteral("modelMb")] = qRound(QFileInfo(model).size() / 1048576.0);
        tools[QStringLiteral("whisper")] = w;
    }
    return tools;
}
