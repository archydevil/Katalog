import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import QtQuick.Dialogs

// "Add a drive" (fork, D34, phase R6): a guided flow in cards — what to
// catalogue, where it goes, then what is read or analysed — with one primary
// button to create. Validation stays an InlineMessage at the top of the form.
ColumnLayout {
    id: pageCreateForm
    spacing: KTheme.gap

    // Labels sit above their fields when the page is narrow
    readonly property bool narrow: width < 560

    // ── Building blocks of this page ───────────────────────────────────────
    // A step of the flow: its number, its title and what it is for
    component StepHeader: RowLayout {
        id: stepHeader
        property int number: 0
        property string title
        property string description
        Layout.fillWidth: true
        spacing: 12
        Rectangle {
            visible: stepHeader.number > 0
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            Layout.alignment: Qt.AlignTop
            radius: 14
            color: KTheme.accentSoft
            Controls.Label {
                anchors.centerIn: parent
                text: stepHeader.number
                color: KTheme.accentText
                font.pixelSize: KTheme.body
                font.weight: Font.Bold
            }
        }
        KSectionHeader {
            level: 3
            title: stepHeader.title
            description: stepHeader.description
        }
    }
    // A field's name, on its left, or above it when the page is narrow
    component FieldLabel: Controls.Label {
        Layout.preferredWidth: 160
        Layout.alignment: Qt.AlignVCenter
        wrapMode: Text.WordWrap
        color: KTheme.text2
        font.pixelSize: KTheme.body
        font.weight: Font.Medium
    }
    // One half of the Standard / Wizard switch
    component ModeButton: Controls.AbstractButton {
        id: modeButton
        property string iconName
        hoverEnabled: true
        implicitHeight: 40
        implicitWidth: modeRow.implicitWidth + leftPadding + rightPadding
        leftPadding: 14
        rightPadding: 16
        Accessible.role: Accessible.RadioButton
        Accessible.name: text
        Accessible.checked: checked
        background: Rectangle {
            radius: KTheme.radiusItem
            color: modeButton.checked ? KTheme.surface : modeButton.hovered ? KTheme.hover : "transparent"
            border.width: modeButton.checked || modeButton.visualFocus ? 1 : 0
            border.color: modeButton.visualFocus ? KTheme.accent : KTheme.line
        }
        contentItem: Item {
            implicitWidth: modeRow.implicitWidth
            implicitHeight: modeRow.implicitHeight
            Row {
                id: modeRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Kirigami.Icon {
                    source: modeButton.iconName
                    isMask: true
                    color: modeButton.checked ? KTheme.accentText : KTheme.text2
                    width: 16
                    height: 16
                    anchors.verticalCenter: parent.verticalCenter
                }
                Controls.Label {
                    text: modeButton.text
                    color: modeButton.checked ? KTheme.text : KTheme.text2
                    font.pixelSize: KTheme.body
                    font.weight: modeButton.checked ? Font.DemiBold : Font.Normal
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

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

    // The message is brought into view and, when it is about one field, that
    // field takes the focus, so the problem is found by eye, by keyboard and
    // by screen reader (WCAG 3.3.1)
    function showValidationMessage(message, field) {
        createValidationMessage.text = message
        createValidationMessage.visible = true
        if (field)
            field.forceActiveFocus(Qt.OtherFocusReason)
        // Said by the screen reader too: the focus lands on the field, not on
        // the message (Accessible.announce, Qt 6.8 and later)
        try { createValidationMessage.Accessible.announce(message) } catch (e) {}
        // After the layout has made room for the message
        Qt.callLater(_revealValidationMessage)
    }
    function _revealValidationMessage() {
        // The page's Flickable, found up the parent chain
        for (var p = pageCreateForm.parent; p; p = p.parent) {
            if (p.contentY !== undefined && p.contentItem !== undefined && typeof p.returnToBounds === "function") {
                var top = createValidationMessage.mapToItem(p.contentItem, 0, 0).y - KTheme.gap
                if (top < p.contentY)
                    p.contentY = Math.max(p.originY - p.topMargin, top)
                return
            }
        }
    }

    // Called from Main.qml Create action
    function triggerCreate() {
        createValidationMessage.visible = false
        if (create_lineEdit_NewCatalogName.text.trim() === "") {
            showValidationMessage(qsTr("Provide a name for this new catalog."), create_lineEdit_NewCatalogName)
            return
        }
        if (create_lineEdit_NewCatalogPath.text.trim() === "") {
            showValidationMessage(qsTr("Provide a path for this new catalog."), create_lineEdit_NewCatalogPath)
            return
        }
        if (create_storageComboBox.selectedDeviceId <= 0) {
            showValidationMessage(qsTr("Select a Storage for this new catalog.\n(Selection panel on the left and dropdown list)"), create_storageComboBox)
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

    // ── Header: what this page does, and the mode (MDI-F1) ───────────────────
    GridLayout {
        Layout.fillWidth: true
        columns: pageCreateForm.width > 720 ? 2 : 1
        columnSpacing: KTheme.gap
        rowSpacing: 12

        KSectionHeader {
            level: 1
            title: qsTr("Add a drive")
            description: qsTr("Catalogue a drive or a folder")
        }

        // Standard form or wizard: a two-way switch, with a line saying what
        // the chosen mode does
        ColumnLayout {
            Layout.alignment: pageCreateForm.width > 720 ? (Qt.AlignRight | Qt.AlignTop) : Qt.AlignLeft
            spacing: 6
            Rectangle {
                implicitWidth: modeButtons.implicitWidth + 8
                implicitHeight: modeButtons.implicitHeight + 8
                radius: KTheme.radiusControl
                color: KTheme.neutralSoft
                Row {
                    id: modeButtons
                    anchors.centerIn: parent
                    spacing: 4
                    ModeButton {
                        text: qsTr("Standard")
                        iconName: "view-list-details"
                        checked: !pageCreateForm.wizardMode
                        onClicked: pageCreateForm.setMode("standard")
                    }
                    ModeButton {
                        text: qsTr("Wizard")
                        iconName: "tools-wizard"
                        checked: pageCreateForm.wizardMode
                        onClicked: pageCreateForm.setMode("wizard")
                    }
                }
            }
            Controls.Label {
                Layout.fillWidth: true
                Layout.maximumWidth: 380
                wrapMode: Text.WordWrap
                color: KTheme.text2
                font.pixelSize: KTheme.note
                text: pageCreateForm.wizardMode
                      ? qsTr("The catalog, then previews and AI analysis on this computer.")
                      : qsTr("The catalog only: the files, their details and the content options.")
            }
        }
    }

    Kirigami.InlineMessage {
        id: createValidationMessage
        Layout.fillWidth: true
        type: Kirigami.MessageType.Warning
        showCloseButton: true
        visible: false
    }

    // ── Step 1: what to catalogue ─────────────────────────────────────────────
    KCard {
        Layout.fillWidth: true
        visible: pageCreateForm.showSource

        StepHeader {
            number: 1
            title: qsTr("What to catalogue")
            description: qsTr("Teca reads the drive, NTFS too, without changing it.")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: pageCreateForm.narrow ? 1 : 2
            columnSpacing: KTheme.gap
            rowSpacing: 10

            FieldLabel { text: qsTr("Source path") }
            RowLayout {
                Layout.fillWidth: true
                Controls.TextField {
                    id: create_lineEdit_NewCatalogPath
                    Layout.fillWidth: true
                    font.family: KTheme.mono
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
        }

        // ── Connected drives (fork, 2026-10-29) ──────────────────────────
        // Katalog catalogs any drive the system has mounted, NTFS included;
        // this shows the external drives, mounted or not, as drive cards, and
        // asks the system to mount one read-only when it did not.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            Controls.Label {
                Layout.fillWidth: true
                text: qsTr("Connected drives")
                color: KTheme.text
                font.pixelSize: KTheme.body
                font.weight: Font.DemiBold
            }
            KButton {
                variant: "link"
                icon.name: "view-refresh"
                text: qsTr("Refresh")
                onClicked: { driveMessage.visible = false; drivesBox.refresh() }
            }
        }

        ColumnLayout {
            id: drivesBox
            Layout.fillWidth: true
            spacing: 10
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

            Controls.Label {
                Layout.fillWidth: true
                visible: drivesBox.drives.length === 0
                wrapMode: Text.WordWrap
                color: KTheme.text3
                text: qsTr("No external drive is connected.")
            }

            GridLayout {
                Layout.fillWidth: true
                visible: drivesBox.drives.length > 0
                columns: Math.max(1, Math.floor(width / 300))
                columnSpacing: 10
                rowSpacing: 10

                Repeater {
                    model: drivesBox.drives
                    // A drive card, in the Home drive-card language. The drive
                    // that is the source path takes the accent and a check mark.
                    delegate: Rectangle {
                        id: driveCard
                        required property var modelData
                        readonly property bool isSource: modelData.mounted
                            && create_lineEdit_NewCatalogPath.text.trim() !== ""
                            && appManager1.normalizeSourcePath(modelData.mountPoint)
                               === appManager1.normalizeSourcePath(create_lineEdit_NewCatalogPath.text)
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        implicitHeight: driveColumn.implicitHeight + 28
                        radius: KTheme.radiusControl
                        color: isSource ? KTheme.accentSoft : KTheme.surface2
                        border.color: isSource ? KTheme.accent : KTheme.line
                        border.width: isSource ? 2 : 1

                        ColumnLayout {
                            id: driveColumn
                            x: 14
                            y: 14
                            width: parent.width - 28
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                Rectangle {
                                    Layout.preferredWidth: 40
                                    Layout.preferredHeight: 40
                                    Layout.alignment: Qt.AlignTop
                                    radius: KTheme.radiusControl
                                    color: driveCard.modelData.mounted ? KTheme.accentSoft : KTheme.neutralSoft
                                    Kirigami.Icon {
                                        anchors.centerIn: parent
                                        width: 22; height: 22
                                        source: "drive-harddisk"
                                        opacity: driveCard.modelData.mounted ? 1.0 : 0.5
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Controls.Label {
                                        Layout.fillWidth: true
                                        text: driveCard.modelData.name || driveCard.modelData.device
                                        wrapMode: Text.Wrap
                                        color: KTheme.text
                                        font.pixelSize: KTheme.titleCard
                                        font.weight: Font.Bold
                                    }
                                    Controls.Label {
                                        Layout.fillWidth: true
                                        // Paths and device names are technical notation
                                        text: driveCard.modelData.mounted ? driveCard.modelData.mountPoint
                                                                          : driveCard.modelData.device
                                        wrapMode: Text.WrapAnywhere
                                        font.family: KTheme.mono
                                        font.pixelSize: KTheme.note
                                        color: KTheme.text3
                                    }
                                }
                                Kirigami.Icon {
                                    visible: driveCard.isSource
                                    source: "checkmark"
                                    isMask: true
                                    color: KTheme.accentText
                                    Layout.preferredWidth: 18
                                    Layout.preferredHeight: 18
                                    Layout.alignment: Qt.AlignTop
                                }
                            }

                            // State and kind; file system names and sizes are
                            // technical notation
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                KInfoTag {
                                    text: driveCard.modelData.mounted ? qsTr("Mounted") : qsTr("Not mounted")
                                    tone: driveCard.modelData.mounted ? "ok" : "warn"
                                }
                                KInfoTag {
                                    visible: driveCard.modelData.readOnly === true
                                    text: qsTr("Read-only")
                                    tone: "info"
                                }
                                KInfoTag {
                                    visible: text.length > 0
                                    text: driveCard.modelData.fileSystem || ""
                                }
                                KInfoTag {
                                    visible: text.length > 0
                                    text: appManager1.formatDataSize(driveCard.modelData.size)
                                }
                                KInfoTag {
                                    visible: text.length > 0
                                    text: driveCard.modelData.driver || ""
                                }
                                KInfoTag {
                                    visible: text.length > 0
                                    text: driveCard.modelData.protocol || ""
                                }
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                KButton {
                                    visible: driveCard.modelData.mounted
                                    variant: "tonal"
                                    icon.name: "go-next"
                                    text: qsTr("Use this drive")
                                    onClicked: create_lineEdit_NewCatalogPath.text = appManager1.normalizeSourcePath(driveCard.modelData.mountPoint)
                                }
                                KButton {
                                    visible: !driveCard.modelData.mounted
                                    variant: "secondary"
                                    icon.name: "drive-harddisk"
                                    text: qsTr("Mount read-only")
                                    onClicked: {
                                        driveMessage.visible = false
                                        const r = appManager1.mountDriveReadOnly(driveCard.modelData.device)
                                        if (r.ok) {
                                            create_lineEdit_NewCatalogPath.text = appManager1.normalizeSourcePath(r.mountPoint)
                                        } else {
                                            driveMessage.text = driveCard.modelData.driver !== ""
                                                ? qsTr("%1 could not mount this drive: its license or its permission in System Settings may need attention. Without it, the system reads NTFS drives on its own, and reading is all Teca needs.").arg(driveCard.modelData.driver)
                                                : qsTr("The system could not mount this drive: %1").arg(r.reason)
                                            driveMessage.visible = true
                                        }
                                        drivesBox.refresh()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Step 2: where it goes ─────────────────────────────────────────────────
    KCard {
        Layout.fillWidth: true
        visible: pageCreateForm.showSource

        StepHeader {
            number: 2
            title: qsTr("Where it goes")
            description: qsTr("The storage it belongs to in the archive, and the name of its catalog.")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: pageCreateForm.narrow ? 1 : 2
            columnSpacing: KTheme.gap
            rowSpacing: 10

            FieldLabel { text: qsTr("Storage") }
            DeviceTreeComboBox {
                visible: pageCreateForm.showSource
                id: create_storageComboBox
                storageOnly: true
                hideCatalogs: true
                Layout.fillWidth: true
                Layout.maximumWidth: Number.POSITIVE_INFINITY  // stretches with the form (CBX-C7)
            }

            FieldLabel { text: qsTr("Catalog name") }
            RowLayout {
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
        }
    }

    // ── Step 3 (Standard): content options ────────────────────────────────────
    KCard {
        Layout.fillWidth: true
        visible: pageCreateForm.showOptions

        StepHeader {
            number: 3
            title: qsTr("Content options")
            description: qsTr("Which files are listed and what is read from each one.")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: pageCreateForm.narrow ? 1 : 2
            columnSpacing: KTheme.gap
            rowSpacing: 10

            FieldLabel { text: qsTr("File type") }
            ComboBoxFitted {
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
                    Item { implicitWidth: Kirigami.Units.smallSpacing }
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

            FieldLabel { text: qsTr("Include subdirectories") }
            Controls.CheckBox {
                id: create_checkBox_IncludeSubDir
                checked: true
            }

            FieldLabel { text: qsTr("Include hidden files") }
            ComboBoxFitted {
                id: create_comboBox_IncludeHidden
                Layout.fillWidth: true
                Layout.maximumWidth: Number.POSITIVE_INFINITY  // stretches with the form (CBX-C7)
                model: [ qsTr("None"), qsTr("All") ]
                currentIndex: 0
            }

            FieldLabel { text: qsTr("Include metadata") }
            ComboBoxFitted {
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

            FieldLabel { text: qsTr("Include checksum") }
            ComboBoxFitted {
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
        }

        // Folder exclusion (catalog only)
        Controls.Label {
            Layout.fillWidth: true
            Layout.topMargin: 6
            text: qsTr("Exclude folders or files")
            color: KTheme.text
            font.pixelSize: KTheme.body
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
        GridLayout {
            Layout.fillWidth: true
            columns: pageCreateForm.narrow ? 1 : 2
            columnSpacing: KTheme.gap
            rowSpacing: 10

            FieldLabel { text: qsTr("Path or name to exclude") }
            RowLayout {
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
                KButton {
                    variant: "secondary"
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
        }

        // Pending exclude folders list
        ColumnLayout {
            Layout.fillWidth: true
            visible: pendingExcludesModel.count > 0
            spacing: 4
            Repeater {
                model: pendingExcludesModel
                delegate: Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: pendingRow.implicitHeight + 4
                    radius: KTheme.radiusItem
                    color: KTheme.surface2
                    RowLayout {
                        id: pendingRow
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 12; rightMargin: 2 }
                        Controls.Label {
                            text: folderPath
                            wrapMode: Text.WrapAnywhere
                            font.family: KTheme.mono
                            font.pixelSize: KTheme.note
                            color: KTheme.text
                            Layout.fillWidth: true
                        }
                        IconButton {
                            icon.name: "edit-delete"
                            text: qsTr("Remove")
                            flat: true
                            implicitWidth: 40
                            implicitHeight: 40
                            onClicked: pendingExcludesModel.remove(index)
                        }
                    }
                }
            }
        }
    }

    // ── Global Parameters (Standard, collapsible) ─────────────────────────────
    KCard {
        Layout.fillWidth: true
        visible: pageCreateForm.showOptions

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            KSectionHeader {
                level: 3
                title: qsTr("Global Parameters")
                description: qsTr("Folders left out of every catalog of this collection.")
            }
            KButton {
                id: create_button_ToggleGlobalParams
                Layout.alignment: Qt.AlignTop
                variant: "link"
                text: pageCreateForm.globalParamsExpanded ? qsTr("Collapse") : qsTr("Expand")
                icon.name: pageCreateForm.globalParamsExpanded ? "go-up" : "go-down"
                onClicked: pageCreateForm.globalParamsExpanded = !pageCreateForm.globalParamsExpanded
            }
        }

        GridLayout {
            Layout.fillWidth: true
            visible: pageCreateForm.globalParamsExpanded
            columns: pageCreateForm.narrow ? 1 : 2
            columnSpacing: KTheme.gap
            rowSpacing: 10

            FieldLabel { text: qsTr("Global exclude directory") }
            RowLayout {
                Layout.fillWidth: true
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
                KButton {
                    variant: "secondary"
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
        }

        // Global excludes list
        ColumnLayout {
            Layout.fillWidth: true
            visible: pageCreateForm.globalParamsExpanded && pageCreateForm.globalExcludes.length > 0
            spacing: 4
            Repeater {
                model: pageCreateForm.globalExcludes
                delegate: Rectangle {
                    required property string modelData
                    Layout.fillWidth: true
                    implicitHeight: globalRow.implicitHeight + 4
                    radius: KTheme.radiusItem
                    color: KTheme.surface2
                    RowLayout {
                        id: globalRow
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 12; rightMargin: 2 }
                        Controls.Label {
                            text: modelData
                            wrapMode: Text.WrapAnywhere
                            font.family: KTheme.mono
                            font.pixelSize: KTheme.note
                            color: KTheme.text
                            Layout.fillWidth: true
                        }
                        IconButton {
                            icon.name: "edit-delete"
                            text: qsTr("Remove")
                            flat: true
                            implicitWidth: 40
                            implicitHeight: 40
                            onClicked: appManager1.removeExcludeDirectory(modelData)
                        }
                    }
                }
            }
        }
    }

    // ── Step 3 (Wizard): what to analyse ──────────────────────────────────────
    KCard {
        Layout.fillWidth: true
        visible: pageCreateForm.wizardMode

        StepHeader {
            number: 3
            title: qsTr("What to analyse")
            description: qsTr("Previews and AI analysis, made after the drive is catalogued.")
        }

        CreateWizard {
            id: createWizard
            Layout.fillWidth: true
            Layout.topMargin: 4
            onChoiceEdited: createValidationMessage.visible = false
        }
    }

    // ── Create ────────────────────────────────────────────────────────────────
    Controls.Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: KTheme.text3
        font.pixelSize: KTheme.note
        text: qsTr("After cataloguing, a quick hash of every file is computed to find duplicates.")
    }

    Flow {
        Layout.fillWidth: true
        Layout.bottomMargin: KTheme.gap
        layoutDirection: Qt.RightToLeft
        spacing: 8
        KButton {
            variant: "primary"
            icon.name: "document-save"
            text: qsTr("Catalogue the drive")
            // Always enabled: a creation requested while another operation
            // runs is queued rather than refused (SpecOperationQueue.md).
            onClicked: pageCreateForm.triggerCreate()
        }
        KButton {
            visible: appManager1.catalogIsCreating
            variant: "secondary"
            icon.name: "process-stop"
            text: qsTr("Stop")
            onClicked: pageCreateForm.triggerStop()
        }
    }
}
