import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Left panel of the Explore page: folder tree for the active explore catalog.
// Entry point: openByDeviceId(deviceId) called from context menus.
// Redesign (D34, phase R2, "Browse the archive"): the tree is a card on the
// design system's ground; rows are marked like the sidebar's entries.
Item {
    id: root

    signal folderClicked(string folderPath)
    signal catalogOpenRequested(var catalogInfo)
    signal tagFolderRequested(string folderPath)   // EXP-F21 / EXP-F22

    // Folder the context menu was opened on; the selection is left as it is (EXP-F21)
    property string contextFolderPath: ""
    // From the keyboard it opens under the row (atItem), not at the pointer
    function openFolderContextMenu(folderPath, atItem) {
        root.contextFolderPath = folderPath
        if (atItem)
            folderContextMenu.popup(atItem, 0, atItem.height)
        else
            folderContextMenu.popup()
    }

    Controls.Menu {
        id: folderContextMenu
        Controls.MenuItem {
            text: qsTr("Tag this folder")
            icon.name: "tag"
            onTriggered: root.tagFolderRequested(root.contextFolderPath)
        }
    }

    property string catalogName: ""
    property string catalogPath: ""
    property int    folderCount: 0

    // Total size of the whole catalog, computed live from the file records — an
    // empty folder path means "the whole catalog" — so it always reconciles with
    // the per-folder figures in the file list rather than drifting from a stored
    // total refreshed only at catalog update (SpecExplore.md EXP-F13/F14).
    property real   catalogTotalSize: 0
    property string selectedFolderPath: ""

    // Row buttons are sized to sit inside the height the text already needs, so
    // they never drive the row height. A row without sub-directories keeps the
    // button in the layout and merely fades it: a button that comes and goes
    // re-lays the row out, which moves everything on it and flickers the tree.
    // Same disclosure control as the Devices tree, same size (CDT-F3). That one
    // fills its table row, which is 30px; smallMedium + largeSpacing is the same
    // 30 in Kirigami's units, so the two match without hard-coding a number
    // here. The icon inside stays iconSizes.small on both - it is the click
    // TARGET that grows, not the chevron.
    // The folder icon follows the icon size the user chose (THM-F16), and the
    // row with it, exactly as the Devices table rows do; with the option off it
    // is still the same 30.
    readonly property real rowIconSize: appManager1.biggerIconSize
                                        ? Kirigami.Units.iconSizes.medium
                                        : Kirigami.Units.iconSizes.smallMedium
    readonly property real rowButton: rowIconSize + Kirigami.Units.largeSpacing

    // How far each level of the tree steps in.
    readonly property real indentStep: Kirigami.Units.gridUnit

    // The whole tree, in the depth-first order the core returns it. The visible
    // rows are derived from it, so collapsing never loses anything.
    property var allFolders: []

    // Collapsed rows, keyed by full path. Transient view state: it is never
    // persisted, and it is rebuilt from scratch each time a catalog is opened.
    property var collapsedPaths: ({})

    property bool canCollapse: false
    property bool canExpand: false

    // Set while a catalog is being opened, so the reveal handler does not run
    // against the tree that is on its way out.
    property bool _loading: false

    // The catalog on show, so selecting it again does not reload it (EXP-F27)
    property int openedDeviceId: -1
    Connections {   // IDs restart in another collection
        target: appManager1
        function onDatabaseModeChanged() { root.openedDeviceId = -1 }
    }

    // Open a catalog by device ID (called from Devices/Selection context menus)
    // folderPath: a folder to select once the catalog is open (EXP-F8), or none.
    // In Memory mode the open may finish later, after a reported load
    // (EXP-F28): exploreCatalogOpened then brings the result.
    property string _pendingFolder: ""
    function openByDeviceId(deviceId, folderPath) {
        appManager1.setBusyCursor(true)   // as K2 does while opening a catalog
        const info = appManager1.exploreOpenCatalog(deviceId)
        if (info.deferred)                // a load runs; this one follows it (EXP-F31)
            return
        root._pendingFolder = folderPath ?? ""
        if (info.loading)
            return
        _handleOpenResult(info)
    }
    function _handleOpenResult(info) {
        appManager1.setBusyCursor(false)
        if (info.refused) {               // another operation runs (EXP-F32)
            applicationWindow().showPassiveNotification(info.message)
            return
        }
        if (info.stopped)                 // Explore keeps what it showed (EXP-F30)
            return
        root.openedDeviceId = info.success ? info.deviceId : -1
        _applyOpenResult(info)
        if (info.success && root._pendingFolder !== "") {
            root.selectedFolderPath = root._pendingFolder
            root.folderClicked(root._pendingFolder)
        }
        root._pendingFolder = ""
    }
    Connections {
        target: appManager1
        function onExploreCatalogOpened(info) { root._handleOpenResult(info) }
        function onExploreOpenPending(deviceId) { root.openByDeviceId(deviceId) }
    }

    function _applyOpenResult(info) {
        root._loading = true
        if (!info.success) {
            root.catalogName = ""
            root.catalogPath = ""
            root.folderCount = 0
            root.catalogTotalSize = 0
            root.allFolders  = []
            root.collapsedPaths = ({})
            folderListModel.clear()
            _updateAbility()
            root._loading = false
            return
        }
        root.catalogName        = info.name
        root.catalogPath        = info.path
        root.folderCount        = info.folderCount
        root.catalogTotalSize   = appManager1.getExploreFolderStats("").totalSize
        root.selectedFolderPath = info.path

        // No synthetic root row for the device. The loader already returns the
        // catalog's own directory as the tree's root — folder paths are made
        // relative to the catalog's PARENT — so adding a device row on top gave
        // two rows for the same thing and cost a level of indent. The selected
        // device is named above the list anyway.
        var rows = []
        var folders = appManager1.getExploreFolders()
        for (var i = 0; i < folders.length; i++) {
            rows.push({
                name:     folders[i].name,
                fullPath: folders[i].fullPath,
                level:    folders[i].level
            })
        }
        root.allFolders = rows

        _collapseToInitialDepth()
        _rebuild()

        root._loading = false
        folderListView.currentIndex = 0
        root.folderClicked(info.path)
        root.catalogOpenRequested(info)
    }

    // A row has children when the row after it steps one level deeper. The list
    // is depth-first, so that is the whole test — the tree needs no other shape.
    function _hasChildren(index) {
        return index + 1 < root.allFolders.length
               && root.allFolders[index + 1].level > root.allFolders[index].level
    }

    // How many ranks stand open when a catalog is loaded. K2 opens the root and
    // its immediate children only (expandToDepth(0)); Katalog 3 opens one rank
    // more, because a catalog's real content usually starts a folder or two
    // below the root and the first thing one did with the K2 tree was open it.
    // Dropped from 2 to 1 when the synthetic device row was removed: every level
    // shifted up by one, so 1 keeps exactly the same folders open as before.
    readonly property int initialOpenDepth: 1

    function _collapseToInitialDepth() {
        var collapsed = ({})
        for (var i = 0; i < root.allFolders.length; i++)
            if (root.allFolders[i].level >= root.initialOpenDepth && _hasChildren(i))
                collapsed[root.allFolders[i].fullPath] = true
        root.collapsedPaths = collapsed
    }

    // A row is hidden when a row above it is collapsed. The tree is depth-first,
    // so one pass with a "skip until the level comes back" mark is enough.
    function _rebuild() {
        folderListModel.clear()
        var skipDeeperThan = -1
        var selectedRow = -1
        for (var i = 0; i < root.allFolders.length; i++) {
            var folder = root.allFolders[i]
            if (skipDeeperThan >= 0) {
                if (folder.level > skipDeeperThan)
                    continue
                skipDeeperThan = -1
            }
            var hasChildren = _hasChildren(i)
            var isCollapsed = hasChildren && root.collapsedPaths[folder.fullPath] === true
            if (folder.fullPath === root.selectedFolderPath)
                selectedRow = folderListModel.count
            folderListModel.append({
                name:        folder.name,
                fullPath:    folder.fullPath,
                level:       folder.level,
                hasChildren: hasChildren,
                isCollapsed: isCollapsed
            })
            if (isCollapsed)
                skipDeeperThan = folder.level
        }
        if (selectedRow >= 0)
            folderListView.currentIndex = selectedRow
        _updateAbility()
    }

    // The header buttons are disabled when they would do nothing, so the tree
    // says what it can still do rather than answering a press with nothing.
    function _updateAbility() {
        var collapsable = false
        for (var i = 0; i < folderListModel.count; i++) {
            var row = folderListModel.get(i)
            if (row.hasChildren && !row.isCollapsed) {
                collapsable = true
                break
            }
        }
        root.canCollapse = collapsable

        var expandable = false
        for (var path in root.collapsedPaths) {
            if (root.collapsedPaths[path] === true) {
                expandable = true
                break
            }
        }
        root.canExpand = expandable
    }

    function toggleCollapsed(folderPath) {
        if (root.collapsedPaths[folderPath] === true)
            delete root.collapsedPaths[folderPath]
        else
            root.collapsedPaths[folderPath] = true
        _rebuild()
    }

    // Keyboard focus on a row (or on its disclosure button) by index. The tree
    // is rebuilt on every open and close, which destroys the row that had the
    // focus, so the caller hands it back to the row of the same folder.
    function focusRow(index, onDisclosure) {
        if (index < 0 || index >= folderListModel.count)
            return
        folderListView.positionViewAtIndex(index, ListView.Contain)
        var row = folderListView.itemAtIndex(index)
        if (row)
            (onDisclosure ? row.disclosure : row).forceActiveFocus(Qt.TabFocusReason)
    }
    function focusFolder(folderPath, onDisclosure) {
        for (var i = 0; i < folderListModel.count; i++)
            if (folderListModel.get(i).fullPath === folderPath)
                return focusRow(i, onDisclosure)
    }
    // Open or close a branch from the keyboard, the focus staying on it
    function toggleFromKeyboard(folderPath, onDisclosure) {
        root.toggleCollapsed(folderPath)
        root.focusFolder(folderPath, onDisclosure)
    }

    function collapseAll() {
        var collapsed = ({})
        for (var i = 0; i < root.allFolders.length; i++)
            if (_hasChildren(i))
                collapsed[root.allFolders[i].fullPath] = true
        root.collapsedPaths = collapsed
        _rebuild()
    }

    function expandAll() {
        root.collapsedPaths = ({})
        _rebuild()
    }

    // Close the deepest rank that is currently open: one press peels one layer
    // off the bottom, which is what "one level" means to the eye.
    function collapseOneLevel() {
        var deepest = -1
        for (var i = 0; i < folderListModel.count; i++) {
            var row = folderListModel.get(i)
            if (row.hasChildren && !row.isCollapsed)
                deepest = Math.max(deepest, row.level)
        }
        if (deepest < 0)
            return
        for (var j = 0; j < folderListModel.count; j++) {
            var candidate = folderListModel.get(j)
            if (candidate.level === deepest && candidate.hasChildren)
                root.collapsedPaths[candidate.fullPath] = true
        }
        _rebuild()
    }

    // Open the shallowest closed rank, so expanding undoes collapsing.
    function expandOneLevel() {
        var shallowest = -1
        for (var i = 0; i < root.allFolders.length; i++) {
            var folder = root.allFolders[i]
            if (root.collapsedPaths[folder.fullPath] !== true)
                continue
            if (shallowest < 0 || folder.level < shallowest)
                shallowest = folder.level
        }
        if (shallowest < 0)
            return
        for (var j = 0; j < root.allFolders.length; j++)
            if (root.allFolders[j].level === shallowest)
                delete root.collapsedPaths[root.allFolders[j].fullPath]
        _rebuild()
    }

    // Navigating into a sub-folder from the file list selects a row that may sit
    // under a closed one. Open its ancestors rather than highlight a row nobody
    // can see.
    function _revealSelected() {
        for (var i = 0; i < folderListModel.count; i++)
            if (folderListModel.get(i).fullPath === root.selectedFolderPath) {
                folderListView.currentIndex = i
                return
            }

        var index = -1
        for (var j = 0; j < root.allFolders.length; j++)
            if (root.allFolders[j].fullPath === root.selectedFolderPath) {
                index = j
                break
            }
        if (index < 0)
            return

        var level = root.allFolders[index].level
        for (var k = index - 1; k >= 0 && level > 0; k--) {
            if (root.allFolders[k].level < level) {
                delete root.collapsedPaths[root.allFolders[k].fullPath]
                level = root.allFolders[k].level
            }
        }
        _rebuild()
    }

    onSelectedFolderPathChanged: {
        if (!root._loading)
            _revealSelected()
    }

    // Depth control of the header: icon only, 40 px square (touch size),
    // with its name as tooltip (ICB-F1)
    component DepthButton: Controls.ToolButton {
        id: depthButton
        display: Controls.AbstractButton.IconOnly
        implicitWidth: 40
        implicitHeight: 40
        icon.color: KTheme.text2
        hoverEnabled: true
        Controls.ToolTip.text: text
        // On keyboard focus too: the name of an icon (WCAG 2.4.7)
        Controls.ToolTip.visible: hovered || visualFocus
        background: Rectangle {
            radius: KTheme.radiusItem
            color: depthButton.hovered && depthButton.enabled ? KTheme.hover : "transparent"

            // Keyboard focus: a ring outside the button (WCAG 2.4.7)
            Rectangle {
                anchors.fill: parent
                anchors.margins: -KTheme.focusOffset
                radius: parent.radius + KTheme.focusOffset
                color: "transparent"
                border.width: KTheme.focusWidth
                border.color: KTheme.accent
                visible: depthButton.visualFocus
            }
        }
    }

    // The tree's card, on the page's ground
    Rectangle {
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

            // Folders count header. Fixed height, matching the Explore file list's
            // card so the two lists line up when shown together.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin:  8
                Layout.rightMargin: 8
                // ...unless an enlarged title needs more (WCAG 1.4.4)
                Layout.preferredHeight: Math.max(applicationWindow().headerRowHeight, implicitHeight)
                spacing: 6
                clip: true

                Controls.Label {
                    text: qsTr("Folders")
                    color: KTheme.text
                    font.weight: Font.Bold
                    font.pixelSize: KTheme.titleCard + 1
                    Accessible.role: Accessible.Heading
                }
                KInfoTag {
                    Layout.leftMargin: 4
                    text: root.folderCount.toLocaleString(Qt.locale(), "f", 0)
                }
                // The catalog's total size, beside the catalog-wide folder count.
                // A formatted data size names its own unit.
                KInfoTag {
                    text: appManager1.formatDataSizeDelta(root.catalogTotalSize)
                    visible: root.catalogTotalSize > 0
                }

                Item { Layout.fillWidth: true }
            }

            // Depth controls, on their own row: the panel is narrow enough that they
            // would push the count off the end if they shared its row. Same height
            // as the file list's tag row (PageExploreFiles.headerSecondRowHeight).
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin:  2
                Layout.rightMargin: 2
                Layout.preferredHeight: 40
                spacing: 0

                DepthButton {
                    icon.name: "go-up"
                    text: qsTr("Collapse one level")
                    enabled: root.canCollapse
                    onClicked: root.collapseOneLevel()
                }
                DepthButton {
                    icon.name: "go-down"
                    text: qsTr("Expand one level")
                    enabled: root.canExpand
                    onClicked: root.expandOneLevel()
                }
                DepthButton {
                    icon.name: "collapse-all"
                    text: qsTr("Collapse all")
                    enabled: root.canCollapse
                    onClicked: root.collapseAll()
                }
                DepthButton {
                    icon.name: "expand-all"
                    text: qsTr("Expand all")
                    enabled: root.canExpand
                    onClicked: root.expandAll()
                }

                Item { Layout.fillWidth: true }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: KTheme.line }

            // Folder tree list
            Controls.ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 4
                Controls.ScrollBar.vertical.policy:   Controls.ScrollBar.AsNeeded
                Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AsNeeded
                background: Rectangle { // List background (empty area)
                    color: KTheme.surface
                }

                ListView {
                    id: folderListView
                    model: ListModel { id: folderListModel }

                    delegate: Controls.ItemDelegate {
                        id: folderDelegate
                        width: folderListView.width
                        highlighted: model.fullPath === root.selectedFolderPath

                        // The folder's name, for assistive technology only: the
                        // row draws its own content below (WCAG 4.1.2)
                        text: model.name
                        Accessible.name: model.name
                        Accessible.selectable: true
                        Accessible.selected: folderDelegate.highlighted
                        // For focusRow(): the row's disclosure button
                        readonly property alias disclosure: disclosureButton

                        // From the keyboard (WCAG 2.1.1): Tab reaches the rows,
                        // Up and Down move between them, Return, Enter or Space
                        // opens the folder, Right and Left open and close its
                        // branch, the Menu key or Shift+F10 opens its menu. A
                        // click does not take the focus.
                        focusPolicy: Qt.TabFocus
                        Keys.onReturnPressed: folderDelegate.clicked()
                        Keys.onEnterPressed: folderDelegate.clicked()
                        Keys.onUpPressed: root.focusRow(index - 1, false)
                        Keys.onDownPressed: root.focusRow(index + 1, false)
                        Keys.onRightPressed: if (model.hasChildren && model.isCollapsed)
                                                 root.toggleFromKeyboard(model.fullPath, false)
                        Keys.onLeftPressed: if (model.hasChildren && !model.isCollapsed)
                                                root.toggleFromKeyboard(model.fullPath, false)
                        Keys.onMenuPressed: root.openFolderContextMenu(model.fullPath, folderDelegate)
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) {
                                root.openFolderContextMenu(model.fullPath, folderDelegate)
                                event.accepted = true
                            }
                        }

                        // One fixed height for every row, the same as a Devices tree
                        // row (CDT-F3). Left to itself an ItemDelegate takes its
                        // height from the content plus whatever vertical padding the
                        // active style applies, which made the rows both taller than
                        // the Devices table and uneven between themselves. The
                        // disclosure button is exactly this tall, so it sets the
                        // floor; only a name enlarged by the text size setting
                        // pushes a row higher (WCAG 1.4.4).
                        implicitHeight: Math.max(root.rowButton, implicitContentHeight)
                        topPadding:    0
                        bottomPadding: 0
                        leftPadding:   4
                        rightPadding:  6
                        hoverEnabled:  true

                        // Painted by the application, not left to the Qt Quick
                        // Controls style (SpecTheme THM-F9): otherwise the row
                        // surface came from whichever style was active - Breeze on
                        // Linux, Fusion on Windows and macOS. Since the redesign
                        // (D34, MDI-F115) the colours are the design system's, as
                        // the sidebar's entries: the selected folder in the accent's
                        // soft tone, the pointed one in the hover tone, no stripes.
                        background: Item {
                            Rectangle {
                                anchors { fill: parent; leftMargin: 2; rightMargin: 2; topMargin: 1; bottomMargin: 1 }
                                radius: KTheme.radiusItem
                                color: folderDelegate.highlighted ? KTheme.accentSoft
                                     : folderDelegate.hovered ? KTheme.hover : "transparent"
                                // Keyboard focus: the accent line round the row,
                                // inside the list so it is never clipped (WCAG
                                // 2.4.7); 4:1 or more on the selected tint
                                border.width: folderDelegate.visualFocus ? KTheme.focusWidth : 0
                                border.color: KTheme.accent
                            }
                        }

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            // Depth is drawn rather than modelled: the list is already
                            // in tree order, so indenting is all the view needs to do.
                            Item {
                                implicitWidth: model.level * root.indentStep
                                implicitHeight: 1
                            }

                            // Shown whenever the directory has something inside it:
                            // whether a branch can be opened is a property of the
                            // tree, not of where the pointer happens to be.
                            Controls.ToolButton {
                                id: disclosureButton
                                padding: 0
                                implicitWidth:  root.rowButton
                                implicitHeight: root.rowButton
                                icon.width:  Kirigami.Units.iconSizes.small
                                icon.height: Kirigami.Units.iconSizes.small
                                icon.color:  folderDelegate.highlighted ? KTheme.accentText : KTheme.text3
                                // No surface of its own; keyboard focus is a ring
                                // outside the button (WCAG 2.4.7)
                                background: Item {
                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: -KTheme.focusOffset
                                        radius: KTheme.radiusItem + KTheme.focusOffset
                                        color: "transparent"
                                        border.width: KTheme.focusWidth
                                        border.color: KTheme.accent
                                        visible: disclosureButton.visualFocus
                                    }
                                }
                                opacity: model.hasChildren ? 1.0 : 0.0
                                enabled: model.hasChildren
                                // Breeze's -symbolic pair are its tree disclosure
                                // chevrons; go-next/go-down are navigation arrows.
                                // Same control as the Devices tree (CDT-F3).
                                icon.name: model.isCollapsed ? "go-next-symbolic" : "go-down-symbolic"
                                // The text stays for accessibility, but no tooltip:
                                // one per row means a pop-up follows the pointer all
                                // the way down the tree, and a disclosure arrow is
                                // self-explanatory in a way the header controls,
                                // which keep theirs (EXP-F9), are not.
                                text: model.isCollapsed ? qsTr("Expand") : qsTr("Collapse")
                                display: Controls.AbstractButton.IconOnly
                                // From the keyboard, the focus follows the folder
                                // into the rebuilt tree
                                onClicked: {
                                    if (disclosureButton.visualFocus)
                                        root.toggleFromKeyboard(model.fullPath, true)
                                    else
                                        root.toggleCollapsed(model.fullPath)
                                }
                            }

                            Kirigami.Icon {
                                source: "folder"
                                implicitWidth:  root.rowIconSize
                                implicitHeight: root.rowIconSize
                                Accessible.ignored: true   // every row is a folder
                            }

                            Controls.Label {
                                text: model.name
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                color: folderDelegate.highlighted ? KTheme.accentText : KTheme.text
                                font.pixelSize: KTheme.body
                                font.weight: folderDelegate.highlighted ? Font.DemiBold : Font.Normal
                                Accessible.ignored: true   // read as the row's own name
                            }
                        }

                        onClicked: {
                            root.selectedFolderPath = model.fullPath
                            folderListView.currentIndex = index
                            root.folderClicked(model.fullPath)
                        }
                        onPressAndHold: root.openFolderContextMenu(model.fullPath)
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: root.openFolderContextMenu(model.fullPath)
                        }
                    }
                }
            }
        }
    }
}
