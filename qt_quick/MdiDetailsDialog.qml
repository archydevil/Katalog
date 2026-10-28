import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Derived data of one file (fork, SpecMediaIntelligence.md MDI-F44..F46):
// previews, caption and tags, summary and excerpts, keyframes with their
// timecodes, clips. Read from the destination path, so it works with the
// source drive disconnected.
Kirigami.Dialog {
    id: mdiDetailsDialog

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

    contentItem: Controls.ScrollView {
        contentWidth: availableWidth
        Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff

        ColumnLayout {
            width: parent.width
            spacing: Kirigami.Units.largeSpacing

            Controls.Label {
                Layout.fillWidth: true
                text: mdiDetailsDialog.fileLabel
                font.bold: true
                wrapMode: Text.WrapAnywhere
            }

            // ── Preview image ─────────────────────────────────────────────
            Image {
                readonly property string url: mdiDetailsDialog.completed("thumbnail")
                                              ? mdiDetailsDialog.firstFileUrl("thumbnail")
                                              : mdiDetailsDialog.firstFileUrl("poster")
                visible: url !== ""
                source: url
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth:  Math.min(sourceSize.width * 1.5, parent.width)
                Layout.preferredHeight: Kirigami.Units.gridUnit * 14
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }

            // ── Caption and tags ──────────────────────────────────────────
            ColumnLayout {
                id: captionBox
                readonly property var e: mdiDetailsDialog.entry("caption")
                visible: e !== null && e.outcome === "completed"
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Heading { level: 4; text: parent.e ? parent.e.label : "" }
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: parent.e && parent.e.value.caption ? parent.e.value.caption : ""
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    Repeater {
                        model: captionBox.e && captionBox.e.value.tags ? captionBox.e.value.tags : []
                        delegate: Kirigami.Chip { required property var modelData; text: modelData; closable: false; checkable: false }
                    }
                }
                Controls.Label { text: parent.e ? mdiDetailsDialog.provenance(parent.e) : ""; opacity: 0.6; font: Kirigami.Theme.smallFont }
            }

            // ── Summary and excerpts ──────────────────────────────────────
            ColumnLayout {
                id: summaryBox
                readonly property var e: mdiDetailsDialog.entry("summary")
                visible: e !== null && e.outcome === "completed"
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Heading { level: 4; text: parent.e ? parent.e.label : "" }
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: parent.e && parent.e.value.summary ? parent.e.value.summary : ""
                }
                Repeater {
                    model: parent.e && parent.e.value.excerpts ? parent.e.value.excerpts : []
                    delegate: Controls.Label {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        wrapMode: Text.WordWrap
                        font.italic: true
                        text: "“" + modelData + "”"
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    Repeater {
                        model: summaryBox.e && summaryBox.e.value.tags ? summaryBox.e.value.tags : []
                        delegate: Kirigami.Chip { required property var modelData; text: modelData; closable: false; checkable: false }
                    }
                }
                Controls.Label { text: parent.e ? mdiDetailsDialog.provenance(parent.e) : ""; opacity: 0.6; font: Kirigami.Theme.smallFont }
            }

            // ── Keyframe strip, with timecodes ────────────────────────────
            ColumnLayout {
                id: keyframesBox
                readonly property var e: mdiDetailsDialog.entry("keyframes")
                visible: e !== null && e.outcome === "completed"
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Heading { level: 4; text: parent.e ? parent.e.label : "" }
                Flow {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    Repeater {
                        model: keyframesBox.e ? keyframesBox.e.files : []
                        delegate: Column {
                            required property var modelData
                            required property int index
                            spacing: 2
                            Image {
                                source: modelData.exists ? modelData.url : ""
                                width: Kirigami.Units.gridUnit * 7
                                height: Kirigami.Units.gridUnit * 4
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                            }
                            Controls.Label {
                                // value.timecodes: seconds, one per frame file
                                readonly property var times: keyframesBox.e && keyframesBox.e.value.timecodes
                                                             ? keyframesBox.e.value.timecodes : []
                                text: times.length > index ? mdiDetailsDialog.timecode(Number(times[index])) : ""
                                font: Kirigami.Theme.smallFont
                                opacity: 0.7
                            }
                        }
                    }
                }
            }

            // ── Topic segments, by timecode (MDI-F36, F38) ────────────────
            ColumnLayout {
                id: segmentsBox
                readonly property var e: mdiDetailsDialog.entry("segments")
                visible: e !== null && e.outcome === "completed"
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Heading { level: 4; text: segmentsBox.e ? segmentsBox.e.label : "" }
                Repeater {
                    model: segmentsBox.e && segmentsBox.e.value.segments ? segmentsBox.e.value.segments : []
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 2
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.bold: true
                            text: mdiDetailsDialog.timecode(modelData.start) + " – "
                                  + mdiDetailsDialog.timecode(modelData.end) + "  " + (modelData.title || "")
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: modelData.summary || ""
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            Repeater {
                                model: modelData.topics || []
                                delegate: Kirigami.Chip { required property var modelData; text: modelData; closable: false; checkable: false }
                            }
                        }
                    }
                }
                Controls.Label { text: segmentsBox.e ? mdiDetailsDialog.provenance(segmentsBox.e) : ""; opacity: 0.6; font: Kirigami.Theme.smallFont }
            }

            // ── Transcript with timecodes (MDI-F35) ───────────────────────
            ColumnLayout {
                id: transcriptBox
                readonly property var e: mdiDetailsDialog.entry("transcript")
                property bool expanded: false
                visible: e !== null && e.outcome === "completed"
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    Kirigami.Heading { level: 4; text: transcriptBox.e ? transcriptBox.e.label : "" }
                    Item { Layout.fillWidth: true }
                    IconButton {
                        text: transcriptBox.expanded ? qsTr("Collapse") : qsTr("Expand")
                        icon.name: transcriptBox.expanded ? "go-up" : "go-down"
                        flat: true
                        onClicked: transcriptBox.expanded = !transcriptBox.expanded
                    }
                }
                Repeater {
                    model: transcriptBox.expanded && transcriptBox.e && transcriptBox.e.value.passages
                           ? transcriptBox.e.value.passages : []
                    delegate: Controls.Label {
                        required property var modelData
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "<span style='opacity:0.6'>" + mdiDetailsDialog.timecode(modelData.start) + "</span>  " + modelData.text
                        textFormat: Text.StyledText
                    }
                }
                Controls.Label { text: transcriptBox.e ? mdiDetailsDialog.provenance(transcriptBox.e) : ""; opacity: 0.6; font: Kirigami.Theme.smallFont }
            }

            // ── Catalog topic clusters (MDI-F37) ──────────────────────────
            Repeater {
                model: mdiDetailsDialog.clusters && mdiDetailsDialog.clusters.clusters
                       ? mdiDetailsDialog.clusters.clusters : []
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 2
                    Kirigami.Heading { level: 4; text: modelData.label || ""; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Controls.Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: modelData.description || "" }
                    Repeater {
                        model: modelData.members || []
                        delegate: Controls.Label {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.leftMargin: Kirigami.Units.largeSpacing
                            elide: Text.ElideRight
                            opacity: 0.8
                            text: mdiDetailsDialog.fileName(modelData.fullPath) + "  "
                                  + mdiDetailsDialog.timecode(modelData.start) + " – "
                                  + mdiDetailsDialog.timecode(modelData.end) + "  " + (modelData.title || "")
                        }
                    }
                }
            }

            // ── Waveform ──────────────────────────────────────────────────
            ColumnLayout {
                readonly property string url: mdiDetailsDialog.firstFileUrl("waveform")
                visible: url !== ""
                Layout.fillWidth: true
                Kirigami.Heading { level: 4; text: mdiDetailsDialog.entry("waveform") ? mdiDetailsDialog.entry("waveform").label : "" }
                Image {
                    source: parent.url
                    Layout.fillWidth: true
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                    fillMode: Image.Stretch
                    asynchronous: true
                }
            }

            // ── Clips: opened with the system's player (MDI-F34, F46) ─────
            Flow {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Repeater {
                    model: ["clip_video", "clip_audio"]
                    delegate: Controls.Button {
                        required property var modelData
                        readonly property string url: mdiDetailsDialog.firstFileUrl(modelData)
                        visible: url !== ""
                        icon.name: modelData === "clip_video" ? "video-mp4" : "audio-x-mpeg"
                        text: mdiDetailsDialog.entry(modelData) ? mdiDetailsDialog.entry(modelData).label : ""
                        onClicked: Qt.openUrlExternally(url)
                    }
                }
            }

            // ── Failed or skipped analyses, with the reason (MDI-F18) ────
            Repeater {
                model: mdiDetailsDialog.entries
                delegate: Controls.Label {
                    required property var modelData
                    visible: modelData.outcome !== "completed"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: modelData.label + " — " + modelData.reason
                }
            }
        }
    }
}
