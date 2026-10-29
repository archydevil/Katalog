import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Activity page (fork, SpecMediaIntelligence.md MDI-F61..F75, D19-D21): what is
// running, where, with which resources and for what. Values come from the core
// resource monitor (sampled only while this page is shown or a job runs) and
// from the runner's per-step activity. History is kept for the session only.
//
// Redesign phase R5 (D34), "Work in progress": a card for the running job,
// resource tiles, the step in progress, the queue and the history of finished
// steps with their outcome as info tags, then the programs, models and tools.
ColumnLayout {
    id: pageActivity
    spacing: 20

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
    // On this computer is the expected case; anything else is worth noticing
    function whereTone(where) {
        return where === "local" ? "ok" : "warn"
    }
    function seconds(ms) {
        const s = Math.round(ms / 100) / 10
        return s < 60 ? s + " s" : Math.floor(s / 60) + " min " + Math.round(s % 60) + " s"
    }
    function valueOr(v, suffix) {
        return (v === undefined || v === null || v < 0) ? "—" : v + suffix
    }
    function outcomeText(outcome) {
        return outcome === "completed" ? qsTr("Completed")
             : outcome === "skipped"   ? qsTr("Skipped")
             : outcome === "stopped"   ? qsTr("Stopped") : qsTr("Failed")
    }
    function outcomeTone(outcome) {
        return outcome === "completed" ? "ok" : outcome === "skipped" ? "neutral"
             : outcome === "stopped" ? "warn" : "bad"
    }

    // State of the running job, as words and as a tag tone
    readonly property string stateText: appManager1.mdiIsPaused ? qsTr("Paused")
                                        : job.waitingForSource ? qsTr("Waiting for the drive") : qsTr("In Progress")
    readonly property string stateTone: appManager1.mdiIsPaused || job.waitingForSource ? "warn" : "info"

    // Elapsed time of the running step, refreshed every second
    property int stepElapsed: 0
    Timer {
        interval: 1000; repeat: true
        running: appManager1.mdiIsRunning && pageActivity.step.startedAt !== undefined
        onTriggered: pageActivity.stepElapsed = Math.max(0, (Date.now() - Date.parse(pageActivity.step.startedAt)) / 1000)
        onRunningChanged: pageActivity.stepElapsed = 0
    }

    // ── Building blocks of this page ───────────────────────────────────────
    // Secondary line inside a card
    component Explanation: Controls.Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: KTheme.text2
    }
    // Small print: times, durations, sizes
    component SmallText: Controls.Label {
        color: KTheme.text3
        font.pixelSize: KTheme.note
    }

    // ── Page intro ─────────────────────────────────────────────────────────
    KSectionHeader {
        level: 1
        title: qsTr("Work in progress")
        description: qsTr("What Teca is doing, where it runs and with which resources. A job whose drive is disconnected waits for it and continues by itself.")
    }

    // ── The running job ────────────────────────────────────────────────────
    KCard {
        Layout.fillWidth: true
        padding: 20
        spacing: 14

        KSectionHeader {
            visible: !appManager1.mdiIsRunning
            level: 2
            title: qsTr("Nothing running")
            description: qsTr("An analysis or a re-interpretation shows here while it runs, with its progress.")
        }

        GridLayout {
            id: heroGrid
            visible: appManager1.mdiIsRunning
            Layout.fillWidth: true
            columns: width >= 640 ? 2 : 1
            columnSpacing: KTheme.gap
            rowSpacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 6
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    KInfoTag {
                        text: pageActivity.stateText
                        tone: pageActivity.stateTone
                    }
                    SmallText {
                        height: implicitHeight
                        text: [pageActivity.job.startedAt ? qsTr("Started at %1").arg(pageActivity.job.startedAt) : "",
                               pageActivity.job.endsAt ? qsTr("Estimated end %1").arg(pageActivity.job.endsAt) : ""]
                              .filter(function (s) { return s !== "" }).join(" · ")
                    }
                }
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    color: KTheme.text
                    font.weight: Font.Bold
                    font.pixelSize: KTheme.titleSection
                    text: pageActivity.job.catalog || ""
                }
                Explanation {
                    visible: text !== ""
                    text: [pageActivity.job.fileCount > 0
                           ? qsTr("File %1 of %2").arg(pageActivity.job.fileIndex).arg(pageActivity.job.fileCount) : "",
                           pageActivity.step.label || ""]
                          .filter(function (s) { return s !== "" }).join(" · ")
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignTop | (heroGrid.columns === 2 ? Qt.AlignRight : Qt.AlignLeft)
                spacing: 8
                KButton {
                    variant: "primary"
                    text: appManager1.mdiIsPaused ? qsTr("Resume") : qsTr("Pause")
                    icon.name: appManager1.mdiIsPaused ? "media-playback-start" : "media-playback-pause"
                    onClicked: appManager1.setEnrichmentPaused(!appManager1.mdiIsPaused)
                }
                KButton {
                    variant: "secondary"
                    text: qsTr("Stop")
                    icon.name: "process-stop"
                    onClicked: appManager1.stopEnrichment()
                }
            }
        }
        KProgressBar {
            visible: appManager1.mdiIsRunning
            Layout.fillWidth: true
            implicitHeight: 8
            indeterminate: !(pageActivity.job.fileCount > 0)
            value: pageActivity.job.fileCount > 0 ? pageActivity.job.fileIndex / pageActivity.job.fileCount : 0
        }
    }

    // ── Resource tiles ─────────────────────────────────────────────────────
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 12

        KSectionHeader {
            title: qsTr("Resources")
            description: (pageActivity.sys.machine ?? "")
                         + (pageActivity.sys.cores ? " · " + qsTr("%1 cores").arg(pageActivity.sys.cores) : "")
        }
        GridLayout {
            Layout.fillWidth: true
            columns: Math.max(2, Math.floor(width / 190))
            columnSpacing: KTheme.gap
            rowSpacing: KTheme.gap

            Repeater {
                // tag: a word saying why a value needs attention, never colour alone
                model: [
                    { icon: "cpu",               label: qsTr("Processor"),
                      value: pageActivity.valueOr(pageActivity.sys.cpu, "%"),
                      tag: pageActivity.sys.cpu > 90 ? qsTr("High") : "", tone: "warn" },
                    { icon: "video-display",     label: qsTr("Graphics card"),
                      value: pageActivity.valueOr(pageActivity.sys.gpu, "%"), tag: "", tone: "neutral" },
                    { icon: "computer",          label: qsTr("Memory"),
                      value: pageActivity.sys.memUsedGb !== undefined
                             ? qsTr("%1 of %2 GB").arg(pageActivity.sys.memUsedGb).arg(pageActivity.sys.memTotalGb) : "—",
                      tag: pageActivity.sys.memTotalGb > 0 && pageActivity.sys.memUsedGb / pageActivity.sys.memTotalGb > 0.9
                           ? qsTr("High") : "", tone: "warn" },
                    { icon: "drive-harddisk",    label: qsTr("Swap"),
                      value: pageActivity.valueOr(pageActivity.sys.swapUsedGb, " GB"),
                      tag: pageActivity.sys.swapUsedGb > 8 ? qsTr("High") : "", tone: "warn" },
                    { icon: "speedometer",       label: qsTr("Memory pressure"),
                      value: pageActivity.valueOr(pageActivity.sys.memPressure, "%"),
                      tag: pageActivity.sys.memPressure > 75 ? qsTr("High") : "", tone: "warn" },
                    { icon: "battery",           label: qsTr("Power"),
                      value: pageActivity.sys.power === "ac" ? qsTr("Mains")
                             : pageActivity.sys.power === "battery" ? qsTr("Battery") + " " + pageActivity.valueOr(pageActivity.sys.batteryPercent, "%")
                             : "—",
                      tag: pageActivity.sys.power === "battery" && pageActivity.sys.batteryPercent < 20 ? qsTr("Low") : "",
                      tone: "warn" },
                    { icon: "drive-removable-media", label: qsTr("Source drive"),
                      value: !appManager1.mdiIsRunning ? "—"
                             : pageActivity.job.sourceConnected ? qsTr("Connected") : qsTr("Not connected"),
                      tag: appManager1.mdiIsRunning && !pageActivity.job.sourceConnected && pageActivity.job.waitingForSource
                           ? qsTr("Waiting for the drive") : "", tone: "warn" },
                    { icon: "network-connect",   label: qsTr("Data sent"),
                      value: pageActivity.job.egress && pageActivity.job.egress.length > 0
                             ? pageActivity.job.egress.join(", ") : qsTr("Nothing sent"),
                      tag: pageActivity.job.egress && pageActivity.job.egress.length > 0
                           ? qsTr("Sent outside this computer") : qsTr("On this computer"),
                      tone: pageActivity.job.egress && pageActivity.job.egress.length > 0 ? "warn" : "ok" }
                ]
                delegate: KCard {
                    id: tile
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignTop
                    padding: 16
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Kirigami.Icon {
                            source: tile.modelData.icon
                            isMask: true
                            color: KTheme.text3
                            implicitWidth: 16
                            implicitHeight: 16
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: tile.modelData.label
                            color: KTheme.text3
                            font.pixelSize: KTheme.note
                            font.weight: Font.Medium
                        }
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        text: tile.modelData.value
                        color: KTheme.text
                        font.pixelSize: KTheme.titleSection
                        font.weight: Font.Bold
                    }
                    KInfoTag {
                        visible: tile.modelData.tag !== ""
                        text: tile.modelData.tag
                        tone: tile.modelData.tone
                    }
                }
            }
        }
    }

    // ── Right now ──────────────────────────────────────────────────────────
    KCard {
        Layout.fillWidth: true
        visible: appManager1.mdiIsRunning

        KSectionHeader { level: 3; title: qsTr("Right now") }
        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            color: KTheme.text
            font.weight: Font.DemiBold
            text: pageActivity.step.label
                  ? pageActivity.step.label + " · " + pageActivity.step.fileName
                  : appManager1.mdiIsPaused ? qsTr("Paused")
                  // MDI-F91: nothing runs until the source drive is back
                  : pageActivity.job.waitingForSource
                    ? qsTr("The analysis continues by itself when %1 is back.").arg(pageActivity.job.drivePath)
                    : qsTr("Checking what is already done")
        }
        Flow {
            Layout.fillWidth: true
            visible: pageActivity.step.tool !== undefined
            spacing: 6
            KInfoTag { text: pageActivity.step.tool ?? "" }
            KInfoTag {
                visible: !!pageActivity.step.model
                text: pageActivity.step.model ?? ""
                tone: "ai"
            }
            KInfoTag {
                visible: text !== ""
                text: pageActivity.whereText(pageActivity.step.where)
                tone: pageActivity.whereTone(pageActivity.step.where)
            }
            KInfoTag { text: pageActivity.seconds(pageActivity.stepElapsed * 1000) }
        }
        KProgressBar {
            Layout.fillWidth: true
            visible: !pageActivity.job.waitingForSource
            indeterminate: pageActivity.step.progress === undefined
            value: pageActivity.step.progress ?? 0
        }
        SmallText {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: pageActivity.step.detail !== undefined
            text: pageActivity.step.detail ?? ""
        }
    }

    // ── Queue and history ──────────────────────────────────────────────────
    GridLayout {
        Layout.fillWidth: true
        columns: width >= 760 ? 2 : 1
        columnSpacing: KTheme.gap
        rowSpacing: KTheme.gap

        // The enrichment jobs waiting, in run order (MDI-F66)
        KCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                KSectionHeader { level: 3; title: qsTr("Queue") }
                KInfoTag {
                    Layout.alignment: Qt.AlignTop
                    visible: (pageActivity.job.waiting ?? []).length > 0
                    text: qsTr("%1 waiting").arg((pageActivity.job.waiting ?? []).length)
                }
            }
            Repeater {
                model: pageActivity.job.waiting ?? []
                delegate: Rectangle {
                    id: waitingRow
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    implicitHeight: waitingLine.implicitHeight + 20
                    radius: KTheme.radiusItem
                    color: KTheme.surface2
                    border.color: KTheme.line
                    border.width: 1
                    RowLayout {
                        id: waitingLine
                        x: 12
                        y: 10
                        width: parent.width - 24
                        spacing: 10
                        SmallText {
                            Layout.alignment: Qt.AlignTop
                            font.family: KTheme.mono
                            text: String(waitingRow.index + 1)
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            color: KTheme.text
                            font.weight: Font.DemiBold
                            text: waitingRow.modelData
                        }
                    }
                }
            }
            Explanation {
                visible: (pageActivity.job.waiting ?? []).length === 0
                text: qsTr("Nothing waiting")
            }
        }

        // Finished steps, newest first (MDI-F67)
        KCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            KSectionHeader { level: 3; title: qsTr("History") }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Repeater {
                    model: appManager1.mdiStepLog.slice(0, 60)
                    delegate: ColumnLayout {
                        id: logRow
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: 0
                        Rectangle {
                            // A hairline between steps
                            visible: logRow.index > 0
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: KTheme.line
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 8
                            Layout.bottomMargin: 8
                            spacing: 10
                            SmallText {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 1
                                font.family: KTheme.mono
                                text: logRow.modelData.time
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Controls.Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                    color: KTheme.text
                                    text: logRow.modelData.label + " · " + logRow.modelData.fileName
                                }
                                SmallText {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: pageActivity.seconds(logRow.modelData.durationMs)
                                          + (logRow.modelData.tokensPerSecond
                                             ? " · " + qsTr("%1 tokens/s").arg(logRow.modelData.tokensPerSecond) : "")
                                }
                                Controls.Label {
                                    visible: !!logRow.modelData.reason
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    color: KTheme.text2
                                    text: logRow.modelData.reason || ""
                                }
                            }
                            KInfoTag {
                                Layout.alignment: Qt.AlignTop
                                text: pageActivity.outcomeText(logRow.modelData.outcome)
                                tone: pageActivity.outcomeTone(logRow.modelData.outcome)
                            }
                        }
                    }
                }
            }
            Explanation {
                visible: appManager1.mdiStepLog.length === 0
                text: qsTr("No step finished in this session.")
            }
        }
    }

    // ── Resource history (session only, D21) ───────────────────────────────
    KCard {
        Layout.fillWidth: true
        KSectionHeader { level: 3; title: qsTr("Since this session started") }
        Canvas {
            id: historyChart
            Layout.fillWidth: true
            Layout.preferredHeight: 160
            readonly property var series: [
                { key: "cpu", color: KTheme.accent },
                { key: "gpu", color: KTheme.ok },
                { key: "mem", color: KTheme.warn }
            ]
            readonly property color gridColor: KTheme.line
            // The colours follow the system's light or dark scheme
            onSeriesChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const h = appManager1.mdiHistory
                ctx.strokeStyle = gridColor
                ctx.lineWidth = 1
                for (let g = 0; g <= 4; g++) {
                    const y = Math.min(height - 0.5, Math.max(0.5, height - height * g / 4))
                    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
                }
                if (h.length < 2) return
                ctx.lineWidth = 2
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
            Layout.fillWidth: true
            spacing: 16
            Flow {
                Layout.fillWidth: true
                spacing: 16
                Repeater {
                    // Each line is named next to its colour
                    model: [ { label: qsTr("Processor"), color: KTheme.accent },
                             { label: qsTr("Graphics card"), color: KTheme.ok },
                             { label: qsTr("Memory"), color: KTheme.warn } ]
                    delegate: Row {
                        id: legendItem
                        required property var modelData
                        spacing: 6
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 3
                            radius: 1.5
                            color: legendItem.modelData.color
                        }
                        Controls.Label {
                            text: legendItem.modelData.label
                            color: KTheme.text2
                            font.pixelSize: KTheme.note
                        }
                    }
                }
            }
            SmallText {
                Layout.alignment: Qt.AlignTop
                font.family: KTheme.mono
                text: appManager1.mdiHistory.length > 0
                      ? appManager1.mdiHistory[0].t + " – " + appManager1.mdiHistory[appManager1.mdiHistory.length - 1].t : ""
            }
        }
    }

    // ── Programs and models in memory ──────────────────────────────────────
    GridLayout {
        Layout.fillWidth: true
        columns: width >= 760 ? 2 : 1
        columnSpacing: KTheme.gap
        rowSpacing: KTheme.gap

        KCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            KSectionHeader { level: 3; title: qsTr("Programs") }
            Repeater {
                model: pageActivity.sys.processes ?? []
                delegate: RowLayout {
                    id: processRow
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8
                    Kirigami.Icon {
                        Layout.alignment: Qt.AlignTop
                        source: processRow.modelData.role === "ollama" ? "preferences-system-network"
                                : processRow.modelData.role === "tool" ? "system-run" : "katalog"
                        implicitWidth: 16
                        implicitHeight: 16
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        color: KTheme.text
                        text: processRow.modelData.name
                    }
                    KInfoTag {
                        Layout.alignment: Qt.AlignTop
                        text: qsTr("Processor") + " " + processRow.modelData.cpu + "%"
                    }
                    KInfoTag {
                        Layout.alignment: Qt.AlignTop
                        text: processRow.modelData.memMb >= 1024
                              ? (Math.round(processRow.modelData.memMb / 102.4) / 10) + " GB"
                              : processRow.modelData.memMb + " MB"
                    }
                }
            }
            Explanation {
                visible: (pageActivity.sys.processes ?? []).length === 0
                text: "—"
            }
        }

        KCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            KSectionHeader { level: 3; title: qsTr("Models in memory") }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Controls.Label {
                    text: "Ollama"
                    color: KTheme.text
                    font.weight: Font.DemiBold
                }
                KInfoTag {
                    text: (pageActivity.sys.ollama ?? {}).running ? qsTr("Connected") : qsTr("Not connected")
                    tone: (pageActivity.sys.ollama ?? {}).running ? "ok" : "bad"
                }
            }
            SmallText {
                Layout.fillWidth: true
                visible: !!pageActivity.tools.ollamaUrl
                wrapMode: Text.WrapAnywhere
                font.family: KTheme.mono
                text: pageActivity.tools.ollamaUrl ?? ""
            }
            Repeater {
                model: (pageActivity.sys.ollama ?? {}).models ?? []
                delegate: RowLayout {
                    id: modelRow
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8
                    Kirigami.Icon {
                        Layout.alignment: Qt.AlignTop
                        source: "preferences-desktop-ai"
                        fallback: "applications-science"
                        isMask: true
                        color: KTheme.ai
                        implicitWidth: 16
                        implicitHeight: 16
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        color: KTheme.text
                        text: modelRow.modelData.name
                    }
                    KInfoTag {
                        Layout.alignment: Qt.AlignTop
                        text: modelRow.modelData.sizeGb + " GB"
                        tone: "ai"
                    }
                }
            }
            Explanation {
                text: qsTr("Images: %1 · Texts: %2 · Search by meaning: %3")
                      .arg(pageActivity.tools.visionModel ?? "—").arg(pageActivity.tools.textModel ?? "—")
                      .arg(pageActivity.tools.embedModel ?? "—")
            }
        }
    }

    // ── Tools ──────────────────────────────────────────────────────────────
    KCard {
        Layout.fillWidth: true
        KSectionHeader { level: 3; title: qsTr("Tools") }
        GridLayout {
            Layout.fillWidth: true
            columns: width >= 640 ? 2 : 1
            columnSpacing: KTheme.gap
            rowSpacing: 10
            Repeater {
                model: [
                    { key: "ffmpeg",    name: "ffmpeg",    use: qsTr("Previews and clips") },
                    { key: "ffprobe",   name: "ffprobe",   use: qsTr("Durations") },
                    { key: "pdftotext", name: "pdftotext", use: qsTr("Text of PDF files") },
                    { key: "whisper",   name: "whisper",   use: qsTr("Transcript") }
                ]
                delegate: RowLayout {
                    id: toolRow
                    required property var modelData
                    readonly property var t: pageActivity.tools[toolRow.modelData.key]
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 8
                    Kirigami.Icon {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 2
                        source: toolRow.t ? "dialog-ok-apply" : "dialog-error"
                        isMask: true
                        color: toolRow.t ? KTheme.ok : KTheme.bad
                        implicitWidth: 16
                        implicitHeight: 16
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            color: KTheme.text
                            font.weight: Font.DemiBold
                            text: toolRow.modelData.name
                                  + (toolRow.t && toolRow.t.version ? " " + toolRow.t.version : "")
                        }
                        SmallText {
                            Layout.fillWidth: true
                            visible: !!(toolRow.t && toolRow.t.model)
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            text: toolRow.t && toolRow.t.model ? toolRow.t.model + " (" + toolRow.t.modelMb + " MB)" : ""
                        }
                        Explanation {
                            visible: !!toolRow.t
                            text: toolRow.modelData.use
                        }
                        KInfoTag {
                            visible: !toolRow.t
                            text: qsTr("Not found")
                            tone: "bad"
                        }
                    }
                }
            }
        }
    }

    Item { Layout.preferredHeight: KTheme.gap }
}
