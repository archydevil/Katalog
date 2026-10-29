import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.AbstractCard {
    id: card

    required property var modelData
    required property int index

    property real delegateCardScale: 1.0

    // Which of the three views is showing, and this row's place in the tree.
    // The collapse state is the page's, held per page and never shared with the
    // Selection page (DVP-C18).
    property string viewFilter: "All"
    readonly property bool cardHasChildren: modelData._hasChildren === true
    readonly property bool cardIsCollapsed: modelData._isCollapsed === true
    signal collapseToggleRequested(int deviceId)

    signal editRequested(int deviceId)
    signal exploreRequested(int deviceId)
    signal splitSubDirRequested(int deviceId, string deviceName)
    signal splitFileTypeRequested(int deviceId, string deviceName, bool deviceActive)
    signal verifyRequested(int deviceId, string deviceName)
    signal verifyMimeRequested(int deviceId)
    signal unassignRequested(int deviceId, int parentId, string deviceName)
    signal deleteRequested(int deviceId, string deviceName, string deviceType)
    signal addVirtualChildRequested(int parentId)
    signal addStorageChildRequested(int parentId)
    signal assignCatalogRequested(int virtualDeviceId)
    signal filelightRequested(int deviceId)

    readonly property int    devId:        modelData.deviceId
    readonly property int    devParent:    modelData.parentId
    readonly property string devName:      modelData.name
    readonly property string devType:      modelData.type
    readonly property string devPath:      modelData.path
    readonly property string devParentType: modelData.parentType ?? ""
    readonly property int    devLevel:     modelData.level
    readonly property bool   devActive:    modelData.active
    readonly property int    devGroupId:   modelData.groupId ?? 0

    // Unassign: only Catalog and Storage devices assigned to a Virtual Group (groupId != 0).
    // Virtual devices are never unassigned - they can only be deleted.
    readonly property bool canUnassign: devType !== "Virtual" && devGroupId !== 0 && devPath !== "EXPORT"

    // The icon size the user chose, shared with K2 (THM-F7). One level of the
    // tree's indent is exactly that size (DVP-F22), so the indent grows and
    // shrinks with the icons.
    readonly property real deviceIconSize: appManager1.biggerIconSize
                                           ? Kirigami.Units.iconSizes.medium
                                           : Kirigami.Units.iconSizes.smallMedium

    // Only the Device tree indents. The Storage and Catalogs lists never show a
    // card's parent, so an indent there marks a hierarchy the user cannot see
    // (DVP-F21).
    readonly property bool showsHierarchy: viewFilter === "All"

    anchors.left:       parent ? parent.left  : undefined
    anchors.leftMargin: showsHierarchy ? devLevel * deviceIconSize : 0
    anchors.right:      parent ? parent.right : undefined

    // Matched to the "My drives" card (DVP-F19), in the Home drive-card
    // language of the redesign (fork, D34, phase R7).
    topPadding:    12
    bottomPadding: 12
    leftPadding:   14
    rightPadding:  6

    // A catalog of saved search results has no drive behind it (path "EXPORT")
    readonly property bool isSavedResults: devType === "Catalog" && devPath === "EXPORT"

    background: Rectangle {
        radius: KTheme.radiusCard
        color: KTheme.surface
        border.color: KTheme.line
        border.width: 1
    }

    function typeLabel(type) {
        return type === "Virtual" ? qsTr("Virtual")
             : type === "Storage" ? qsTr("Storage")
             : qsTr("Catalog")
    }

    // Lets the Devices page open this same menu for a table row, so the table
    // and the cards can never offer different actions (SpecDevicesPage DVP-F6).
    // anchorItem null: opens at the pointer; otherwise under that item (the
    // table row, when opened with the Menu key - CDT-F8).
    function openContextMenu(anchorItem) {
        if (anchorItem)
            contextMenu.popup(anchorItem, 0, anchorItem.height)
        else
            contextMenu.popup()
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: contextMenu.popup()
    }
    TapHandler {
        onLongPressed: contextMenu.popup()
    }

    Controls.Menu {
        id: contextMenu

        Controls.MenuItem {
            text: card.devName
            enabled: false
            font.bold: true
        }

        // K2 has NO separator between the header and the first action items.
        // Update: Catalog only if active; Storage and Virtual always
        Controls.MenuItem {
            text: qsTr("Update")
            icon.name: "media-playlist-repeat"
            visible: (card.devType === "Catalog" && card.devActive)
                     || card.devType === "Storage"
                     || card.devType === "Virtual"
            // Always enabled: while an operation runs the request is queued
            // rather than dropped (SpecOperationQueue.md).
            onTriggered: appManager1.updateDevice(card.devId)
        }
        Controls.MenuItem {
            text: qsTr("Explore")
            icon.name: "view-list-tree"
            visible: card.devType === "Catalog"
            onTriggered: card.exploreRequested(card.devId)
        }
        Controls.MenuItem {
            text: qsTr("Edit")
            icon.name: "document-edit-sign"
            onTriggered: card.editRequested(card.devId)
        }
        Controls.MenuItem {
            text: qsTr("Open folder")
            icon.name: "document-open-folder"
            visible: card.devPath.length > 0 && card.devPath !== "EXPORT"
            onTriggered: appManager1.openDeviceFolder(card.devId)
        }

        // Storage: Filelight with NO separator before it (K2 structure)
        Controls.MenuItem {
            text: qsTr("Filelight")
            icon.name: "view-statistics"
            visible: card.devType === "Storage" && card.devActive
            onTriggered: card.filelightRequested(card.devId)
        }

        // Catalog section: separator, Verify, separator+Filelight only when active, separator, Splits
        Controls.MenuSeparator { visible: card.devType === "Catalog" }
        Controls.MenuItem {
            text: qsTr("Verify Checksums")
            icon.name: "document-properties"
            visible: card.devType === "Catalog"
            onTriggered: card.verifyRequested(card.devId, card.devName)
        }
        // SpecMimeVerification MIV-F1 / MIV-F2
        Controls.MenuItem {
            text: qsTr("Verify MIME types")
            icon.name: "document-edit-verify"
            visible: card.devType === "Catalog"
            enabled: card.devActive && !appManager1.deviceUpdateIsRunning
                     && !appManager1.catalogIsCreating && !appManager1.searchIsRunning
                     && !appManager1.exploreLoadIsRunning   // OPQ-F20
            onTriggered: card.verifyMimeRequested(card.devId)
        }
        Controls.MenuSeparator { visible: card.devType === "Catalog" && card.devActive }
        Controls.MenuItem {
            text: qsTr("Filelight")
            icon.name: "view-statistics"
            visible: card.devType === "Catalog" && card.devActive
            onTriggered: card.filelightRequested(card.devId)
        }
        Controls.MenuSeparator { visible: card.devType === "Catalog" }
        Controls.MenuItem {
            text: qsTr("Split catalog by sub-directory")
            icon.name: "edit-cut"
            visible: card.devType === "Catalog"
            onTriggered: card.splitSubDirRequested(card.devId, card.devName)
        }
        Controls.MenuItem {
            text: qsTr("Split catalog by file type")
            icon.name: "edit-cut"
            visible: card.devType === "Catalog"
            onTriggered: card.splitFileTypeRequested(card.devId, card.devName, card.devActive)
        }

        // Virtual section: separator, then virtual device actions
        Controls.MenuSeparator { visible: card.devType === "Virtual" }
        Controls.MenuItem {
            text: qsTr("Add Virtual device")
            icon.name: "document-new"
            visible: card.devType === "Virtual"
            onTriggered: card.addVirtualChildRequested(card.devId)
        }
        Controls.MenuItem {
            text: qsTr("Add Storage device")
            icon.name: "document-new"
            visible: card.devType === "Virtual" && card.devGroupId === 0
            onTriggered: card.addStorageChildRequested(card.devId)
        }
        Controls.MenuItem {
            text: qsTr("Assign selected catalog")
            icon.name: "document-new"
            visible: card.devType === "Virtual" && card.devGroupId !== 0
            enabled: appManager1.selectedDeviceType === "Catalog"
            onTriggered: card.assignCatalogRequested(card.devId)
        }

        // Separator before Unassign/Delete (always visible after all type-specific items)
        Controls.MenuSeparator {}
        Controls.MenuItem {
            text: card.devType === "Storage" ? qsTr("Unassign this storage") : qsTr("Unassign this catalog")
            icon.name: "edit-cut"
            visible: card.canUnassign
            onTriggered: card.unassignRequested(card.devId, card.devParent, card.devName)
        }
        Controls.MenuItem {
            text: card.devType === "Catalog" ? qsTr("Delete this catalog")
                : card.devType === "Storage" ? qsTr("Delete this storage")
                : qsTr("Delete")
            icon.name: "edit-delete"
            visible: !card.canUnassign
            onTriggered: card.deleteRequested(card.devId, card.devName, card.devType)
        }
    }

    contentItem: Item {
        implicitWidth:  deviceDelegateLayout.implicitWidth
        implicitHeight: deviceDelegateLayout.implicitHeight

        RowLayout {
            id: deviceDelegateLayout
            anchors {
                left:  parent.left
                top:   parent.top
                right: parent.right
            }
            spacing: 6

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    // The Selection page's own name rules - per-type weight,
                    // italic and opacity - taken from the shared component
                    // rather than copied, so the two pages cannot drift
                    // (DVP-F17 / DVP-C14).
                    DeviceIdentity {
                        id: cardIdentity
                        Layout.fillWidth: true
                        // Icon as well as name, from the one component: a second
                        // icon rule in this file is what DVP-C20 removes.
                        showIcon:       true
                        iconTile:       true
                        iconSize:       card.deviceIconSize
                        // Cards wrap rather than hide (CDT-F1).
                        nameWraps:      true
                        deviceType:     card.devType
                        deviceName:     card.devName
                        deviceIsActive: card.devActive
                    }

                    // The user's own note, beside the name when there is one.
                    // Sized like the detail line below the name rather than like
                    // the heading, so it annotates the name without competing
                    // with it (SpecDeviceComment.md).
                    Controls.Label {
                        Layout.maximumWidth: parent.width * 0.5
                        Layout.alignment: Qt.AlignVCenter
                        visible: text.length > 0
                        text: card.modelData.comment !== undefined
                              ? card.modelData.comment : ""
                        // The half-width cap stays - it is what keeps the note
                        // from crowding the name - and the text wraps inside it
                        // instead of being cut off (CDT-F1 / DVP-C17).
                        wrapMode: Text.Wrap
                        color: KTheme.text2
                        font.pixelSize: KTheme.note
                    }
                }

                // Everything below lines up with the name, past the icon tile
                // (DVP-F26).
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: cardIdentity.tileSize + cardIdentity.spacing
                    spacing: 8

                    // Where the device is, in the Home card's path style. Wraps
                    // anywhere rather than eliding (CDT-C1).
                    Controls.Label {
                        Layout.fillWidth: true
                        visible: card.devPath.length > 0 && !card.isSavedResults
                        text: card.devPath
                        wrapMode: Text.WrapAnywhere
                        font.family: KTheme.mono
                        font.pixelSize: KTheme.note
                        color: KTheme.text3
                    }

                    // Kind, state, contents and space as info tags. A catalog
                    // states its figures even when both are zero: an empty
                    // catalog is a fact worth reading (DVP-F10), hence
                    // formatDataSizeDelta(), which renders zero (DVP-C9). A
                    // device that reports space states used, free and total, in
                    // that order (DVP-F16), only when there is space to report
                    // (DVP-C13).
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6

                        KInfoTag {
                            text: card.typeLabel(card.devType)
                            tone: card.devType === "Catalog" ? "info" : "neutral"
                        }
                        KInfoTag {
                            visible: (card.devType === "Storage" || card.devType === "Catalog")
                                     && !card.isSavedResults
                            text: card.devActive ? qsTr("Connected") : qsTr("Not connected")
                            tone: card.devActive ? "ok" : "warn"
                        }
                        KInfoTag {
                            visible: card.isSavedResults
                            text: qsTr("Saved search results")
                            tone: "info"
                        }
                        // File system names are technical notation
                        KInfoTag {
                            visible: card.devType === "Storage" && text.length > 0
                            text: card.modelData.storageFileSystem || ""
                        }
                        KInfoTag {
                            visible: card.devType === "Catalog" || card.modelData.fileCount > 0
                            text: Number(card.modelData.fileCount || 0).toLocaleString(Qt.locale(), "f", 0)
                                  + " " + qsTr("files")
                        }
                        KInfoTag {
                            visible: card.devType === "Catalog" || card.modelData.fileCount > 0
                            text: appManager1.formatDataSizeDelta(card.modelData.totalFileSize || 0)
                        }
                        KInfoTag {
                            visible: (card.devType === "Storage" || card.devType === "Virtual")
                                     && card.modelData.totalSpace > 0
                            text: qsTr("used") + ": " + appManager1.formatDataSizeDelta(card.modelData.usedSpace || 0)
                        }
                        KInfoTag {
                            visible: (card.devType === "Storage" || card.devType === "Virtual")
                                     && card.modelData.totalSpace > 0
                            text: qsTr("free") + ": " + appManager1.formatDataSizeDelta(card.modelData.freeSpace || 0)
                        }
                        KInfoTag {
                            visible: (card.devType === "Storage" || card.devType === "Virtual")
                                     && card.modelData.totalSpace > 0
                            text: qsTr("total") + ": " + appManager1.formatDataSizeDelta(card.modelData.totalSpace || 0)
                        }
                        KInfoTag {
                            visible: text.length > 0
                            text: card.modelData.dateUpdated || ""
                        }
                    }
                }
            }

            // The Selection card's own expand/collapse control, same icons
            // (DVP-F24). It keeps its place in the layout when the device has no
            // children, so every card in the view lines up. Only the Device tree
            // shows a hierarchy, so only it carries the control.
            Controls.ToolButton {
                Layout.alignment: Qt.AlignTop
                visible: card.showsHierarchy
                icon.name: card.cardIsCollapsed ? "go-down" : "go-up"
                icon.width:  Kirigami.Units.iconSizes.small
                icon.height: Kirigami.Units.iconSizes.small
                implicitWidth:  40
                implicitHeight: 40
                padding: 0
                opacity: card.cardHasChildren ? 1.0 : 0.0
                enabled: card.cardHasChildren
                onClicked: card.collapseToggleRequested(card.devId)
                // The text stays for accessibility, but no tooltip: per-item
                // chevrons are the exception to ICB-F1 (SpecIconButtons.md ICB-C3).
                text: card.cardIsCollapsed ? qsTr("Expand") : qsTr("Collapse")
                display: Controls.AbstractButton.IconOnly
            }

            IconButton {
                Layout.alignment: Qt.AlignTop
                flat: true
                implicitWidth:  40
                implicitHeight: 40
                icon.name: "application-menu"
                onClicked: contextMenu.popup()
                Controls.ToolTip.text: qsTr("Actions")
                Controls.ToolTip.visible: hovered
            }
        }
    }
}
