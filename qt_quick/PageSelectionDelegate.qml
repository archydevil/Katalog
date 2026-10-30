import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import Qt.labs.platform

// A drive card of "My drives" (fork, D34, phase R7), in the Home drive-card
// language: the icon tile, the name, then info tags for the kind of device,
// whether it is connected and what it holds. The selected card takes the
// accent fill and border and shows a check mark, so the selection is never
// told by colour alone. Selection, tree and menu behaviour are unchanged
// (SpecSelection.md).
Kirigami.AbstractCard {
    id: card

    // Guarded: a card being released by its list briefly has no parent.
    // The focus ring's width is kept free on both sides: the list clips.
    anchors.left: parent ? parent.left : undefined
    anchors.leftMargin: model.level * Kirigami.Units.gridUnit + KTheme.focusOffset
    anchors.right: parent ? parent.right : undefined
    anchors.rightMargin: KTheme.focusOffset

    topPadding:    10
    bottomPadding: 10
    leftPadding:   12
    rightPadding:  6
    hoverEnabled:  true

    property bool isSelected: appManager1.selectedDeviceId === model.deviceId

    // The same two-state rule the Devices cards follow, on the same setting
    // shared with K2 (THM-F7 / SEL-F7). One preference drives both pages: a
    // third size here would defeat that and put the two cards out of step
    // (DVP-C20).
    readonly property real deviceIconSize: appManager1.biggerIconSize
                                           ? Kirigami.Units.iconSizes.medium
                                           : Kirigami.Units.iconSizes.smallMedium

    // The counts line of the model ("1,234 files · 12 GB · 500 GB"), one tag
    // per part.
    readonly property var infoParts: (model.description || "").split(/\s*[·\n]\s*/)
                                         .filter(function(s) { return s.length > 0 })

    // Kind and state, shown as tags and read with the name
    readonly property string typeText: model.type === "Virtual" ? qsTr("Virtual")
                                     : model.type === "Storage" ? qsTr("Storage")
                                     : qsTr("Catalog")
    readonly property bool showsState: model.type === "Storage" || model.type === "Catalog"
    readonly property string stateText: model.isActive ? qsTr("Connected") : qsTr("Not connected")

    // Read as an entry of a list that says whether it is the chosen one, with
    // what its tags say (WCAG 4.1.2)
    Accessible.role: Accessible.ListItem
    Accessible.name: model.name
    Accessible.description: [card.typeText].concat(card.showsState ? [card.stateText] : [],
                                                    appManager1.showDeviceInfo ? card.infoParts : []).join(", ")
    Accessible.selectable: true
    Accessible.selected: card.isSelected

    // From the keyboard (WCAG 2.1.1): Tab reaches the cards, Up and Down move
    // between them, Return, Enter or Space selects, the Menu key or Shift+F10
    // opens the menu. A click does not take the focus.
    focusPolicy: Qt.TabFocus
    Keys.onReturnPressed: card.clicked()
    Keys.onEnterPressed: card.clicked()
    Keys.onUpPressed: card.focusCard(index - 1)
    Keys.onDownPressed: card.focusCard(index + 1)
    Keys.onMenuPressed: card.openMenuFromKeyboard()
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) {
            card.openMenuFromKeyboard()
            event.accepted = true
        }
    }
    // A card reached from the keyboard is scrolled fully into view
    onActiveFocusChanged: if (activeFocus && ListView.view) ListView.view.positionViewAtIndex(index, ListView.Contain)

    function focusCard(i) {
        const view = ListView.view
        if (!view || i < 0 || i >= view.count)
            return
        view.positionViewAtIndex(i, ListView.Contain)
        const next = view.itemAtIndex(i)
        if (next)
            next.forceActiveFocus(Qt.TabFocusReason)
    }
    function openMenuFromKeyboard() {
        contextMenu.popup(card, card.leftPadding, card.height / 2)
    }

    background: Rectangle {
        radius: KTheme.radiusCard
        color: card.isSelected ? KTheme.accentSoft
             : card.hovered    ? KTheme.hover : KTheme.surface
        border.color: card.isSelected ? KTheme.accent : KTheme.line
        border.width: card.isSelected ? 2 : 1

        // Keyboard focus: a ring outside the card, apart from the selected
        // card's own accent border (WCAG 2.4.7)
        Rectangle {
            anchors.fill: parent
            anchors.margins: -KTheme.focusOffset
            radius: parent.radius + KTheme.focusOffset
            color: "transparent"
            border.width: KTheme.focusWidth
            border.color: KTheme.accent
            visible: card.visualFocus
        }
    }

    // Selecting is the card's click, so the keyboard selects too (Return,
    // Enter, Space)
    onClicked: {
        appManager1.selectDeviceById(model.deviceId)
        // With Explore open, a catalog card opens there too (EXP-F27)
        if (model.type === "Catalog")
            root.followSelectionInExplore(model.deviceId)
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: contextMenu.popup()
    }

    // A long press opens the menu and, being handled, is not also a click
    onPressAndHold: contextMenu.popup()

    Controls.Menu {
        id: contextMenu

        Controls.MenuItem {
            text: model.name
            enabled: false
            font.bold: true
        }
        Controls.MenuSeparator {}

        // Quick jump to the Search page without opening the drawer. Always
        // shown (SEL-F3): when Search is already open, showPage() just moves to
        // it and keeps Results.
        Controls.MenuItem {
            text:      qsTr("Search")
            icon.name: "edit-find"
            // Select first, so the highlighted card and the action agree
            // (SpecSelection.md SEL-F3). Update, Open folder and Edit below
            // deliberately do NOT select (SEL-F4).
            onTriggered: {
                appManager1.selectDeviceById(model.deviceId)
                appManager1.setLastPage("Search")
                root.showPage(pageSearch)
            }
        }

        Controls.MenuItem {
            text:      qsTr("Update")
            icon.name: "media-playlist-repeat"
            // Match K2 (mainwindow_tab_filters.cpp): Update is available for Storage and
            // Virtual devices unconditionally, and for Catalogs only when active. The C++
            // updateDevice() → updateDeviceHierarchy() path already cascades sub-catalogs.
            visible:   model.type === "Storage" || model.type === "Virtual"
                       || (model.type === "Catalog" && model.isActive)
            height:    visible ? implicitHeight : 0
            // Always enabled: while an operation runs the request is queued
            // rather than dropped (SpecOperationQueue.md).
            onTriggered: appManager1.updateDevice(model.deviceId)
        }

        Controls.MenuItem {
            text: qsTr("Explore")
            icon.name: "view-list-tree"
            visible: model.type === "Catalog"
            height: visible ? implicitHeight : 0
            // Selects first, as Search does (SEL-F3).
            onTriggered: {
                appManager1.selectDeviceById(model.deviceId)
                appManager1.setLastPage("Explore")
                exploreFolders.openByDeviceId(model.deviceId)
                root.showPage(pageExplore)
            }
        }

        Controls.MenuItem {
            text: qsTr("Open folder")
            icon.name: "document-open"
            onTriggered: appManager1.openDeviceFolder(model.deviceId)
        }

        Controls.MenuItem {
            text: qsTr("Edit")
            icon.name: "document-edit"
            onTriggered: {
                pageDeviceEdit.fromDevicesPage = false
                pageDeviceEdit_form.deviceId = model.deviceId
                pageDeviceEdit_form.loadDevice()
                root.showPage(pageDeviceEdit)
            }
        }
    }

    contentItem: Item {
        implicitWidth: deviceDelegateLayout.implicitWidth
        implicitHeight: deviceDelegateLayout.implicitHeight

        RowLayout {
            id: deviceDelegateLayout
            anchors {
                left: parent.left
                top: parent.top
                right: parent.right
            }
            spacing: 6

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    // Icon and name come from the shared component, so this card and
                    // the selected-device reminder above the list cannot drift apart
                    // (SpecSelection.md SEL-C1).
                    DeviceIdentity {
                        id: cardIdentity
                        Layout.fillWidth: true
                        deviceType:     model.type
                        deviceName:     model.name
                        deviceIsActive: model.isActive
                        iconSize:       card.deviceIconSize
                        iconTile:       true
                        // A card never hides text, it wraps it (CDT-C1, MDI-C38)
                        nameWraps:      true
                        nameColor:      KTheme.text
                    }

                    // The selection is shown by a mark as well as by the accent
                    Kirigami.Icon {
                        visible: card.isSelected
                        source: "checkmark"
                        isMask: true
                        color: KTheme.accentText
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        Layout.alignment: Qt.AlignVCenter
                        Accessible.ignored: true   // the card reports itself selected
                    }
                }

                // Kind, state and contents, lined up with the name rather than
                // with the icon tile (SEL-C8).
                Flow {
                    Layout.fillWidth: true
                    Layout.leftMargin: cardIdentity.tileSize + cardIdentity.spacing
                    spacing: 6

                    KInfoTag {
                        text: card.typeText
                        tone: model.type === "Catalog" ? "info" : "neutral"
                    }
                    KInfoTag {
                        visible: card.showsState
                        text: card.stateText
                        tone: model.isActive ? "ok" : "warn"
                    }
                    Repeater {
                        model: appManager1.showDeviceInfo ? card.infoParts : []
                        delegate: KInfoTag {
                            required property string modelData
                            text: modelData
                        }
                    }
                }
            }

            // One button, always in the layout, rather than two that come and
            // go. A Catalog can never hold children, so its slot was simply
            // absent and its card came out shorter than a Storage or Virtual
            // one; reserving the space — as the Explore folder tree does —
            // makes every card the same width. No tooltip: the hidden button
            // still reports hover, so it showed on Catalogs too.
            Controls.ToolButton {
                Layout.alignment: Qt.AlignTop
                icon.name: model.isCollapsed ? "go-down" : "go-up"
                icon.width:  Kirigami.Units.iconSizes.small
                icon.height: Kirigami.Units.iconSizes.small
                implicitWidth:  40
                implicitHeight: 40
                padding: 0
                opacity: model.hasChildren ? 1.0 : 0.0
                enabled: model.hasChildren
                // Named for accessibility only: a text would bring a tooltip
                // with some styles (ICB-C3)
                Accessible.name: model.isCollapsed ? qsTr("Expand") : qsTr("Collapse")
                onClicked: {
                    if (model.isCollapsed)
                        appManager1.expandDevice(model.deviceId)
                    else
                        appManager1.collapseDevice(model.deviceId)
                }
            }
        }
    }
}
