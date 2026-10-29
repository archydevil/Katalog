import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Controls   // required for ScrollBar attached property (namespace alias prevents it)
import org.kde.kirigami as Kirigami

// Preview of a safety-copy link, or the report of the last run (fork, D34 phase
// R8): key numbers as metric cards, the file table in a card. The compare, the
// table model, the export and the messages are unchanged (SpecBackup.md).
Kirigami.Page {
    id: root
    property Kirigami.Action escapeAction: escapeCloseAction  // Esc (KBS-F1)
    title: qsTr("Backup Preview")
    padding: 0
    background: Rectangle { color: KTheme.ground }   // design system ground (D34)

    readonly property bool narrow: width < Kirigami.Units.gridUnit * 30
    function count(n) { return Number(n || 0).toLocaleString(Qt.locale(), "f", 0) }
    readonly property int copyCount: root.previewData && root.previewData.filesToCopy ? root.previewData.filesToCopy.length : 0
    readonly property int conflictCount: root.previewData && root.previewData.fileConflicts ? root.previewData.fileConflicts.length : 0

    property int mappingId: -1
    property var previewData: null
    // Report of the last run instead of a preview (BKP-F29): same table, its own
    // totals line, none of the preview-only items
    property bool   reportMode: false
    property string reportSummary: ""

    // Header labels for the resizable preview table (BKP-F15). Same strings as the
    // former Repeater header row, kept in this file so no translation slot changes.
    readonly property var columnHeaders: [qsTr("Status"), qsTr("File Name"), qsTr("Path"), qsTr("Size")]

    function doPreview() {
        // The compare already ran on the Backup page (BKP-F16); read its result and the
        // populated table model — no compare happens on this page.
        root.previewData = appManager1.lastBackupPreviewSummary()
        if (root.previewData && root.previewData.mappingName)
            root.title = qsTr("Preview") + " - " + root.previewData.mappingName
    }

    function closeLayer() {
        pageStack.layers.pop()
    }

    // The Backup page runs any pre-preview catalog update and only pushes this page once
    // the report is ready (BKP-F14), so the preview is computed synchronously here.
    Component.onCompleted: {
        if (root.reportMode) {
            root.title = qsTr("Report")
            root.reportSummary = appManager1.showLastBackupReport()
        } else {
            doPreview()
        }
    }

    actions: [
        Kirigami.Action {
            text:        qsTr("Export to CSV")
            icon.name:   "document-export"
            displayHint: Kirigami.DisplayHint.KeepVisible
            visible:     !root.reportMode
            enabled:     root.previewData !== null && (root.copyCount + root.conflictCount) > 0
            onTriggered: {
                var path = appManager1.exportLastBackupPreviewToCsv()
                exportMsg.filePath = path
                // A failure comes back as a message, not a path: no link then
                exportMsg.text = path.startsWith("/") || /^[A-Za-z]:[\\/]/.test(path)
                    ? qsTr("Exported to: %1").arg("<a href=\"file\">"
                          + path.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") + "</a>")
                    : path
                exportMsg.visible = true
            }
        },
        Kirigami.Action {
            id: escapeCloseAction
            text:        qsTr("Close")
            icon.name:   "view-close"
            displayHint: Kirigami.DisplayHint.KeepVisible
            onTriggered: root.closeLayer()
        }
    ]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.narrow ? KTheme.gap : KTheme.pagePadding * 0.75
        spacing: KTheme.gap

        // ── Intro ─────────────────────────────────────────────────────────────
        KSectionHeader {
            level: 1
            title: root.title
            description: root.reportMode
                         ? qsTr("What the last run did, file by file.")
                         : qsTr("What running this link would do. Nothing is copied or moved until it runs.")
        }

        // Computing state (momentary — preview is synchronous)
        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            visible: root.previewData === null && !root.reportMode
            text: qsTr("Computing preview…")
            icon.name: "view-refresh"
        }

        // Error state
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type:    Kirigami.MessageType.Error
            visible: root.previewData !== null && !!root.previewData.error
            text:    root.previewData ? root.previewData.error : ""
        }

        // Export feedback
        // Clicking the path opens the file, as K2's dialog link did (BKP-F26)
        Kirigami.InlineMessage {
            id: exportMsg
            property string filePath: ""
            Layout.fillWidth: true
            type: Kirigami.MessageType.Positive
            visible: false
            showCloseButton: true
            onLinkActivated: appManager1.openFile(filePath)
        }

        // Summary: the run report's own totals line (BKP-F29). A preview shows its
        // totals as the key numbers below.
        Kirigami.InlineMessage {
            id: summaryMsg
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            showCloseButton: false
            visible: root.reportMode
            text: {
                if (root.reportMode) return root.reportSummary.replace(/^(&nbsp;)+/, "")
                if (!root.previewData) return ""
                var pd = root.previewData
                var copyLabel  = pd.isArchive ? qsTr("Move") : qsTr("Copy")
                return copyLabel + ": " + root.copyCount + " " + qsTr("file(s)")
                    + " · " + qsTr("Conflicts: %1").arg(root.conflictCount)
                    + " · " + qsTr("Already in target: %1").arg(pd.skippedCount || 0)
                    + (pd.sourceActive ? "" : " - ⚠ " + qsTr("source offline"))
                    + (pd.targetActive ? "" : " - ⚠ " + qsTr("target offline"))
            }
        }

        // ── Key numbers of the preview ────────────────────────────────────────
        GridLayout {
            Layout.fillWidth: true
            visible: !root.reportMode && root.previewData !== null && root.previewData.hasData === true
            columns: Math.max(1, Math.min(4, Math.floor(width / 200)))
            columnSpacing: KTheme.gap
            rowSpacing: KTheme.gap
            KMetricCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                label: root.previewData && root.previewData.isArchive ? qsTr("Move") : qsTr("Copy")
                value: root.count(root.copyCount)
                tagText: qsTr("file(s)")
                tagTone: root.copyCount > 0 ? "info" : "ok"
            }
            KMetricCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                label: qsTr("Conflicts")
                value: root.count(root.conflictCount)
            }
            KMetricCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                label: qsTr("Already in target")
                value: root.count(root.previewData ? root.previewData.skippedCount : 0)
            }
            KMetricCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                visible: !!root.previewData && !!root.previewData.spaceStatus && root.previewData.spaceStatus !== "Unknown"
                label: qsTr("Free space on the target")
                value: root.previewData ? appManager1.formatDataSize(root.previewData.spaceAvailable || 0) : ""
                tagText: !root.previewData ? ""
                         : root.previewData.spaceStatus === "Insufficient" ? qsTr("Not enough space")
                         : root.previewData.spaceStatus === "Low" ? qsTr("Low space")
                         : qsTr("Enough space")
                tagTone: !root.previewData ? "neutral"
                         : root.previewData.spaceStatus === "Insufficient" ? "bad"
                         : root.previewData.spaceStatus === "Low" ? "warn" : "ok"
            }
        }

        // The two ends, when one is not connected
        Flow {
            Layout.fillWidth: true
            visible: !root.reportMode && root.previewData !== null && root.previewData.hasData === true
                     && (!root.previewData.sourceActive || !root.previewData.targetActive)
            spacing: 6
            KInfoTag {
                visible: !!root.previewData && !root.previewData.sourceActive
                text: qsTr("source offline")
                tone: "warn"
            }
            KInfoTag {
                visible: !!root.previewData && !root.previewData.targetActive
                text: qsTr("target offline")
                tone: "warn"
            }
        }

        // Space warning
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: {
                if (!root.previewData) return Kirigami.MessageType.Information
                if (root.previewData.spaceStatus === "Insufficient") return Kirigami.MessageType.Error
                return Kirigami.MessageType.Warning
            }
            visible: root.previewData !== null
                     && (root.previewData.spaceStatus === "Low" || root.previewData.spaceStatus === "Insufficient")
            text: {
                if (!root.previewData) return ""
                if (root.previewData.spaceStatus === "Insufficient")
                    return qsTr("Insufficient disk space - Required: %1, Available: %2")
                        .arg(appManager1.formatDataSize(root.previewData.spaceRequired))
                        .arg(appManager1.formatDataSize(root.previewData.spaceAvailable))
                return qsTr("Low target space - %1 remaining after operation")
                    .arg(appManager1.formatDataSize(root.previewData.spaceAvailable - root.previewData.spaceRequired))
            }
            showCloseButton: false
        }

        // ── Resizable file table: Status | File Name | Path | Size (BKP-F15) ──
        // Column widths are user-adjustable by dragging the header dividers,
        // consistent with the search-results and explore tables. Drawn as a card.
        Rectangle {
            id: tableCard
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: KTheme.surface
            border.color: KTheme.line
            border.width: 1
            radius: KTheme.radiusCard
            visible: root.reportMode
                     ? previewTableView.rows > 0
                     : root.previewData !== null && root.previewData.hasData === true
                       && (root.copyCount + root.conflictCount) > 0

            Item {
                anchors.fill: parent
                anchors.margins: 1
                anchors.topMargin: KTheme.radiusCard / 2
                anchors.bottomMargin: KTheme.radiusCard / 2
                clip: true

                Controls.HorizontalHeaderView {
                    id: previewHeaderView
                    anchors { top: parent.top; left: parent.left; right: parent.right }
                    syncView: previewTableView
                    clip: true
                    implicitHeight: 34
                    resizableColumns: true

                    delegate: Rectangle {
                        required property int column
                        color: KTheme.surface
                        implicitHeight: previewHeaderView.implicitHeight
                        // Full, unelided width, read by the double-click fit (CDT-F4).
                        implicitWidth: 12 + headerLabel.implicitWidth

                        Controls.Label {
                            id: headerLabel
                            anchors {
                                left: parent.left; right: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 8; rightMargin: 6
                            }
                            text: root.columnHeaders[column] !== undefined ? root.columnHeaders[column] : ""
                            elide: Text.ElideRight
                            font.weight: Font.DemiBold
                            font.pixelSize: KTheme.note
                            color: KTheme.text3
                        }

                        Rectangle { // Vertical separator
                            anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
                            width: 1
                            color: KTheme.line
                        }

                        // This header does not sort; its only tap action is the
                        // divider double-click fit (CDT-F4).
                        TapHandler {
                            onTapped: function(eventPoint) {
                                applicationWindow().headerTapped(previewTableView, previewHeaderView, column,
                                                                 parent.width, eventPoint.position.x)
                            }
                        }
                    }
                }

                Rectangle {
                    id: previewHeaderSep
                    anchors { top: previewHeaderView.bottom; left: parent.left; right: parent.right }
                    height: 1
                    color: KTheme.line
                }

                TableView {
                    id: previewTableView
                    anchors { top: previewHeaderSep.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
                    columnSpacing: 0
                    rowSpacing: 0
                    clip: true
                    model: appManager1.backupPreviewModel

                    ScrollBar.vertical:   ScrollBar { policy: ScrollBar.AsNeeded }
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                    rowHeightProvider: function(row) { return 34 }

                    // Keyboard navigation (CDT-F5–F11), shared in Main.qml. There is
                    // no row action and no row menu here (CDT-F7/F8): the keys move
                    // the selection, type-ahead matches File Name (CDT-F9) and
                    // Left/Right scroll (CDT-F11).
                    property int selectedRow: -1
                    activeFocusOnTab: true
                    Keys.onPressed: function(event) {
                        applicationWindow().tableKeyPressed(previewTableView, event, {
                            nameAt: function(r) {
                                let m = appManager1.backupPreviewModel
                                return m.data(m.index(r, 1), Qt.DisplayRole)   // File Name
                            }
                        })
                    }

                    columnWidthProvider: function(column) {
                        let w = previewTableView.explicitColumnWidth(column)
                        if (w >= 0) return w
                        switch (column) {
                            case 0: return 110  // Status
                            case 1: return 220  // File Name
                            case 2: return 360  // Path
                            case 3: return 110  // Size
                        }
                        return 120
                    }

                    delegate: Rectangle {
                        id: cell
                        required property int    row
                        required property int    column
                        required property string fileName
                        required property string folderPath
                        required property string fileSizeStr
                        required property bool   isConflict
                        implicitHeight: 34
                        // Full, unelided content width, read by the double-click fit (CDT-F4).
                        implicitWidth: 12 + (column === 0 ? statusTag.implicitWidth : cellLabel.implicitWidth)

                        readonly property bool selected: previewTableView.selectedRow === row
                        color: selected ? applicationWindow().selectionHighlightColor
                                        : row % 2 === 0 ? KTheme.surface : KTheme.surface2

                        // Column separator
                        Rectangle {
                            visible: cell.column > 0
                            anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
                            width: 1
                            color: KTheme.line
                        }

                        // Status as an info tag: its text says what happens to the file
                        KInfoTag {
                            id: statusTag
                            visible: cell.column === 0
                            anchors.left: parent.left
                            anchors.leftMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            text: cell.isConflict ? qsTr("Conflict")
                                                  : (root.previewData && root.previewData.isArchive ? qsTr("Move") : qsTr("Copy"))
                            tone: cell.isConflict ? "warn" : "ok"
                        }

                        Controls.Label {
                            id: cellLabel
                            visible: cell.column !== 0
                            anchors {
                                left: parent.left; right: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 8; rightMargin: 6
                            }
                            elide: Text.ElideRight
                            horizontalAlignment: cell.column === 3 ? Text.AlignRight : Text.AlignLeft
                            font.family: cell.column === 2 ? KTheme.mono : Qt.application.font.family
                            font.pixelSize: cell.column === 2 ? KTheme.note : KTheme.body
                            color: cell.selected ? Kirigami.Theme.highlightedTextColor
                                                 : cell.column === 2 ? KTheme.text2 : KTheme.text
                            text: {
                                switch (cell.column) {
                                    case 1: return cell.fileName
                                    case 2: return cell.folderPath
                                    case 3: return cell.fileSizeStr
                                }
                                return ""
                            }
                        }

                        // Selects the row and gives the table keyboard focus (CDT-F5).
                        TapHandler {
                            onTapped: {
                                previewTableView.selectedRow = cell.row
                                previewTableView.forceActiveFocus()
                            }
                        }
                    }
                }
            }
        }

        // Empty preview (in sync)
        KCard {
            Layout.fillWidth: true
            visible: root.previewData !== null && root.previewData.hasData === true
                     && root.copyCount === 0 && root.conflictCount === 0
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Rectangle {
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    Layout.alignment: Qt.AlignTop
                    radius: KTheme.radiusControl
                    color: KTheme.okSoft
                    Kirigami.Icon { anchors.centerIn: parent; width: 22; height: 22; source: "checkmark"; isMask: true; color: KTheme.ok }
                }
                KSectionHeader {
                    level: 3
                    title: (root.previewData && root.previewData.isArchive) ? qsTr("Nothing to move")
                                                                            : qsTr("Nothing to copy")
                    description: qsTr("No files need to be copied or moved.")
                }
            }
        }

        // Keeps the cards at the top when the table is hidden
        Item {
            Layout.fillHeight: true
            visible: !tableCard.visible
        }
    }
}
