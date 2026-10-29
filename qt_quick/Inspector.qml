import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Offline Inspector (fork, owner's request 2026-10-29): a side panel beside
// the Explore list and the Search results that follows the selected file and
// shows what Katalog knows about it, readable with its disk disconnected:
// preview or contact sheet, text snippet, technical data, metadata with a map
// link, hashes, archive contents, and whether the file's disk is online.
// Shown and hidden with the Space bar or the "Inspector" action.
Rectangle {
    id: inspector
    color: Kirigami.Theme.backgroundColor

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

    // Left edge, as the other panes' separators
    Kirigami.Separator { anchors { top: parent.top; bottom: parent.bottom; left: parent.left } }

    Controls.Label {
        anchors.centerIn: parent
        visible: inspector.fullPath === ""
        text: qsTr("No file selected.")
        opacity: 0.6
    }

    Controls.ScrollView {
        id: scroll
        visible: inspector.fullPath !== ""
        anchors.fill: parent
        anchors.leftMargin: 1
        contentWidth: availableWidth
        Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff

        ColumnLayout {
            width: scroll.availableWidth
            spacing: Kirigami.Units.largeSpacing

            Item { Layout.preferredHeight: Kirigami.Units.smallSpacing }

            Controls.Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                text: inspector.info.name || ""
                font.bold: true
                wrapMode: Text.WrapAnywhere
            }

            // ── Disk status: open the file, or say which disk to connect ──
            Rectangle {
                id: badge
                readonly property bool online: inspector.info.online === true
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                implicitHeight: badgeRow.implicitHeight + Kirigami.Units.smallSpacing * 2
                radius: Kirigami.Units.smallSpacing
                color: Qt.alpha(online ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.neutralTextColor, 0.15)
                RowLayout {
                    id: badgeRow
                    anchors { fill: parent; margins: Kirigami.Units.smallSpacing }
                    Kirigami.Icon {
                        source: badge.online ? "document-open" : "drive-removable-media"
                        implicitWidth: Kirigami.Units.iconSizes.small; implicitHeight: implicitWidth
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: badge.online
                              ? qsTr("Online – click to open")
                              : qsTr("Offline – connect %1 to open the file").arg(inspector.info.volumeName || "")
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: badge.online
                    cursorShape: badge.online ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: Qt.openUrlExternally(inspector.info.url)
                }
            }

            // ── Preview: thumbnail, poster frame or contact sheet ──────────
            Image {
                readonly property string url: inspector.fileUrl("thumbnail") !== "" ? inspector.fileUrl("thumbnail")
                                              : inspector.fileUrl("keyframes") !== "" ? inspector.fileUrl("keyframes")
                                              : inspector.fileUrl("poster")
                visible: url !== ""
                source: url
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                Layout.preferredHeight: visible ? Math.min(Kirigami.Units.gridUnit * 12,
                                                           width * (sourceSize.height / Math.max(1, sourceSize.width))) : 0
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }

            // ── Caption or summary, short ──────────────────────────────────
            Controls.Label {
                readonly property var e: inspector.entry("caption") || inspector.entry("summary")
                visible: e !== null
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                wrapMode: Text.Wrap
                text: e ? (e.value.caption || e.value.summary || "") : ""
            }

            // ── Text snippet ───────────────────────────────────────────────
            ColumnLayout {
                readonly property var e: inspector.entry("snippet")
                visible: e !== null
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                Kirigami.Heading { level: 5; text: parent.e ? parent.e.label : "" }
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    opacity: 0.85
                    text: parent.e ? parent.e.value.text + (parent.e.value.truncated ? " …" : "") : ""
                }
            }

            // ── Technical data (ffprobe) ───────────────────────────────────
            ColumnLayout {
                readonly property var e: inspector.entry("mediainfo")
                visible: e !== null
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                Kirigami.Heading { level: 5; text: parent.e ? parent.e.label : "" }
                // Codec names and units are technical notation
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    readonly property var v: parent.e ? parent.e.value.video : null
                    readonly property var a: parent.e ? parent.e.value.audio : null
                    text: (v ? String(v.codec).toUpperCase() + " · " + v.width + " × " + v.height + " · " + v.fps + " fps" : "")
                          + (v && a ? "\n" : "")
                          + (a ? String(a.codec).toUpperCase() + " · " + a.sampleRate + " Hz · " + a.channels + " ch" : "")
                }
            }

            // ── Extended metadata (EXIF, ID3...) and GPS ───────────────────
            ColumnLayout {
                visible: (inspector.info.metadata || []).length > 0 || inspector.info.gps !== undefined
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                spacing: 2
                Kirigami.Heading { level: 5; text: qsTr("Extended Metadata") }
                Repeater {
                    model: (inspector.info.metadata || []).slice(0, 40)
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Controls.Label {
                            text: modelData.label
                            opacity: 0.7
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 7
                            wrapMode: Text.Wrap
                        }
                        Controls.Label {
                            text: String(modelData.value)
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
                Controls.Button {
                    visible: inspector.info.gps !== undefined
                    flat: true
                    icon.name: "mark-location"
                    text: qsTr("Open on map")
                    onClicked: Qt.openUrlExternally(inspector.info.gps.url)
                }
            }

            // ── Hashes ─────────────────────────────────────────────────────
            ColumnLayout {
                readonly property var q: inspector.entry("quickhash")
                visible: q !== null || (inspector.info.sha256 || "") !== ""
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                spacing: 2
                Kirigami.Heading { level: 5; text: qsTr("Checksum") }
                Controls.Label {
                    visible: parent.q !== null
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                    font.family: "monospace"
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    text: parent.q ? parent.q.label + ": " + parent.q.value.hash : ""
                }
                Controls.Label {
                    visible: (inspector.info.sha256 || "") !== ""
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                    font.family: "monospace"
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    text: "SHA-256: " + (inspector.info.sha256 || "")
                }
            }

            // ── Archive contents, first entries ────────────────────────────
            ColumnLayout {
                readonly property var e: inspector.entry("archive")
                visible: e !== null
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                spacing: 2
                Kirigami.Heading { level: 5; text: parent.e ? parent.e.label : "" }
                Repeater {
                    model: parent.e && parent.e.value.entries ? parent.e.value.entries.slice(0, 50) : []
                    delegate: Controls.Label {
                        required property var modelData
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAnywhere
                        text: (modelData.dir ? "▸ " : "") + modelData.path
                        opacity: 0.85
                    }
                }
            }

            Controls.Button {
                Layout.leftMargin: Kirigami.Units.largeSpacing
                visible: (inspector.info.analysis || []).length > 0
                icon.name: "view-preview"
                text: qsTr("Show analysis")
                onClicked: inspectorDetails.showFor(inspector.catalogId, inspector.fullPath, inspector.info.name)
            }

            Item { Layout.preferredHeight: Kirigami.Units.gridUnit }
        }
    }

    MdiDetailsDialog { id: inspectorDetails }
}
