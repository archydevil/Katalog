import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import QtQuick.Dialogs

// Labels (fork, D34 phase R8; K2 "Tags"): a label is put on a folder path and
// then used by Search. Two cards: label a folder, and the labelled folders.
// Behaviour unchanged: same validation (SpecValidationRules T3), same calls.
ColumnLayout {
    id: root
    spacing: KTheme.gap * 2

    property var    tagEntries: []
    property var    tagNames:   []
    property string selectedTagFilter: ""

    readonly property bool narrow: width < Kirigami.Units.gridUnit * 30

    function reload() {
        tagNames   = appManager1.getTagNames()
        tagEntries = appManager1.getTagEntries(root.selectedTagFilter)
    }

    Connections {
        target: appManager1
        function onTagsChanged() { root.reload() }
    }

    Component.onCompleted: reload()

    // Pre-filled by "Tag this folder" in Explore (EXP-F22)
    function setFolderPath(folderPath) {
        folderPathField.text = folderPath
    }

    Component {   // a fresh dialog per open (openFreshDialog)
        id: tagFolderDialogComponent
        FolderDialog {
            id: tagFolderDialog
            onAccepted: folderPathField.text = appManager1.pathFromFileUrl(selectedFolder.toString())
        }
    }

    // ── Intro ─────────────────────────────────────────────────────────────────
    KSectionHeader {
        level: 1
        title: qsTr("Labels")
        description: qsTr("A label marks folders, wherever they are, so a search can find just their files.")
    }

    // ── Label a folder ────────────────────────────────────────────────────────
    KCard {
        Layout.fillWidth: true

        KSectionHeader {
            level: 3
            title: qsTr("Label a folder")
            description: qsTr("The folder gets a label already in use, or a new one typed here.")
        }

        // Blocking validation at the top of the form (SpecValidationRules)
        Kirigami.InlineMessage {
            id: tagValidationMessage
            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            showCloseButton: true
            visible: false
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.narrow ? 1 : 2
            columnSpacing: KTheme.gap
            rowSpacing: root.narrow ? 4 : 10

            Controls.Label {
                text: qsTr("Folder")
                color: KTheme.text2
                font.weight: Font.Medium
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Controls.TextField {
                    id: folderPathField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    placeholderText: qsTr("Folder path")
                    font.family: KTheme.mono
                    onTextChanged: tagValidationMessage.visible = false
                }
                IconButton {
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    icon.name: "folder-open"
                    text: qsTr("Select a folder")
                    onClicked: {
                        var p = folderPathField.text.trim()
                        applicationWindow().openFreshDialog(tagFolderDialogComponent, p)
                    }
                }
            }

            Controls.Label {
                Layout.topMargin: root.narrow ? 6 : 0
                text: qsTr("Label")
                color: KTheme.text2
                font.weight: Font.Medium
            }
            ComboBoxFitted {
                id: tagNameCombo
                Layout.fillWidth: true
                Layout.maximumWidth: Number.POSITIVE_INFINITY  // stretches with the form (CBX-C7)
                Layout.preferredHeight: 40
                editable: true
                model: root.tagNames
                onEditTextChanged: tagValidationMessage.visible = false
            }
        }

        KButton {
            Layout.alignment: Qt.AlignRight
            variant: "primary"
            icon.name: "tag"
            text: qsTr("Label the folder")
            onClicked: {
                var folder = folderPathField.text.trim()
                var name   = tagNameCombo.editText.trim()
                if (folder === "") {
                    tagValidationMessage.text = qsTr("Select or enter a folder to label.")
                    tagValidationMessage.visible = true
                    return
                }
                if (name === "") {
                    tagValidationMessage.text = qsTr("Select or enter a label name.")
                    tagValidationMessage.visible = true
                    return
                }
                // Same folder, same label: refused (SpecValidationRules T3)
                var existing = appManager1.getTagEntries(name)
                for (var i = 0; i < existing.length; i++) {
                    if (existing[i].folder === folder) {
                        tagValidationMessage.text = qsTr("Folder already has the label %1.").arg(name)
                        tagValidationMessage.visible = true
                        return
                    }
                }
                appManager1.createTag(name, folder)
                folderPathField.text = ""
                tagNameCombo.currentIndex = -1
            }
        }
    }

    // ── Labelled folders ──────────────────────────────────────────────────────
    KCard {
        Layout.fillWidth: true

        GridLayout {
            Layout.fillWidth: true
            columns: root.narrow ? 1 : 2
            columnSpacing: KTheme.gap
            rowSpacing: 8

            KSectionHeader {
                level: 3
                title: qsTr("Labelled folders")
                description: qsTr("Removing a label leaves the folder and its files untouched.")
            }
            // Label filter (PGL-F7): one label, or all of them
            RowLayout {
                Layout.alignment: root.narrow ? Qt.AlignLeft : (Qt.AlignRight | Qt.AlignTop)
                spacing: 8
                Controls.Label {
                    text: qsTr("Filter")
                    color: KTheme.text2
                    font.weight: Font.Medium
                }
                ComboBoxFitted {
                    id: filterCombo
                    Layout.preferredHeight: 40
                    model: [qsTr("All")].concat(root.tagNames)
                    onActivated: {
                        root.selectedTagFilter = currentIndex === 0 ? "" : currentText
                        root.reload()
                    }
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            visible: root.tagEntries.length > 0
            spacing: 6
            KInfoTag { text: qsTr("Folders: %1").arg(root.tagEntries.length) }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Repeater {
                model: root.tagEntries
                delegate: ColumnLayout {
                    id: entryRow
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 0

                    Rectangle {
                        visible: entryRow.index > 0
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: KTheme.line
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        Layout.bottomMargin: 6
                        spacing: 10
                        Kirigami.Icon {
                            source: "folder"
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            Layout.alignment: Qt.AlignVCenter
                        }
                        // A path is never elided in a card (CDT-C1): it wraps
                        Controls.Label {
                            Layout.fillWidth: true
                            text: entryRow.modelData.folder
                            wrapMode: Text.WrapAnywhere
                            font.family: KTheme.mono
                            font.pixelSize: KTheme.note
                            color: KTheme.text
                        }
                        KInfoTag {
                            Layout.alignment: Qt.AlignVCenter
                            text: entryRow.modelData.name
                            tone: "info"
                        }
                        IconButton {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            icon.name: "edit-delete"
                            text: qsTr("Remove this label")
                            flat: true
                            onClicked: {
                                removeLabelDialog.tagId  = entryRow.modelData.tagId
                                removeLabelDialog.label  = entryRow.modelData.name
                                removeLabelDialog.folder = entryRow.modelData.folder
                                removeLabelDialog.open()
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                visible: root.tagEntries.length === 0
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 4
                Controls.Label {
                    Layout.fillWidth: true
                    text: qsTr("No labels yet")
                    font.weight: Font.DemiBold
                    color: KTheme.text
                }
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: KTheme.text2
                    text: qsTr("A labelled folder shows here, with its label.")
                }
            }
        }
    }

    Item { Layout.fillHeight: true }

    // Removing a label asks first, and says what stays
    Kirigami.PromptDialog {
        id: removeLabelDialog
        property var    tagId
        property string label:  ""
        property string folder: ""
        background: Rectangle { color: KTheme.surface; radius: KTheme.radiusCard; border.width: 1; border.color: KTheme.line }
        padding: Kirigami.Units.largeSpacing * 2
        title: qsTr("Remove this label?")
        subtitle: qsTr("The label %1 is taken off %2: a search by this label no longer finds its files.")
                      .arg(removeLabelDialog.label).arg(removeLabelDialog.folder)
                  + " " + qsTr("Removing a label leaves the folder and its files untouched.")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: qsTranslate("Main", "Remove")
                icon.name: "edit-delete"
                onTriggered: {
                    appManager1.deleteTag(removeLabelDialog.tagId)
                    removeLabelDialog.close()
                }
            },
            Kirigami.Action {
                text: qsTranslate("Main", "Cancel")
                icon.name: "dialog-cancel"
                onTriggered: removeLabelDialog.close()
            }
        ]
    }
}
