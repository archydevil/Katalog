import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Home page (fork, D34): the archive at a glance — its drives as cards with
// their state and contents, the work in progress, the archive's knowledge.
// Navigation goes through openPage(name), handled by Main.qml.
ColumnLayout {
    id: home
    spacing: 28

    signal openPage(string name)
    property var ov: ({})
    readonly property var job: appManager1.mdiJobInfo

    function refresh() { ov = appManager1.homeOverview() }
    // Called by Main.qml when the page is shown
    function setShown(shown) { if (shown) refresh() }
    function count(n) { return Number(n || 0).toLocaleString(Qt.locale(), "f", 0) }

    Connections {
        target: appManager1
        function onMdiStateChanged() { if (home.visible) home.refresh() }
        function onKnowledgeStateChanged() { if (home.visible) home.refresh() }
        function onRecentCollectionsChanged() { if (home.visible) home.refresh() }
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
            onClicked: home.openPage("Search")
        }
        KButton {
            variant: "primary"
            icon.name: "list-add"
            text: qsTr("Add a drive")
            onClicked: home.openPage("Create")
        }
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
            value: home.job.egress && home.job.egress.length > 0 ? home.job.egress.join(", ") : qsTr("Nothing sent")
            tagText: qsTr("On this computer")
            tagTone: home.job.egress && home.job.egress.length > 0 ? "warn" : "ok"
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
                                source: "drive-harddisk"
                                isMask: true
                                color: driveCard.modelData.connected ? KTheme.accentText : KTheme.text3
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
                                elide: Text.ElideRight
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
                            text: driveCard.modelData.connected ? qsTr("Connected") : qsTr("Not connected")
                            tone: driveCard.modelData.connected ? "ok" : "warn"
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
                        visible: !driveCard.modelData.connected
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
                            onClicked: home.openPage("Context")
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
                onClicked: home.openPage("Create")
                background: Rectangle {
                    radius: KTheme.radiusCard
                    color: addCard.hovered ? KTheme.hover : "transparent"
                    border.color: KTheme.line
                    border.width: 1.5
                }
                contentItem: ColumnLayout {
                    spacing: 8
                    Rectangle {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        radius: 12
                        color: KTheme.accentSoft
                        Kirigami.Icon { anchors.centerIn: parent; width: 22; height: 22; source: "list-add"; isMask: true; color: KTheme.accentText }
                    }
                    Controls.Label { text: qsTr("Add a drive or a folder"); font.weight: Font.Bold; font.pixelSize: KTheme.titleCard; color: KTheme.text }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: KTheme.text2
                        text: qsTr("Katalog reads the drive, NTFS too, without changing it.")
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
}
