import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Controls   // required for ScrollBar attached property (namespace alias prevents it)
import org.kde.kirigami as Kirigami

// Right panel of the Explore page: file and folder list for the selected folder.
// Redesign (D34, phase R2, "Browse the archive"): the list is a card on the
// design system's ground, headed by the folder's name and info tags.
Item {
    id: root

    // Height of the header's second row: the tags here, the depth controls in
    // the folder tree, so both lists start at the same height (touch size).
    readonly property real headerSecondRowHeight: 40
    function folderName(path) {
        const p = String(path).replace(/[\/\\]+$/, "")
        const name = p.substring(Math.max(p.lastIndexOf("/"), p.lastIndexOf("\\")) + 1)
        return name !== "" ? name : path
    }

    // The row icon follows the icon size the user chose, shared with K2
    // (THM-F7). The row grows with it: smallMedium + largeSpacing is the
    // table's 30px, so with the option off nothing moves.
    readonly property real rowIconSize: appManager1.biggerIconSize
                                        ? Kirigami.Units.iconSizes.medium
                                        : Kirigami.Units.iconSizes.smallMedium
    readonly property real rowHeight: rowIconSize + Kirigami.Units.largeSpacing
    // rowHeightProvider is not a binding: the table must be told to ask again.
    onRowHeightChanged: exploreTableView.forceLayout()

    property int    catalogId:         0
    property string currentFolderPath: ""
    property string catalogPath:       ""
    property string catalogName:       ""
    property bool   showFolders:       true
    property bool   showSubFolders:    false
    property int    folderFileCount:   0
    property var    folderTotalSize:   0

    // Metadata columns follow the open catalog's metadata setting (EXP-F17).
    readonly property bool showMetadataColumns: catalogId > 0 && appManager1.catalogIncludesMetadata(catalogId)
    onShowMetadataColumnsChanged: exploreTableView.forceLayout()

    function formatDuration(secs) {
        let s = Number(secs)
        if (!s || s <= 0) return ""
        let h   = Math.floor(s / 3600)
        let m   = Math.floor((s % 3600) / 60)
        let sec = Math.floor(s % 60)
        return String(h).padStart(2, '0') + ":" + String(m).padStart(2, '0') + ":" + String(sec).padStart(2, '0')
    }

    // Persisted column width (CDT-F12/C9): Name.
    readonly property var persistedColumnKeys: ({ 0: "Explore/ColumnWidthName" })
    property var savedColumnWidths: ({})
    function reloadColumnWidths() {
        savedColumnWidths = applicationWindow().loadPersistedColumnWidths(exploreTableView, persistedColumnKeys)
        exploreTableView.forceLayout()
    }
    Connections {   // another collection: its own widths (CDT-F12)
        target: appManager1
        function onDatabaseModeChanged() { root.reloadColumnWidths() }
    }

    // Sort state
    property int  sortColumn:    -1
    property bool sortAscending: true

    Component.onCompleted: {
        reloadColumnWidths()
        var col   = appManager1.getExploreSortColumn()
        var order = appManager1.getExploreSortOrder()
        if (col >= 0) {
            root.sortColumn    = col
            root.sortAscending = (order === 0)
        }
    }

    // Active entry state for context menu and checksum dialogs
    property string _activeFilePath:   ""
    property string _activeFileName:   ""
    property string _activeFolderPath: ""
    property string _activeChecksum:   ""
    property string _activeEntryType:  ""

    signal folderNavigated(string folderPath)
    // Fork: the Inspector follows the selected file; Space shows or hides it
    property string selectedFullPath: ""
    signal inspectorToggleRequested()
    // A folder that is a catalog of its own (a split catalog's sub-folder):
    // the device ID of that catalog, 0 otherwise (D34, "Sfoglia mai vuota")
    property var catalogForFolder: function(folderPath) { return 0 }

    // Several files chosen at once (D38): Cmd/Ctrl + click adds or removes one,
    // Shift + click takes the range from the last one clicked. Their information
    // and people are then edited together.
    property var pickedPaths: ({})
    readonly property int pickedCount: Object.keys(pickedPaths).length
    signal infoRequested(var files)
    function pickRow(rowIndex, toggle, range) {
        let next = (toggle || range) ? Object.assign({}, root.pickedPaths) : ({})
        const from = range && exploreTableView.selectedRow >= 0 ? Math.min(exploreTableView.selectedRow, rowIndex) : rowIndex
        const to   = range && exploreTableView.selectedRow >= 0 ? Math.max(exploreTableView.selectedRow, rowIndex) : rowIndex
        for (let r = from; r <= to; ++r) {
            const info = appManager1.exploreRowInfo(r)
            if (info.entryType !== "file")
                continue
            if (toggle && !range && next[info.fullPath])
                delete next[info.fullPath]
            else
                next[info.fullPath] = true
        }
        root.pickedPaths = next
    }
    // The files the information applies to: the ones chosen, else the one selected
    function infoFiles() {
        const paths = Object.keys(root.pickedPaths)
        if (paths.length === 0 && root.selectedFullPath !== "")
            paths.push(root.selectedFullPath)
        return paths.map(p => ({ catalogId: root.catalogId, fullPath: p }))
    }

    function loadEntries() {
        if (currentFolderPath === "") return
        exploreTableView.selectedRow = -1
        appManager1.loadExploreEntries(currentFolderPath, showFolders, showSubFolders)
        var stats = appManager1.getExploreFolderStats(currentFolderPath)
        root.folderFileCount = stats.fileCount
        root.folderTotalSize = stats.totalSize
    }

    // Refuse-and-inform when the explored device is not reachable, or the device is
    // but the path is gone, instead of handing the path to the desktop
    // environment and letting it report a generic "does not exist"
    // (SpecDeviceActiveStatus.md DAS-O7 / DAS-F11 / DAS-F13).
    function openEntryFile(path) {
        if (!appManager1.probeExploreDeviceActive()) {
            showPassiveNotification(qsTr("The device is not active. It may be disconnected, or its path may have changed."), 3000)
            return
        }
        if (!appManager1.pathExists(path)) {
            showPassiveNotification(qsTr("File not found or could not be read"), 3000)
            return
        }
        appManager1.openFile(path)
    }
    function openEntryFolder(path) {
        if (!appManager1.probeExploreDeviceActive()) {
            showPassiveNotification(qsTr("The device is not active. It may be disconnected, or its path may have changed."), 3000)
            return
        }
        if (!appManager1.pathExists(path)) {
            showPassiveNotification(qsTr("Folder not found."), 3000)
            return
        }
        appManager1.openFolder(path)
    }

    // The list's card, on the page's ground
    Rectangle {
        id: filesCard
        anchors.fill: parent
        anchors.margins: 10
        color: KTheme.surface
        border.color: KTheme.line
        border.width: 1
        radius: KTheme.radiusCard

        ColumnLayout {
            anchors.fill: parent
            // Inside the rounded corners: the rows never cover the card's border
            anchors.margins: 6
            spacing: 0

            // Header: the folder's name + options. Fixed height, matching the
            // Explore directory tree's card beside it.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin:  8
                Layout.rightMargin: 4
                Layout.preferredHeight: applicationWindow().headerRowHeight
                spacing: 8

                Kirigami.Icon {
                    source: "folder"
                    implicitWidth:  22
                    implicitHeight: 22
                }
                // Name and options share the row: each starts from its natural
                // width and both give way as the window narrows.
                Controls.Label {
                    text: root.currentFolderPath !== "" ? root.folderName(root.currentFolderPath) : qsTr("No folder selected")
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.preferredWidth: implicitWidth
                    color: root.currentFolderPath !== "" ? KTheme.text : KTheme.text3
                    font.weight: Font.Bold
                    font.pixelSize: KTheme.titleCard + 1
                    Accessible.role: Accessible.Heading
                }

                // An ActionToolBar rather than a plain row, as on Devices: what no
                // longer fits moves into the overflow menu instead of being clipped
                // (EXP-F24). Folding runs from the end, so "Order folders first"
                // goes first and "Display folders" last. The two options keep their
                // tick boxes in the row; in the menu they are checkable entries.
                Kirigami.ActionToolBar {
                    Layout.fillWidth: true
                    Layout.preferredWidth: maximumContentWidth
                    alignment: Qt.AlignRight
                    background: null

                    actions: [
                        Kirigami.Action {
                            id: showFoldersAction
                            text: qsTr("Display folders")
                            checkable: true
                            checked: root.showFolders
                            displayComponent: Controls.CheckBox {
                                text: showFoldersAction.text
                                checked: showFoldersAction.checked
                                onToggled: showFoldersAction.trigger()
                            }
                            onTriggered: {
                                root.showFolders = checked
                                if (!checked)
                                    root.showSubFolders = false
                                root.loadEntries()
                                checked = Qt.binding(() => root.showFolders)
                            }
                        },
                        Kirigami.Action {
                            id: showSubFoldersAction
                            text: qsTr("and all sub-folders")
                            checkable: true
                            checked: root.showSubFolders
                            enabled: root.showFolders
                            displayComponent: Controls.CheckBox {
                                text: showSubFoldersAction.text
                                checked: showSubFoldersAction.checked
                                enabled: showSubFoldersAction.enabled
                                onToggled: showSubFoldersAction.trigger()
                            }
                            onTriggered: {
                                root.showSubFolders = checked
                                root.loadEntries()
                                checked = Qt.binding(() => root.showSubFolders)
                            }
                        },
                        Kirigami.Action {
                            text: qsTr("Order folders first")
                            icon.name: "view-sort"
                            enabled: root.showFolders
                            onTriggered: {
                                root.sortColumn    = -1
                                root.sortAscending = true
                                appManager1.sortExplore(6, 0)   // folders-first key, K2's index (EXP-F18)
                            }
                        }
                    ]
                }
            }

            // Files count and total size as info tags, then the folder's path.
            // The tags keep their text whole; the path gives way, from its start,
            // as the pane narrows. One row of fixed height, so it lines up with
            // the directory tree's second row.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin:  8
                Layout.rightMargin: 8
                Layout.preferredHeight: root.headerSecondRowHeight
                spacing: 6
                clip: true

                KInfoTag {
                    text: qsTr("%1 files").arg(root.folderFileCount.toLocaleString(Qt.locale(), "f", 0))
                }
                KInfoTag {
                    text: appManager1.formatDataSize(root.folderTotalSize) || "0"
                }
                Controls.Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    text: root.currentFolderPath
                    elide: Text.ElideLeft
                    color: KTheme.text3
                    font.family: KTheme.mono
                    font.pixelSize: KTheme.note
                }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: KTheme.line }

            // ── Column headers + Table ────────────────────────────────────────
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Controls.HorizontalHeaderView {
                    id: exploreHeaderView
                    anchors { top: parent.top; left: parent.left; right: parent.right }
                    syncView: exploreTableView
                    clip: true
                    implicitHeight: 34
                    resizableColumns: true

                    delegate: Rectangle {
                        required property int    column
                        required property string display
                        color: KTheme.surface2
                        implicitHeight: exploreHeaderView.implicitHeight
                        // Full, unelided width (6 + label + 2 + room for the sort
                        // mark + 4), read by the double-click fit (CDT-F4).
                        implicitWidth: 12 + headerLabel.implicitWidth + Kirigami.Units.gridUnit

                        RowLayout {
                            anchors {
                                left: parent.left; right: sortIndicator.left
                                verticalCenter: parent.verticalCenter
                                leftMargin: 6; rightMargin: 2
                            }
                            Controls.Label {
                                id: headerLabel
                                Layout.fillWidth: true
                                text: display
                                elide: Text.ElideRight
                                color: KTheme.text3
                                font.pixelSize: KTheme.note
                                font.weight: Font.DemiBold
                            }
                        }

                        Controls.Label {
                            id: sortIndicator
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 4 }
                            text: root.sortColumn === column ? (root.sortAscending ? "▲" : "▼") : ""
                            color: KTheme.accentText
                            font.pixelSize: KTheme.note
                        }

                        Rectangle { // Vertical separator
                            anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
                            width: 1
                            color: KTheme.line
                        }

                        TapHandler {
                            onTapped: function(eventPoint) {
                                // A divider tap fits a column and never sorts (CDT-C6).
                                if (applicationWindow().headerTapped(exploreTableView, exploreHeaderView, column,
                                                                     parent.width, eventPoint.position.x))
                                    return
                                exploreTableView.selectedRow = -1
                                if (root.sortColumn === column) {
                                    root.sortAscending = !root.sortAscending
                                } else {
                                    root.sortColumn    = column
                                    root.sortAscending = true
                                }
                                appManager1.sortExplore(root.sortColumn, root.sortAscending ? 0 : 1)
                            }
                        }
                    }
                }

                Rectangle {
                    id: exploreHeaderSep
                    anchors { top: exploreHeaderView.bottom; left: parent.left; right: parent.right }
                    height: 1
                    color: KTheme.line
                }

                Rectangle { // List background (empty area)
                    anchors.fill: exploreTableView
                    color: KTheme.surface
                }

                TableView {
                    id: exploreTableView
                    anchors { top: exploreHeaderSep.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
                    columnSpacing: 0
                    rowSpacing: 0
                    model: appManager1.exploreSortModel

                    property int selectedRow: -1
                    onSelectedRowChanged: {
                        const info = selectedRow >= 0 ? appManager1.exploreRowInfo(selectedRow) : ({})
                        root.selectedFullPath = info.entryType === "file" ? info.fullPath : ""
                    }

                    ScrollBar.vertical:   ScrollBar { policy: ScrollBar.AsNeeded }
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                    rowHeightProvider: function(row) { return root.rowHeight }

                    // Keyboard navigation (CDT-F5–F11), shared in Main.qml. Enter and
                    // the Menu key go through the selected row's own cell.
                    activeFocusOnTab: true
                    Keys.onPressed: function(event) {
                        // Space shows/hides the Inspector, unless it continues a type-ahead name
                        if (event.key === Qt.Key_Space && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                                && applicationWindow().typeAheadIdle(exploreTableView)) {
                            root.inspectorToggleRequested()
                            event.accepted = true
                            return
                        }
                        applicationWindow().tableKeyPressed(exploreTableView, event, {
                            activate: function(r) {
                                let cell = exploreTableView.itemAtCell(exploreTableView.leftColumn, r)
                                if (cell) cell.activate()
                            },
                            openMenu: function(r) {
                                let cell = exploreTableView.itemAtCell(exploreTableView.leftColumn, r)
                                if (cell) cell.openExploreContextMenu(cell)
                            },
                            nameAt: function(r) {
                                let m = appManager1.exploreSortModel
                                return m.data(m.index(r, 0), Qt.DisplayRole)
                            }
                        })
                    }

                    // TableView estimates its contentWidth from the columns loaded so
                    // far, scaled to the full column count - hidden zero-width ones
                    // included - which left a large empty band at the right end of
                    // the scroll range (measured: 2745 for 2170 of real columns).
                    // The real sum is set instead, after every layout pass, so it
                    // follows drags, the double-click fit and shown/hidden columns.
                    function columnsTotalWidth() {
                        let total = 0
                        for (let c = 0; c < columns; c++)
                            total += Math.max(0, exploreTableView.columnWidthProvider(c))
                        return total
                    }
                    onLayoutChanged: {
                        contentWidth = columnsTotalWidth()
                        columnWidthSaveTimer.restart()
                    }

                    // Persisted column widths (CDT-F12): saved once a resize settles,
                    // not on every step of a drag.
                    Timer {
                        id: columnWidthSaveTimer
                        interval: 500
                        onTriggered: root.savedColumnWidths =
                            applicationWindow().savePersistedColumnWidths(exploreTableView, root.persistedColumnKeys,
                                                                          root.savedColumnWidths)
                    }
                    onColumnsChanged: contentWidth = columnsTotalWidth()

                    // A hidden column (default width 0) stays hidden whatever width
                    // Qt's drag-resize gives it: grabbing just right of a divider that
                    // hidden columns share resizes a hidden one (measured), which made
                    // it appear. Other columns: the user's width, else the default.
                    columnWidthProvider: function(column) {
                        const d = defaultColumnWidth(column)
                        if (d === 0) return 0
                        const w = exploreTableView.explicitColumnWidth(column)
                        return w >= 0 ? w : d
                    }

                    function defaultColumnWidth(column) {
                        // Same index layout as Search results (EXP-C13); the shown
                        // set mirrors K2's Explore file list (EXP-F16/F17).
                        let meta = root.showMetadataColumns
                        switch (column) {
                            case  0: return root.savedColumnWidths[0] ?? 260  // Name (CDT-F12)
                            case  1: return 90   // Size
                            case  2: return 150  // Date
                            case  3: return 0    // Directory (hidden — every row is in the open folder, as K2)
                            case  4: return 0    // Catalog Name (not used in Explore)
                            case  5: return 0    // Catalog ID (not used in Explore)
                            case  6: return 0    // folders-first sort key (hidden)
                            case  7: return 0    // Path (hidden)
                            case  8: return 80   // File Type
                            case  9: return 140  // MIME Type
                            case 10: return meta ? 60  : 0  // Width (merged image + video)
                            case 11: return meta ? 60  : 0  // Height (merged image + video)
                            case 12: return meta ? 80  : 0  // Duration (merged video + audio)
                            case 13: return 0    // Video Width  (merged into Width)
                            case 14: return 0    // Video Height (merged into Height)
                            case 15: return 0    // Audio Duration (merged into Duration)
                            case 16: return meta ? 140 : 0  // Artist
                            case 17: return meta ? 140 : 0  // Album
                            case 18: return meta ? 140 : 0  // Title
                            case 19: return 260  // Checksum (SHA256)
                            case 20: return 130  // Checksum Date
                        }
                        return 0
                    }

                    delegate: Rectangle {
                        required property int    row
                        required property int    column
                        required property var    display
                        required property string entryType
                        required property string fileType
                        required property string fullPath
                        required property string folderPath
                        required property string name
                        required property string checksumSha256
                        implicitHeight: root.rowHeight
                        // Full, unelided content width, read by the double-click fit (CDT-F4).
                        implicitWidth: cellLabel.x + cellLabel.implicitWidth + 4
                                       + (rowTag.visible ? rowTag.width + 4 : 0)

                        readonly property string checksum: checksumSha256

                        // Design system colours (D34, MDI-F115): the selected row
                        // in the accent's soft tone, the others alternating the
                        // card's two surfaces.
                        readonly property bool selected: exploreTableView.selectedRow === row
                                                         || (entryType === "file" && root.pickedPaths[fullPath] === true)
                        color: selected ? KTheme.accentSoft
                                        : (row % 2 === 0 ? KTheme.surface : KTheme.surface2)

                        // Column separator, faint: it guides the eye along a row
                        Rectangle {
                            visible: column > 0
                            anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
                            width: 1
                            color: KTheme.line
                            opacity: 0.6
                        }

                        // File type icon (column 0 only)
                        Kirigami.Icon {
                            id: fileTypeIcon
                            visible: column === 0
                            anchors {
                                left: parent.left
                                verticalCenter: parent.verticalCenter
                                leftMargin: 6
                            }
                            // A preview made by the enrichment runner replaces
                            // the type icon (fork, MDI-F44); it is read from the
                            // destination drive, so it shows with the source offline
                            readonly property string previewUrl:
                                column === 0 && entryType === "file"
                                ? appManager1.mdiPreviewUrl(root.catalogId, fullPath, appManager1.mdiRevision) : ""
                            source: {
                                if (previewUrl !== "") return previewUrl
                                if (entryType === "folder") return "folder"
                                switch (fileType) {
                                    case "audio":  return "audio-x-mpeg"
                                    case "image":  return "image-jpeg"
                                    case "video":  return "video-mp4"
                                    case "text":   return "view-list-text"
                                    case "other":  return "document-open"
                                    default:       return "application-x-zerosize"
                                }
                            }
                            implicitWidth:  root.rowIconSize
                            implicitHeight: root.rowIconSize
                        }

                        // The row's menu without a right click: a "more" button
                        // at the end of the selected row's name. Only on the
                        // selected row, so the list stays quiet; the Menu key,
                        // the right click and a long press open the same menu.
                        IconButton {
                            id: rowMoreButton
                            visible: column === 0 && exploreTableView.selectedRow === row
                            // Above the row-wide MouseArea declared after it
                            z: 2
                            anchors { right: parent.right; rightMargin: 2; verticalCenter: parent.verticalCenter }
                            flat: true
                            focusPolicy: Qt.NoFocus
                            icon.name: "overflow-menu"
                            text: qsTranslate("PageBackupForm", "More actions")
                            onClicked: parent.openExploreContextMenu(rowMoreButton)
                        }

                        // A file that already has a description, summary, transcript
                        // or classification (File details shows them); a folder
                        // that is a catalog of its own, which opening it shows
                        KInfoTag {
                            id: rowTag
                            readonly property bool ownCatalog: entryType === "folder" && root.catalogForFolder(fullPath) > 0
                            visible: column === 0
                                     && (ownCatalog || (entryType === "file"
                                         && appManager1.mdiHasAi(root.catalogId, fullPath, appManager1.mdiRevision)))
                            anchors {
                                right: rowMoreButton.visible ? rowMoreButton.left : parent.right
                                rightMargin: 4
                                verticalCenter: parent.verticalCenter
                            }
                            text: ownCatalog ? qsTr("Catalog") : qsTr("AI")
                            tone: ownCatalog ? "info" : "ai"
                        }

                        Controls.Label {
                            id: cellLabel
                            anchors {
                                left: column === 0 ? fileTypeIcon.right : parent.left
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin:  column === 0 ? 4 : 6
                                rightMargin: (rowTag.visible ? rowTag.width + 8 : 4)
                                             + (rowMoreButton.visible ? rowMoreButton.width + 2 : 0)
                            }
                            text: {
                                // Merged metadata columns, as in Search results: the
                                // primary value is `display`, the secondary is read
                                // from its sibling column.
                                let m = appManager1.exploreSortModel
                                if (column === 10) {   // Width  — image (10) ∥ video (13)
                                    let w = Number(display) || Number(m.data(m.index(row, 13), Qt.DisplayRole))
                                    return w > 0 ? String(w) : ""
                                }
                                if (column === 11) {   // Height — image (11) ∥ video (14)
                                    let h = Number(display) || Number(m.data(m.index(row, 14), Qt.DisplayRole))
                                    return h > 0 ? String(h) : ""
                                }
                                if (column === 12) {   // Duration — video (12) ∥ audio (15)
                                    let d = Number(display) || Number(m.data(m.index(row, 15), Qt.DisplayRole))
                                    return root.formatDuration(d)
                                }
                                if (display === undefined || display === null) return ""
                                // Folder rows carry their recursive total, so the
                                // cell is no longer suppressed by entry type
                                // (EXP-C11); and a folder holding nothing at any
                                // depth must read as a zero rather than as a blank
                                // that looks like missing data (EXP-F10). The same
                                // now applies to an empty file.
                                // A folder that is a catalog of its own holds its files
                                // there: no size here rather than a misleading 0
                                if (column === 1 && entryType === "folder" && root.catalogForFolder(fullPath) > 0)
                                    return "—"
                                if (column === 1) return appManager1.formatDataSizeDelta(Number(display))
                                return String(display)
                            }
                            horizontalAlignment: (column === 1 || column === 12) ? Text.AlignRight : Text.AlignLeft
                            color: parent.selected ? KTheme.accentText
                                                   : (column === 0 ? KTheme.text : KTheme.text2)
                            font.pixelSize: KTheme.body
                            elide: column === 3 ? Text.ElideLeft : Text.ElideRight
                            clip: true
                        }

                        // The row's left-click action and context menu, on the cell
                        // so the mouse and the keyboard share them (CDT-F7/F8, CDT-C8).
                        function activate() {
                            if (entryType === "folder")
                                root.folderNavigated(fullPath)
                            if (entryType === "file")
                                root.openEntryFile(fullPath)
                        }

                        // anchorItem null: opens at the pointer; otherwise under that cell.
                        function openExploreContextMenu(anchorItem) {
                            root._activeFilePath   = fullPath
                            root._activeFileName   = name
                            root._activeFolderPath = folderPath
                            root._activeChecksum   = checksum
                            root._activeEntryType  = entryType
                            if (anchorItem)
                                exploreContextMenu.popup(anchorItem, 0, anchorItem.height)
                            else
                                exploreContextMenu.popup()
                        }

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            // A click on a file only selects it: File details shows its
                            // preview. It opens with a double click or Enter (owner,
                            // 2026-10-30; K2 opens it on the click). A folder opens on
                            // the click, as before.
                            onClicked: function(mouse) {
                                // Cmd/Ctrl adds or removes the file, Shift takes a range (D38)
                                const toggle = (mouse.modifiers & Qt.ControlModifier) !== 0
                                const range  = (mouse.modifiers & Qt.ShiftModifier) !== 0
                                if (mouse.button === Qt.LeftButton && entryType === "file" && (toggle || range))
                                    root.pickRow(row, toggle, range)
                                else if (mouse.button === Qt.LeftButton)
                                    root.pickedPaths = ({})
                                exploreTableView.selectedRow = row
                                exploreTableView.forceActiveFocus()   // keyboard follows the click (CDT-F5)
                                if (mouse.button === Qt.LeftButton && entryType === "folder")
                                    parent.activate()
                                if (mouse.button === Qt.RightButton)
                                    parent.openExploreContextMenu(null)
                            }
                            // Only on the row the first click selected: a click on a
                            // folder reloads the list, and the row now under the
                            // pointer must not open
                            onDoubleClicked: function(mouse) {
                                if (mouse.button === Qt.LeftButton && entryType === "file"
                                        && exploreTableView.selectedRow === row)
                                    parent.activate()
                            }
                            onPressAndHold: {
                                exploreTableView.selectedRow = row
                                exploreTableView.forceActiveFocus()
                                parent.openExploreContextMenu(null)
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Context menu ─────────────────────────────────────────────────────
    Controls.Menu {
        id: exploreContextMenu

        // Information and people of the file, or of every file chosen (D38)
        Controls.MenuItem {
            visible: root._activeEntryType === "file"
            height: visible ? implicitHeight : 0
            text: root.pickedCount > 1 ? qsTr("Add information to %1 files…").arg(root.pickedCount)
                                       : qsTr("Add information…")
            icon.name: "document-edit"
            onTriggered: root.infoRequested(root.pickedCount > 1 ? root.infoFiles()
                                                                 : [{ catalogId: root.catalogId, fullPath: root._activeFilePath }])
        }

        Controls.MenuItem {
            text: root._activeEntryType === "folder" ? qsTr("Open folder") : qsTr("Open file")
            icon.name: "document-open"
            onTriggered: {
                if (root._activeEntryType === "folder")
                    root.openEntryFolder(root._activeFilePath)
                else
                    root.openEntryFile(root._activeFilePath)
            }
        }

        Controls.MenuItem {
            text: qsTr("Open folder")
            icon.name: "document-open-data"
            visible: root._activeEntryType === "file"
            height: visible ? implicitHeight : 0
            onTriggered: root.openEntryFolder(root._activeFolderPath)
        }

        Controls.MenuSeparator {}

        Controls.MenuItem {
            text: qsTr("Show extended metadata (JSON)")
            icon.name: "configure"
            // Explore is always scoped to one catalog, so the level is read from
            // that catalog rather than from a per-row column as in Search.
            readonly property bool hasMetadata:
                root._activeEntryType === "file" &&
                root.catalogId > 0 &&
                appManager1.catalogIncludesExtendedMetadata(root.catalogId)
            visible: hasMetadata
            height:  hasMetadata ? implicitHeight : 0
            onTriggered: metadataDialog.showFor(root.catalogId,
                                                root._activeFileName,
                                                root._activeFolderPath)
        }

        Controls.MenuItem {
            text: qsTr("Show analysis")
            icon.name: "view-preview"
            readonly property bool hasAnalysis:
                root._activeEntryType === "file" && root.catalogId > 0 &&
                appManager1.mdiHasData(root.catalogId, root._activeFilePath, appManager1.mdiRevision)
            visible: hasAnalysis
            height:  hasAnalysis ? implicitHeight : 0
            onTriggered: mdiDetailsDialog.showFor(root.catalogId, root._activeFilePath, root._activeFileName)
        }

        Controls.MenuSeparator {
            visible: root._activeEntryType === "file" && root.catalogId > 0
                     && appManager1.catalogIncludesExtendedMetadata(root.catalogId)
            height: visible ? implicitHeight : 0
        }

        // Copy entries, shortest copied text first (EXP-F19/F20), with K2's
        // wording (EXP-C17). Each shows only on the row type it applies to, so
        // file rows read: name without extension, name with extension, folder
        // path, absolute path; folder rows: folder name, folder path.
        Controls.MenuItem {
            text: qsTr("Copy file name without extension")
            icon.name: "edit-copy"
            visible: root._activeEntryType === "file"
            height: visible ? implicitHeight : 0
            onTriggered: {
                var n = root._activeFileName
                var dot = n.lastIndexOf(".")
                appManager1.copyToClipboard(dot > 0 ? n.substring(0, dot) : n)
            }
        }

        Controls.MenuItem {
            text: qsTr("Copy file name with extension")
            icon.name: "edit-copy"
            visible: root._activeEntryType === "file"
            height: visible ? implicitHeight : 0
            onTriggered: appManager1.copyToClipboard(root._activeFileName)
        }

        Controls.MenuItem {
            text: qsTr("Copy folder name")
            icon.name: "edit-copy"
            visible: root._activeEntryType === "folder"
            height: visible ? implicitHeight : 0
            onTriggered: appManager1.copyToClipboard(root._activeFileName)
        }

        Controls.MenuItem {
            text: qsTr("Copy folder path")
            icon.name: "edit-copy"
            onTriggered: appManager1.copyToClipboard(root._activeFolderPath)
        }

        Controls.MenuItem {
            text: qsTr("Copy file absolute path")
            icon.name: "edit-copy"
            visible: root._activeEntryType === "file"
            height: visible ? implicitHeight : 0
            onTriggered: appManager1.copyToClipboard(root._activeFilePath)
        }

        Controls.MenuItem {
            text: qsTr("Copy Checksum")
            icon.name: "edit-copy"
            visible: root._activeEntryType === "file" && root._activeChecksum !== ""
            height: visible ? implicitHeight : 0
            onTriggered: appManager1.copyToClipboard(root._activeChecksum)
        }

        Controls.MenuSeparator {
            visible: root._activeEntryType === "file"
            height: visible ? implicitHeight : 0
        }

        Controls.MenuItem {
            text: qsTr("Calculate Checksum") + " (SHA-256)"
            icon.name: "document-properties"
            visible: root._activeEntryType === "file" && root._activeChecksum === ""
            height: visible ? implicitHeight : 0
            onTriggered: {
                var cksum = appManager1.calculateAndSaveChecksum(
                    root._activeFilePath,
                    root._activeFileName,
                    root._activeFolderPath,
                    root.catalogId)
                if (cksum.length > 0) {
                    root._activeChecksum = cksum
                    exploreChecksumResultDialog.checksumText = cksum
                    exploreChecksumResultDialog.wasSaved = root.catalogId > 0
                    exploreChecksumResultDialog.open()
                } else {
                    showPassiveNotification(qsTr("File not found or could not be read"))
                }
            }
        }

        Controls.MenuItem {
            text: qsTr("Verify Checksum") + " (SHA-256)"
            icon.name: "document-properties"
            visible: root._activeEntryType === "file" && root._activeChecksum !== ""
            height: visible ? implicitHeight : 0
            onTriggered: {
                var result = appManager1.verifyFileChecksum(
                    root._activeFilePath,
                    root._activeChecksum)
                if (result === "match") {
                    exploreChecksumResultDialog.checksumText = root._activeChecksum
                    exploreChecksumResultDialog.wasSaved = false
                    exploreChecksumResultDialog.open()
                } else if (result.startsWith("mismatch:")) {
                    exploreChecksumMismatchDialog.expectedChecksum = root._activeChecksum
                    exploreChecksumMismatchDialog.actualChecksum   = result.substring(9)
                    exploreChecksumMismatchDialog.open()
                } else {
                    showPassiveNotification(qsTr("Error: ") + result.substring(6))
                }
            }
        }

        Controls.MenuItem {
            text: qsTr("Calculate Checksum") + " (MD5)"
            icon.name: "document-properties"
            visible: root._activeEntryType === "file"
            height: visible ? implicitHeight : 0
            onTriggered: {
                var cksum = appManager1.calculateChecksumMd5(root._activeFilePath)
                if (cksum.length > 0) {
                    exploreChecksumResultDialog.algorithm    = "MD5"
                    exploreChecksumResultDialog.checksumText = cksum
                    exploreChecksumResultDialog.open()
                } else {
                    showPassiveNotification(qsTr("File not found or could not be read"))
                }
            }
        }

        Controls.MenuSeparator {}

        Controls.MenuItem {
            text: qsTr("Move file to Trash")
            icon.name: "user-trash"
            visible: root._activeEntryType === "file"
            height: visible ? implicitHeight : 0
            onTriggered: exploreTrashDialog.ask(root._activeFilePath, exploreTableView.selectedRow)
        }

        Controls.MenuItem {
            text: qsTr("Move folder to Trash")
            icon.name: "user-trash"
            visible: root._activeEntryType === "folder"
            height: visible ? implicitHeight : 0
            onTriggered: exploreTrashDialog.ask(root._activeFilePath, exploreTableView.selectedRow)
        }

        Controls.MenuItem {
            text: qsTr("Delete file")
            icon.name: "edit-delete"
            visible: root._activeEntryType === "file"
            height: visible ? implicitHeight : 0
            // Greyed rather than hidden while permanent deletion is switched off,
            // so the action stays discoverable and the setting can be found.
            enabled: appManager1.allowFileDeletion
            onTriggered: exploreDeleteFileDialog.open()
        }
    }

    // Moving to the trash acts on the real drive: it asks first and then says
    // how it went, as K2's Explore does (its strings, verbatim)
    Kirigami.Dialog {
        id: exploreTrashDialog
        property string path: ""
        property int    row: -1
        function ask(fullPath, rowIndex) {
            path = fullPath
            row  = rowIndex
            open()
        }
        // The design system surface, not Kirigami's View colours (D34)
        background: Rectangle {
            color: KTheme.surface
            radius: KTheme.radiusCard
            border.width: 1
            border.color: KTheme.line
        }
        title: qsTr("Confirmation")
        preferredWidth: Kirigami.Units.gridUnit * 28
        padding: Kirigami.Units.largeSpacing

        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("Move to Trash")
                icon.name: "user-trash"
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
            }
            Controls.Button {
                text: qsTr("Cancel")
                icon.name: "dialog-cancel"
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.RejectRole
            }
            onAccepted: {
                if (appManager1.moveFileToTrash(exploreTrashDialog.path)) {
                    appManager1.exploreRemoveRow(exploreTrashDialog.row)
                    exploreTableView.selectedRow = -1   // File details no longer shows it
                    showPassiveNotification(qsTr("Moved to trash:<br/>") + exploreTrashDialog.path)
                } else {
                    showPassiveNotification(qsTr("Move to trash failed."))
                }
                exploreTrashDialog.close()
            }
            onRejected: exploreTrashDialog.close()
        }

        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                text: qsTr("Move\n%1\nto the trash?").arg(exploreTrashDialog.path)
                wrapMode: Text.WrapAnywhere
            }
        }
    }

    // Permanent deletion asks first, as the Search Results one does. Without this
    // the Explore menu destroyed a file on a single click, unrecoverably.
    Kirigami.Dialog {
        id: exploreDeleteFileDialog
        // The design system surface, not Kirigami's View colours (D34)
        background: Rectangle {
            color: KTheme.surface
            radius: KTheme.radiusCard
            border.width: 1
            border.color: KTheme.line
        }
        title: qsTr("Delete File")
        preferredWidth: Kirigami.Units.gridUnit * 28
        padding: Kirigami.Units.largeSpacing

        // Each button carries a role and the style places it, so every platform
        // gets the order it expects (SpecValidationRules D1/D2). The pair cannot
        // go through customFooterActions: Kirigami builds those as role-less
        // buttons inside this same button box, so they sort after every
        // role-carrying button and would always land right of Cancel.
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("Delete")
                icon.name: "edit-delete"
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
            }
            Controls.Button {
                text: qsTr("Cancel")
                icon.name: "dialog-cancel"
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.RejectRole
            }
            onAccepted: {
                if (appManager1.deleteSingleFile(root._activeFilePath)) {
                    appManager1.exploreRemoveRow(exploreTableView.selectedRow)
                    exploreTableView.selectedRow = -1   // File details no longer shows it
                    showPassiveNotification(qsTr("File deleted"))
                } else {
                    showPassiveNotification(qsTr("Could not delete file"))
                }
                exploreDeleteFileDialog.close()
            }
            onRejected: exploreDeleteFileDialog.close()
        }

        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                text: qsTr("Permanently delete this file? This cannot be undone.\n\n%1")
                          .arg(root._activeFilePath)
                wrapMode: Text.WordWrap
            }
        }
    }

    onCurrentFolderPathChanged: {
        pickedPaths = ({})   // a choice of files belongs to its folder
        loadEntries()
    }
    // Another catalog at the same folder (a joined or split catalog keeps its
    // path): the list is reloaded too, or it shows the previous catalog's rows
    onCatalogIdChanged: loadEntries()

    // ── Checksum result dialog ────────────────────────────────────────────
    Kirigami.Dialog {
        id: exploreChecksumResultDialog
        // The design system surface, not Kirigami's View colours (D34)
        background: Rectangle {
            color: KTheme.surface
            radius: KTheme.radiusCard
            border.width: 1
            border.color: KTheme.line
        }
        property string checksumText: ""
        property bool   wasSaved: false
        // "MD5" for an on-demand MD5, which is never stored (SpecCatalogChecksum CKS-F2)
        property string algorithm: "SHA-256"
        title: qsTr("Checksum") + " (" + algorithm + ")"
        onClosed: algorithm = "SHA-256"
        standardButtons: Kirigami.Dialog.Close
        preferredWidth: Kirigami.Units.gridUnit * 36
        padding: Kirigami.Units.largeSpacing

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Controls.Label {
                text: exploreChecksumResultDialog.algorithm + ": " + exploreChecksumResultDialog.checksumText
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
                font.family: KTheme.mono
            }
            Controls.Label {
                text: exploreChecksumResultDialog.wasSaved
                      ? qsTr("Checksum saved to database.")
                      : qsTr("Checksums match.")
                opacity: 0.7
                visible: exploreChecksumResultDialog.algorithm !== "MD5"
            }
            KButton {
                variant: "secondary"
                text: qsTr("Copy to Clipboard")
                icon.name: "edit-copy"
                onClicked: {
                    appManager1.copyToClipboard(exploreChecksumResultDialog.checksumText)
                    showPassiveNotification(qsTr("Checksum copied to clipboard"))
                }
            }
        }
    }

    // ── Checksum mismatch dialog ──────────────────────────────────────────
    Kirigami.Dialog {
        id: exploreChecksumMismatchDialog
        // The design system surface, not Kirigami's View colours (D34)
        background: Rectangle {
            color: KTheme.surface
            radius: KTheme.radiusCard
            border.width: 1
            border.color: KTheme.line
        }
        property string expectedChecksum: ""
        property string actualChecksum:   ""
        title: qsTr("Checksum Mismatch")
        preferredWidth: Kirigami.Units.gridUnit * 36
        padding: Kirigami.Units.largeSpacing

        // Each button carries a role and the style places it, so every platform
        // gets the order it expects (SpecValidationRules D1/D2). The pair cannot
        // go through customFooterActions: Kirigami builds those as role-less
        // buttons inside this same button box, so they sort after every
        // role-carrying button and would always land right of Cancel.
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("Update Checksum")
                icon.name: "document-save"
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
            }
            Controls.Button {
                text: qsTr("Cancel")
                icon.name: "dialog-cancel"
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.RejectRole
            }
            onAccepted: {
                appManager1.calculateAndSaveChecksum(
                    root._activeFilePath,
                    root._activeFileName,
                    root._activeFolderPath,
                    root.catalogId)
                root._activeChecksum = exploreChecksumMismatchDialog.actualChecksum
                showPassiveNotification(qsTr("Checksum saved to database"))
                exploreChecksumMismatchDialog.close()
            }
            onRejected: exploreChecksumMismatchDialog.close()
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Controls.Label { text: qsTr("Checksums do not match."); font.bold: true }
            Controls.Label { text: qsTr("Expected:"); opacity: 0.7 }
            Controls.Label {
                text: exploreChecksumMismatchDialog.expectedChecksum
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
                font.family: KTheme.mono
            }
            Controls.Label { text: qsTr("Actual:"); opacity: 0.7 }
            Controls.Label {
                text: exploreChecksumMismatchDialog.actualChecksum
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
                font.family: KTheme.mono
            }
        }
    }

    // ── Metadata dialog ───────────────────────────────────────────────────
    MetadataDialog { id: metadataDialog }
    MdiDetailsDialog { id: mdiDetailsDialog }
    function showClusters() { mdiDetailsDialog.showClusters(root.catalogId, root.catalogName) }
}
