import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import QtQuick.Dialogs

ColumnLayout {
    id: pageCreateForm
    spacing: 0

    // Empty-dir confirmation is a Yes/No decision → modal dialog lives in Main.qml
    signal emptyDirConfirmNeeded()

    // Standard form or wizard (fork, SpecMediaIntelligence.md MDI-F1). The wizard
    // is one page: this form's "Catalog definition" section, then the enrichment
    // options, so the values common to both modes are the same fields.
    property string mode: "standard"
    readonly property bool wizardMode:  mode === "wizard"
    readonly property bool showSource:  true
    readonly property bool showOptions: !wizardMode

    function setMode(newMode) {
        if (mode === newMode) return
        mode = newMode
        createValidationMessage.visible = false
        appManager1.setCreateSetting("Mode", newMode)   // restored on next open (MDI-F1)
    }

    function showValidationMessage(message) {
        createValidationMessage.text = message
        createValidationMessage.visible = true
    }

    // Called from Main.qml Create action
    function triggerCreate() {
        createValidationMessage.visible = false
        if (create_lineEdit_NewCatalogName.text.trim() === "") {
            showValidationMessage(qsTr("Provide a name for this new catalog."))
            return
        }
        if (create_lineEdit_NewCatalogPath.text.trim() === "") {
            showValidationMessage(qsTr("Provide a path for this new catalog."))
            return
        }
        if (create_storageComboBox.selectedDeviceId <= 0) {
            showValidationMessage(qsTr("Select a Storage for this new catalog.\n(Selection panel on the left and dropdown list)"))
            return
        }
        if (wizardMode && !validateWizard())
            return
        if (appManager1.isDirectoryEmpty(appManager1.normalizeSourcePath(create_lineEdit_NewCatalogPath.text))) {
            emptyDirConfirmNeeded()
            return
        }
        doCreate()
    }

    // Wizard-only checks (MDI-F6)
    function validateWizard() {
        var source      = appManager1.normalizeSourcePath(create_lineEdit_NewCatalogPath.text)
        var destination = appManager1.normalizeSourcePath(createWizard.destinationPath)
        if (destination === "") {
            showValidationMessage(qsTr("Provide a destination path for the previews and derived data."))
            return false
        }
        if (!appManager1.isPathWritable(destination)) {
            showValidationMessage(qsTr("Select a destination path that can be written to."))
            return false
        }
        if (appManager1.isPathInside(destination, source)) {
            showValidationMessage(qsTr("Select a destination path outside the source path."))
            return false
        }
        if (!createWizard.anyFileType() && createWizard.anyPreview()) {
            showValidationMessage(qsTr("Select at least one file type, or turn off all previews."))
            return false
        }
        return true
    }

    // Called from Main.qml Stop action
    function triggerStop() {
        appManager1.stopCatalogCreation()
    }

    // Called by triggerCreate() and by Main.qml empty-dir confirmation dialog
    function doCreate() {
        var excludes = []
        for (var i = 0; i < pendingExcludesModel.count; i++)
            excludes.push(pendingExcludesModel.get(i).folderPath)

        var name   = create_lineEdit_NewCatalogName.text.trim()
        var source = appManager1.normalizeSourcePath(create_lineEdit_NewCatalogPath.text)
        var error
        if (wizardMode) {
            // Same creation as standard mode, with the options of the standard
            // form (hidden in wizard mode); the profile is saved on success (MDI-F7)
            createWizard.rememberSettings()
            error = appManager1.createCatalogWithEnrichment(
                name, source,
                create_storageComboBox.selectedDeviceId,
                create_comboBox_FileType.currentValue,
                create_checkBox_IncludeSubDir.checked,
                create_comboBox_IncludeHidden.currentIndex === 1,
                false,
                false,
                create_comboBox_Metadata.currentValue,
                create_comboBox_Checksum.currentValue,
                excludes,
                createWizard.buildProfile()
            )
        } else {
            error = appManager1.createCatalog(
                name, source,
                create_storageComboBox.selectedDeviceId,
                create_comboBox_FileType.currentValue,
                create_checkBox_IncludeSubDir.checked,
                create_comboBox_IncludeHidden.currentIndex === 1,
                false,
                false,
                create_comboBox_Metadata.currentValue,
                create_comboBox_Checksum.currentValue,
                excludes
            )
        }
        if (error !== "")
            showValidationMessage(error)
    }

    // ── Per-catalog pending exclude folders ────────────────────────────────────
    ListModel { id: pendingExcludesModel }

    // ── Global exclude directories (live from appManager) ─────────────────────
    property var  globalExcludes: []
    property bool globalParamsExpanded: true

    Connections {
        target: appManager1
        function onExcludeDirectoriesChanged() {
            globalExcludes = appManager1.getExcludeDirectories()
        }
        function onSelectedDeviceChanged() {
            // selectedDevice->loadDevice() runs after the signal is emitted;
            // defer one event-loop tick so the path reflects the new device.
            Qt.callLater(function() {
                create_lineEdit_NewCatalogPath.text = appManager1.normalizeSourcePath(appManager1.selectedDevicePath)
            })
        }
    }

    Component.onCompleted: {
        if (appManager1.getCreateSetting("Mode") === "wizard")
            mode = "wizard"
        createWizard.restoreSettings()
        globalExcludes = appManager1.getExcludeDirectories()
        // The last selected device is restored before QML loads (initiateApp), so no
        // selectedDeviceChanged reaches this page at startup: seed the path here (K2 parity).
        create_lineEdit_NewCatalogPath.text = appManager1.normalizeSourcePath(appManager1.selectedDevicePath)
    }

    // ── Folder dialogs ─────────────────────────────────────────────────────────
    Component {   // a fresh dialog per open (openFreshDialog)
        id: sourcePathDialogComponent
        FolderDialog {
            id: sourcePathDialog
            onAccepted: create_lineEdit_NewCatalogPath.text = appManager1.normalizeSourcePath(appManager1.pathFromFileUrl(selectedFolder.toString()))
        }
    }
    Component {   // a fresh dialog per open (openFreshDialog)
        id: perCatalogExcludeDialogComponent
        FolderDialog {
            id: perCatalogExcludeDialog
            onAccepted: create_lineEdit_NewExcludeFolder.text = appManager1.normalizeSourcePath(appManager1.pathFromFileUrl(selectedFolder.toString()))
        }
    }
    Component {   // a fresh dialog per open (openFreshDialog)
        id: globalExcludeDialogComponent
        FolderDialog {
            id: globalExcludeDialog
            onAccepted: create_lineEdit_FolderToExclude.text = appManager1.normalizeSourcePath(appManager1.pathFromFileUrl(selectedFolder.toString()))
        }
    }

    // ── Mode: Standard / Wizard (MDI-F1) ─────────────────────────────────────
    RowLayout {
        Layout.leftMargin:  Kirigami.Units.gridUnit
        Layout.rightMargin: Kirigami.Units.gridUnit
        Layout.topMargin:   Kirigami.Units.gridUnit
        spacing: Kirigami.Units.smallSpacing
        Controls.ToolButton {
            text: qsTr("Standard")
            checkable: true
            checked: !pageCreateForm.wizardMode
            onClicked: pageCreateForm.setMode("standard")
        }
        Controls.ToolButton {
            text: qsTr("Wizard")
            icon.name: "tools-wizard"
            checkable: true
            checked: pageCreateForm.wizardMode
            onClicked: pageCreateForm.setMode("wizard")
        }
    }

    Kirigami.InlineMessage {
        id: createValidationMessage
        Layout.fillWidth: true
        Layout.leftMargin:  Kirigami.Units.gridUnit
        Layout.rightMargin: Kirigami.Units.gridUnit
        Layout.topMargin:   Kirigami.Units.gridUnit
        type: Kirigami.MessageType.Warning
        showCloseButton: true
        visible: false
    }


    // ════════════════════════════════════════════════════════════════════════════
    // Master GridLayout - 2 columns shared across all sections
    // ════════════════════════════════════════════════════════════════════════════
    GridLayout {
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.gridUnit
        columns: 2
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: Kirigami.Units.smallSpacing

        // ── Section 1: Required ───────────────────────────────────────────────
        RowLayout {
            Layout.columnSpan: 2
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing
            Controls.Label { text: qsTr("Catalog definition"); color: applicationWindow().titleColor }
            Kirigami.Separator { Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter }
        }

        Controls.Label { text: qsTr("Source path"); opacity: 0.7; visible: pageCreateForm.showSource }
        RowLayout {
            visible: pageCreateForm.showSource
            Layout.fillWidth: true
            Controls.TextField {
                id: create_lineEdit_NewCatalogPath
                Layout.fillWidth: true
                placeholderText: qsTr("Path to index")
                onTextChanged: createValidationMessage.visible = false
                onEditingFinished: text = appManager1.normalizeSourcePath(text)
            }
            IconButton {
                icon.name: "folder-open"
                text: qsTr("Select the path")
                onClicked: {
                    var p = create_lineEdit_NewCatalogPath.text.trim()
                    applicationWindow().openFreshDialog(sourcePathDialogComponent, p)
                }
            }
        }

        // ── Connected drives (fork, 2026-10-29) ──────────────────────────
        // Katalog catalogs any drive the system has mounted, NTFS included;
        // this shows the external drives, mounted or not, and asks the system
        // to mount one read-only when it did not.
        Controls.Label {
            text: qsTr("Connected drives")
            opacity: 0.7
            visible: pageCreateForm.showSource
            Layout.alignment: Qt.AlignTop
        }
        ColumnLayout {
            id: drivesBox
            visible: pageCreateForm.showSource
            Layout.fillWidth: true
            spacing: 2
            property var drives: []
            function refresh() { drives = appManager1.connectedDrives() }
            Component.onCompleted: refresh()

            Kirigami.InlineMessage {
                id: driveMessage
                Layout.fillWidth: true
                type: Kirigami.MessageType.Error
                showCloseButton: true
                visible: false
            }
            Repeater {
                model: drivesBox.drives
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Kirigami.Icon {
                        source: "drive-harddisk"
                        implicitWidth: Kirigami.Units.iconSizes.small; implicitHeight: implicitWidth
                        opacity: modelData.mounted ? 1.0 : 0.5
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        // File system names and sizes are technical notation
                        text: (modelData.name || modelData.device) + " · " + modelData.fileSystem
                              + " · " + appManager1.formatDataSize(modelData.size)
                              + (modelData.mounted ? " · " + modelData.mountPoint : " · " + qsTr("Not mounted"))
                        opacity: modelData.mounted ? 1.0 : 0.7
                    }
                    Controls.Button {
                        visible: modelData.mounted
                        icon.name: "go-next"
                        text: qsTr("Source path")
                        onClicked: create_lineEdit_NewCatalogPath.text = appManager1.normalizeSourcePath(modelData.mountPoint)
                    }
                    Controls.Button {
                        visible: !modelData.mounted
                        icon.name: "drive-harddisk"
                        text: qsTr("Mount read-only")
                        onClicked: {
                            driveMessage.visible = false
                            const r = appManager1.mountDriveReadOnly(modelData.device)
                            if (r.ok) {
                                create_lineEdit_NewCatalogPath.text = appManager1.normalizeSourcePath(r.mountPoint)
                            } else {
                                driveMessage.text = modelData.driver !== ""
                                    ? qsTr("%1 could not mount this drive: its license or its permission in System Settings may need attention. Without it, the system reads NTFS drives on its own, and reading is all Katalog needs.").arg(modelData.driver)
                                    : qsTr("The system could not mount this drive: %1").arg(r.reason)
                                driveMessage.visible = true
                            }
                            drivesBox.refresh()
                        }
                    }
                }
            }
            RowLayout {
                Controls.Label {
                    visible: drivesBox.drives.length === 0
                    text: "—"
                    opacity: 0.6
                }
                IconButton {
                    icon.name: "view-refresh"
                    text: qsTr("Refresh")
                    flat: true
                    onClicked: { driveMessage.visible = false; drivesBox.refresh() }
                }
            }
        }

        Controls.Label { text: qsTr("Storage"); opacity: 0.7; visible: pageCreateForm.showSource }
        DeviceTreeComboBox {
            visible: pageCreateForm.showSource
            id: create_storageComboBox
            storageOnly: true
            hideCatalogs: true
            Layout.fillWidth: true
            Layout.maximumWidth: Number.POSITIVE_INFINITY  // stretches with the form (CBX-C7)
        }

        Controls.Label { text: qsTr("Catalog name"); opacity: 0.7; visible: pageCreateForm.showSource }
        RowLayout {
            visible: pageCreateForm.showSource
            Layout.fillWidth: true
            Controls.TextField {
                id: create_lineEdit_NewCatalogName
                Layout.fillWidth: true
                placeholderText: qsTr("New catalog name")
                onTextChanged: createValidationMessage.visible = false
            }
            IconButton {
                icon.name: "tools-wizard"
                Controls.ToolTip.text: qsTr("Generate name from path")
                Controls.ToolTip.visible: hovered
                onClicked: {
                    var generated = appManager1.normalizeSourcePath(create_lineEdit_NewCatalogPath.text)
                    generated = generated.replace(/\//g, "_").replace(/:/g, "_")
                    create_lineEdit_NewCatalogName.text = generated
                }
            }
        }

        // ── Section 2: Catalog Options ────────────────────────────────────────
        RowLayout {
            visible: pageCreateForm.showOptions
            Layout.columnSpan: 2
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing
            Controls.Label { text: qsTr("Content options"); color: applicationWindow().titleColor }
            Kirigami.Separator { Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter }
        }

        Controls.Label { text: qsTr("File type"); opacity: 0.7; visible: pageCreateForm.showOptions }
        ComboBoxFitted {
            visible: pageCreateForm.showOptions
            id: create_comboBox_FileType
            Layout.fillWidth: true
            Layout.maximumWidth: Number.POSITIVE_INFINITY  // stretches with the form (CBX-C7)
            rowLeadingWidth: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing * 2  // row icon
            textRole: "text"
            valueRole: "value"
            displayText: ""
            model: [
                { text: qsTr("All"),   value: "All",   iconName: "folder"                  },
                { text: qsTr("Audio"), value: "Audio", iconName: "audio-x-mpeg"             },
                { text: qsTr("Image"), value: "Image", iconName: "image-jpeg"               },
                { text: qsTr("Text"),  value: "Text",  iconName: "view-list-text"           },
                { text: qsTr("Video"), value: "Video", iconName: "video-mp4"                },
                { text: qsTr("Other"), value: "Other", iconName: "document-open"            },
                { text: qsTr("None"),  value: "None",  iconName: "application-x-zerosize"  }
            ]
            currentIndex: 0
            delegate: Controls.ItemDelegate {
                width: ListView.view ? ListView.view.width : create_comboBox_FileType.width  // fills the list, which may be wider than the box
                text: modelData.text
                icon.name: modelData.iconName
                highlighted: create_comboBox_FileType.highlightedIndex === index
            }
            contentItem: RowLayout {
                function positionToRectangle(pos) { return Qt.rect(0, 0, 0, 0) }
                property int selectionStart: 0
                spacing: Kirigami.Units.smallSpacing
                Item { width: Kirigami.Units.smallSpacing }
                Kirigami.Icon {
                    source: create_comboBox_FileType.currentIndex >= 0
                            ? create_comboBox_FileType.model[create_comboBox_FileType.currentIndex].iconName : ""
                    implicitWidth:  Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }
                Controls.Label {
                    text: create_comboBox_FileType.currentIndex >= 0
                          ? create_comboBox_FileType.model[create_comboBox_FileType.currentIndex].text : ""
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        Controls.Label { text: qsTr("Include subdirectories"); opacity: 0.7; visible: pageCreateForm.showOptions }
        Controls.CheckBox {
            visible: pageCreateForm.showOptions
            id: create_checkBox_IncludeSubDir
            checked: true
        }

        Controls.Label { text: qsTr("Include hidden files"); opacity: 0.7; visible: pageCreateForm.showOptions }
        ComboBoxFitted {
            visible: pageCreateForm.showOptions
            id: create_comboBox_IncludeHidden
            Layout.fillWidth: true
            Layout.maximumWidth: Number.POSITIVE_INFINITY  // stretches with the form (CBX-C7)
            model: [ qsTr("None"), qsTr("All") ]
            currentIndex: 0
        }

        Controls.Label { text: qsTr("Include metadata"); opacity: 0.7; visible: pageCreateForm.showOptions }
        ComboBoxFitted {
            visible: pageCreateForm.showOptions
            id: create_comboBox_Metadata
            Layout.fillWidth: true
            Layout.maximumWidth: Number.POSITIVE_INFINITY  // stretches with the form (CBX-C7)
            textRole: "text"
            valueRole: "value"
            model: [
                { text: qsTr("None"),           value: "None"          },
                { text: qsTr("Media Basic"),     value: "MediaBasic"    },
                { text: qsTr("Media Extended"),  value: "MediaExtended" },
                { text: qsTr("Full Extended"),   value: "FullExtended"  }
            ]
            currentIndex: 0
        }

        Controls.Label { text: qsTr("Include checksum"); opacity: 0.7; visible: pageCreateForm.showOptions }
        ComboBoxFitted {
            visible: pageCreateForm.showOptions
            id: create_comboBox_Checksum
            Layout.fillWidth: true
            Layout.maximumWidth: Number.POSITIVE_INFINITY  // stretches with the form (CBX-C7)
            textRole: "text"
            valueRole: "value"
            model: [
                { text: qsTr("None"),  value: "None"   },
                { text: "SHA-256",     value: "SHA256" }
            ]
            currentIndex: 0
        }

        //Folder exclusion (catalog only)
        Controls.Label {
            visible: pageCreateForm.showOptions
            text: qsTr("Exclude folders or files")
            Layout.columnSpan: 2
            Layout.topMargin: Kirigami.Units.smallSpacing
        }
        Controls.Label { text: qsTr("Path or name to exclude"); opacity: 0.7; visible: pageCreateForm.showOptions }
        RowLayout {
            visible: pageCreateForm.showOptions
            Layout.fillWidth: true
            Controls.TextField {
                id: create_lineEdit_NewExcludeFolder
                Layout.fillWidth: true
                placeholderText: qsTr("Path or name to exclude")
                onEditingFinished: text = appManager1.normalizeSourcePath(text)
            }
            IconButton {
                icon.name: "folder-open"
                text: qsTr("Select the path")
                onClicked: {
                    var p = create_lineEdit_NewCatalogPath.text.trim()
                    applicationWindow().openFreshDialog(perCatalogExcludeDialogComponent, p)
                }
            }
            Controls.Button {
                icon.name: "list-add"
                text: qsTr("Add")
                onClicked: {
                    var p = appManager1.normalizeSourcePath(create_lineEdit_NewExcludeFolder.text)
                    if (p === "") return
                    for (var i = 0; i < pendingExcludesModel.count; i++)
                        if (pendingExcludesModel.get(i).folderPath === p) return
                    pendingExcludesModel.append({ folderPath: p })
                    create_lineEdit_NewExcludeFolder.text = ""
                }
            }
        }

        // Pending exclude folders list
        Item { Layout.preferredWidth: 1; visible: pageCreateForm.showOptions }
        ColumnLayout {
            visible: pageCreateForm.showOptions
            Layout.fillWidth: true
            spacing: 2
            Repeater {
                model: pendingExcludesModel
                delegate: RowLayout {
                    Layout.fillWidth: true
                    Controls.Label {
                        text: folderPath
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    IconButton {
                        icon.name: "edit-delete"
                        text: qsTr("Remove")
                        flat: true
                        onClicked: pendingExcludesModel.remove(index)
                    }
                }
            }
        }


        // Global Parameters (collapsible) ────────────────────────
        RowLayout {
            visible: pageCreateForm.showOptions
            Layout.columnSpan: 2
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing

            Controls.Label {
                text: qsTr("Global Parameters")
                color: applicationWindow().titleColor
            }
            Kirigami.Separator { Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter }
            IconButton {
                id: create_button_ToggleGlobalParams
                text: globalParamsExpanded ? qsTr("Collapse") : qsTr("Expand")
                icon.name: globalParamsExpanded ? "go-up" : "go-down"
                flat: true
                onClicked: globalParamsExpanded = !globalParamsExpanded
            }
        }

        Controls.Label {
            text: qsTr("Global exclude directory")
            opacity: 0.7
            visible: pageCreateForm.showOptions && (globalParamsExpanded)
        }
        RowLayout {
            Layout.fillWidth: true
            visible: pageCreateForm.showOptions && (globalParamsExpanded)
            Controls.TextField {
                id: create_lineEdit_FolderToExclude
                Layout.fillWidth: true
                placeholderText: qsTr("Path to exclude globally")
                onEditingFinished: text = appManager1.normalizeSourcePath(text)
            }
            IconButton {
                icon.name: "folder-open"
                text: qsTr("Select the path")
                onClicked: {
                    var p = create_lineEdit_NewCatalogPath.text.trim()
                    applicationWindow().openFreshDialog(globalExcludeDialogComponent, p)
                }
            }
            Controls.Button {
                icon.name: "list-add"
                text: qsTr("Add")
                onClicked: {
                    var p = appManager1.normalizeSourcePath(create_lineEdit_FolderToExclude.text)
                    if (p !== "") {
                        appManager1.addExcludeDirectory(p)
                        create_lineEdit_FolderToExclude.text = ""
                    }
                }
            }
        }

        // Global excludes list
        Item {
            visible: pageCreateForm.showOptions && (globalParamsExpanded && globalExcludes.length > 0)
            Layout.preferredWidth: 1
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: pageCreateForm.showOptions && (globalParamsExpanded && globalExcludes.length > 0)
            spacing: 2
            Repeater {
                model: globalExcludes
                delegate: RowLayout {
                    Layout.fillWidth: true
                    Controls.Label {
                        text: modelData
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    IconButton {
                        icon.name: "edit-delete"
                        text: qsTr("Remove")
                        flat: true
                        onClicked: appManager1.removeExcludeDirectory(modelData)
                    }
                }
            }
        }

        // Bottom spacer
        Item { Layout.columnSpan: 2; Layout.preferredHeight: Kirigami.Units.gridUnit; visible: pageCreateForm.showOptions }
    }

    // ── Wizard: enrichment options and Create ─────────────────────────────────
    CreateWizard {
        id: createWizard
        visible: pageCreateForm.wizardMode
        Layout.fillWidth: true
        Layout.leftMargin:  Kirigami.Units.gridUnit
        Layout.rightMargin: Kirigami.Units.gridUnit
        onChoiceEdited: createValidationMessage.visible = false
    }

    RowLayout {
        visible: pageCreateForm.wizardMode
        Layout.fillWidth: true
        Layout.leftMargin:   Kirigami.Units.gridUnit
        Layout.rightMargin:  Kirigami.Units.gridUnit
        Layout.topMargin:    Kirigami.Units.largeSpacing
        Layout.bottomMargin: Kirigami.Units.gridUnit
        Item { Layout.fillWidth: true }
        Controls.Button {
            text: qsTr("Create")
            icon.name: "document-save"
            onClicked: pageCreateForm.triggerCreate()
        }
    }
}
