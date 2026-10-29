import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Extended metadata viewer, shared by the Search results and Explore pages.
// Call showFor(catalogId, fileName, folderPath): it fetches the parsed fields and
// the raw JSON, and reports when a file has no extended metadata.
// Drawn with the design system (fork, D34 phase R8): the file as a header with
// the number of fields, the fields in a rounded table of alternating rows.
Kirigami.Dialog {
    id: metadataDialog
    property string   jsonText:    ""
    property var      fields:      []   // QVariantList of {label, value}
    property string   fileLabel:   ""

    // Fetch and show the metadata for one file, or report that it has none.
    function showFor(catalogId, fileName, folderPath) {
        let f = appManager1.getFileMetadataParsedFields(catalogId, fileName, folderPath)
        if (f.length > 0) {
            metadataDialog.fields    = f
            metadataDialog.jsonText  = appManager1.getFileMetadataJson(catalogId, fileName, folderPath)
            metadataDialog.fileLabel = fileName
            metadataDialog.open()
        } else {
            applicationWindow().showPassiveNotification(qsTr("No extended metadata available"))
        }
    }

    title: qsTr("Extended Metadata")
    width: Math.min(560, applicationWindow().width - 32)
    preferredHeight: Kirigami.Units.gridUnit * 32

    standardButtons: Kirigami.Dialog.Close

    // Copy JSON is an extra action, not the accept button, so it takes the
    // leading slot at the far left and Close keeps the trailing position the
    // platform gives it (SpecValidationRules D1/D2). Left in customFooterActions
    // it rendered to the right of Close, which no other dialog does.
    footerLeadingComponent: Controls.Button {
        text: qsTr("Copy JSON")
        icon.name: "edit-copy"
        onClicked: {
            appManager1.copyToClipboard(metadataDialog.jsonText)
            applicationWindow().showPassiveNotification(qsTr("Metadata JSON copied to clipboard"))
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        // ── The file ─────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 8
            implicitHeight: fileRow.implicitHeight + 20
            radius: KTheme.radiusItem
            color: KTheme.surface2
            border.color: KTheme.line
            border.width: 1

            RowLayout {
                id: fileRow
                x: 12
                y: 10
                width: parent.width - 24
                spacing: 10
                Kirigami.Icon {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    Layout.alignment: Qt.AlignTop
                    source: "documentinfo"
                    fallback: "dialog-information"
                    isMask: true
                    color: KTheme.text2
                }
                // The name wraps rather than elides (CDT-C1)
                Controls.Label {
                    id: fileNameLabel
                    Layout.fillWidth: true
                    text: metadataDialog.fileLabel
                    wrapMode: Text.WrapAnywhere
                    color: KTheme.text
                    font.weight: Font.Bold
                }
                KInfoTag {
                    Layout.alignment: Qt.AlignTop
                    text: qsTr("Fields: %1").arg(metadataDialog.fields.length)
                }
            }
        }

        // ── The fields ───────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 8
            radius: KTheme.radiusItem
            color: KTheme.surface
            border.color: KTheme.line
            border.width: 1
            clip: true

            // Column headers
            Rectangle {
                id: headerRow
                anchors { top: parent.top; left: parent.left; right: parent.right; margins: 1 }
                implicitHeight: 32
                height: implicitHeight
                radius: KTheme.radiusItem
                color: KTheme.surface2

                // Square lower corners: only the top follows the rounded frame
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: parent.radius
                    color: parent.color
                }

                Controls.Label {
                    x: 12; y: 0
                    width: 148
                    height: parent.height
                    text: qsTr("Field")
                    font.weight: Font.DemiBold
                    font.pixelSize: KTheme.note
                    color: KTheme.text3
                    verticalAlignment: Text.AlignVCenter
                }
                Controls.Label {
                    x: 172; y: 0
                    width: parent.width - x - 12
                    height: parent.height
                    text: qsTr("Value")
                    font.weight: Font.DemiBold
                    font.pixelSize: KTheme.note
                    color: KTheme.text3
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Rectangle {
                id: headerSep
                anchors { top: headerRow.bottom; left: parent.left; right: parent.right }
                height: 1
                color: KTheme.line
            }

            // Scrollable table
            Controls.ScrollView {
                id: metadataScrollView
                anchors { top: headerSep.bottom; left: parent.left; right: parent.right; bottom: parent.bottom
                          leftMargin: 1; rightMargin: 1; bottomMargin: KTheme.radiusItem / 2 }
                clip: true

                ListView {
                    id: metadataListView
                    width: metadataScrollView.availableWidth
                    model: metadataDialog.fields
                    clip: true

                    delegate: Rectangle {
                        id: fieldRow
                        required property var   modelData
                        required property int   index
                        width: ListView.view.width

                        readonly property int   colWidth: 160
                        readonly property int   hPad:     12
                        implicitHeight: Math.max(34, Math.max(fieldLabel.implicitHeight, valueText.implicitHeight) + 14)

                        color: index % 2 === 0 ? KTheme.surface : KTheme.surface2

                        Controls.Label {
                            id: fieldLabel
                            x: fieldRow.hPad
                            y: 7
                            width: fieldRow.colWidth - fieldRow.hPad
                            text: fieldRow.modelData.label
                            wrapMode: Text.Wrap
                            color: KTheme.text2
                            font.pixelSize: KTheme.note
                            font.weight: Font.Medium
                        }

                        Rectangle {
                            x: fieldRow.colWidth
                            y: 0
                            width: 1
                            height: parent.height
                            color: KTheme.line
                        }

                        Controls.Label {
                            id: valueText
                            x: fieldRow.colWidth + 1 + fieldRow.hPad
                            y: 7
                            width: parent.width - x - fieldRow.hPad
                            text: fieldRow.modelData.value
                            wrapMode: Text.WrapAnywhere
                            color: KTheme.text
                        }

                        Rectangle {
                            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                            height: 1
                            color: KTheme.line
                            opacity: 0.6
                        }
                    }
                }
            }
        }
    }
}
