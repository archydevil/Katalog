import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Activity page (fork, SpecMediaIntelligence.md MDI-F61..F75, D19-D21): what is
// running, where, with which resources and for what. Values come from the core
// resource monitor (sampled only while this page is shown or a job runs) and
// from the runner's per-step activity. History is kept for the session only.
ColumnLayout {
    id: pageActivity
    spacing: Kirigami.Units.largeSpacing

    readonly property var sys:  appManager1.mdiSystem
    readonly property var job:  appManager1.mdiJobInfo
    readonly property var step: appManager1.mdiStep
    property var tools: ({})

    // Called by Main.qml when the page is shown or hidden
    function setShown(shown) {
        appManager1.setActivityMonitoring(shown)
        if (shown)
            tools = appManager1.mdiToolsInfo()
    }

    function whereText(where) {
        if (where === "local")         return qsTr("On this computer")
        if (where === "local-network") return qsTr("On the local network")
        if (where === "remote")        return qsTr("On the Internet")
        return ""
    }
    function seconds(ms) {
        const s = Math.round(ms / 100) / 10
        return s < 60 ? s + " s" : Math.floor(s / 60) + " min " + Math.round(s % 60) + " s"
    }
    function valueOr(v, suffix) {
        return (v === undefined || v === null || v < 0) ? "—" : v + suffix
    }

    // Elapsed time of the running step, refreshed every second
    property int stepElapsed: 0
    Timer {
        interval: 1000; repeat: true
        running: appManager1.mdiIsRunning && pageActivity.step.startedAt !== undefined
        onTriggered: pageActivity.stepElapsed = Math.max(0, (Date.now() - Date.parse(pageActivity.step.startedAt)) / 1000)
        onRunningChanged: pageActivity.stepElapsed = 0
    }

    // ── Status line ──────────────────────────────────────────────────────────
    Kirigami.Card {
        Layout.fillWidth: true
        contentItem: RowLayout {
            spacing: Kirigami.Units.largeSpacing
            Rectangle {
                width: Kirigami.Units.gridUnit * 0.8; height: width; radius: width / 2
                color: !appManager1.mdiIsRunning ? Kirigami.Theme.disabledTextColor
                       : appManager1.mdiIsPaused || pageActivity.job.waitingForSource ? Kirigami.Theme.neutralTextColor
                                                 : Kirigami.Theme.positiveTextColor
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.bold: true
                    text: !appManager1.mdiIsRunning
                          ? qsTr("Nothing running")
                          : (appManager1.mdiIsPaused ? qsTr("Paused")
                             : pageActivity.job.waitingForSource ? qsTr("Waiting for the drive") : qsTr("In Progress"))
                            + " · " + pageActivity.job.catalog
                            + (pageActivity.job.fileCount > 0
                               ? " · " + qsTr("File %1 of %2").arg(pageActivity.job.fileIndex).arg(pageActivity.job.fileCount) : "")
                            + (pageActivity.job.endsAt ? " · " + qsTr("Estimated end %1").arg(pageActivity.job.endsAt) : "")
                }
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: (pageActivity.sys.machine ?? "")
                          + (pageActivity.sys.cores ? " · " + qsTr("%1 cores").arg(pageActivity.sys.cores) : "")
                          + (pageActivity.job.waiting && pageActivity.job.waiting.length > 0
                             ? " · " + qsTr("Waiting: %1").arg(pageActivity.job.waiting.join(", ")) : "")
                }
            }
            Controls.Button {
                visible: appManager1.mdiIsRunning
                text: appManager1.mdiIsPaused ? qsTr("Resume") : qsTr("Pause")
                icon.name: appManager1.mdiIsPaused ? "media-playback-start" : "media-playback-pause"
                onClicked: appManager1.setEnrichmentPaused(!appManager1.mdiIsPaused)
            }
            Controls.Button {
                visible: appManager1.mdiIsRunning
                text: qsTr("Stop")
                icon.name: "process-stop"
                onClicked: appManager1.stopEnrichment()
            }
        }
    }

    // ── Resource tiles ───────────────────────────────────────────────────────
    GridLayout {
        Layout.fillWidth: true
        columns: Math.max(2, Math.floor(width / (Kirigami.Units.gridUnit * 10)))
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: Kirigami.Units.largeSpacing

        Repeater {
            model: [
                { icon: "cpu",               label: qsTr("CPU"),
                  value: pageActivity.valueOr(pageActivity.sys.cpu, "%"), warn: pageActivity.sys.cpu > 90 },
                { icon: "video-display",     label: qsTr("GPU"),
                  value: pageActivity.valueOr(pageActivity.sys.gpu, "%"), warn: false },
                { icon: "computer",          label: qsTr("Memory"),
                  value: pageActivity.sys.memUsedGb !== undefined
                         ? qsTr("%1 of %2 GB").arg(pageActivity.sys.memUsedGb).arg(pageActivity.sys.memTotalGb) : "—",
                  warn: pageActivity.sys.memTotalGb > 0 && pageActivity.sys.memUsedGb / pageActivity.sys.memTotalGb > 0.9 },
                { icon: "drive-harddisk",    label: qsTr("Swap"),
                  value: pageActivity.valueOr(pageActivity.sys.swapUsedGb, " GB"), warn: pageActivity.sys.swapUsedGb > 8 },
                { icon: "speedometer",       label: qsTr("Memory pressure"),
                  value: pageActivity.valueOr(pageActivity.sys.memPressure, "%"), warn: pageActivity.sys.memPressure > 75 },
                { icon: "battery",           label: qsTr("Power"),
                  value: pageActivity.sys.power === "ac" ? qsTr("Mains")
                         : pageActivity.sys.power === "battery" ? qsTr("Battery") + " " + pageActivity.valueOr(pageActivity.sys.batteryPercent, "%")
                         : "—",
                  warn: pageActivity.sys.power === "battery" && pageActivity.sys.batteryPercent < 20 },
                { icon: "drive-removable-media", label: qsTr("Source drive"),
                  value: !appManager1.mdiIsRunning ? "—"
                         : pageActivity.job.sourceConnected ? qsTr("Connected") : qsTr("Not connected"),
                  warn: appManager1.mdiIsRunning && !pageActivity.job.sourceConnected },
                { icon: "network-connect",   label: qsTr("Data sent"),
                  value: pageActivity.job.egress && pageActivity.job.egress.length > 0
                         ? pageActivity.job.egress.join(", ") : qsTr("Nothing sent"),
                  warn: pageActivity.job.egress && pageActivity.job.egress.length > 0 }
            ]
            delegate: Kirigami.Card {
                required property var modelData
                Layout.fillWidth: true
                contentItem: ColumnLayout {
                    spacing: 0
                    RowLayout {
                        Kirigami.Icon { source: modelData.icon; implicitWidth: Kirigami.Units.iconSizes.small; implicitHeight: implicitWidth }
                        Controls.Label { text: modelData.label; opacity: 0.7; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: modelData.value
                        font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.5
                        color: modelData.warn ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
                    }
                }
            }
        }
    }

    // ── Right now ────────────────────────────────────────────────────────────
    Kirigami.Card {
        Layout.fillWidth: true
        visible: appManager1.mdiIsRunning
        header: Kirigami.Heading { text: qsTr("Right now"); level: 3; padding: Kirigami.Units.smallSpacing }
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.bold: true
                text: pageActivity.step.label
                      ? pageActivity.step.label + " · " + pageActivity.step.fileName
                      : appManager1.mdiIsPaused ? qsTr("Paused")
                      // MDI-F91: nothing runs until the source drive is back
                      : pageActivity.job.waitingForSource
                        ? qsTr("The analysis continues by itself when %1 is back.").arg(pageActivity.job.drivePath)
                        : qsTr("Checking what is already done")
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.8
                visible: pageActivity.step.tool !== undefined
                text: (pageActivity.step.tool ?? "")
                      + (pageActivity.step.model ? " · " + pageActivity.step.model : "")
                      + " · " + pageActivity.whereText(pageActivity.step.where)
                      + " · " + pageActivity.seconds(pageActivity.stepElapsed * 1000)
            }
            Controls.ProgressBar {
                Layout.fillWidth: true
                visible: !pageActivity.job.waitingForSource
                from: 0; to: 1
                indeterminate: pageActivity.step.progress === undefined
                value: pageActivity.step.progress ?? 0
            }
            Controls.Label {
                visible: pageActivity.step.detail !== undefined
                text: pageActivity.step.detail ?? ""
                opacity: 0.7
            }
        }
    }

    // ── Resource history (session only, D21) ─────────────────────────────────
    Kirigami.Card {
        Layout.fillWidth: true
        header: Kirigami.Heading { text: qsTr("Since this session started"); level: 3; padding: Kirigami.Units.smallSpacing }
        contentItem: ColumnLayout {
            Canvas {
                id: historyChart
                Layout.fillWidth: true
                Layout.preferredHeight: Kirigami.Units.gridUnit * 8
                readonly property var series: [
                    { key: "cpu", color: Kirigami.Theme.highlightColor },
                    { key: "gpu", color: Kirigami.Theme.positiveTextColor },
                    { key: "mem", color: Kirigami.Theme.neutralTextColor }
                ]
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const h = appManager1.mdiHistory
                    ctx.strokeStyle = Kirigami.Theme.disabledTextColor
                    ctx.globalAlpha = 0.3
                    for (let g = 0; g <= 4; g++) {
                        const y = height - height * g / 4
                        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
                    }
                    ctx.globalAlpha = 1
                    if (h.length < 2) return
                    ctx.lineWidth = 1.5
                    for (const s of series) {
                        ctx.strokeStyle = s.color
                        ctx.beginPath()
                        let started = false
                        for (let i = 0; i < h.length; i++) {
                            const v = h[i][s.key]
                            if (v === undefined || v < 0) continue
                            const x = width * i / (h.length - 1)
                            const y = height - height * Math.min(100, v) / 100
                            if (!started) { ctx.moveTo(x, y); started = true } else ctx.lineTo(x, y)
                        }
                        ctx.stroke()
                    }
                }
                Connections {
                    target: appManager1
                    function onMdiSystemChanged() { historyChart.requestPaint() }
                }
            }
            RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Repeater {
                    model: [ { label: qsTr("CPU"), color: Kirigami.Theme.highlightColor },
                             { label: qsTr("GPU"), color: Kirigami.Theme.positiveTextColor },
                             { label: qsTr("Memory"), color: Kirigami.Theme.neutralTextColor } ]
                    delegate: RowLayout {
                        required property var modelData
                        Rectangle { width: Kirigami.Units.gridUnit; height: 3; color: modelData.color }
                        Controls.Label { text: modelData.label; opacity: 0.8 }
                    }
                }
                Item { Layout.fillWidth: true }
                Controls.Label {
                    opacity: 0.6
                    text: appManager1.mdiHistory.length > 0
                          ? appManager1.mdiHistory[0].t + " – " + appManager1.mdiHistory[appManager1.mdiHistory.length - 1].t : ""
                }
            }
        }
    }

    // ── Programs and models in memory ────────────────────────────────────────
    GridLayout {
        Layout.fillWidth: true
        columns: width > Kirigami.Units.gridUnit * 36 ? 2 : 1
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: Kirigami.Units.largeSpacing

        Kirigami.Card {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            header: Kirigami.Heading { text: qsTr("Programs"); level: 3; padding: Kirigami.Units.smallSpacing }
            contentItem: ColumnLayout {
                Repeater {
                    model: pageActivity.sys.processes ?? []
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Kirigami.Icon {
                            source: modelData.role === "ollama" ? "preferences-system-network"
                                    : modelData.role === "tool" ? "system-run" : "katalog"
                            implicitWidth: Kirigami.Units.iconSizes.small; implicitHeight: implicitWidth
                        }
                        Controls.Label { text: modelData.name; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Controls.Label { text: qsTr("CPU") + " " + modelData.cpu + "%"; opacity: 0.8 }
                        Controls.Label { text: modelData.memMb >= 1024 ? (Math.round(modelData.memMb / 102.4) / 10) + " GB" : modelData.memMb + " MB"; opacity: 0.8 }
                    }
                }
                Controls.Label {
                    visible: (pageActivity.sys.processes ?? []).length === 0
                    text: "—"
                    opacity: 0.6
                }
            }
        }

        Kirigami.Card {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            header: Kirigami.Heading { text: qsTr("Models in memory"); level: 3; padding: Kirigami.Units.smallSpacing }
            contentItem: ColumnLayout {
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Ollama · " + ((pageActivity.sys.ollama ?? {}).running ? qsTr("Connected") : qsTr("Not connected"))
                          + (pageActivity.tools.ollamaUrl ? " · " + pageActivity.tools.ollamaUrl : "")
                    color: (pageActivity.sys.ollama ?? {}).running ? Kirigami.Theme.textColor : Kirigami.Theme.negativeTextColor
                }
                Repeater {
                    model: (pageActivity.sys.ollama ?? {}).models ?? []
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Kirigami.Icon { source: "preferences-desktop-ai"; fallback: "applications-science"; implicitWidth: Kirigami.Units.iconSizes.small; implicitHeight: implicitWidth }
                        Controls.Label { text: modelData.name; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Controls.Label { text: modelData.sizeGb + " GB"; opacity: 0.8 }
                    }
                }
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: qsTr("Images: %1 · Texts: %2 · Search by meaning: %3")
                          .arg(pageActivity.tools.visionModel ?? "—").arg(pageActivity.tools.textModel ?? "—")
                          .arg(pageActivity.tools.embedModel ?? "—")
                }
            }
        }
    }

    // ── Tools ────────────────────────────────────────────────────────────────
    Kirigami.Card {
        Layout.fillWidth: true
        header: Kirigami.Heading { text: qsTr("Tools"); level: 3; padding: Kirigami.Units.smallSpacing }
        contentItem: Flow {
            spacing: Kirigami.Units.largeSpacing
            Repeater {
                model: [
                    { key: "ffmpeg",    name: "ffmpeg",    use: qsTr("Previews and clips") },
                    { key: "ffprobe",   name: "ffprobe",   use: qsTr("Durations") },
                    { key: "pdftotext", name: "pdftotext", use: qsTr("Text of PDF files") },
                    { key: "whisper",   name: "whisper",   use: qsTr("Transcript") }
                ]
                delegate: RowLayout {
                    required property var modelData
                    readonly property var t: pageActivity.tools[modelData.key]
                    Kirigami.Icon {
                        source: t ? "dialog-ok-apply" : "dialog-error"
                        color: t ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                        implicitWidth: Kirigami.Units.iconSizes.small; implicitHeight: implicitWidth
                    }
                    Controls.Label {
                        text: modelData.name
                              + (t && t.version ? " " + t.version : "")
                              + (t && t.model ? " · " + t.model + " (" + t.modelMb + " MB)" : "")
                              + " — " + (t ? modelData.use : qsTr("Not found"))
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    // ── Log of finished steps ────────────────────────────────────────────────
    Kirigami.Card {
        Layout.fillWidth: true
        header: Kirigami.Heading { text: qsTr("Log"); level: 3; padding: Kirigami.Units.smallSpacing }
        contentItem: ColumnLayout {
            spacing: 2
            Repeater {
                model: appManager1.mdiStepLog.slice(0, 60)
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Controls.Label { text: modelData.time; opacity: 0.6 }
                    Kirigami.Icon {
                        source: modelData.outcome === "completed" ? "dialog-ok-apply"
                                : modelData.outcome === "skipped" ? "dialog-information"
                                : modelData.outcome === "stopped" ? "process-stop" : "dialog-error"
                        color: modelData.outcome === "completed" ? Kirigami.Theme.positiveTextColor
                               : modelData.outcome === "failed" ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.neutralTextColor
                        implicitWidth: Kirigami.Units.iconSizes.small; implicitHeight: implicitWidth
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: modelData.label + " · " + modelData.fileName
                              + " · " + pageActivity.seconds(modelData.durationMs)
                              + (modelData.tokensPerSecond ? " · " + qsTr("%1 tokens/s").arg(modelData.tokensPerSecond) : "")
                              + (modelData.reason ? " — " + modelData.reason : "")
                    }
                }
            }
            Controls.Label {
                visible: appManager1.mdiStepLog.length === 0
                text: qsTr("No step finished in this session.")
                opacity: 0.6
            }
        }
    }

    Item { Layout.preferredHeight: Kirigami.Units.gridUnit }
}
