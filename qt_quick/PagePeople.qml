import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// People of the archive (fork, D38, owner 2026-10-30): who they are, the photos
// they are in, and their faces. Faces found by the analysis stay proposals
// until the owner approves them, one by one or many at once; once a person has
// confirmed faces, similar faces are searched and attributed in single or bulk.
ColumnLayout {
    id: people
    spacing: KTheme.gap

    property var    list: []
    property string filter: ""
    property int    currentId: -1
    property var    current: ({})
    property var    personFiles: []
    property var    pending: []
    property var    similar: []
    property var    groups: []
    property bool   similarShown: false
    property bool   allPhotos: false
    // Faces ticked in a grid, by face id: the bulk actions apply to them
    property var    picked: ({})
    readonly property bool facesOn: appManager1.facesAvailable()
    // Something changed while the page was hidden: it reloads when shown
    property bool   stale: true
    readonly property int groupFacesShown: 12

    // Reading everything is heavy (the grouping compares every unnamed face
    // with the others): only while the page is shown, and once for a burst of
    // changes (an approval alone signals people, faces and information)
    function refresh() {
        if (!people.visible) {
            people.stale = true
            return
        }
        refreshTimer.restart()
    }
    function setShown(shown) {
        if (shown && people.stale)
            people.refreshNow()
    }
    function refreshNow() {
        refreshTimer.stop()
        people.stale = false
        people.list = appManager1.people(people.filter)
        if (people.currentId > 0) {
            people.current = appManager1.person(people.currentId)
            people.personFiles = appManager1.filesOfPerson(people.currentId)
            if (people.similarShown)
                people.similar = appManager1.similarFaces(people.currentId, 80, 0.30)
        }
        if (people.facesOn) {
            people.pending = appManager1.pendingFaces(120)
            people.groups  = appManager1.unnamedFaceGroups(24)
        }
    }
    Timer {
        id: refreshTimer
        interval: 150
        onTriggered: people.refreshNow()
    }
    // A face search or a dating job in the background writes for hours: the
    // page follows it, at most every 10 seconds
    Timer {
        id: jobTimer
        interval: 10000
        onTriggered: people.refresh()
    }
    function choose(personId) {
        people.currentId = personId
        people.similarShown = false
        people.allPhotos = false
        people.similar = []
        people.picked = ({})
        people.current = appManager1.person(personId)
        people.personFiles = appManager1.filesOfPerson(personId)
    }
    function pick(faceId, on) {
        let p = Object.assign({}, people.picked)
        if (on) p[faceId] = true
        else delete p[faceId]
        people.picked = p
    }
    function pickedIds(from) {
        return from.filter(f => people.picked[f.faceId]).map(f => f.faceId)
    }
    function nameFaces(ids) {
        applicationWindow().pickPerson(function(personId) {
            if (personId <= 0) return
            const error = appManager1.confirmFaces(ids, personId)
            people.picked = ({})
            people.notify(error, qsTr("Named."))
        })
    }
    function notify(error, done) {
        applicationWindow().showPassiveNotification(error !== "" ? error : done)
    }
    function count(n) { return Number(n || 0).toLocaleString(Qt.locale(), "f", 0) }

    Connections {
        target: appManager1
        function onPeopleChanged()   { people.refresh() }
        function onFacesChanged()    { people.refresh() }
        function onFileInfoChanged() { people.refresh() }
        function onMdiRevisionChanged() {
            if (!people.visible)
                people.stale = true
            else if (!jobTimer.running)
                jobTimer.start()
        }
    }

    // A face as a small tile: the crop, who it is (or is proposed to be), how
    // sure, a tick for the bulk actions
    component FaceTile: Controls.AbstractButton {
        id: tile
        property var face: ({})
        property bool tickable: true
        readonly property bool ticked: people.picked[face.faceId] === true
        implicitWidth: 112
        implicitHeight: 150
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        checkable: tickable
        checked: ticked
        onToggled: people.pick(face.faceId, checked)
        Keys.onReturnPressed: if (tickable) people.pick(face.faceId, !ticked)
        Keys.onEnterPressed:  if (tickable) people.pick(face.faceId, !ticked)
        Accessible.name: (face.personName || face.suggestedName || qsTr("Unknown person")) + " · " + (face.fileName || "")
        // Which photo the face comes from
        Controls.ToolTip.text: face.fullPath || ""
        Controls.ToolTip.visible: hovered && (face.fullPath || "") !== ""
        Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
        background: Rectangle {
            radius: KTheme.radiusItem
            color: tile.ticked ? KTheme.accentSoft : tile.hovered ? KTheme.hover : KTheme.surface2
            border.width: tile.ticked || tile.visualFocus ? 2 : 1
            border.color: tile.ticked || tile.visualFocus ? KTheme.accent : KTheme.line
        }
        contentItem: ColumnLayout {
            spacing: 4
            Image {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 96
                Layout.preferredHeight: 96
                source: tile.face.cropUrl || ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                Accessible.ignored: true
            }
            Controls.Label {
                Layout.fillWidth: true
                text: tile.face.personName || tile.face.suggestedName || qsTr("Unknown person")
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                color: KTheme.text
                font.pixelSize: KTheme.note
            }
            KInfoTag {
                Layout.alignment: Qt.AlignHCenter
                visible: (tile.face.score || 0) > 0
                text: qsTr("%1% similar").arg(Math.round((tile.face.score || 0) * 100))
                tone: tile.face.status === "confirmed" ? "ok" : "ai"
            }
        }
    }

    // What the page is for
    Controls.Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: KTheme.text2
        text: qsTr("The people of the archive: who they are and the photos they are in. Faces found by the analysis stay proposals until you approve them.")
    }
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: !people.facesOn
        type: Kirigami.MessageType.Information
        text: appManager1.facesUnavailableReason() + " " + qsTr("People can still be added to photos by hand, from File details.")
    }

    // ── Faces to approve ──────────────────────────────────────────────────
    KSectionHeader {
        visible: people.facesOn && people.pending.length > 0
        level: 2
        title: qsTr("Faces to approve")
        description: qsTr("The analysis proposes who these faces are. Only the ones approved get the name; the others stay without one.")
    }
    Flow {
        visible: people.facesOn && people.pending.length > 0
        Layout.fillWidth: true
        spacing: 8
        Repeater {
            model: people.pending
            delegate: FaceTile { required property var modelData; face: modelData }
        }
    }
    Flow {
        visible: people.facesOn && people.pending.length > 0
        Layout.fillWidth: true
        spacing: 8
        KButton {
            variant: "primary"
            icon.name: "dialog-ok-apply"
            enabled: people.pickedIds(people.pending).length > 0
            text: qsTr("Approve the selected (%1)").arg(people.pickedIds(people.pending).length)
            onClicked: {
                // Each face goes to the person proposed for it
                const chosen = people.pending.filter(f => people.picked[f.faceId])
                let byPerson = {}
                for (const f of chosen) {
                    if (!byPerson[f.suggestedPersonId]) byPerson[f.suggestedPersonId] = []
                    byPerson[f.suggestedPersonId].push(f.faceId)
                }
                let error = ""
                for (const personId in byPerson)
                    error = appManager1.confirmFaces(byPerson[personId], Number(personId)) || error
                people.picked = ({})
                people.notify(error, qsTr("Approved."))
            }
        }
        KButton {
            variant: "secondary"
            icon.name: "dialog-cancel"
            enabled: people.pickedIds(people.pending).length > 0
            text: qsTr("Not them")
            onClicked: {
                const error = appManager1.rejectFaces(people.pickedIds(people.pending))
                people.picked = ({})
                people.notify(error, qsTr("The proposals were removed; the faces stay without a name."))
            }
        }
    }

    // ── People ────────────────────────────────────────────────────────────
    KSectionHeader {
        level: 2
        title: qsTr("People")
        description: people.list.length > 0
                     ? qsTr("A person chosen shows their photos, and the faces like theirs in the other photos.")
                     : qsTr("Nobody yet. A person is added here, or from File details on a photo.")
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        Controls.TextField {
            Layout.fillWidth: true
            placeholderText: qsTr("Name, role or organisation…")
            Accessible.name: placeholderText
            onTextChanged: { people.filter = text; people.list = appManager1.people(people.filter) }
        }
        KButton {
            variant: "primary"
            icon.name: "list-add"
            text: qsTr("New person")
            onClicked: applicationWindow().editPerson(-1, function(id) { if (id > 0) people.choose(id) })
        }
    }
    GridLayout {
        Layout.fillWidth: true
        columns: Math.max(1, Math.floor(width / 280))
        columnSpacing: KTheme.gap
        rowSpacing: KTheme.gap
        Repeater {
            model: people.list
            delegate: Controls.AbstractButton {
                id: personCard
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: personRow.implicitHeight + 24
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.name
                Accessible.description: modelData.role || ""
                Accessible.selected: people.currentId === modelData.id
                onClicked: people.choose(modelData.id)
                Keys.onReturnPressed: people.choose(modelData.id)
                Keys.onEnterPressed:  people.choose(modelData.id)
                background: Rectangle {
                    radius: KTheme.radiusCard
                    color: people.currentId === personCard.modelData.id ? KTheme.accentSoft
                         : personCard.hovered ? KTheme.hover : KTheme.surface
                    border.width: people.currentId === personCard.modelData.id || personCard.visualFocus ? 2 : 1
                    border.color: people.currentId === personCard.modelData.id || personCard.visualFocus ? KTheme.accent : KTheme.line
                }
                contentItem: RowLayout {
                    id: personRow
                    spacing: 12
                    anchors.margins: 12
                    // The person's face, or their initials
                    Rectangle {
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48
                        Layout.leftMargin: 12
                        radius: 24
                        clip: true
                        color: KTheme.neutralSoft
                        Accessible.ignored: true
                        Image {
                            anchors.fill: parent
                            visible: (personCard.modelData.coverUrl || "") !== ""
                            source: personCard.modelData.coverUrl || ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                        Controls.Label {
                            anchors.centerIn: parent
                            visible: (personCard.modelData.coverUrl || "") === ""
                            text: ((personCard.modelData.firstName || "?")[0] + (personCard.modelData.lastName || "")[0] || "").toUpperCase()
                            color: KTheme.text2
                            font.weight: Font.Bold
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Controls.Label {
                            Layout.fillWidth: true
                            text: personCard.modelData.name
                            font.weight: Font.Bold
                            color: KTheme.text
                            wrapMode: Text.Wrap
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: [personCard.modelData.role, personCard.modelData.organisation].filter(x => x).join(" · ")
                            color: KTheme.text2
                            font.pixelSize: KTheme.note
                            wrapMode: Text.Wrap
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4
                            KInfoTag { text: qsTr("%1 photos").arg(people.count(personCard.modelData.fileCount)) }
                            KInfoTag {
                                visible: (personCard.modelData.pendingCount || 0) > 0
                                text: qsTr("%1 to approve").arg(people.count(personCard.modelData.pendingCount))
                                tone: "warn"
                            }
                        }
                    }
                }
            }
        }
    }

    // ── The person chosen ─────────────────────────────────────────────────
    KCard {
        visible: people.currentId > 0
        Layout.fillWidth: true
        RowLayout {
            Layout.fillWidth: true
            KSectionHeader {
                level: 2
                title: people.current.name || ""
                description: [people.current.role, people.current.organisation].filter(x => x).join(" · ")
            }
            KButton {
                variant: "secondary"
                icon.name: "document-edit"
                text: qsTr("Edit")
                Accessible.description: people.current.name || ""
                onClicked: applicationWindow().editPerson(people.currentId, function(id) { people.refresh() })
            }
        }
        Controls.Label {
            Layout.fillWidth: true
            visible: (people.current.notes || "") !== ""
            text: people.current.notes || ""
            wrapMode: Text.Wrap
            color: KTheme.text2
        }

        // The photos the person is in
        KSectionHeader {
            level: 3
            title: qsTr("Photos (%1)").arg(people.personFiles.length)
            description: people.personFiles.length > 0 ? qsTr("Selecting one shows it in Browse the archive.")
                                                       : qsTr("No photo yet: their faces, once approved, or File details on a photo, add them.")
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: people.allPhotos ? people.personFiles : people.personFiles.slice(0, 60)
                delegate: Controls.AbstractButton {
                    id: photoTile
                    required property var modelData
                    implicitWidth: 84
                    implicitHeight: 84
                    hoverEnabled: true
                    focusPolicy: Qt.TabFocus
                    Accessible.name: modelData.name
                    Controls.ToolTip.text: modelData.name
                    Controls.ToolTip.visible: hovered || visualFocus
                    onClicked: applicationWindow().showFileInBrowse(modelData.deviceId, modelData.fullPath)
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed:  clicked()
                    background: Rectangle {
                        radius: KTheme.radiusItem
                        color: KTheme.surface2
                        border.width: photoTile.visualFocus ? 2 : 1
                        border.color: photoTile.visualFocus ? KTheme.accent : KTheme.line
                    }
                    contentItem: Image {
                        source: photoTile.modelData.thumbUrl || ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                }
            }
        }
        KButton {
            visible: !people.allPhotos && people.personFiles.length > 60
            variant: "link"
            text: qsTr("Show all %1 photos").arg(people.count(people.personFiles.length))
            onClicked: people.allPhotos = true
        }

        // Their face in the other photos
        KSectionHeader {
            visible: people.facesOn
            level: 3
            title: qsTr("Similar faces")
            description: qsTr("Faces in the other photos that look like the approved faces of %1, most alike first. The ones ticked are attributed together; one face can also be attributed on its own.")
                         .arg(people.current.name || "")
        }
        KButton {
            visible: people.facesOn && !people.similarShown
            variant: "tonal"
            icon.name: "edit-find"
            text: qsTr("Look for similar faces")
            onClicked: {
                people.similarShown = true
                people.similar = appManager1.similarFaces(people.currentId, 80, 0.30)
                if (people.similar.length === 0)
                    applicationWindow().showPassiveNotification(qsTr("No similar face found. Approving more faces of this person helps."))
            }
        }
        Flow {
            visible: people.similarShown
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: people.similar
                delegate: FaceTile { required property var modelData; face: modelData }
            }
        }
        Flow {
            visible: people.similarShown && people.similar.length > 0
            Layout.fillWidth: true
            spacing: 8
            KButton {
                variant: "primary"
                icon.name: "dialog-ok-apply"
                enabled: people.pickedIds(people.similar).length > 0
                text: qsTr("Attribute to %1 (%2)").arg(people.current.name || "").arg(people.pickedIds(people.similar).length)
                onClicked: {
                    const error = appManager1.confirmFaces(people.pickedIds(people.similar), people.currentId)
                    people.picked = ({})
                    people.notify(error, qsTr("Attributed to %1.").arg(people.current.name || ""))
                }
            }
            KButton {
                variant: "secondary"
                text: qsTr("Tick all")
                onClicked: {
                    let p = Object.assign({}, people.picked)
                    for (const f of people.similar) p[f.faceId] = true
                    people.picked = p
                }
            }
            KButton {
                variant: "secondary"
                icon.name: "dialog-cancel"
                enabled: people.pickedIds(people.similar).length > 0
                text: qsTr("Not %1").arg(people.current.name || "")
                onClicked: {
                    const error = appManager1.rejectFaces(people.pickedIds(people.similar))
                    people.picked = ({})
                    people.similar = appManager1.similarFaces(people.currentId, 80, 0.30)
                    people.notify(error, qsTr("Done."))
                }
            }
        }
    }

    // ── Faces without a name, grouped ─────────────────────────────────────
    KSectionHeader {
        visible: people.facesOn && people.groups.length > 0
        level: 2
        title: qsTr("Faces without a name")
        description: qsTr("Faces that look alike are grouped. Naming a group attributes its ticked faces, or all of them when none is ticked.")
    }
    Repeater {
        model: people.facesOn ? people.groups : []
        delegate: KCard {
            id: groupCard
            required property var modelData
            Layout.fillWidth: true
            property bool expanded: false
            KSectionHeader {
                level: 3
                title: qsTr("%1 faces").arg(people.count(groupCard.modelData.size))
            }
            // A large group draws only its first faces until asked
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: groupCard.expanded ? groupCard.modelData.faces
                                              : groupCard.modelData.faces.slice(0, people.groupFacesShown)
                    delegate: FaceTile { required property var modelData; face: modelData }
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                KButton {
                    variant: "primary"
                    icon.name: "list-add"
                    text: qsTr("Name these faces")
                    onClicked: {
                        const faces = groupCard.modelData.faces
                        const ticked = faces.filter(f => people.picked[f.faceId]).map(f => f.faceId)
                        // None ticked names the whole group, faces not seen included: asked first
                        if (ticked.length > 0)
                            people.nameFaces(ticked)
                        else
                            nameGroupDialog.ask(faces.map(f => f.faceId))
                    }
                }
                KButton {
                    visible: !groupCard.expanded && groupCard.modelData.faces.length > people.groupFacesShown
                    variant: "link"
                    text: qsTr("Show all %1 faces").arg(people.count(groupCard.modelData.faces.length))
                    onClicked: groupCard.expanded = true
                }
            }
        }
    }
    Kirigami.PromptDialog {
        id: nameGroupDialog
        property var ids: []
        function ask(faceIds) { ids = faceIds; open() }
        background: Rectangle { color: KTheme.surface; radius: KTheme.radiusCard; border.width: 1; border.color: KTheme.line }
        padding: Kirigami.Units.largeSpacing * 2
        title: qsTr("Name all %n faces of the group?", "", nameGroupDialog.ids.length)
        subtitle: qsTr("No face is ticked, so the name goes to the whole group, the faces not shown included. The groups are approximate: ticking the right faces first is safer.")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Name these faces")
                icon.name: "list-add"
                onTriggered: {
                    nameGroupDialog.close()
                    people.nameFaces(nameGroupDialog.ids)
                }
            },
            Kirigami.Action {
                text: qsTr("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: nameGroupDialog.close()
            }
        ]
    }

    // ── Looking for faces on a drive ──────────────────────────────────────
    KCard {
        visible: people.facesOn
        Layout.fillWidth: true
        KSectionHeader {
            level: 3
            title: qsTr("Find the faces in a drive")
            description: qsTr("Looks for faces in the photos of a catalogued drive, on this computer, in the background. Nothing is attributed without your approval.")
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Controls.ComboBox {
                id: faceDrive
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "deviceId"
                model: (appManager1.homeOverview().drives || []).filter(d => d.isDrive)
                Accessible.name: qsTr("Drive")
            }
            KButton {
                variant: "primary"
                icon.name: "media-playback-start"
                enabled: faceDrive.currentIndex >= 0
                text: qsTr("Find the faces")
                onClicked: people.notify(appManager1.startFaceAnalysis(faceDrive.currentValue),
                                         qsTr("The faces are looked for in the background: Work in progress shows how it goes."))
            }
        }
    }

    // ── Face data (GDPR) ──────────────────────────────────────────────────
    // Recognisable faces are personal data: they can be forgotten all at once
    // Shown even when this build cannot recognise faces: data found by another
    // build must still be deletable (GDPR)
    KCard {
        Layout.fillWidth: true
        KSectionHeader {
            level: 3
            title: qsTr("Face data")
            description: qsTr("The faces found, their crops and the names given through them stay on this computer. Deleting them forgets every face of this collection; people and the names added by hand stay.")
        }
        KButton {
            variant: "secondary"
            icon.name: "edit-delete"
            text: qsTr("Delete all face data")
            onClicked: clearFacesDialog.open()
        }
    }
    Kirigami.PromptDialog {
        id: clearFacesDialog
        background: Rectangle { color: KTheme.surface; radius: KTheme.radiusCard; border.width: 1; border.color: KTheme.line }
        padding: Kirigami.Units.largeSpacing * 2
        title: qsTr("Delete all face data?")
        subtitle: qsTr("Every face found in this collection, its crop and the names given through faces are deleted. People, the names added by hand and the photos stay. A new face search can be run later.")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    const error = appManager1.clearFaceData()
                    clearFacesDialog.close()
                    people.notify(error, qsTr("Face data deleted."))
                }
            },
            Kirigami.Action {
                text: qsTr("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: clearFacesDialog.close()
            }
        ]
    }
}
