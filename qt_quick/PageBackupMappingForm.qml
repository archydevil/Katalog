import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Add / edit a safety-copy link (fork, D34 phase R8): the source, the target
// and the options in cards, Save as the one primary button. Fields, validation
// messages and save are unchanged (SpecBackup.md).
ScrollablePageFitted {
    id: root
    property Kirigami.Action escapeAction: escapeCloseAction  // Esc (KBS-F1)
    background: Rectangle { color: KTheme.ground }   // design system ground (D34)

    // -1 = creating a new link; >= 0 = editing the link with this mapping id.
    property int editMappingId: -1
    // Initial values used to pre-fill the form when editing (set via showLayer props).
    property var mappingData: null

    title: editMappingId < 0 ? qsTr("Add Link") : qsTr("Edit Link")

    Component.onCompleted: {
        if (editMappingId >= 0 && mappingData) {
            nameField.text          = mappingData.mappingName
            var ti = typeCombo.indexOfValue(mappingData.mappingType)
            if (ti >= 0) typeCombo.currentIndex = ti
            sourceCombo.selectById(mappingData.sourceDeviceId)
            targetCombo.selectById(mappingData.targetDeviceId)
            strictCopyCheck.checked = mappingData.strictCopy
            var ci = conflictModeCombo.indexOfValue(mappingData.conflictMode)
            if (ci >= 0) conflictModeCombo.currentIndex = ci
            sourceDriveCheck.checked = mappingData.sourceDrive
            includeEmptyDirsCheck.checked = mappingData.includeEmptyDirs
        }
    }

    // Generate a suggested name from source → target names
    function generateName() {
        var srcName = sourceCombo.selectedDeviceName
        var tgtName = targetCombo.selectedDeviceName
        if (srcName && tgtName)
            nameField.text = srcName + " -> " + tgtName
    }

    function save() {
        var strict = (typeCombo.currentValue !== "Archive") && strictCopyCheck.checked
        var err = editMappingId < 0
            ? appManager1.createBackupMapping(
                nameField.text.trim(),
                typeCombo.currentValue,
                sourceCombo.selectedDeviceId,
                targetCombo.selectedDeviceId,
                strict,
                conflictModeCombo.currentValue,
                sourceDriveCheck.checked,
                includeEmptyDirsCheck.checked)
            : appManager1.updateBackupMapping(
                editMappingId,
                nameField.text.trim(),
                typeCombo.currentValue,
                sourceCombo.selectedDeviceId,
                targetCombo.selectedDeviceId,
                strict,
                conflictModeCombo.currentValue,
                sourceDriveCheck.checked,
                includeEmptyDirsCheck.checked)
        if (err) {
            errorMessage.text    = err
            errorMessage.visible = true
        } else {
            closeLayer()
        }
    }

    function closeLayer() {
        pageStack.layers.pop()
    }

    actions: [
        Kirigami.Action {
            text:        qsTr("Save")
            icon.name:   "document-save"
            displayHint: Kirigami.DisplayHint.KeepVisible
            onTriggered: root.save()
        },
        Kirigami.Action {
            id: escapeCloseAction
            text:        qsTr("Cancel")
            icon.name:   "dialog-cancel"
            displayHint: Kirigami.DisplayHint.KeepVisible
            onTriggered: root.closeLayer()
        }
    ]

    ColumnLayout {
        id: formColumn
        spacing: KTheme.gap * 2
        width: parent.width

        readonly property bool narrow: width < Kirigami.Units.gridUnit * 30
        // The label column of the three cards, so their fields line up
        readonly property real labelWidth: Math.max(sourceCatalogLabel.implicitWidth,
                                                    targetCatalogLabel.implicitWidth,
                                                    optionLabels.maxWidth)

        // ── Intro ─────────────────────────────────────────────────────────────
        KSectionHeader {
            level: 1
            title: root.title
            description: qsTr("A link copies or moves the files of a source catalog to a target catalog.")
        }

        // Blocking validation from save, at the top of the form
        Kirigami.InlineMessage {
            id: errorMessage
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            visible: false
            showCloseButton: true
        }

        // ── Source ────────────────────────────────────────────────────────────
        KCard {
            Layout.fillWidth: true
            KSectionHeader {
                level: 3
                title: qsTr("Source")
                description: qsTr("The catalog whose files are copied or moved.")
            }
            GridLayout {
                Layout.fillWidth: true
                columns: formColumn.narrow ? 1 : 2
                columnSpacing: KTheme.gap
                rowSpacing: 4
                Controls.Label {
                    id: sourceCatalogLabel
                    Layout.preferredWidth: formColumn.narrow ? implicitWidth : formColumn.labelWidth
                    text: qsTr("Source catalog")
                    color: KTheme.text2
                    font.weight: Font.Medium
                }
                DeviceTreeComboBox {
                    id: sourceCombo
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 24
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 24  // set width, shrinks only to fit (CBX-C7)
                    Layout.preferredHeight: 40
                    catalogOnly: true
                }
            }
        }

        // ── Target ────────────────────────────────────────────────────────────
        KCard {
            Layout.fillWidth: true
            KSectionHeader {
                level: 3
                title: qsTr("Target")
                description: qsTr("The catalog that receives the files.")
            }
            GridLayout {
                Layout.fillWidth: true
                columns: formColumn.narrow ? 1 : 2
                columnSpacing: KTheme.gap
                rowSpacing: 4
                Controls.Label {
                    id: targetCatalogLabel
                    Layout.preferredWidth: formColumn.narrow ? implicitWidth : formColumn.labelWidth
                    text: qsTr("Target catalog")
                    color: KTheme.text2
                    font.weight: Font.Medium
                }
                DeviceTreeComboBox {
                    id: targetCombo
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 24
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 24  // set width, shrinks only to fit (CBX-C7)
                    Layout.preferredHeight: 40
                    catalogOnly: true
                }
            }
        }

        // ── Options ───────────────────────────────────────────────────────────
        KCard {
            Layout.fillWidth: true
            KSectionHeader {
                level: 3
                title: qsTr("Options")
                description: qsTr("The name of the link, what it does with the files, and what happens on a conflict.")
            }
            GridLayout {
                id: optionLabels
                Layout.fillWidth: true
                columns: formColumn.narrow ? 1 : 2
                columnSpacing: KTheme.gap
                rowSpacing: formColumn.narrow ? 4 : 10

                // Widest label of this card, shared with the other cards
                readonly property real maxWidth: Math.max(nameLabel.implicitWidth, typeLabel.implicitWidth,
                                                          dirsLabel.implicitWidth, strictLabel.implicitWidth,
                                                          conflictLabel.implicitWidth, sourceModeLabel.implicitWidth)

                // Name + auto-generate
                Controls.Label {
                    id: nameLabel
                    Layout.preferredWidth: formColumn.narrow ? implicitWidth : formColumn.labelWidth
                    text: qsTr("Name"); color: KTheme.text2; font.weight: Font.Medium
                }
                RowLayout {
                    // Field + button end level with the combo boxes above (gridUnit*24)
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 24
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                    spacing: 6
                    Controls.TextField {
                        id: nameField
                        placeholderText: qsTr("e.g. Docs -> NAS_Docs")
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        onTextEdited: errorMessage.visible = false
                    }
                    IconButton {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        icon.name: "system-run"
                        Controls.ToolTip.text: qsTr("Auto-generate name")
                        Controls.ToolTip.visible: hovered
                        onClicked: root.generateName()
                    }
                }

                // Type
                Controls.Label {
                    id: typeLabel
                    Layout.preferredWidth: formColumn.narrow ? implicitWidth : formColumn.labelWidth
                    Layout.topMargin: formColumn.narrow ? 6 : 0
                    text: qsTr("Type"); color: KTheme.text2; font.weight: Font.Medium
                }
                ComboBoxFitted {
                    id: typeCombo
                    Layout.preferredHeight: 40
                    textRole:  "text"
                    valueRole: "value"
                    model: [
                        { value: "Backup",  text: qsTr("Backup")  },
                        { value: "Archive", text: qsTr("Archive") }
                    ]
                    onCurrentValueChanged: {
                        // Archive mode disables strict copy
                        strictCopyCheck.enabled = (currentValue !== "Archive")
                        if (currentValue === "Archive") strictCopyCheck.checked = false
                    }
                }

                Controls.Label {
                    id: dirsLabel
                    Layout.preferredWidth: formColumn.narrow ? implicitWidth : formColumn.labelWidth
                    Layout.topMargin: formColumn.narrow ? 6 : 0
                    text: qsTr("Directories"); color: KTheme.text2; font.weight: Font.Medium
                }
                Controls.CheckBox {
                    id: includeEmptyDirsCheck
                    Layout.fillWidth: true
                    text: qsTr("Include empty")
                    checked: true
                }

                Controls.Label {
                    id: strictLabel
                    Layout.preferredWidth: formColumn.narrow ? implicitWidth : formColumn.labelWidth
                    Layout.topMargin: formColumn.narrow ? 6 : 0
                    text: qsTr("Strict copy"); color: KTheme.text2; font.weight: Font.Medium
                }
                Controls.CheckBox {
                    id: strictCopyCheck
                    Layout.fillWidth: true
                    text: qsTr("Mirror folder structure exactly (default)")
                    checked: true
                    Component.onCompleted: contentItem.wrapMode = Text.WordWrap
                }

                Controls.Label {
                    id: conflictLabel
                    Layout.preferredWidth: formColumn.narrow ? implicitWidth : formColumn.labelWidth
                    Layout.topMargin: formColumn.narrow ? 6 : 0
                    text: qsTr("On conflict"); color: KTheme.text2; font.weight: Font.Medium
                }
                ComboBoxFitted {
                    id: conflictModeCombo
                    Layout.preferredHeight: 40
                    textRole:  "text"
                    valueRole: "value"
                    model: [
                        { value: "RenameOldest", text: qsTr("Rename oldest - rename target, copy source") },
                        { value: "Skip",         text: qsTr("Skip - leave target untouched") }
                    ]
                }

                Controls.Label {
                    id: sourceModeLabel
                    Layout.preferredWidth: formColumn.narrow ? implicitWidth : formColumn.labelWidth
                    Layout.topMargin: formColumn.narrow ? 6 : 0
                    text: qsTr("Source mode"); color: KTheme.text2; font.weight: Font.Medium
                }
                Controls.CheckBox {
                    id: sourceDriveCheck
                    Layout.fillWidth: true
                    text: qsTr("Scan source drive directly (requires connected source)")
                    Component.onCompleted: contentItem.wrapMode = Text.WordWrap
                }
            }
        }

        // ── Save / Cancel, as in the page actions ─────────────────────────────
        Flow {
            Layout.fillWidth: true
            Layout.bottomMargin: KTheme.gap
            spacing: 8
            KButton {
                variant: "primary"
                icon.name: "document-save"
                text: qsTr("Save")
                onClicked: root.save()
            }
            KButton {
                variant: "secondary"
                icon.name: "dialog-cancel"
                text: qsTr("Cancel")
                onClicked: root.closeLayer()
            }
        }
    }
}
