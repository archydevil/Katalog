import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import QtQuick.Dialogs as Dialogs

// Home page (fork, D34): the archive at a glance — its drives as cards with
// their state and contents, the work in progress, the archive's knowledge.
// Navigation goes through openPage(name), handled by Main.qml.
ColumnLayout {
    id: home
    spacing: 28

    signal openPage(string name)
    // A drive card's "Re-interpret with the context": Archive knowledge opens
    // on that catalog, not on the first one of its list
    signal reinterpretDrive(int deviceId)
    // "To review": Browse on that catalog's folder, with its proposals (MDI-F184)
    signal reviewProposals(int deviceId, string folder)
    property var ov: ({})
    readonly property var job: appManager1.mdiJobInfo

    // What left this computer in this session: the AI services that are not
    // local, and the web searches sent (counted by Main.qml)
    readonly property var egress: job.egress || []
    readonly property int webSearches: applicationWindow().webSearchesSent
    property bool webAllowed: false
    // Copy of the archive data: see AppManager::archiveCopyState
    property var copy: ({})

    function refresh() {
        ov = appManager1.homeOverview()
        webAllowed = appManager1.webSearchEnabled()
        copy = appManager1.archiveCopyState()
    }
    // Called by Main.qml when the page is shown
    function setShown(shown) { if (shown) refresh() }
    function count(n) { return Number(n || 0).toLocaleString(Qt.locale(), "f", 0) }

    Connections {
        target: appManager1
        function onMdiStateChanged() { if (home.visible) home.refresh() }
        function onKnowledgeStateChanged() { if (home.visible) home.refresh() }
        function onRecentCollectionsChanged() { if (home.visible) home.refresh() }
        function onHomeOverviewChanged() { if (home.visible) home.refresh() }
    }

    // ── Header ─────────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        KSectionHeader {
            level: 1
            title: qsTr("Your archive")
            description: home.ov.organisation
                         ? home.ov.organisation + (home.ov.period ? " · " + home.ov.period : "")
                         : appManager1.currentCollectionDisplayName
        }
        KButton {
            variant: "secondary"
            icon.name: "edit-find"
            text: qsTr("Search all the drives")
            // What it says: every drive, whatever was chosen before (bug hunt #9)
            onClicked: {
                appManager1.selectDeviceById(0)
                home.openPage("Search")
            }
        }
        KButton {
            variant: "primary"
            icon.name: "list-add"
            text: qsTr("Add a drive")
            onClicked: home.openPage("Create")
        }
    }

    // A reminder to save a copy of the archive data (fork, 2026-10-30): none
    // saved yet, or the last one more than 30 days old
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: home.copy.due === true && !appManager1.archiveCopyRunning
                 && (home.ov.drives || []).length > 0
        type: Kirigami.MessageType.Information
        text: home.copy.lastCopy
              ? qsTr("The last copy of the archive data is more than 30 days old (%1). Titles, events, people and approved faces live only in this collection.").arg(home.copy.lastCopyText)
              : qsTr("No copy of the archive data has been saved yet. Titles, events, people and approved faces live only in this collection.")
        actions: [
            Kirigami.Action {
                text: qsTr("Save a copy…")
                icon.name: "document-save-as"
                onTriggered: home.saveCopy()
            }
        ]
    }

    // ── Key numbers ────────────────────────────────────────────────────────
    GridLayout {
        Layout.fillWidth: true
        columns: Math.max(1, Math.min(4, Math.floor(width / 220)))
        columnSpacing: KTheme.gap
        rowSpacing: KTheme.gap
        KMetricCard {
            Layout.fillWidth: true
            label: qsTr("Catalogued files")
            value: home.count(home.ov.totalFiles)
            tagText: qsTr("Drives: %1").arg((home.ov.drives || []).length)
        }
        KMetricCard {
            Layout.fillWidth: true
            label: qsTr("Files with AI results")
            value: home.count(home.ov.aiFiles)
            tagText: qsTr("Previews, descriptions, transcripts")
            tagTone: "ai"
        }
        KMetricCard {
            Layout.fillWidth: true
            label: qsTr("Reference documents")
            value: home.count(home.ov.documents)
            tagText: qsTr("%1 passages").arg(home.count(home.ov.passages))
            tagTone: "ok"
        }
        KMetricCard {
            Layout.fillWidth: true
            label: qsTr("Data sent")
            value: home.egress.length > 0 ? home.egress.join(", ")
                   : home.webSearches > 0 ? qsTr("Web searches: %1").arg(home.webSearches)
                   : qsTr("Nothing sent")
            // Never "On this computer" once something has left it
            tagText: home.egress.length > 0 && home.webSearches > 0 ? qsTr("Web searches: %1").arg(home.webSearches)
                     : home.egress.length > 0 || home.webSearches > 0 ? qsTranslate("PageActivity", "Sent outside this computer")
                     : home.webAllowed ? qsTranslate("Main", "Web searches allowed for this collection")
                     : qsTr("On this computer")
            tagTone: home.egress.length > 0 || home.webSearches > 0 ? "warn" : home.webAllowed ? "info" : "ok"
        }
    }

    // ── To review (fork, MDI-F184) ─────────────────────────────────────────
    // What waits for the owner, each item opening the place where that work
    // is done. Read when Home is shown, from indexed counts (no file list,
    // MDI-C38); hidden when nothing waits, and in a Hosted collection. The
    // documents not studied yet are not counted: no cheap read gives them.
    KCard {
        id: reviewCard
        property var counts: ({})
        readonly property int faces: counts.faces || 0
        readonly property int proposals: counts.proposals || 0
        function reload() { reviewCard.counts = appManager1.reviewCounts() }
        visible: faces > 0 || proposals > 0
        Layout.fillWidth: true

        Component.onCompleted: if (home.visible) reviewCard.reload()
        Connections {
            target: home
            function onVisibleChanged() { if (home.visible) reviewCard.reload() }
        }
        // While Home is shown: an approval, a job's new faces or proposals,
        // another collection — read again at most every 10 seconds
        Connections {
            target: appManager1
            function onFacesChanged()              { reviewCard.reloadSoon() }
            function onProposalsChanged()          { reviewCard.reloadSoon() }
            function onMdiRevisionChanged()        { reviewCard.reloadSoon() }
            function onRecentCollectionsChanged()  { if (home.visible) reviewCard.reload() }
        }
        function reloadSoon() { if (home.visible && !reviewTimer.running) reviewTimer.start() }
        Timer {
            id: reviewTimer
            interval: 10000
            onTriggered: if (home.visible) reviewCard.reload()
        }

        KSectionHeader {
            level: 3
            title: qsTr("To review")
            description: qsTr("What waits for your approval.")
        }
        RowLayout {
            visible: reviewCard.faces > 0
            Layout.fillWidth: true
            spacing: 10
            Controls.Label {
                Layout.fillWidth: true
                text: qsTranslate("PagePeople", "Faces to approve")
                color: KTheme.text
                wrapMode: Text.WordWrap
            }
            KInfoTag { text: home.count(reviewCard.faces); tone: "ai" }
            KButton {
                variant: "link"
                text: qsTranslate("Main", "People")
                Accessible.description: qsTranslate("PagePeople", "Faces to approve") + " · " + home.count(reviewCard.faces)
                onClicked: home.openPage("People")
            }
        }
        RowLayout {
            visible: reviewCard.proposals > 0
            Layout.fillWidth: true
            spacing: 10
            Controls.Label {
                Layout.fillWidth: true
                text: qsTr("Proposals to approve")
                color: KTheme.text
                wrapMode: Text.WordWrap
            }
            KInfoTag { text: home.count(reviewCard.proposals); tone: "ai" }
            KButton {
                variant: "link"
                text: qsTr("Browse")
                Accessible.description: qsTr("Proposals to approve") + " · " + home.count(reviewCard.proposals)
                enabled: (reviewCard.counts.proposalsDeviceId || 0) > 0
                onClicked: home.reviewProposals(reviewCard.counts.proposalsDeviceId, reviewCard.counts.proposalsFolder || "")
            }
        }
    }

    // ── My drives ──────────────────────────────────────────────────────────
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            KSectionHeader {
                title: qsTr("My drives")
                description: qsTr("Each drive is a card: whether it is connected, what it holds and what the AI has understood.")
            }
            KButton {
                variant: "link"
                text: qsTr("All the drives")
                onClicked: home.openPage("Selection")
            }
        }
        GridLayout {
            id: driveGrid
            Layout.fillWidth: true
            columns: Math.max(1, Math.floor(width / 320))
            columnSpacing: KTheme.gap
            rowSpacing: KTheme.gap

            Repeater {
                model: home.ov.drives || []
                delegate: KCard {
                    id: driveCard
                    required property var modelData
                    readonly property var types: modelData.types || {}
                    readonly property int total: Math.max(1, (types.image || 0) + (types.video || 0) + (types.audio || 0) + (types.text || 0) + (types.other || 0))
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop

                    RowLayout {
                        spacing: 12
                        Layout.fillWidth: true
                        Rectangle {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            Layout.alignment: Qt.AlignTop
                            radius: 12
                            color: driveCard.modelData.connected ? KTheme.accentSoft : KTheme.neutralSoft
                            Kirigami.Icon {
                                anchors.centerIn: parent
                                width: 22; height: 22
                                // A coloured device icon: drawn as it is, faded when offline
                                source: "drive-harddisk"
                                opacity: driveCard.modelData.connected ? 1 : 0.5
                                Accessible.ignored: true   // decoration; the tags say the state
                            }
                        }
                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true
                            Controls.Label {
                                Layout.fillWidth: true
                                text: driveCard.modelData.name
                                font.weight: Font.Bold
                                font.pixelSize: KTheme.titleCard
                                color: KTheme.text
                                // Never cut (CDT-C1): split catalogs differ only at the end
                                wrapMode: Text.Wrap
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                text: driveCard.modelData.path
                                font.family: KTheme.mono
                                font.pixelSize: KTheme.note
                                color: KTheme.text3
                                elide: Text.ElideMiddle
                            }
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        KInfoTag {
                            visible: driveCard.modelData.isDrive
                            text: driveCard.modelData.connected ? qsTr("Connected") : qsTr("Not connected")
                            tone: driveCard.modelData.connected ? "ok" : "warn"
                        }
                        KInfoTag {
                            visible: !driveCard.modelData.isDrive
                            text: qsTr("Saved search results")
                            tone: "info"
                        }
                        KInfoTag { text: qsTr("%1 files").arg(home.count(driveCard.modelData.files)) }
                        KInfoTag { text: driveCard.modelData.size }
                        KInfoTag {
                            visible: driveCard.modelData.aiFiles > 0
                            text: qsTr("AI: %1 files").arg(home.count(driveCard.modelData.aiFiles))
                            tone: "ai"
                        }
                    }
                    // What the drive holds, by type (written labels, not colour alone)
                    Row {
                        Layout.fillWidth: true
                        height: 8
                        clip: true
                        Repeater {
                            model: [
                                { n: driveCard.types.image || 0, c: KTheme.typeImage },
                                { n: driveCard.types.text  || 0, c: KTheme.typeText },
                                { n: driveCard.types.audio || 0, c: KTheme.typeAudio },
                                { n: driveCard.types.video || 0, c: KTheme.typeVideo },
                                { n: driveCard.types.other || 0, c: KTheme.line }
                            ]
                            delegate: Rectangle {
                                required property var modelData
                                width: parent.width * modelData.n / driveCard.total
                                height: 8
                                color: modelData.c
                            }
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 12
                        Controls.Label { text: qsTr("Image") + " " + home.count(driveCard.types.image); color: KTheme.text2; font.pixelSize: KTheme.note }
                        Controls.Label { text: qsTr("Text") + " " + home.count(driveCard.types.text); color: KTheme.text2; font.pixelSize: KTheme.note }
                        Controls.Label { text: qsTr("Audio") + " " + home.count(driveCard.types.audio); color: KTheme.text2; font.pixelSize: KTheme.note }
                        Controls.Label { text: qsTr("Video") + " " + home.count(driveCard.types.video); color: KTheme.text2; font.pixelSize: KTheme.note }
                    }
                    Controls.Label {
                        visible: driveCard.modelData.isDrive && !driveCard.modelData.connected
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: KTheme.text2
                        text: qsTr("Previews and descriptions stay visible; the drive is needed to open the files.")
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        KButton {
                            variant: "tonal"
                            text: qsTr("Browse")
                            onClicked: {
                                appManager1.selectDeviceById(driveCard.modelData.deviceId)
                                home.openPage("Explore")
                            }
                        }
                        KButton {
                            visible: home.ov.hasContext === true
                            variant: "secondary"
                            text: qsTr("Re-interpret with the context")
                            onClicked: home.reinterpretDrive(driveCard.modelData.deviceId)
                        }
                    }
                }
            }

            // A new drive
            Controls.AbstractButton {
                id: addCard
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.minimumHeight: 180
                hoverEnabled: true
                padding: 18
                Accessible.name: qsTr("Add a drive or a folder")
                Accessible.description: qsTr("Teca reads the drive, NTFS too, without changing it.")
                onClicked: home.openPage("Create")
                // Return and Enter open it, as Space does (WCAG 2.1.1)
                Keys.onReturnPressed: addCard.clicked()
                Keys.onEnterPressed: addCard.clicked()
                background: Rectangle {
                    radius: KTheme.radiusCard
                    color: addCard.hovered ? KTheme.hover : "transparent"
                    border.color: KTheme.line
                    border.width: 1.5

                    // Keyboard focus: a ring outside the card (WCAG 2.4.7)
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -KTheme.focusOffset
                        radius: parent.radius + KTheme.focusOffset
                        color: "transparent"
                        border.width: KTheme.focusWidth
                        border.color: KTheme.accent
                        visible: addCard.visualFocus
                    }
                }
                contentItem: ColumnLayout {
                    spacing: 8
                    Rectangle {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        radius: 12
                        color: KTheme.accentSoft
                        Kirigami.Icon { anchors.centerIn: parent; width: 22; height: 22; source: "list-add"; isMask: true; color: KTheme.accentText; Accessible.ignored: true }
                    }
                    // Read as the card's own name and description
                    Controls.Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Add a drive or a folder"); font.weight: Font.Bold; font.pixelSize: KTheme.titleCard; color: KTheme.text; Accessible.ignored: true }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: KTheme.text2
                        text: qsTr("Teca reads the drive, NTFS too, without changing it.")
                        Accessible.ignored: true
                    }
                }
            }
        }
    }

    // ── Work in progress and knowledge ─────────────────────────────────────
    GridLayout {
        Layout.fillWidth: true
        columns: width > 760 ? 2 : 1
        columnSpacing: KTheme.gap
        rowSpacing: KTheme.gap

        KCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            RowLayout {
                Layout.fillWidth: true
                KSectionHeader { level: 3; title: qsTr("Work in progress") }
                KInfoTag {
                    visible: appManager1.mdiIsRunning
                    text: appManager1.mdiIsPaused ? qsTr("Paused")
                          : home.job.waitingForSource ? qsTr("Waiting for the drive") : qsTr("In Progress")
                    tone: appManager1.mdiIsPaused || home.job.waitingForSource ? "warn" : "info"
                }
            }
            Controls.Label {
                Layout.fillWidth: true
                visible: !appManager1.mdiIsRunning
                text: qsTr("Nothing running")
                color: KTheme.text2
            }
            Controls.Label {
                Layout.fillWidth: true
                visible: appManager1.mdiIsRunning
                text: appManager1.mdiStatusText.replace(/^(&nbsp;)+/, "")
                textFormat: Text.StyledText
                wrapMode: Text.WordWrap
                color: KTheme.text
            }
            KProgressBar {
                Layout.fillWidth: true
                visible: appManager1.mdiIsRunning
                indeterminate: !(home.job.fileCount > 0)
                value: home.job.fileCount > 0 ? home.job.fileIndex / home.job.fileCount : 0
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                KButton {
                    visible: appManager1.mdiIsRunning
                    variant: "secondary"
                    icon.name: appManager1.mdiIsPaused ? "media-playback-start" : "media-playback-pause"
                    text: appManager1.mdiIsPaused ? qsTr("Resume") : qsTr("Pause")
                    onClicked: appManager1.setEnrichmentPaused(!appManager1.mdiIsPaused)
                }
                KButton {
                    variant: "link"
                    text: qsTr("Details and resources")
                    onClicked: home.openPage("Activity")
                }
            }
        }

        KCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            RowLayout {
                Layout.fillWidth: true
                KSectionHeader { level: 3; title: qsTr("Archive knowledge") }
                KInfoTag {
                    text: home.ov.hasContext ? qsTr("Context active") : qsTr("No context yet")
                    tone: home.ov.hasContext ? "ok" : "neutral"
                }
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: KTheme.text2
                text: home.ov.hasContext
                      ? qsTr("New files are classified with the archive's own language. Files already analysed are re-interpreted when you choose, one folder at a time too.")
                      : qsTr("Describing the archive lets the AI classify the files with its own terms, categories and names.")
            }
            Flow {
                Layout.fillWidth: true
                visible: home.ov.hasContext === true
                spacing: 6
                KInfoTag { text: qsTr("%1 terms").arg(home.ov.terms || 0) }
                KInfoTag { text: qsTr("%1 categories").arg(home.ov.categories || 0) }
                KInfoTag { text: qsTr("%1 known names").arg(home.ov.names || 0) }
                KInfoTag { text: qsTr("%1 documents").arg(home.count(home.ov.documents)) }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                KButton {
                    visible: home.ov.hasContext === true
                    variant: "tonal"
                    text: qsTr("Re-interpret with the context")
                    onClicked: home.openPage("Context")
                }
                KButton {
                    variant: home.ov.hasContext ? "link" : "tonal"
                    text: home.ov.hasContext ? qsTr("Edit the context") : qsTr("Describe the archive")
                    onClicked: home.openPage("Context")
                }
            }
        }
    }

    // ── Copy of the archive data (fork, 2026-10-30) ────────────────────────
    // Titles, events, people and approved faces live only in this
    // collection's AI data: a dated copy of it, of the collection and of the
    // settings, in a folder the owner picks; written on a worker
    function saveCopy() {
        applicationWindow().openFreshDialog(copyFolderDialogComponent, "")
    }
    function startCopy(folder) {
        copyFailure.visible = false
        const refusal = appManager1.saveArchiveCopy(folder)
        if (refusal !== "") {
            copyFailure.text = refusal
            copyFailure.visible = true
        }
    }
    Component {   // a fresh dialog per open (openFreshDialog)
        id: copyFolderDialogComponent
        Dialogs.FolderDialog {
            title: qsTr("Save a copy…")
            onAccepted: home.startCopy(appManager1.pathFromFileUrl(selectedFolder.toString()))
        }
    }
    Connections {
        target: appManager1
        function onArchiveCopyFinished(ok, message) {
            home.copy = appManager1.archiveCopyState()
            if (ok) {
                applicationWindow().showPassiveNotification(message)
            } else {
                copyFailure.text = message
                copyFailure.visible = true
            }
        }
    }
    KCard {
        Layout.fillWidth: true
        RowLayout {
            Layout.fillWidth: true
            KSectionHeader {
                level: 3
                title: qsTr("Copy of the archive data")
                description: qsTr("The collection, its AI data, the action history and the settings, in a dated folder. The AI analysis keeps running meanwhile.")
            }
            KInfoTag {
                text: home.copy.lastCopy ? qsTr("Last copy: %1").arg(home.copy.lastCopyText) : qsTr("No copy yet")
                tone: home.copy.due === true ? "warn" : "ok"
            }
        }
        // Why the last try did not save a copy; closed by hand or by a new try
        Kirigami.InlineMessage {
            id: copyFailure
            Layout.fillWidth: true
            visible: false
            type: Kirigami.MessageType.Warning
            showCloseButton: true
        }
        Controls.Label {
            Layout.fillWidth: true
            visible: home.copy.hosted === true
            wrapMode: Text.WordWrap
            color: KTheme.text2
            text: qsTr("The hosted database is not part of the copy: it is kept on its server.")
        }
        Controls.Label {
            Layout.fillWidth: true
            visible: appManager1.archiveCopyRunning
            text: appManager1.archiveCopyStatusText.replace(/^(&nbsp;)+/, "")
            textFormat: Text.StyledText
            wrapMode: Text.WordWrap
            color: KTheme.text
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            KButton {
                id: saveCopyButton
                // Not primary: "Add a drive" is this page's one primary action
                variant: home.copy.due === true ? "tonal" : "secondary"
                icon.name: "document-save-as"
                text: qsTr("Save a copy…")
                enabled: !appManager1.archiveCopyRunning
                onClicked: home.saveCopy()
                Keys.onReturnPressed: saveCopyButton.clicked()
                Keys.onEnterPressed: saveCopyButton.clicked()
            }
        }
    }
}
