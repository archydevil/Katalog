import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Derived data of one file (fork, SpecMediaIntelligence.md MDI-F44..F46):
// previews, caption and tags, summary and excerpts, keyframes with their
// timecodes, clips. Read from the destination path, so it works with the
// source drive disconnected. Redesign (D34, phase R2): one card per analysis
// on the design system's ground, AI results as AI tags.
Kirigami.Dialog {
    id: mdiDetailsDialog
    // The design system surface, not Kirigami's View colours (D34)
    background: Rectangle {
        color: KTheme.surface
        radius: KTheme.radiusCard
        border.width: 1
        border.color: KTheme.line
    }

    property string fileLabel: ""
    property var    entries:   []      // appManager1.mdiDetails(): {analysis, label, outcome, reason, value, files, model, tool, date}
    property var    clusters:  null    // appManager1.mdiClusters(): catalog-level topic clusters

    function showFor(catalogId, fullPath, fileName) {
        clusters  = null
        entries   = appManager1.mdiDetails(catalogId, fullPath)
        fileLabel = fileName
        title     = qsTr("Show analysis")
        open()
    }
    function showClusters(catalogId, catalogName) {
        entries   = []
        clusters  = appManager1.mdiClusters(catalogId)
        fileLabel = catalogName
        title     = qsTr("Topic clusters")
        open()
    }
    function fileName(path) {
        return path.substring(Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\")) + 1)
    }

    function entry(analysis) {
        for (var i = 0; i < entries.length; i++)
            if (entries[i].analysis === analysis) return entries[i]
        return null
    }
    function completed(analysis) {
        var e = entry(analysis)
        return e !== null && e.outcome === "completed"
    }
    function firstFileUrl(analysis) {
        var e = entry(analysis)
        return (e && e.files.length > 0 && e.files[0].exists) ? e.files[0].url : ""
    }
    function timecode(seconds) {
        var s = Math.floor(seconds), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60)
        s = s % 60
        return h + ":" + (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
    }
    function provenance(e) {
        var who = e.model !== "" ? e.model : e.tool
        return who + (e.date !== "" ? " · " + e.date.replace("T", " ") : "")
    }

    width: Math.min(applicationWindow().width - Kirigami.Units.gridUnit * 4, Kirigami.Units.gridUnit * 40)
    preferredHeight: Kirigami.Units.gridUnit * 36
    standardButtons: Kirigami.Dialog.Close
    padding: 0

    contentItem: Controls.ScrollView {
        id: detailsScroll
        contentWidth: availableWidth
        Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
        background: Rectangle { color: KTheme.ground }

        Item {
            width: detailsScroll.availableWidth
            implicitHeight: detailsColumn.implicitHeight + 2 * KTheme.gap

            ColumnLayout {
                id: detailsColumn
                x: KTheme.gap
                y: KTheme.gap
                width: parent.width - 2 * KTheme.gap
                spacing: 12

                // ── The file: preview, name and its clips ─────────────────────
                KCard {
                    Layout.fillWidth: true
                    padding: 16

                    Rectangle {
                        id: previewBox
                        readonly property string url: mdiDetailsDialog.completed("thumbnail")
                                                      ? mdiDetailsDialog.firstFileUrl("thumbnail")
                                                      : mdiDetailsDialog.firstFileUrl("poster")
                        visible: url !== ""
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? Kirigami.Units.gridUnit * 14 : 0
                        radius: KTheme.radiusControl
                        color: KTheme.surface2
                        border.color: KTheme.line
                        border.width: 1
                        Image {
                            anchors { fill: parent; margins: 4 }
                            source: previewBox.url
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }
                    }

                    Controls.Label {
                        Layout.fillWidth: true
                        text: mdiDetailsDialog.fileLabel
                        color: KTheme.text
                        font.weight: Font.Bold
                        font.pixelSize: KTheme.titleCard + 1
                        wrapMode: Text.WrapAnywhere
                        Accessible.role: Accessible.Heading
                    }

                    // Clips: opened with the system's player (MDI-F34, F46)
                    Flow {
                        id: clipsRow
                        readonly property bool any: mdiDetailsDialog.firstFileUrl("clip_video") !== ""
                                                    || mdiDetailsDialog.firstFileUrl("clip_audio") !== ""
                        visible: any
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: ["clip_video", "clip_audio"]
                            delegate: KButton {
                                required property var modelData
                                readonly property string url: mdiDetailsDialog.firstFileUrl(modelData)
                                visible: url !== ""
                                // One primary action: the video clip, else the audio one
                                variant: modelData === "clip_video" || mdiDetailsDialog.firstFileUrl("clip_video") === ""
                                         ? "primary" : "secondary"
                                icon.name: modelData === "clip_video" ? "video-mp4" : "audio-x-mpeg"
                                text: mdiDetailsDialog.entry(modelData) ? mdiDetailsDialog.entry(modelData).label : ""
                                onClicked: Qt.openUrlExternally(url)
                            }
                        }
                    }
                }

                // ── Technical data (ffprobe) ───────────────────────────────────
                KCard {
                    id: mediaBox
                    readonly property var e: mdiDetailsDialog.entry("mediainfo")
                    readonly property var v: e && e.value.video ? e.value.video : null
                    readonly property var a: e && e.value.audio ? e.value.audio : null
                    visible: e !== null && e.outcome === "completed"
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 8
                    KSectionHeader { level: 3; title: mediaBox.e ? mediaBox.e.label : "" }
                    // Codec names and units are technical notation, the same in every language
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 4
                        Controls.Label { visible: mediaBox.v !== null; text: qsTr("Video"); color: KTheme.text3 }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: mediaBox.v !== null
                            color: KTheme.text
                            text: mediaBox.v ? [String(mediaBox.v.codec).toUpperCase(),
                                                mediaBox.v.width + " × " + mediaBox.v.height,
                                                mediaBox.v.fps + " fps"].join(" · ") : ""
                        }
                        Controls.Label { visible: mediaBox.a !== null; text: qsTr("Audio"); color: KTheme.text3 }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: mediaBox.a !== null
                            color: KTheme.text
                            text: mediaBox.a ? [String(mediaBox.a.codec).toUpperCase(),
                                                mediaBox.a.sampleRate + " Hz",
                                                mediaBox.a.channels + " ch"].join(" · ") : ""
                        }
                        Controls.Label { text: qsTr("Duration"); color: KTheme.text3 }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: KTheme.text
                            text: mediaBox.e ? mdiDetailsDialog.timecode(Number(mediaBox.e.value.duration))
                                               + (mediaBox.e.value.bitrate > 0
                                                  ? " · " + (Math.round(mediaBox.e.value.bitrate / 100000) / 10) + " Mbit/s" : "")
                                             : ""
                        }
                    }
                }

                // ── Classification in the collection's context (D33) ─────────
                KCard {
                    id: classificationBox
                    readonly property var e: mdiDetailsDialog.entry("classification")
                    readonly property var v: e ? e.value : ({})
                    visible: e !== null && e.outcome === "completed"
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 10
                    KSectionHeader { level: 3; title: classificationBox.e ? classificationBox.e.label : "" }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: KTheme.text
                        font.weight: Font.Bold
                        font.pixelSize: KTheme.titleCard
                        visible: text !== ""
                        text: classificationBox.v.title || ""
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: KTheme.text2
                        visible: text !== ""
                        text: classificationBox.v.description || ""
                    }
                    // Each category, its values as AI tags beside it
                    Repeater {
                        model: Object.keys(classificationBox.v.categories || {})
                        delegate: GridLayout {
                            id: categoryRow
                            required property string modelData
                            Layout.fillWidth: true
                            columns: classificationBox.width > 420 ? 2 : 1
                            columnSpacing: 12
                            rowSpacing: 4
                            Controls.Label {
                                Layout.preferredWidth: categoryRow.columns === 2 ? 140 : -1
                                Layout.fillWidth: categoryRow.columns === 1
                                Layout.alignment: Qt.AlignTop
                                topPadding: categoryRow.columns === 2 ? 3 : 0
                                text: categoryRow.modelData
                                wrapMode: Text.WordWrap
                                color: KTheme.text3
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
                        wrapMode: Text.WordWrap
                        color: KTheme.text2
                        font.pixelSize: KTheme.note
                        visible: (classificationBox.v.sources || []).length > 0
                        text: qsTr("Reference documents") + ": "
                              + [...new Set((classificationBox.v.sources || []).map(function (s) { return s.title }))].join(", ")
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: classificationBox.e ? mdiDetailsDialog.provenance(classificationBox.e) : ""
                        color: KTheme.text3
                        font.pixelSize: KTheme.note
                    }
                }

                // ── Caption and tags ──────────────────────────────────────────
                KCard {
                    id: captionBox
                    readonly property var e: mdiDetailsDialog.entry("caption")
                    visible: e !== null && e.outcome === "completed"
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 8
                    KSectionHeader { level: 3; title: captionBox.e ? captionBox.e.label : "" }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: KTheme.text
                        text: captionBox.e && captionBox.e.value.caption ? captionBox.e.value.caption : ""
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: captionBox.e && captionBox.e.value.tags ? captionBox.e.value.tags : []
                            delegate: KInfoTag { required property var modelData; text: modelData; tone: "ai" }
                        }
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: captionBox.e ? mdiDetailsDialog.provenance(captionBox.e) : ""
                        color: KTheme.text3
                        font.pixelSize: KTheme.note
                    }
                }

                // ── Summary and excerpts ──────────────────────────────────────
                KCard {
                    id: summaryBox
                    readonly property var e: mdiDetailsDialog.entry("summary")
                    visible: e !== null && e.outcome === "completed"
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 8
                    KSectionHeader { level: 3; title: summaryBox.e ? summaryBox.e.label : "" }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: KTheme.text
                        text: summaryBox.e && summaryBox.e.value.summary ? summaryBox.e.value.summary : ""
                    }
                    Repeater {
                        model: summaryBox.e && summaryBox.e.value.excerpts ? summaryBox.e.value.excerpts : []
                        delegate: Controls.Label {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            wrapMode: Text.WordWrap
                            color: KTheme.text2
                            font.italic: true
                            text: "“" + modelData + "”"
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: summaryBox.e && summaryBox.e.value.tags ? summaryBox.e.value.tags : []
                            delegate: KInfoTag { required property var modelData; text: modelData; tone: "ai" }
                        }
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: summaryBox.e ? mdiDetailsDialog.provenance(summaryBox.e) : ""
                        color: KTheme.text3
                        font.pixelSize: KTheme.note
                    }
                }

                // ── Text snippet (first characters, offline search) ───────────
                KCard {
                    id: snippetBox
                    readonly property var e: mdiDetailsDialog.entry("snippet")
                    visible: e !== null && e.outcome === "completed"
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

                // ── Archive contents, read without extracting ─────────────────
                KCard {
                    id: archiveBox
                    readonly property var e: mdiDetailsDialog.entry("archive")
                    visible: e !== null && e.outcome === "completed"
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 6
                    KSectionHeader { level: 3; title: archiveBox.e ? archiveBox.e.label : "" }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        KInfoTag { text: archiveBox.e ? String(archiveBox.e.value.format).toUpperCase() : "" }
                        KInfoTag { text: archiveBox.e ? qsTr("Files") + " " + archiveBox.e.value.count : "" }
                        KInfoTag { text: archiveBox.e ? qsTr("Size") + " " + appManager1.formatDataSize(archiveBox.e.value.totalSize) : "" }
                    }
                    Repeater {
                        // The first entries; the full list stays searchable offline
                        model: archiveBox.e && archiveBox.e.value.entries ? archiveBox.e.value.entries.slice(0, 300) : []
                        delegate: RowLayout {
                            id: archiveRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Kirigami.Icon {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 1
                                source: archiveRow.modelData.dir ? "folder" : "document-open"
                                isMask: true
                                color: KTheme.text3
                                implicitWidth: 16; implicitHeight: 16
                            }
                            Controls.Label {
                                text: archiveRow.modelData.path
                                Layout.fillWidth: true
                                wrapMode: Text.WrapAnywhere
                                color: KTheme.text
                            }
                            Controls.Label {
                                Layout.alignment: Qt.AlignTop
                                visible: !archiveRow.modelData.dir
                                text: appManager1.formatDataSize(archiveRow.modelData.size || 0)
                                color: KTheme.text3
                                font.pixelSize: KTheme.note
                            }
                        }
                    }
                    Controls.Label {
                        visible: archiveBox.e !== null && archiveBox.e.value.count > 300
                        text: "…"
                        color: KTheme.text3
                    }
                }

                // ── Keyframe strip, with timecodes ────────────────────────────
                KCard {
                    id: keyframesBox
                    readonly property var e: mdiDetailsDialog.entry("keyframes")
                    visible: e !== null && e.outcome === "completed"
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 8
                    KSectionHeader { level: 3; title: keyframesBox.e ? keyframesBox.e.label : "" }
                    // Contact sheet: one image, frames side by side; value.frames
                    // gives each frame's position and timecode
                    Item {
                        id: sheetItem
                        readonly property bool isSheet: keyframesBox.e !== null && keyframesBox.e.value.sheet === true
                        readonly property var frames: isSheet && keyframesBox.e.value.frames ? keyframesBox.e.value.frames : []
                        visible: isSheet
                        Layout.fillWidth: true
                        implicitHeight: sheetImage.paintedHeight + Kirigami.Units.gridUnit
                        Image {
                            id: sheetImage
                            width: parent.width
                            source: sheetItem.isSheet && keyframesBox.e.files.length > 0 && keyframesBox.e.files[0].exists
                                    ? keyframesBox.e.files[0].url : ""
                            fillMode: Image.PreserveAspectFit
                            horizontalAlignment: Image.AlignLeft
                            asynchronous: true
                        }
                        Repeater {
                            model: sheetItem.frames
                            delegate: Controls.Label {
                                id: frameLabel
                                required property var modelData
                                readonly property real scale: sheetImage.sourceSize.width > 0
                                                              ? sheetImage.paintedWidth / sheetImage.sourceSize.width : 0
                                x: frameLabel.modelData.x * scale + 2
                                y: sheetImage.paintedHeight + 2
                                text: mdiDetailsDialog.timecode(Number(frameLabel.modelData.t))
                                color: KTheme.text3
                                font.family: KTheme.mono
                                font.pixelSize: KTheme.note
                            }
                        }
                    }
                    Flow {
                        visible: !sheetItem.isSheet
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: keyframesBox.e && !sheetItem.isSheet ? keyframesBox.e.files : []
                            delegate: Column {
                                id: frameItem
                                required property var modelData
                                required property int index
                                spacing: 2
                                Rectangle {
                                    width: Kirigami.Units.gridUnit * 7
                                    height: Kirigami.Units.gridUnit * 4
                                    radius: KTheme.radiusItem
                                    color: KTheme.surface2
                                    border.color: KTheme.line
                                    border.width: 1
                                    Image {
                                        anchors { fill: parent; margins: 2 }
                                        source: frameItem.modelData.exists ? frameItem.modelData.url : ""
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                    }
                                }
                                Controls.Label {
                                    // value.timecodes: seconds, one per frame file
                                    readonly property var times: keyframesBox.e && keyframesBox.e.value.timecodes
                                                                 ? keyframesBox.e.value.timecodes : []
                                    text: times.length > frameItem.index ? mdiDetailsDialog.timecode(Number(times[frameItem.index])) : ""
                                    color: KTheme.text3
                                    font.family: KTheme.mono
                                    font.pixelSize: KTheme.note
                                }
                            }
                        }
                    }
                }

                // ── Topic segments, by timecode (MDI-F36, F38) ────────────────
                KCard {
                    id: segmentsBox
                    readonly property var e: mdiDetailsDialog.entry("segments")
                    visible: e !== null && e.outcome === "completed"
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 12
                    KSectionHeader { level: 3; title: segmentsBox.e ? segmentsBox.e.label : "" }
                    Repeater {
                        model: segmentsBox.e && segmentsBox.e.value.segments ? segmentsBox.e.value.segments : []
                        delegate: ColumnLayout {
                            id: segmentItem
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 4
                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                KInfoTag {
                                    text: mdiDetailsDialog.timecode(segmentItem.modelData.start) + " – "
                                          + mdiDetailsDialog.timecode(segmentItem.modelData.end)
                                }
                                Controls.Label {
                                    width: Math.min(implicitWidth, parent.width)
                                    wrapMode: Text.WordWrap
                                    color: KTheme.text
                                    font.weight: Font.DemiBold
                                    text: segmentItem.modelData.title || ""
                                }
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                visible: text !== ""
                                wrapMode: Text.WordWrap
                                color: KTheme.text2
                                text: segmentItem.modelData.summary || ""
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: segmentItem.modelData.topics || []
                                    delegate: KInfoTag { required property var modelData; text: modelData; tone: "ai" }
                                }
                            }
                        }
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: segmentsBox.e ? mdiDetailsDialog.provenance(segmentsBox.e) : ""
                        color: KTheme.text3
                        font.pixelSize: KTheme.note
                    }
                }

                // ── Transcript with timecodes (MDI-F35) ───────────────────────
                KCard {
                    id: transcriptBox
                    readonly property var e: mdiDetailsDialog.entry("transcript")
                    property bool expanded: false
                    visible: e !== null && e.outcome === "completed"
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        KSectionHeader { level: 3; title: transcriptBox.e ? transcriptBox.e.label : "" }
                        KButton {
                            variant: "link"
                            text: transcriptBox.expanded ? qsTr("Collapse") : qsTr("Expand")
                            icon.name: transcriptBox.expanded ? "go-up" : "go-down"
                            onClicked: transcriptBox.expanded = !transcriptBox.expanded
                        }
                    }
                    Repeater {
                        model: transcriptBox.expanded && transcriptBox.e && transcriptBox.e.value.passages
                               ? transcriptBox.e.value.passages : []
                        delegate: RowLayout {
                            id: passageRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 10
                            Controls.Label {
                                Layout.alignment: Qt.AlignTop
                                text: mdiDetailsDialog.timecode(passageRow.modelData.start)
                                color: KTheme.text3
                                font.family: KTheme.mono
                                font.pixelSize: KTheme.note
                                topPadding: 1
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: KTheme.text
                                text: passageRow.modelData.text
                            }
                        }
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: transcriptBox.e ? mdiDetailsDialog.provenance(transcriptBox.e) : ""
                        color: KTheme.text3
                        font.pixelSize: KTheme.note
                    }
                }

                // ── Catalog topic clusters (MDI-F37) ──────────────────────────
                Repeater {
                    model: mdiDetailsDialog.clusters && mdiDetailsDialog.clusters.clusters
                           ? mdiDetailsDialog.clusters.clusters : []
                    delegate: KCard {
                        id: clusterCard
                        required property var modelData
                        Layout.fillWidth: true
                        padding: 16
                        spacing: 6
                        KSectionHeader {
                            level: 3
                            title: clusterCard.modelData.label || ""
                            description: clusterCard.modelData.description || ""
                        }
                        Repeater {
                            model: clusterCard.modelData.members || []
                            delegate: RowLayout {
                                id: memberRow
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 8
                                Controls.Label {
                                    Layout.alignment: Qt.AlignTop
                                    text: mdiDetailsDialog.timecode(memberRow.modelData.start) + " – "
                                          + mdiDetailsDialog.timecode(memberRow.modelData.end)
                                    color: KTheme.text3
                                    font.family: KTheme.mono
                                    font.pixelSize: KTheme.note
                                    topPadding: 1
                                }
                                Controls.Label {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    color: KTheme.text
                                    text: mdiDetailsDialog.fileName(memberRow.modelData.fullPath) + "  "
                                          + (memberRow.modelData.title || "")
                                }
                            }
                        }
                    }
                }

                // ── Waveform ──────────────────────────────────────────────────
                KCard {
                    id: waveformBox
                    readonly property string url: mdiDetailsDialog.firstFileUrl("waveform")
                    visible: url !== ""
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 8
                    KSectionHeader { level: 3; title: mdiDetailsDialog.entry("waveform") ? mdiDetailsDialog.entry("waveform").label : "" }
                    Image {
                        source: waveformBox.url
                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                        fillMode: Image.Stretch
                        asynchronous: true
                    }
                }

                // ── Failed or skipped analyses, with the reason (MDI-F18) ────
                KCard {
                    id: notCompletedBox
                    readonly property bool any: mdiDetailsDialog.entries.some(function (e) { return e.outcome !== "completed" })
                    visible: any
                    Layout.fillWidth: true
                    padding: 16
                    spacing: 6
                    KSectionHeader { level: 3; title: qsTr("Analyses not completed") }
                    Repeater {
                        model: mdiDetailsDialog.entries
                        delegate: Controls.Label {
                            required property var modelData
                            visible: modelData.outcome !== "completed"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: KTheme.text2
                            text: modelData.label + " — " + modelData.reason
                        }
                    }
                }
            }
        }
    }
}
