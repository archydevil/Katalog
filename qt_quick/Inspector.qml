import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// File details (fork, owner's request 2026-10-29; the Inspector, renamed and
// rebuilt in the redesign, D34 phase R2): a side panel beside the Explore list
// and the Search results that follows the selected file and shows what Katalog
// knows about it, readable with its disk disconnected: preview or contact
// sheet, the AI's title, description and classification, text snippet,
// technical data, metadata with a map link, hashes, archive contents, and
// whether the file's disk is online. Shown and hidden with the Space bar or
// the "Inspector" action. Built as cards on the design system's ground.
Rectangle {
    id: inspector
    color: KTheme.ground

    property int    catalogId: 0
    property string fullPath:  ""
    property var    info: ({})

    function reload() {
        info = (catalogId > 0 && fullPath !== "") ? appManager1.inspectorInfo(catalogId, fullPath) : ({})
    }
    onCatalogIdChanged: reload()
    onFullPathChanged:  reload()
    onVisibleChanged:   if (visible) reload()
    Connections {
        target: appManager1
        // New results of the enrichment job for the file shown
        function onMdiRevisionChanged() { if (inspector.visible) inspector.reload() }
    }

    function entry(analysis) {
        const list = info.analysis || []
        for (let i = 0; i < list.length; i++)
            if (list[i].analysis === analysis && list[i].outcome === "completed")
                return list[i]
        return null
    }
    function fileUrl(analysis) {
        const e = entry(analysis)
        return e && e.files.length > 0 && e.files[0].exists ? e.files[0].url : ""
    }

    readonly property bool online: info.online === true
    readonly property var classification: entry("classification")
    readonly property var classValue: classification ? classification.value : ({})
    // A caption (image) or a summary (document): the AI's description when the
    // classification gives none of its own
    readonly property var described: entry("caption") || entry("summary")
    readonly property string describedText: described ? (described.value.caption || described.value.summary || "") : ""

    // Key / value rows put the key beside its value when the panel has room,
    // above it when narrow
    readonly property bool wide: width > 300
    readonly property int  keyWidth: 110

    // Left edge, as the other panes' separators
    Rectangle {
        anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
        width: 1
        color: KTheme.line
    }

    // ── No file selected ───────────────────────────────────────────────────
    ColumnLayout {
        visible: inspector.fullPath === ""
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * KTheme.gap - 1, 280)
        spacing: 8
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            radius: KTheme.radiusControl
            color: KTheme.neutralSoft
            Kirigami.Icon {
                anchors.centerIn: parent
                width: 24; height: 24
                source: "document-preview"
                isMask: true
                color: KTheme.text3
            }
        }
        Controls.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("No file selected.")
            color: KTheme.text
            font.weight: Font.DemiBold
            font.pixelSize: KTheme.body
        }
        Controls.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("The details of the selected file appear here: preview, AI description, technical data.")
            color: KTheme.text2
            font.pixelSize: KTheme.note
        }
    }

    Controls.ScrollView {
        id: scroll
        visible: inspector.fullPath !== ""
        anchors.fill: parent
        anchors.leftMargin: 1
        contentWidth: availableWidth
        Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff

        Item {
            width: scroll.availableWidth
            implicitHeight: cards.implicitHeight + 2 * KTheme.gap

            ColumnLayout {
                id: cards
                x: KTheme.gap - 2
                y: KTheme.gap
                width: parent.width - 2 * (KTheme.gap - 2)
                spacing: 12

                // ── The file: preview, title, state, AI description, actions ──
                KCard {
                    Layout.fillWidth: true
                    padding: 16

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Controls.Label {
                            Layout.fillWidth: true
                            text: qsTr("File details").toUpperCase()
                            wrapMode: Text.WordWrap
                            color: KTheme.text3
                            font.pixelSize: KTheme.note
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.6
                            Accessible.role: Accessible.Heading
                        }
                        Controls.Label {
                            Layout.maximumWidth: parent.width / 2
                            text: qsTr("Space to close")
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignRight
                            color: KTheme.text3
                            font.pixelSize: KTheme.note
                        }
                    }

                    // Preview: thumbnail, contact sheet or poster frame
                    Rectangle {
                        id: previewBox
                        readonly property string url: inspector.fileUrl("thumbnail") !== "" ? inspector.fileUrl("thumbnail")
                                                      : inspector.fileUrl("keyframes") !== "" ? inspector.fileUrl("keyframes")
                                                      : inspector.fileUrl("poster")
                        visible: url !== ""
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible
                            ? Math.max(64, Math.min(220, (width - 8) * (preview.sourceSize.height
                                                                        / Math.max(1, preview.sourceSize.width)) + 8))
                            : 0
                        radius: KTheme.radiusControl
                        color: KTheme.surface2
                        border.color: KTheme.line
                        border.width: 1
                        Image {
                            id: preview
                            anchors { fill: parent; margins: 4 }
                            source: previewBox.url
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }
                    }

                    // Title given by the AI's classification, else the file name
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Controls.Label {
                            Layout.fillWidth: true
                            text: inspector.classValue.title || inspector.info.name || ""
                            wrapMode: Text.Wrap
                            color: KTheme.text
                            font.weight: Font.Bold
                            font.pixelSize: KTheme.titleCard + 1
                            Accessible.role: Accessible.Heading
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            visible: (inspector.classValue.title || "") !== ""
                            text: inspector.info.name || ""
                            wrapMode: Text.WrapAnywhere
                            color: KTheme.text3
                            font.family: KTheme.mono
                            font.pixelSize: KTheme.note
                        }
                    }

                    // State of the file's disk; offline, the disk it needs
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        KInfoTag {
                            text: inspector.online ? qsTr("Online") : qsTr("Offline")
                            tone: inspector.online ? "ok" : "warn"
                        }
                        KInfoTag {
                            visible: !inspector.online && (inspector.info.volumeName || "") !== ""
                            text: inspector.info.volumeName || ""
                        }
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        visible: !inspector.online
                        wrapMode: Text.WordWrap
                        color: KTheme.text2
                        font.pixelSize: KTheme.note
                        text: qsTr("Previews and descriptions stay visible; the drive is needed to open the files.")
                    }

                    // The AI's description: the classification's, else the
                    // caption or summary; both when there are both
                    Controls.Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        wrapMode: Text.Wrap
                        color: KTheme.text
                        font.pixelSize: KTheme.body
                        text: inspector.classValue.description || inspector.describedText
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        visible: (inspector.classValue.description || "") !== "" && inspector.describedText !== ""
                        wrapMode: Text.Wrap
                        color: KTheme.text2
                        font.pixelSize: KTheme.body
                        text: inspector.describedText
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        KButton {
                            variant: "primary"
                            icon.name: "document-open"
                            text: qsTr("Open file")
                            enabled: inspector.online
                            onClicked: Qt.openUrlExternally(inspector.info.url)
                        }
                        KButton {
                            visible: (inspector.info.analysis || []).length > 0
                            variant: "link"
                            icon.name: "view-preview"
                            text: qsTr("Show analysis")
                            onClicked: inspectorDetails.showFor(inspector.catalogId, inspector.fullPath, inspector.info.name)
                        }
                    }
                }

                // ── Classification in the collection's context (D33) ──────────
                KCard {
                    id: classificationBox
                    readonly property var e: inspector.classification
                    readonly property var v: inspector.classValue
                    visible: e !== null
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 10

                    KSectionHeader { level: 3; title: classificationBox.e ? classificationBox.e.label : "" }
                    // Each category, its values as AI tags beside it (below it
                    // when the panel is narrow)
                    Repeater {
                        model: Object.keys(classificationBox.v.categories || {})
                        delegate: GridLayout {
                            id: categoryRow
                            required property string modelData
                            Layout.fillWidth: true
                            columns: inspector.wide ? 2 : 1
                            columnSpacing: 10
                            rowSpacing: 4
                            Controls.Label {
                                Layout.preferredWidth: inspector.wide ? inspector.keyWidth : -1
                                Layout.fillWidth: !inspector.wide
                                Layout.alignment: Qt.AlignTop
                                topPadding: inspector.wide ? 3 : 0
                                text: categoryRow.modelData
                                wrapMode: Text.WordWrap
                                color: KTheme.text3
                                font.pixelSize: KTheme.note
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: classificationBox.v.categories[categoryRow.modelData] || []
                                    delegate: KInfoTag { required property var modelData; text: modelData; tone: "ai" }
                                }
                            }
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        visible: (classificationBox.v.tags || []).length > 0
                        spacing: 6
                        Repeater {
                            model: classificationBox.v.tags || []
                            delegate: KInfoTag { required property var modelData; text: modelData; tone: "ai" }
                        }
                    }
                    ColumnLayout {
                        // People, places and events recognised in the file (MDI-F102)
                        Layout.fillWidth: true
                        visible: (classificationBox.v.entities || []).length > 0
                        spacing: 2
                        Controls.Label {
                            text: qsTr("Recognised names")
                            color: KTheme.text2
                            font.pixelSize: KTheme.note
                            font.weight: Font.DemiBold
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: classificationBox.v.entities || []
                                delegate: KInfoTag { required property var modelData; text: modelData; tone: "ai" }
                            }
                        }
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: KTheme.text2
                        font.pixelSize: KTheme.note
                        visible: (classificationBox.v.sources || []).length > 0
                        text: qsTr("Reference documents") + ": "
                              + [...new Set((classificationBox.v.sources || []).map(function (s) { return s.title }))].join(", ")
                    }
                }

                // ── Text snippet ───────────────────────────────────────────────
                KCard {
                    id: snippetBox
                    readonly property var e: inspector.entry("snippet")
                    visible: e !== null
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 8
                    KSectionHeader { level: 3; title: snippetBox.e ? snippetBox.e.label : "" }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: KTheme.text2
                        text: snippetBox.e ? snippetBox.e.value.text + (snippetBox.e.value.truncated ? " …" : "") : ""
                    }
                }

                // ── Technical data (ffprobe) ───────────────────────────────────
                KCard {
                    id: mediaBox
                    readonly property var e: inspector.entry("mediainfo")
                    readonly property var v: e && e.value.video ? e.value.video : null
                    readonly property var a: e && e.value.audio ? e.value.audio : null
                    visible: e !== null
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 8
                    KSectionHeader { level: 3; title: mediaBox.e ? mediaBox.e.label : "" }
                    // Codec names and units are technical notation
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 4
                        // Short keys: beside their value at every width
                        Controls.Label { visible: mediaBox.v !== null; text: qsTr("Video"); color: KTheme.text3; font.pixelSize: KTheme.note }
                        Controls.Label {
                            visible: mediaBox.v !== null
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: KTheme.text
                            text: mediaBox.v ? String(mediaBox.v.codec).toUpperCase() + " · " + mediaBox.v.width + " × "
                                               + mediaBox.v.height + " · " + mediaBox.v.fps + " fps" : ""
                        }
                        Controls.Label { visible: mediaBox.a !== null; text: qsTr("Audio"); color: KTheme.text3; font.pixelSize: KTheme.note }
                        Controls.Label {
                            visible: mediaBox.a !== null
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: KTheme.text
                            text: mediaBox.a ? String(mediaBox.a.codec).toUpperCase() + " · " + mediaBox.a.sampleRate
                                               + " Hz · " + mediaBox.a.channels + " ch" : ""
                        }
                    }
                }

                // ── Extended metadata (EXIF, ID3...) and GPS ───────────────────
                KCard {
                    visible: (inspector.info.metadata || []).length > 0 || inspector.info.gps !== undefined
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 8
                    KSectionHeader { level: 3; title: qsTr("Extended Metadata") }
                    Repeater {
                        model: (inspector.info.metadata || []).slice(0, 40)
                        delegate: GridLayout {
                            id: metadataRow
                            required property var modelData
                            Layout.fillWidth: true
                            columns: inspector.wide ? 2 : 1
                            columnSpacing: 10
                            rowSpacing: 0
                            Controls.Label {
                                Layout.preferredWidth: inspector.wide ? inspector.keyWidth : -1
                                Layout.fillWidth: !inspector.wide
                                Layout.alignment: Qt.AlignTop
                                text: metadataRow.modelData.label
                                wrapMode: Text.Wrap
                                color: KTheme.text3
                                font.pixelSize: KTheme.note
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                text: String(metadataRow.modelData.value)
                                wrapMode: Text.WrapAnywhere
                                color: KTheme.text
                                font.pixelSize: KTheme.note
                            }
                        }
                    }
                    KButton {
                        visible: inspector.info.gps !== undefined
                        variant: "link"
                        icon.name: "globe"
                        text: qsTr("Open on map")
                        onClicked: Qt.openUrlExternally(inspector.info.gps.url)
                    }
                }

                // ── Hashes ─────────────────────────────────────────────────────
                KCard {
                    id: hashBox
                    readonly property var q: inspector.entry("quickhash")
                    visible: q !== null || (inspector.info.sha256 || "") !== ""
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 8
                    KSectionHeader { level: 3; title: qsTr("Checksum") }
                    ColumnLayout {
                        visible: hashBox.q !== null
                        Layout.fillWidth: true
                        spacing: 2
                        Controls.Label { text: hashBox.q ? hashBox.q.label : ""; color: KTheme.text3; font.pixelSize: KTheme.note }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            color: KTheme.text
                            font.family: KTheme.mono
                            font.pixelSize: KTheme.note
                            text: hashBox.q ? hashBox.q.value.hash : ""
                        }
                    }
                    ColumnLayout {
                        visible: (inspector.info.sha256 || "") !== ""
                        Layout.fillWidth: true
                        spacing: 2
                        Controls.Label { text: "SHA-256"; color: KTheme.text3; font.pixelSize: KTheme.note }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            color: KTheme.text
                            font.family: KTheme.mono
                            font.pixelSize: KTheme.note
                            text: inspector.info.sha256 || ""
                        }
                    }
                }

                // ── Archive contents, first entries ────────────────────────────
                KCard {
                    id: archiveBox
                    readonly property var e: inspector.entry("archive")
                    visible: e !== null
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 4
                    KSectionHeader { level: 3; title: archiveBox.e ? archiveBox.e.label : "" }
                    Repeater {
                        model: archiveBox.e && archiveBox.e.value.entries ? archiveBox.e.value.entries.slice(0, 50) : []
                        delegate: RowLayout {
                            id: archiveRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 6
                            Kirigami.Icon {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 1
                                source: archiveRow.modelData.dir ? "folder" : "document-open"
                                isMask: true
                                color: KTheme.text3
                                implicitWidth: 16; implicitHeight: 16
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WrapAnywhere
                                color: KTheme.text2
                                font.pixelSize: KTheme.note
                                text: archiveRow.modelData.path
                            }
                        }
                    }
                }
            }
        }
    }

    MdiDetailsDialog { id: inspectorDetails }
}
