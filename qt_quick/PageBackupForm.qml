import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Safety copies (fork, D34 phase R8; K2 "Backup"): the links of the selected
// drive as cards, their totals as key numbers, the listed links run together.
// Every action, confirmation and message is the one of SpecBackup.md; only the
// layout and the look follow the design system.
ColumnLayout {
    id: root
    spacing: KTheme.gap * 2

    // Global card text scale from the text-size setting (1.0 = default), set by
    // Main.qml. Kept for that binding; the redesigned cards use the fixed sizes
    // of the design system (D34, MDI-F115).
    property real cardScale: 1.0

    readonly property bool narrow: width < Kirigami.Units.gridUnit * 30
    // No run, no update and no preview going on: a link may start
    readonly property bool canStart: root.runningMappingId === -1
                                     && !appManager1.catalogUpdateForBackupRunning
                                     && !appManager1.backupPreviewRunning
    function count(n) { return Number(n || 0).toLocaleString(Qt.locale(), "f", 0) }

    // Internal state
    property string filterType:        "None"   // "None"|"Source"|"Target"
    property string mappingTypeFilter: "All"    // "All"|"Backup"|"Archive"
    property var    mappings:          []
    property var    totals:            ({})
    property int    runningMappingId:  -1
    property bool   isPaused:          false
    property int    progressFilesDone: 0
    property int    progressTotalFiles:0
    property real   progressFraction:  0.0
    property string progressFile:      ""
    property string lastReportSummary: ""
    property string lastExportFilePath: ""
    property bool   lastReportIsError: false
    property bool   runningIsArchive:  false   // type of the mapping currently running
    property string progressSpeedStr:  ""      // e.g. "12.3 MB/s"
    property string progressEtaStr:    ""      // e.g. "2:30"
    property double runStartMs:         0       // Date.now() when the run started

    // The listed links that can actually run: same condition the per-card Run button
    // uses. Filtering the page IS the selection (SpecBackup.md BKP-F18).
    function runnableListedIds() {
        var ids = []
        for (var i = 0; i < root.mappings.length; i++) {
            var m = root.mappings[i]
            if (m.sourceActive && m.targetActive)
                ids.push(m.mappingId)
        }
        return ids
    }

    // Figures for the confirmation. Deliberately built from the totals already on the
    // cards, not from a compare: an exact volume is what Preview computes (BKP-F16), and
    // running it for every link would double the work. The archive figure is a sound
    // upper bound; the backup figure is only approximate, and BKP-F19 requires both to
    // be worded that way.
    function listedRunSummary() {
        var r = { total: 0, backupCount: 0, archiveCount: 0,
                  backupBytes: 0, archiveBytes: 0, skipped: 0 }
        for (var i = 0; i < root.mappings.length; i++) {
            var m = root.mappings[i]
            if (!m.sourceActive || !m.targetActive) { r.skipped++; continue }
            r.total++
            if (m.mappingType === "Archive") {
                r.archiveCount++
                r.archiveBytes += m.sourceSize
            } else {
                r.backupCount++
                if (m.sizeDiff > 0) r.backupBytes += m.sizeDiff
            }
        }
        return r
    }

    // Signals for sub-page navigation (connected in Main.qml)
    signal requestAddMapping()
    signal requestPreviewMapping(int mappingId)
    signal requestEditMapping(var mappingData)
    signal requestReport()   // BKP-F29

    function refresh() {
        var deviceId = appManager1.selectedDeviceId
        root.mappings = appManager1.getBackupMappings(root.filterType, deviceId, root.mappingTypeFilter)
        root.totals   = appManager1.getBackupTotals(root.filterType, deviceId, root.mappingTypeFilter)
    }

    // Off-screen refreshes are deferred: getBackupMappings() + getBackupTotals() ran
    // synchronously on every Selection card tap (via onSelectedDeviceChanged), delaying
    // the card-highlight repaint. Mark dirty when hidden and refresh once, on becoming
    // visible.
    property bool needsRefresh: false
    function requestRefresh() {
        if (root.visible) root.refresh()
        else root.needsRefresh = true
    }
    onVisibleChanged: if (visible && needsRefresh) { needsRefresh = false; refresh() }

    // Format a duration in seconds as M:SS or H:MM:SS
    function formatDuration(totalSec) {
        totalSec = Math.max(0, Math.round(totalSec))
        var h = Math.floor(totalSec / 3600)
        var m = Math.floor((totalSec % 3600) / 60)
        var s = totalSec % 60
        var ss = (s < 10 ? "0" : "") + s
        if (h > 0) {
            var mm = (m < 10 ? "0" : "") + m
            return h + ":" + mm + ":" + ss
        }
        return m + ":" + ss
    }

    Component.onCompleted: {
        var mt = appManager1.getBackupSetting("MappingTypeFilter")
        if (mt !== "") {
            root.mappingTypeFilter = mt
            var mi = typeFilterCombo.indexOfValue(mt)
            if (mi >= 0) typeFilterCombo.currentIndex = mi
        }
        var ft = appManager1.getBackupSetting("FilterType")
        if (ft !== "") root.filterType = ft
        refresh()
    }

    Connections {
        target: appManager1
        function onSelectedDeviceChanged()   { root.requestRefresh() }
        function onBackupMappingsChanged()   { root.requestRefresh() }
        function onBackupProgress(filesDone, totalFiles, bytesCopied, totalBytes, currentFile) {
            root.progressFilesDone  = filesDone
            root.progressTotalFiles = totalFiles
            root.progressFraction   = (totalBytes > 0) ? bytesCopied / totalBytes : 0
            root.progressFile       = currentFile

            // Average speed + ETA from cumulative bytes over elapsed time
            var elapsedSec = (Date.now() - root.runStartMs) / 1000
            if (elapsedSec > 0.5 && bytesCopied > 0) {
                var bytesPerSec = bytesCopied / elapsedSec
                root.progressSpeedStr = appManager1.formatDataSize(bytesPerSec) + "/s"
                if (totalBytes > bytesCopied && bytesPerSec > 0)
                    root.progressEtaStr = root.formatDuration((totalBytes - bytesCopied) / bytesPerSec)
                else
                    root.progressEtaStr = ""
            }
        }
        function onBackupFinished(copiedCount, movedCount, renamedCount, conflictCount, errorCount, totalBytesCopied, wasCancelled) {
            root.runningMappingId  = -1
            root.isPaused          = false
            root.progressFraction  = 1.0
            root.progressFile      = ""
            root.progressSpeedStr  = ""
            root.progressEtaStr    = ""
            root.lastReportIsError = false
            var status = wasCancelled ? qsTr("Cancelled") : qsTr("Completed")
            var transferred = copiedCount + movedCount
            root.lastReportSummary = status + " - "
                + (root.runningIsArchive ? qsTr("Moved: %1").arg(transferred)
                                         : qsTr("Copied: %1").arg(transferred))
                + (renamedCount > 0 ? " · " + qsTr("Archived & copied: %1").arg(renamedCount) : "")
                + (conflictCount > 0 ? " · " + qsTr("Conflicts: %1").arg(conflictCount) : "")
                + (errorCount    > 0 ? " · " + qsTr("Errors: %1").arg(errorCount)        : "")
            root.refresh()
        }
        // The running link is now decided in AppManager, so a link started by the
        // sequencer highlights its card exactly as a clicked one does (BKP-C10).
        function onRunningBackupMappingIdChanged() {
            var id = appManager1.runningBackupMappingId
            if (id === -1)
                return
            root.runningMappingId   = id
            root.runningIsArchive   = false
            for (var i = 0; i < root.mappings.length; i++)
                if (root.mappings[i].mappingId === id)
                    root.runningIsArchive = (root.mappings[i].mappingType === "Archive")
            root.isPaused           = false
            root.progressFraction   = 0
            root.progressFilesDone  = 0
            root.progressTotalFiles = 0
            root.progressFile       = ""
            root.progressSpeedStr   = ""
            root.progressEtaStr     = ""
            root.runStartMs         = Date.now()
            root.lastReportSummary  = ""
        }
        // A link card's Export to CSV: the path opens the file (BKP-F26)
        function onBackupCsvExported(filePath) {
            root.lastReportIsError  = false
            root.lastExportFilePath = filePath
            root.lastReportSummary  = qsTr("Exported to: %1").arg("<a href=\"file\">"
                + filePath.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") + "</a>")
        }
        function onBackupNotification(message, isError) {
            root.lastReportIsError = isError
            root.lastReportSummary = message
            if (isError && root.runningMappingId !== -1)
                root.runningMappingId = -1
        }
    }

    // One end of a link: its role, whether its drive is connected, its contents
    component LinkEnd: ColumnLayout {
        id: linkEnd
        property string role
        property string name
        property bool   active
        property string sizeStr
        property var    fileCount
        spacing: 4
        Layout.fillWidth: true
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Controls.Label {
                text: linkEnd.role
                height: endStateTag.height
                verticalAlignment: Text.AlignVCenter
                color: KTheme.text3
                font.pixelSize: KTheme.note
                font.weight: Font.Medium
            }
            KInfoTag {
                id: endStateTag
                text: linkEnd.active ? qsTr("Connected") : qsTr("Not connected")
                tone: linkEnd.active ? "ok" : "warn"
            }
        }
        // Card text is never elided (CDT-C1): names and figures wrap
        Controls.Label {
            Layout.fillWidth: true
            text: linkEnd.name
            wrapMode: Text.Wrap
            color: KTheme.text
            font.weight: Font.DemiBold
        }
        Controls.Label {
            Layout.fillWidth: true
            text: linkEnd.sizeStr + " · " + linkEnd.fileCount + " " + qsTr("files")
            wrapMode: Text.Wrap
            color: KTheme.text2
            font.pixelSize: KTheme.note
        }
    }

    // ─── Intro ────────────────────────────────────────────────────────────────
    GridLayout {
        Layout.fillWidth: true
        columns: root.narrow ? 1 : 2
        columnSpacing: 12
        rowSpacing: 12
        KSectionHeader {
            level: 1
            title: qsTr("Safety copies")
            description: qsTr("A link copies or moves the files of one catalog to another; the catalogs tell what is missing, so a drive can stay disconnected until the run.")
        }
        KButton {
            Layout.alignment: root.narrow ? Qt.AlignLeft : (Qt.AlignRight | Qt.AlignTop)
            variant: "secondary"
            icon.name: "list-add"
            text: qsTr("Add a link")
            onClicked: root.requestAddMapping()
        }
    }

    // ─── Coverage summary: the key numbers of the listed links ────────────────
    // Only with something to say: without a device name there is no summary.
    GridLayout {
        id: summaryBar
        Layout.fillWidth: true
        visible: root.totals.totalMappings !== undefined && !!root.totals.deviceName
        columns: Math.max(1, Math.min(4, Math.floor(width / 200)))
        columnSpacing: KTheme.gap
        rowSpacing: KTheme.gap
        KMetricCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            label: qsTr("Links")
            value: root.count(root.totals.totalMappings)
            tagText: root.totals.deviceName || ""
        }
        KMetricCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            label: qsTr("Source")
            value: root.totals.totalSourceSizeStr || "0"
            tagText: root.count(root.totals.totalSourceFileCount) + " " + qsTr("files")
        }
        KMetricCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            label: qsTr("Target")
            value: root.totals.totalTargetSizeStr || "0"
            tagText: root.count(root.totals.totalTargetFileCount) + " " + qsTr("files")
        }
        // Coverage is a Backup concept (how much of the source is mirrored in the
        // target); it has no meaning for Archive (files are moved out of the source).
        KMetricCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            visible: root.mappingTypeFilter !== "Archive"
            label: qsTr("Coverage")
            value: (root.totals.coveragePct || "0") + "%"
            tagText: root.totals.deviceSizeStr ? qsTr("Of %1").arg(root.totals.deviceSizeStr) : ""
        }
    }

    // ─── Listed links: the filter is the selection (BKP-F18) ──────────────────
    KCard {
        Layout.fillWidth: true

        KSectionHeader {
            level: 3
            title: qsTr("Listed links")
            description: qsTr("The links of the drive selected in My drives. The filter decides which ones are listed, and Run listed links runs exactly those.")
        }

        // A Flow, not a RowLayout: every control here has a fixed minimum width, so
        // as a RowLayout the bar set a floor for the whole page. Each cluster is a
        // RowLayout so it wraps as a unit rather than splitting mid-group.
        Flow {
            Layout.fillWidth: true
            spacing: KTheme.gap

            RowLayout {
                width: Math.min(implicitWidth, parent.width)  // lets its combo box shrink (CBX-F2)
                spacing: 8
                Controls.Label { text: qsTr("Type"); color: KTheme.text2; font.weight: Font.Medium }
                ComboBoxFitted {
                    id: typeFilterCombo
                    Layout.preferredHeight: 40
                    textRole:  "text"
                    valueRole: "value"
                    model: [
                        { value: "All",     text: qsTr("All")     },
                        { value: "Backup",  text: qsTr("Backup")  },
                        { value: "Archive", text: qsTr("Archive") }
                    ]
                    onActivated: {
                        root.mappingTypeFilter = currentValue
                        appManager1.setBackupSetting("MappingTypeFilter", currentValue)
                        root.refresh()
                    }
                }
            }

            RowLayout {
                spacing: 8
                Controls.Label { text: qsTr("Filter"); color: KTheme.text2; font.weight: Font.Medium }
                RowLayout {
                    spacing: 4
                    Repeater {
                        model: [
                            { value: "None",   label: qsTr("All")    },
                            { value: "Source", label: qsTr("Source") },
                            { value: "Target", label: qsTr("Target") }
                        ]
                        delegate: KButton {
                            required property var modelData
                            text:      modelData.label
                            variant:   root.filterType === modelData.value ? "tonal" : "secondary"
                            checked:   root.filterType === modelData.value
                            checkable: true
                            onClicked: {
                                root.filterType = modelData.value
                                appManager1.setBackupSetting("FilterType", modelData.value)
                                root.refresh()
                            }
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: KTheme.line }

        Flow {
            Layout.fillWidth: true
            spacing: KTheme.gap
            KButton {
                variant:   "primary"
                text:      qsTr("Run listed links")
                icon.name: "media-playback-start"
                enabled:   root.canStart && root.runnableListedIds().length > 0
                onClicked: runListedDialog.open()
            }
            Controls.CheckBox {
                height: 40
                text:    qsTr("Update catalogs")
                checked: appManager1.updateBeforeBackup
                onToggled: appManager1.updateBeforeBackup = checked
            }
        }
    }

    Kirigami.PromptDialog {
        id: runListedDialog
        title: qsTr("Run listed links")
        // Roles rather than source order, so Cancel follows the platform instead
        // of a position hard-coded for KDE (SpecValidationRules D1/D2). BKP-F19
        // is unchanged: Cancel still starts nothing, Continue still runs the list.
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("Continue")
                icon.name: "media-playback-start"
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
            }
            Controls.Button {
                text: qsTr("Cancel")
                icon.name: "dialog-cancel"
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.RejectRole
            }
            onAccepted: {
                appManager1.runListedBackups(root.runnableListedIds())
                runListedDialog.close()
            }
            onRejected: runListedDialog.close()
        }

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            property var summary: runListedDialog.visible ? root.listedRunSummary() : ({})

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("%1 link(s) will run, one after another.").arg(parent.summary.total || 0)
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: (parent.summary.archiveCount || 0) > 0
                text: qsTr("%1 archive link(s) - up to %2 moved out of the source.")
                        .arg(parent.summary.archiveCount || 0)
                        .arg(appManager1.formatDataSize(parent.summary.archiveBytes || 0))
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: (parent.summary.backupCount || 0) > 0
                text: qsTr("%1 backup link(s) - roughly %2 to copy.")
                        .arg(parent.summary.backupCount || 0)
                        .arg(appManager1.formatDataSize(parent.summary.backupBytes || 0))
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: (parent.summary.skipped || 0) > 0
                text: qsTr("%1 listed link(s) will be skipped: source or target not available.")
                        .arg(parent.summary.skipped || 0)
            }
        }
    }

    // ─── Report summary (backup finished / profile generated / error) ─────────
    Kirigami.InlineMessage {
        Layout.fillWidth:    true
        visible:             root.lastReportSummary !== ""
        type:                root.lastReportIsError ? Kirigami.MessageType.Error
                                                    : Kirigami.MessageType.Positive
        showCloseButton:     true
        text:                root.lastReportSummary
        onVisibleChanged:    if (!visible) { root.lastReportSummary = ""; root.lastReportIsError = false }
        onLinkActivated:     appManager1.openFile(root.lastExportFilePath)
        // Per-file report of the last run (BKP-F29)
        actions: [
            Kirigami.Action {
                text:        qsTr("Report")
                icon.name:   "document-preview"
                visible:     appManager1.backupReportAvailable
                onTriggered: root.requestReport()
            }
        ]
    }

    // ─── No links yet ─────────────────────────────────────────────────────────
    KCard {
        Layout.fillWidth: true
        visible: root.mappings.length === 0
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Rectangle {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                Layout.alignment: Qt.AlignTop
                radius: KTheme.radiusControl
                color: KTheme.accentSoft
                Kirigami.Icon { anchors.centerIn: parent; width: 22; height: 22; source: "backup" }
            }
            KSectionHeader {
                level: 3
                title: qsTr("No backup links")
                description: qsTr("Create a link to define a source and target for backup or archive operations.")
            }
        }
        KButton {
            variant: "tonal"
            icon.name: "list-add"
            text: qsTr("Add a link")
            onClicked: root.requestAddMapping()
        }
    }

    // ─── Link cards ───────────────────────────────────────────────────────────
    //
    // KCard (a Rectangle whose height binds to its inner ColumnLayout) instead of
    // Kirigami.AbstractCard: AbstractCard is a QQC2 ItemDelegate whose internal
    // layout chain goes stale after any page visibility cycle (layer push/pop,
    // close/reopen). A direct binding is always correct by construction.
    GridLayout {
        id: linkGrid
        Layout.fillWidth: true
        Layout.bottomMargin: KTheme.gap
        visible: root.mappings.length > 0
        columns: Math.max(1, Math.floor(width / 460))
        columnSpacing: KTheme.gap
        rowSpacing: KTheme.gap

        Repeater {
            id: mappingRepeater
            model: root.mappings
            delegate: KCard {
                id: mappingCard
                required property var modelData
                required property int index

                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                // The link running now is outlined in the accent colour
                border.color: isRunning ? KTheme.accent : KTheme.line
                border.width: isRunning ? 2 : 1

                property bool isRunning: root.runningMappingId === modelData.mappingId
                readonly property bool isArchive: modelData.mappingType === "Archive"

                // Below this the two ends of the link stack instead of sitting side by side
                readonly property bool narrow: width < Kirigami.Units.gridUnit * 24

                // ── Header: icon, name, type and state ──────────────────────
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Rectangle {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        Layout.alignment: Qt.AlignTop
                        radius: KTheme.radiusControl
                        color: mappingCard.isArchive ? KTheme.warnSoft : KTheme.okSoft
                        Kirigami.Icon {
                            anchors.centerIn: parent
                            width: 22; height: 22
                            source: mappingCard.isArchive ? "archive-extract" : "backup"
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Controls.Label {
                            Layout.fillWidth: true
                            text: mappingCard.modelData.mappingName
                            wrapMode: Text.Wrap
                            color: KTheme.text
                            font.weight: Font.Bold
                            font.pixelSize: KTheme.titleCard
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            // Backup copies, Archive moves out of the source
                            KInfoTag {
                                text: mappingCard.isArchive ? qsTr("Archive") : qsTr("Backup")
                                tone: mappingCard.isArchive ? "warn" : "ok"
                            }
                            KInfoTag {
                                visible: mappingCard.isRunning
                                text: root.isPaused ? qsTr("Paused") : qsTr("In Progress")
                                tone: root.isPaused ? "warn" : "info"
                            }
                        }
                    }
                }

                // ── Source → Target ─────────────────────────────────────────
                GridLayout {
                    Layout.fillWidth: true
                    columns: mappingCard.narrow ? 1 : 3
                    columnSpacing: 12
                    rowSpacing: 8

                    LinkEnd {
                        Layout.alignment: Qt.AlignTop
                        role:      qsTr("Source")
                        name:      mappingCard.modelData.sourceName
                        active:    mappingCard.modelData.sourceActive
                        sizeStr:   mappingCard.modelData.sourceSizeStr
                        fileCount: mappingCard.modelData.sourceFileCount
                    }
                    Kirigami.Icon {
                        Layout.alignment: mappingCard.narrow ? Qt.AlignLeft : Qt.AlignVCenter
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        source: mappingCard.narrow ? "go-down" : "go-next"
                        isMask: true
                        color: KTheme.text3
                    }
                    LinkEnd {
                        Layout.alignment: Qt.AlignTop
                        role:      qsTr("Target")
                        name:      mappingCard.modelData.targetName
                        active:    mappingCard.modelData.targetActive
                        sizeStr:   mappingCard.modelData.targetSizeStr
                        fileCount: mappingCard.modelData.targetFileCount
                    }
                }

                // ── Diff (Backup) / To move (Archive), and the last run ──────
                // Archive moves files out of the source, so a size/coverage "diff"
                // against the target is meaningless — show the quantity to move instead.
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: diffColumn.implicitHeight + 20
                    radius: KTheme.radiusItem
                    color: KTheme.surface2
                    border.color: KTheme.line
                    border.width: 1

                    ColumnLayout {
                        id: diffColumn
                        x: 12
                        y: 10
                        width: parent.width - 24
                        spacing: 4

                        // Archive: something to do when there are files to move (source not empty).
                        // Backup: something to do when the source is larger than the target.
                        readonly property bool pending: mappingCard.isArchive
                                                        ? mappingCard.modelData.sourceFileCount > 0
                                                        : mappingCard.modelData.sizeDiff > 0

                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            Controls.Label {
                                text: mappingCard.isArchive ? qsTr("To move") : qsTr("Diff")
                                height: diffValue.implicitHeight
                                verticalAlignment: Text.AlignVCenter
                                color: KTheme.text3
                                font.pixelSize: KTheme.note
                                font.weight: Font.Medium
                            }
                            Controls.Label {
                                id: diffValue
                                width: Math.min(implicitWidth, parent.width)
                                wrapMode: Text.Wrap
                                font.weight: Font.DemiBold
                                color: diffColumn.pending ? KTheme.warn : KTheme.ok
                                text: {
                                    var m = mappingCard.modelData
                                    if (mappingCard.isArchive) {
                                        if (m.sourceFileCount === 0)
                                            return qsTr("Nothing to move")
                                        return m.sourceFileCount + " " + qsTr("files")
                                    }
                                    var sizeSign  = m.sizeDiff      > 0 ? "+" : (m.sizeDiff      < 0 ? "−" : "")
                                    var filesSign = m.fileCountDiff > 0 ? "+" : (m.fileCountDiff < 0 ? "−" : "")
                                    if (m.sizeDiff === 0 && m.fileCountDiff === 0)
                                        return qsTr("Up to date")
                                    return sizeSign + m.sizeDiffStr
                                        + " · " + filesSign + Math.abs(m.fileCountDiff) + " " + qsTr("files")
                                        + (m.sourceDateUpdated ? " · " + m.sourceDateUpdated : "")
                                }
                            }
                        }
                        // "Last archive" for Archive links, "Last backup" otherwise
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: KTheme.text2
                            font.pixelSize: KTheme.note
                            text: (mappingCard.isArchive ? qsTr("Last archive") : qsTr("Last backup"))
                                  + ": "
                                  + (mappingCard.modelData.lastBackupDate
                                     ? mappingCard.modelData.lastBackupDate + " · " + mappingCard.modelData.lastBackupSizeStr
                                     : qsTr("never"))
                        }
                    }
                }

                // ── Progress (only while running) ───────────────────────────
                Loader {
                    id: progressLoader
                    Layout.fillWidth: true
                    active:  mappingCard.isRunning
                    visible: mappingCard.isRunning

                    sourceComponent: ColumnLayout {
                        width: progressLoader.width
                        spacing: 6

                        KProgressBar {
                            Layout.fillWidth: true
                            value: root.progressFraction
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            text: Math.round(root.progressFraction * 100) + "% · "
                                + root.progressFilesDone + "/" + root.progressTotalFiles
                                + (root.progressSpeedStr ? " · " + root.progressSpeedStr : "")
                                + (root.progressEtaStr   ? " · " + qsTr("ETA %1").arg(root.progressEtaStr) : "")
                                + (root.progressFile ? " · " + root.progressFile : "")
                            wrapMode: Text.WrapAnywhere
                            color: KTheme.text2
                            font.pixelSize: KTheme.note
                        }
                    }
                }

                // ── Actions - a Flow wraps on narrow windows ────────────────
                Flow {
                    Layout.fillWidth: true
                    spacing: 8

                    KButton {
                        visible:   !mappingCard.isRunning
                        variant:   "tonal"
                        text:      mappingCard.isArchive ? qsTr("Run Archive") : qsTr("Run Backup")
                        icon.name: "media-playback-start"
                        enabled:   mappingCard.modelData.sourceActive && mappingCard.modelData.targetActive
                                   && root.canStart
                        onClicked: {
                            // State is set by onRunningBackupMappingIdChanged, which
                            // covers both this click and the sequencer (BKP-C10).
                            appManager1.runBackup(mappingCard.modelData.mappingId)
                        }
                    }
                    KButton {
                        visible:   mappingCard.isRunning && !root.isPaused
                        text:      qsTr("Pause")
                        icon.name: "media-playback-pause"
                        onClicked: { root.isPaused = true; appManager1.pauseBackup() }
                    }
                    KButton {
                        visible:   mappingCard.isRunning && root.isPaused
                        text:      qsTr("Resume")
                        icon.name: "media-playback-start"
                        onClicked: { root.isPaused = false; appManager1.resumeBackup() }
                    }
                    KButton {
                        visible:   mappingCard.isRunning
                        text:      qsTr("Cancel")
                        icon.name: "process-stop"
                        onClicked: appManager1.stopBackup()
                    }
                    KButton {
                        visible:   !mappingCard.isRunning
                        text:      mappingCard.isArchive ? qsTr("Preview Archive") : qsTr("Preview Backup")
                        icon.name: "view-preview"
                        enabled:   root.canStart
                        onClicked: root.requestPreviewMapping(mappingCard.modelData.mappingId)
                    }
                    KButton {
                        visible:   !mappingCard.isRunning
                        implicitWidth: 40
                        leftPadding: 12
                        rightPadding: 12
                        icon.name: "overflow-menu"
                        // Icon only: the tooltip and the accessible name say what it is (ICB-F1)
                        Accessible.name:          qsTr("More actions")
                        Controls.ToolTip.text:    qsTr("More actions")
                        Controls.ToolTip.visible: hovered
                        onClicked: moreMenu.open()

                        Controls.Menu {
                            id: moreMenu
                            Controls.MenuItem {
                                text: qsTr("Edit")
                                icon.name: "document-edit"
                                onTriggered: root.requestEditMapping(mappingCard.modelData)
                            }
                            Controls.MenuItem {
                                text: qsTr("Duplicate")
                                icon.name: "edit-copy"
                                onTriggered: {
                                    // SpecBackup.md BKP-F22: creates the copy and
                                    // nothing else — no run, no preview, no change
                                    // to the original.
                                    var err = appManager1.duplicateBackupMapping(mappingCard.modelData.mappingId)
                                    if (err)
                                        root.lastReportSummary = err
                                    else
                                        root.refresh()
                                }
                            }
                            Controls.MenuItem {
                                text: qsTr("Replicate directories")
                                icon.name: "folder-sync"
                                onTriggered: {
                                    var r = appManager1.replicateDirectories(mappingCard.modelData.mappingId)
                                    root.lastReportSummary = r.error || (qsTr("Replicate") + " - "
                                        + qsTr("Created: %1").arg(r.created || 0)
                                        + " · " + qsTr("Already existing: %1").arg(r.skipped || 0)
                                        + (r.errors > 0 ? " · " + qsTr("Errors: %1").arg(r.errors) : ""))
                                }
                            }
                            Controls.MenuItem {
                                text: qsTr("Invert (swap source and target)")
                                icon.name: "object-flip-horizontal"
                                onTriggered: {
                                    if (appManager1.invertBackupMapping(mappingCard.modelData.mappingId))
                                        root.refresh()
                                }
                            }
                            Controls.MenuItem {
                                text: qsTr("Export to CSV")
                                icon.name: "document-export"
                                // This link's preview, computed then exported (BKP-F26)
                                onTriggered: appManager1.exportBackupLinkToCsv(mappingCard.modelData.mappingId)
                            }
                            Controls.MenuSeparator {}
                            Controls.MenuItem {
                                text: qsTr("Delete")
                                icon.name: "edit-delete"
                                onTriggered: deleteDialog.open()
                            }
                        }

                        Kirigami.PromptDialog {
                            id: deleteDialog
                            title: qsTr("Delete link")
                            subtitle: qsTr("Delete link \"%1\"? This cannot be undone.").arg(mappingCard.modelData.mappingName)
                            standardButtons: Kirigami.Dialog.Yes | Kirigami.Dialog.No
                            onAccepted: {
                                if (appManager1.deleteBackupMapping(mappingCard.modelData.mappingId))
                                    root.refresh()
                            }
                        }
                    }
                }
            }
        }
    }
}
