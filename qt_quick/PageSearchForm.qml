import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import Qt.labs.platform

// Search the drives (fork, D34 phase R3, SpecMediaIntelligence.md MDI-F117):
// a search card with the two existing ways to search — by name and by meaning —
// as described choices and the page's one primary Search button; where to
// search; then the other criteria grouped in described cards that open their
// fields when switched on. Presentation only (MDI-C40): every criterion, its
// validation, the search history and its restore are unchanged.
ColumnLayout {
    id: pageSearchForm
    spacing: KTheme.gap

    // Emitted when the user requests a search from within the form (Enter in a
    // text field, or the Search button). Main.qml runs the same action as the
    // page's Search action (run + show results).
    signal searchRequested()

    // Width of the left label column inside the cards
    readonly property int labelW: Kirigami.Units.gridUnit * 5
    // One vertical gap between rows of a card and between the wrapped lines of a
    // row; a Flow has a single spacing, so it is also the gap between its groups (PGL-F13)
    readonly property int rowSpacing: Kirigami.Units.mediumSpacing

    // Height of the first line of a row's fields, to centre the left-column label or
    // checkbox on it (PGL-F12): the tallest visible item on a Flow's first line, or
    // the first line of the first visible child of a column.
    function firstLineHeight(item) {
        if (!item) return 0
        if (item instanceof Flow) {
            var h = 0
            for (var i = 0; i < item.children.length; ++i) {
                var c = item.children[i]
                if (c.visible && c.y < 1) h = Math.max(h, c.height)
            }
            return h
        }
        if (item instanceof ColumnLayout) {
            for (var j = 0; j < item.children.length; ++j)
                if (item.children[j].visible) return firstLineHeight(item.children[j])
            return 0
        }
        return item.height
    }
    // Plain left-column labels start at the card's content edge: the section
    // titles they used to line up with are now card headers (D34)
    readonly property real labelInset: 0

    property var tagNames: []

    // A way to search (by name, by meaning): a described choice that switches its
    // criterion on or off. The check mark and the border say it is on, not the
    // colour alone (MDI-F114).
    component WayChoice: Controls.CheckBox {
        id: way
        property string hint: ""
        property string iconName: ""

        hoverEnabled: true
        padding: 10
        leftPadding: 14
        rightPadding: 14
        spacing: 10
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        implicitHeight: Math.max(52, implicitContentHeight + topPadding + bottomPadding)
        Accessible.description: hint

        indicator: Item { visible: false; implicitWidth: 0; implicitHeight: 0 }
        background: Rectangle {
            radius: KTheme.radiusControl
            color: way.checked ? KTheme.accentSoft : (way.hovered ? KTheme.hover : KTheme.surface)
            border.width: way.checked || way.visualFocus ? 2 : 1
            border.color: way.checked || way.visualFocus ? KTheme.accent : KTheme.line
            opacity: way.enabled ? (way.down ? 0.85 : 1) : 0.5
        }
        contentItem: RowLayout {
            spacing: way.spacing
            Kirigami.Icon {
                Layout.alignment: Qt.AlignTop
                implicitWidth: 20
                implicitHeight: 20
                source: way.iconName
                isMask: true
                color: way.checked ? KTheme.accentText : KTheme.text2
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Controls.Label {
                    Layout.fillWidth: true
                    text: way.text
                    wrapMode: Text.WordWrap
                    font.weight: Font.DemiBold
                    font.pixelSize: KTheme.body
                    color: way.checked ? KTheme.accentText : KTheme.text
                }
                Controls.Label {
                    Layout.fillWidth: true
                    visible: way.hint !== ""
                    text: way.hint
                    wrapMode: Text.WordWrap
                    font.pixelSize: KTheme.note
                    color: way.checked ? KTheme.accentText : KTheme.text2
                }
            }
            Kirigami.Icon {
                Layout.alignment: Qt.AlignTop
                implicitWidth: 16
                implicitHeight: 16
                source: "checkmark"
                isMask: true
                color: KTheme.accentText
                opacity: way.checked ? 1 : 0
            }
        }
    }

    // The header of a criteria card: its title and the line that says what it is
    // for, with the switch that turns the criterion on. The whole header is the
    // switch. Turned on, the card shows its fields, as the section titles did.
    component CriteriaSwitch: Controls.CheckBox {
        id: toggle
        property string description: ""

        Layout.fillWidth: true
        hoverEnabled: true
        padding: 0
        spacing: 12
        leftPadding: mirrored ? indicator.width + spacing : 0
        rightPadding: mirrored ? 0 : indicator.width + spacing
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        implicitHeight: Math.max(40, implicitContentHeight)
        Accessible.description: description

        indicator: Rectangle {
            implicitWidth: 40
            implicitHeight: 24
            x: toggle.mirrored ? 0 : toggle.width - width
            y: (toggle.height - height) / 2
            radius: height / 2
            color: toggle.checked ? KTheme.accent : KTheme.neutralSoft
            border.width: toggle.visualFocus ? 2 : 1
            border.color: toggle.checked || toggle.visualFocus ? KTheme.accent : KTheme.line
            opacity: toggle.enabled ? 1 : 0.5
            Rectangle {
                width: 18
                height: 18
                radius: 9
                y: 3
                x: toggle.checked ? parent.width - width - 3 : 3
                color: toggle.checked ? KTheme.onAccent : KTheme.surface
                border.width: toggle.checked ? 0 : 1
                border.color: KTheme.line
                Behavior on x { NumberAnimation { duration: Kirigami.Units.shortDuration } }
            }
        }
        contentItem: KSectionHeader {
            level: 3
            title: toggle.text
            description: toggle.description
            opacity: toggle.enabled ? 1 : 0.5
        }
        background: Item {}
    }

    Connections {
        target: appManager1
        function onTagsChanged() { pageSearchForm.tagNames = appManager1.getTagNames() }
    }

    Component.onCompleted: { pageSearchForm.tagNames = appManager1.getTagNames() }

    function restoreLastSearch() {
        var hist = appManager1.getSearchHistory()
        if (hist.length === 0) return
        var map = appManager1.restoreSearchHistory(hist[0].dateTime)
        applyHistoryCriteria(map)
    }

    function getCriteria() {
        //Drop blank term rows from the form before the criteria are read
        search_TextField_FileNameText.normalize()
        search_TextField_FileNameExclude.normalize()

        //Global type of search
        newSearch1.properties = {"searchInCatalogsChecked":  search_radioButton_SearchInCatalogs.checked};
        newSearch1.properties = {"searchInConnectedChecked": search_radioButton_SearchInConnected.checked};
        newSearch1.properties = {"connectedDirectory":       search_lineEdit_ConnectedDirectory.text};

        //File name
        newSearch1.properties = {"searchOnFileName":        search_checkBox_FileNameCriteria.checked};
        newSearch1.properties = {"searchText":              search_TextField_FileNameText.text};
        newSearch1.properties = {"selectedTextCriteria":    search_ComboBox_TextCriteriaWith.currentValue};
        newSearch1.properties = {"selectedSearchIn":        search_ComboBox_TextCriteriaIn.currentValue};
        newSearch1.properties = {"caseSensitive":           search_CheckBox_FileNameCaseSensitive.checked};
        newSearch1.properties = {"selectedSearchExclude":   search_TextField_FileNameExclude.text};

        //File Attributes
        newSearch1.properties = {"searchOnFileCriteria":    checkBoxFileAttributesCriteria.checked};
        //Type
        newSearch1.properties = {"searchOnType":            search_checkBox_Type.checked};
        newSearch1.properties = {"selectedFileType":        search_comboBox_FileType.currentValue};
        //Size
        newSearch1.properties = {"searchOnSize":            checkBoxFileAttributesCriteria.checked && search_checkBox_Size.checked};
        newSearch1.properties = {"selectedMinimumSize":     search_spinBox_MinimumSize.value};
        newSearch1.properties = {"selectedMaximumSize":     search_spinBox_MaximumSize.value};
        newSearch1.properties = {"selectedMinSizeUnit":     search_comboBox_MinSizeUnit.currentText};
        newSearch1.properties = {"selectedMaxSizeUnit":     search_comboBox_MaxSizeUnit.currentText};
        //Date
        newSearch1.properties = {"searchOnDate":            checkBoxFileAttributesCriteria.checked && search_checkBox_Date.checked};
        newSearch1.properties = {"selectedDateMin":         search_dateTimeEdit_Min.text};
        newSearch1.properties = {"selectedDateMax":         search_dateTimeEdit_Max.text};

        //File metadata
        newSearch1.properties = {"searchOnFileMetadata":    search_checkBox_FileMetadata.checked};
        newSearch1.properties = {"searchOnMetadataText":    search_checkBox_FileMetadata.checked && search_checkBox_MetadataText.checked};
        newSearch1.properties = {"metadataTextSearch":      search_lineEdit_MetadataText.text};
        newSearch1.properties = {"searchOnMetadataSize":    search_checkBox_FileMetadata.checked && search_checkBox_MetadataSize.checked};
        newSearch1.properties = {"metadataMinimumHeight":   search_spinBox_MetadataMinimumHeight.value};
        newSearch1.properties = {"metadataMaximumHeight":   search_spinBox_MetadataMaximumHeight.value};
        newSearch1.properties = {"metadataMinimumWidth":    search_spinBox_MetadataMinimumWidth.value};
        newSearch1.properties = {"metadataMaximumWidth":    search_spinBox_MetadataMaximumWidth.value};
        newSearch1.properties = {"searchOnMetadataDuration":search_checkBox_FileMetadata.checked && search_checkBox_MetadataDuration.checked};
        newSearch1.properties = {"metadataDurationMin":     search_dateTimeEdit_MetadataDurationMin.text};
        newSearch1.properties = {"metadataDurationMax":     search_dateTimeEdit_MetadataDurationMax.text};

        //Folder Attributes
        newSearch1.properties = {"searchOnFolderCriteria":  search_checkBox_FolderCriteria.checked};
        newSearch1.properties = {"showFoldersOnly":         search_checkBox_ShowFoldersOnly.checked};
        newSearch1.properties = {"searchOnTags":            search_checkBox_SearchOnTags.checked};
        // Every entry is a real tag: getTagNames() returns no "All" placeholder,
        // unlike the Tags page combo this guard was copied from. Treating index 0
        // as "none" sent an empty name, and the search then skipped the tag
        // filter altogether and returned everything.
        newSearch1.properties = {"selectedTagName":         search_comboBox_FolderTag.currentText};

        //Duplicates
        newSearch1.properties = {"searchOnDuplicates":             search_checkBox_Duplicates.checked};
        newSearch1.properties = {"searchDuplicatesOnName":         search_checkBox_DuplicatesOnName.checked};
        newSearch1.properties = {"searchDuplicatesOnSize":         search_checkBox_DuplicatesOnSize.checked};
        newSearch1.properties = {"searchDuplicatesOnDate":         search_checkBox_DuplicatesOnDate.checked};
        newSearch1.properties = {"searchDuplicatesOnChecksum":     search_checkBox_DuplicatesOnChecksum.checked};
        newSearch1.properties = {"searchDuplicatesChecksumEqual":  search_comboBox_DuplicateChecksumSign.currentIndex === 0};
        newSearch1.properties = {"searchDuplicatesChecksumVerify": search_checkBox_DuplicatesChecksumVerify.checked
                                                                   && search_checkBox_DuplicatesChecksumVerify.enabled};
        newSearch1.properties = {"duplicatesCompareDevices":       search_radioButton_DuplicatesCompareTwoDevices.checked};
        newSearch1.properties = {"duplicatesDeviceID1":            search_comboBox_DuplicatesDevice1.selectedDeviceId};
        newSearch1.properties = {"duplicatesDeviceID2":            search_comboBox_DuplicatesDevice2.selectedDeviceId};

        //Differences
        newSearch1.properties = {"searchOnDifferences":            search_checkBox_Differences.checked};
        newSearch1.properties = {"differencesOnName":              search_checkBox_DifferencesOnName.checked};
        newSearch1.properties = {"differencesOnSize":              search_checkBox_DifferencesOnSize.checked};
        newSearch1.properties = {"differencesOnDate":              search_checkBox_DifferencesOnDate.checked};
        newSearch1.properties = {"differencesOnChecksum":          search_checkBox_DifferencesOnChecksum.checked};
        newSearch1.properties = {"differencesChecksumEqual":       search_comboBox_DifferenceChecksumSign.currentIndex === 0};
        newSearch1.properties = {"differencesDeviceID1":           search_comboBox_DifferencesDevice1.selectedDeviceId};
        newSearch1.properties = {"differencesDeviceID2":           search_comboBox_DifferencesDevice2.selectedDeviceId};
    }

    function resetSearch() {
        // Reset search scope
        search_radioButton_SearchInCatalogs.checked = true
        search_lineEdit_ConnectedDirectory.text = ""

        // Reset fileNameCriteria
        search_checkBox_FileNameCriteria.checked = true
        search_checkBox_MeaningCriteria.checked = false
        search_TextField_Meaning.text = ""
        search_TextField_FileNameText.clearAll()
        search_ComboBox_TextCriteriaWith.currentIndex = 0
        search_ComboBox_TextCriteriaIn.currentIndex = 0
        search_CheckBox_FileNameCaseSensitive.checked = false
        search_TextField_FileNameExclude.clearAll()

        // Reset fileAtrributes
            checkBoxFileAttributesCriteria.checked = false
            //Size
            search_checkBox_Size.checked = false
            search_spinBox_MinimumSize.value = 0
            search_comboBox_MinSizeUnit.currentIndex = 0 //"Bytes"
            search_spinBox_MaximumSize.value = 1000
            search_comboBox_MaxSizeUnit.currentIndex = 3 //"GiB"
            //Date
            search_checkBox_Date.checked = false
            search_dateTimeEdit_Min.text = "1970/01/01 00:00:00"
            search_dateTimeEdit_Max.text = "2030/01/01 00:00:00"
            //Type
            search_checkBox_Type.checked = false
            search_comboBox_FileType.currentIndex = 0

        //File metadata
        search_checkBox_FileMetadata.checked = false
        search_checkBox_MetadataText.checked = false
        search_lineEdit_MetadataText.text = ""
        search_checkBox_MetadataSize.checked = false
        search_spinBox_MetadataMinimumHeight.value = 0
        search_spinBox_MetadataMaximumHeight.value = 30000
        search_spinBox_MetadataMinimumWidth.value = 0
        search_spinBox_MetadataMaximumWidth.value = 30000
        search_checkBox_MetadataDuration.checked = false
        search_dateTimeEdit_MetadataDurationMin.text = "00:00:00"
        search_dateTimeEdit_MetadataDurationMax.text = "23:59:59"

        //Duplicates
        search_checkBox_Duplicates.checked = false
        search_checkBox_DuplicatesOnName.checked = true
        search_checkBox_DuplicatesOnSize.checked = false
        search_checkBox_DuplicatesOnDate.checked = false
        search_checkBox_DuplicatesOnChecksum.checked = false
        search_comboBox_DuplicateChecksumSign.currentIndex = 0
        search_radioButton_DuplicatesWithinSelectedDevice.checked = true
        search_comboBox_DuplicatesDevice1.resetSelection()
        search_comboBox_DuplicatesDevice2.resetSelection()

        //Differences
        search_checkBox_Differences.checked = false
        search_checkBox_DifferencesOnName.checked = true
        search_checkBox_DifferencesOnSize.checked = false
        search_checkBox_DifferencesOnDate.checked = false
        search_checkBox_DifferencesOnChecksum.checked = false
        search_comboBox_DifferenceChecksumSign.currentIndex = 0
        search_comboBox_DifferencesDevice1.resetSelection()
        search_comboBox_DifferencesDevice2.resetSelection()

        // Reset folderAttributes
        search_checkBox_FolderCriteria.checked = false
        search_checkBox_ShowFoldersOnly.checked = false
        search_checkBox_SearchOnTags.checked = false
        search_comboBox_FolderTag.currentIndex = 0
    }

    function returnCleanedDate(date) {
        var dateArray = date.split(" ");
        var datePart = dateArray[0].split("/");
        var timePart = dateArray[1].split(":");

        var yyyy = datePart[0];
        var mm = datePart[1];
        var dd = datePart[2];
        var hh = timePart[0];
        var min = timePart[1];
        var ss = timePart[2];

        if(dd<10) { dd = '0'+dd }
        if(mm<10) { mm = '0'+mm }
        if(hh<10) { hh = '0'+hh }
        if(min<10) { min = '0'+min }
        if(ss<10) { ss = '0'+ss }

        return yyyy + '/' + mm + '/' + dd + ' ' + hh + ':' + min + ':' + ss;
    }

    function executeSearch() {
        // By meaning (fork, MDI-F47): its own list on the Results page. The
        // table search runs too when any other criterion is set.
        var meaning = search_checkBox_MeaningCriteria.checked
                      && search_TextField_Meaning.text.trim() !== ""
        var others  = search_checkBox_FileNameCriteria.checked || checkBoxFileAttributesCriteria.checked
                      || search_checkBox_FileMetadata.checked || search_checkBox_FolderCriteria.checked
                      || search_checkBox_Duplicates.checked   || search_checkBox_Differences.checked
        if (meaning) {
            var error = appManager1.runSemanticSearch(search_TextField_Meaning.text, !others)
            if (error !== "")
                applicationWindow().showPassiveNotification(qsTr("The local model is not reachable: %1").arg(error), "long")
        } else {
            appManager1.clearSemanticResults()
        }
        if (!meaning || others) {
            getCriteria()
            appManager1.executeSearch()
        }
    }

    function openHistorySheet() {
        _refreshHistoryModel()
        searchHistoryDialog.open()
    }

    function _refreshHistoryModel() {
        historyListModel.clear()
        historyListView.widestRow = 0
        var hist = appManager1.getSearchHistory()
        for (var i = 0; i < hist.length; ++i)
            historyListModel.append(hist[i])
    }

    function applyHistoryCriteria(map) {
        // Search scope
        if (map.searchInConnectedChecked ?? false) {
            search_radioButton_SearchInConnected.checked = true
        } else {
            search_radioButton_SearchInCatalogs.checked = true
        }
        search_lineEdit_ConnectedDirectory.text           = map.connectedDirectory ?? ""

        // File name
        search_checkBox_FileNameCriteria.checked          = map.searchOnFileName ?? false
        search_TextField_FileNameText.text                = map.searchText ?? ""
        var idx = search_ComboBox_TextCriteriaWith.indexOfValue(map.selectedTextCriteria ?? "")
        if (idx >= 0) search_ComboBox_TextCriteriaWith.currentIndex = idx
        idx = search_ComboBox_TextCriteriaIn.indexOfValue(map.selectedSearchIn ?? "")
        if (idx >= 0) search_ComboBox_TextCriteriaIn.currentIndex = idx
        search_CheckBox_FileNameCaseSensitive.checked     = map.caseSensitive ?? false
        search_TextField_FileNameExclude.text             = map.selectedSearchExclude ?? ""

        // File attributes
        checkBoxFileAttributesCriteria.checked            = map.searchOnFileCriteria ?? false
        search_checkBox_Type.checked                      = map.searchOnType ?? false
        for (var i = 0; i < search_comboBox_FileType.count; ++i) {
            if (search_comboBox_FileType.model[i].value === (map.selectedFileType ?? "All")) {
                search_comboBox_FileType.currentIndex = i
                break
            }
        }
        search_checkBox_Size.checked                      = map.searchOnSize ?? false
        search_spinBox_MinimumSize.value                  = map.selectedMinimumSize ?? 0
        idx = search_comboBox_MinSizeUnit.find(map.selectedMinSizeUnit ?? "Bytes")
        if (idx >= 0) search_comboBox_MinSizeUnit.currentIndex = idx
        search_spinBox_MaximumSize.value                  = map.selectedMaximumSize ?? 1000
        idx = search_comboBox_MaxSizeUnit.find(map.selectedMaxSizeUnit ?? "GiB")
        if (idx >= 0) search_comboBox_MaxSizeUnit.currentIndex = idx
        search_checkBox_Date.checked                      = map.searchOnDate ?? false
        search_dateTimeEdit_Min.text                      = map.selectedDateMin ?? "1970/01/01 00:00:00"
        search_dateTimeEdit_Max.text                      = map.selectedDateMax ?? "2030/01/01 00:00:00"

        // File metadata
        search_checkBox_FileMetadata.checked              = map.searchOnFileMetadata ?? false
        search_checkBox_MetadataText.checked              = map.searchOnMetadataText ?? false
        search_lineEdit_MetadataText.text                 = map.metadataTextSearch ?? ""
        search_checkBox_MetadataSize.checked              = map.searchOnMetadataSize ?? false
        search_spinBox_MetadataMinimumHeight.value        = map.metadataMinimumHeight ?? 0
        search_spinBox_MetadataMaximumHeight.value        = map.metadataMaximumHeight ?? 30000
        search_spinBox_MetadataMinimumWidth.value         = map.metadataMinimumWidth ?? 0
        search_spinBox_MetadataMaximumWidth.value         = map.metadataMaximumWidth ?? 30000
        search_checkBox_MetadataDuration.checked          = map.searchOnMetadataDuration ?? false
        search_dateTimeEdit_MetadataDurationMin.text      = map.metadataDurationMin ?? "00:00:00"
        search_dateTimeEdit_MetadataDurationMax.text      = map.metadataDurationMax ?? "23:59:59"

        // Folder attributes
        search_checkBox_FolderCriteria.checked            = map.searchOnFolderCriteria ?? false
        search_checkBox_ShowFoldersOnly.checked           = map.showFoldersOnly ?? false
        search_checkBox_SearchOnTags.checked              = map.searchOnTags ?? false
        if (map.selectedTagName) {
            idx = search_comboBox_FolderTag.find(map.selectedTagName)
            if (idx >= 0) search_comboBox_FolderTag.currentIndex = idx
        } else {
            search_comboBox_FolderTag.currentIndex = 0
        }

        // Duplicates — restore Size/Date/Checksum before Name so the "at least one" guard doesn't fire
        search_checkBox_Duplicates.checked                = map.searchOnDuplicates ?? false
        search_checkBox_DuplicatesOnSize.checked          = map.searchDuplicatesOnSize ?? false
        search_checkBox_DuplicatesOnDate.checked          = map.searchDuplicatesOnDate ?? false
        search_checkBox_DuplicatesOnChecksum.checked      = map.searchDuplicatesOnChecksum ?? false
        search_checkBox_DuplicatesOnName.checked          = map.searchDuplicatesOnName ?? true
        search_comboBox_DuplicateChecksumSign.currentIndex = (map.searchDuplicatesChecksumEqual ?? true) ? 0 : 1
        if (map.duplicatesCompareDevices ?? false)
            search_radioButton_DuplicatesCompareTwoDevices.checked = true
        else
            search_radioButton_DuplicatesWithinSelectedDevice.checked = true
        if ((map.duplicatesDeviceID1 ?? 0) > 0)
            search_comboBox_DuplicatesDevice1._applyDevice(map.duplicatesDeviceID1)
        else
            search_comboBox_DuplicatesDevice1.resetSelection()
        if ((map.duplicatesDeviceID2 ?? 0) > 0)
            search_comboBox_DuplicatesDevice2._applyDevice(map.duplicatesDeviceID2)
        else
            search_comboBox_DuplicatesDevice2.resetSelection()

        // Differences — restore Size/Date/Checksum before Name so the "at least one" guard doesn't fire
        search_checkBox_Differences.checked               = map.searchOnDifferences ?? false
        search_checkBox_DifferencesOnSize.checked         = map.differencesOnSize ?? false
        search_checkBox_DifferencesOnDate.checked         = map.differencesOnDate ?? false
        search_checkBox_DifferencesOnChecksum.checked     = map.differencesOnChecksum ?? false
        search_checkBox_DifferencesOnName.checked         = map.differencesOnName ?? true
        search_comboBox_DifferenceChecksumSign.currentIndex = (map.differencesChecksumEqual ?? true) ? 0 : 1
        if ((map.differencesDeviceID1 ?? 0) > 0)
            search_comboBox_DifferencesDevice1._applyDevice(map.differencesDeviceID1)
        else
            search_comboBox_DifferencesDevice1.resetSelection()
        if ((map.differencesDeviceID2 ?? 0) > 0)
            search_comboBox_DifferencesDevice2._applyDevice(map.differencesDeviceID2)
        else
            search_comboBox_DifferencesDevice2.resetSelection()

        // Device scope. Restoring every criterion but the scope re-ran the search
        // against whatever happened to be selected, which silently produced
        // different results (SpecSelection.md SEL-F1). Only the first id is used:
        // the Selection page holds one device, as in K2 (SEL-C3). A device that no
        // longer exists falls back to All rather than selecting nothing.
        var scopeIds = map.selectedDeviceIDList ?? []
        var scopeId  = scopeIds.length > 0 ? scopeIds[0] : 0
        if (scopeId > 0 && !appManager1.getDeviceDetails(scopeId).name)
            scopeId = 0
        appManager1.selectDeviceById(scopeId)
    }

    Kirigami.Dialog {
        id: dateDialog
        // The design system surface, not Kirigami's View colours (D34)
        background: Rectangle {
            color: KTheme.surface
            radius: KTheme.radiusCard
            border.width: 1
            border.color: KTheme.line
        }
        property string selectedDateField
        title: qsTr("Select a date")

        contentItem: ColumnLayout {
            RowLayout {
                spacing: 8
                KButton {
                    text: qsTr("Now")
                    onClicked: {
                        var today = new Date();
                        var dd = today.getDate();
                        var mm = today.getMonth()+1;
                        var yyyy = today.getFullYear();
                        var hh = today.getHours();
                        var min = today.getMinutes();
                        var ss = today.getSeconds();
                        var todayDate = yyyy + '/' + mm + '/' + dd + ' ' + hh + ':' + min + ':' + ss;
                        if (dateDialog.selectedDateField === "Min")
                            search_dateTimeEdit_Min.text = returnCleanedDate(todayDate)
                        else
                            search_dateTimeEdit_Max.text = returnCleanedDate(todayDate)
                        dateDialog.close()
                    }
                }
                KButton {
                    text: qsTr("1 day ago")
                    onClicked: {
                        var today = new Date();
                        var yesterday = new Date(today);
                        yesterday.setDate(today.getDate() - 1);
                        var fullDate = yesterday.getFullYear() + '/' + (yesterday.getMonth()+1) + '/' + yesterday.getDate() + ' ' + yesterday.getHours() + ':' + yesterday.getMinutes() + ':' + yesterday.getSeconds();
                        if (dateDialog.selectedDateField === "Min")
                            search_dateTimeEdit_Min.text = returnCleanedDate(fullDate)
                        else
                            search_dateTimeEdit_Max.text = returnCleanedDate(fullDate)
                        dateDialog.close()
                    }
                }
                KButton {
                    text: qsTr("1 week ago")
                    onClicked: {
                        var today = new Date();
                        var lastWeek = new Date(today);
                        lastWeek.setDate(today.getDate() - 7);
                        var fullDate = lastWeek.getFullYear() + '/' + (lastWeek.getMonth()+1) + '/' + lastWeek.getDate() + ' ' + lastWeek.getHours() + ':' + lastWeek.getMinutes() + ':' + lastWeek.getSeconds();
                        if (dateDialog.selectedDateField === "Min")
                            search_dateTimeEdit_Min.text = returnCleanedDate(fullDate)
                        else
                            search_dateTimeEdit_Max.text = returnCleanedDate(fullDate)
                        dateDialog.close()
                    }
                }
                KButton {
                    text: qsTr("1 month ago")
                    onClicked: {
                        var today = new Date();
                        var lastMonth = new Date(today);
                        lastMonth.setMonth(today.getMonth() - 1);
                        var fullDate = lastMonth.getFullYear() + '/' + (lastMonth.getMonth()+1) + '/' + lastMonth.getDate() + ' ' + lastMonth.getHours() + ':' + lastMonth.getMinutes() + ':' + lastMonth.getSeconds();
                        if (dateDialog.selectedDateField === "Min")
                            search_dateTimeEdit_Min.text = returnCleanedDate(fullDate)
                        else
                            search_dateTimeEdit_Max.text = returnCleanedDate(fullDate)
                        dateDialog.close()
                    }
                }
                KButton {
                    text: qsTr("1 year ago")
                    onClicked: {
                        var today = new Date();
                        var lastYear = new Date(today);
                        lastYear.setFullYear(today.getFullYear() - 1);
                        var fullDate = lastYear.getFullYear() + '/' + (lastYear.getMonth()+1) + '/' + lastYear.getDate() + ' ' + lastYear.getHours() + ':' + lastYear.getMinutes() + ':' + lastYear.getSeconds();
                        if (dateDialog.selectedDateField === "Min")
                            search_dateTimeEdit_Min.text = returnCleanedDate(fullDate)
                        else
                            search_dateTimeEdit_Max.text = returnCleanedDate(fullDate)
                        dateDialog.close()
                    }
                }
                KButton {
                    text: qsTr("Reset")
                    onClicked: {
                        if (dateDialog.selectedDateField === "Min")
                            search_dateTimeEdit_Min.text = "1970/01/01 00:00:00"
                        else
                            search_dateTimeEdit_Max.text = "2030/01/01 00:00:00"
                        dateDialog.close()
                    }
                }
            }
        }
    }

    Kirigami.PromptDialog {
        id: clearHistoryConfirmDialog
        // The design system surface, not Kirigami's View colours (D34)
        background: Rectangle {
            color: KTheme.surface
            radius: KTheme.radiusCard
            border.width: 1
            border.color: KTheme.line
        }
        title: qsTr("Delete search history")
        subtitle: qsTr("This will delete all search history entries. Continue?")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: {
            appManager1.clearSearchHistory()
            historyListModel.clear()
        }
    }

    Kirigami.PromptDialog {
        id: keepLastHistoryConfirmDialog
        // The design system surface, not Kirigami's View colours (D34)
        background: Rectangle {
            color: KTheme.surface
            radius: KTheme.radiusCard
            border.width: 1
            border.color: KTheme.line
        }
        title: qsTr("Keep last 10")
        subtitle: qsTr("This will delete all but the last 10 search history entries. Continue?")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: {
            appManager1.keepLastSearchHistory(10)
            pageSearchForm._refreshHistoryModel()
        }
    }

    Kirigami.Dialog {
        id: searchHistoryDialog
        // The design system surface, not Kirigami's View colours (D34)
        background: Rectangle {
            color: KTheme.surface
            radius: KTheme.radiusCard
            border.width: 1
            border.color: KTheme.line
        }
        title: qsTr("Search History")

        // Roles rather than source order, so Cancel follows the platform instead
        // of a position hard-coded for KDE (SpecValidationRules D1/D2).
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("Keep Last 10")
                icon.name: "edit-delete"
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.ActionRole
                onClicked: keepLastHistoryConfirmDialog.open()
            }
            Controls.Button {
                text: qsTr("Clear")
                icon.name: "edit-delete"
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.ActionRole
                onClicked: clearHistoryConfirmDialog.open()
            }
            Controls.Button {
                text: qsTr("Cancel")
                icon.name: "dialog-cancel"
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.RejectRole
            }
            onRejected: searchHistoryDialog.close()
        }

        // Kept within the Search page's own column, never over the Selection
        // panel beside it. Selection cards drawn under this dialog composite on
        // top of it on any repaint — a Qt Quick layering fault we cannot correct
        // from here, so the dialog stays clear of that area instead. Anything
        // that lets it cover Selection again brings the artifact back.
        //
        // The popup's parent is the window overlay, so its coordinates are
        // window coordinates and the page origin has to be mapped into them.
        // width and applicationWindow().width are read inside the binding so it
        // re-evaluates when the window or the column layout changes.
        // Recomputed on every open rather than bound: mapToItem() is not a
        // reactive expression, so after the page stack changed — running a search
        // and closing Results — a bound origin kept the position the page had had
        // before, and the dialog drifted back over Selection.
        onAboutToShow: {
            width  = Math.min(applicationWindow().width * 0.8, pageSearchForm.width)
            height = applicationWindow().height * 0.8
            var origin = pageSearchForm.mapToItem(searchHistoryDialog.parent, 0, 0)
            x = origin.x + (pageSearchForm.width - width) / 2
            y = (applicationWindow().height - height) / 2
        }

        contentItem: ListView {
            id: historyListView
            clip: true
            model: ListModel { id: historyListModel }

            // Widest row seen so far, reported by the delegates as they are
            // created. Rows are laid out at their natural width and the list
            // scrolls sideways to reach them, so no name or criteria list is
            // ever shortened to fit.
            property real widestRow: 0

            contentWidth: Math.max(width, widestRow)
            flickableDirection: Flickable.AutoFlickDirection

            // A long history scrolls rather than running past the dialog.
            Controls.ScrollBar.vertical: Controls.ScrollBar {
                policy: Controls.ScrollBar.AsNeeded
            }
            Controls.ScrollBar.horizontal: Controls.ScrollBar {
                policy: Controls.ScrollBar.AsNeeded
            }

            delegate: Controls.ItemDelegate {
                id: historyDelegate

                // Natural width when the content needs more than the viewport,
                // so the row is never squeezed into an ellipsis.
                width: Math.max(historyListView.width,
                                historyRow.implicitWidth + leftPadding + rightPadding)
                onWidthChanged: historyListView.widestRow =
                                Math.max(historyListView.widestRow, width)
                // One line per entry: when, on what, with what criteria — read
                // left to right, each group set off by a rule so the eye can
                // scan a column at a time (SEL-F2).
                contentItem: RowLayout {
                    id: historyRow
                    spacing: Kirigami.Units.largeSpacing

                    Controls.Label {
                        text: model.dateTime
                        color: Kirigami.Theme.disabledTextColor
                    }

                    Kirigami.Separator {
                        Layout.preferredHeight: Kirigami.Units.gridUnit
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Controls.Label {
                        // Empty means the search covered every device; the wording
                        // comes from the existing "All" in this file, so nothing
                        // new needs translating.
                        text: model.deviceName.length > 0 ? model.deviceName : qsTr("All")
                    }

                    Kirigami.Separator {
                        Layout.preferredHeight: Kirigami.Units.gridUnit
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Controls.Label {
                        // Shown in full on one line; the list scrolls sideways
                        // rather than cutting the criteria short.
                        text: model.summary.length > 0 ? model.summary : qsTr("(no text filter)")
                        Layout.fillWidth: true
                        font.italic: model.summary.length === 0
                    }
                }
                onClicked: {
                    var map = appManager1.restoreSearchHistory(model.dateTime)
                    pageSearchForm.applyHistoryCriteria(map)
                    searchHistoryDialog.close()
                }
            }
        }
    }

    FolderDialog {
        id: connectedDirectoryDialog
        onAccepted: search_lineEdit_ConnectedDirectory.text = folder.toString().replace("file://", "")
    }

    // ── Page header ──────────────────────────────────────────────────────────
    KSectionHeader {
        level: 1
        title: qsTr("Search the drives")
        description: qsTr("Teca searches its catalogs, so the drives can stay disconnected. By meaning, it also searches the descriptions and transcripts made by the AI.")
    }

    // ── What to look for: the ways to search (MDI-F117) ──────────────────────
    // By name and by meaning are the two existing criteria, shown as described
    // choices. Both can be on together: the search then fills both lists of the
    // Results page, as before (MDI-F47).
    KCard {
        Layout.fillWidth: true
        spacing: KTheme.gap

        KSectionHeader {
            level: 3
            title: qsTr("What are you looking for")
            description: qsTr("By name, by meaning, or both at once.")
        }

        Flow {
            id: search_flow_Ways
            Layout.fillWidth: true
            spacing: 8

            WayChoice {
                id: search_checkBox_FileNameCriteria
                width: Math.min(implicitWidth, search_flow_Ways.width)
                checked: true
                text: qsTr("By name")
                hint: qsTr("In the names of files and folders")
                iconName: "edit-rename"
                onCheckedChanged: search_FormLayout_FileNameCriteria.visible = checked
            }
            WayChoice {
                id: search_checkBox_MeaningCriteria
                width: Math.min(implicitWidth, search_flow_Ways.width)
                checked: false
                text: qsTr("By meaning")
                hint: qsTr("With other words too, in the descriptions, summaries and transcripts made by the AI")
                iconName: "tools-wizard"
            }
        }

        // By name: the text criteria (SpecSearchList.md)
        ColumnLayout {
            id: search_FormLayout_FileNameCriteria
            Layout.fillWidth: true
            spacing: pageSearchForm.rowSpacing

            // Says which field is which when both ways are on
            Controls.Label {
                visible: search_checkBox_MeaningCriteria.checked
                text: qsTr("By name")
                color: KTheme.text3
                font.pixelSize: KTheme.note
                font.weight: Font.DemiBold
            }
            RowLayout {
                Layout.fillWidth: true
                // Capped at the card's width: the "in" combo can be wider than a
                // narrow column, and the rows would stretch to match it, which
                // pushed the Paste and Clean buttons past the edge.
                Layout.maximumWidth: search_FormLayout_FileNameCriteria.width
                // Centred on the first term row, not on the whole list.
                Controls.Label { text: qsTr("text"); color: KTheme.text2; Layout.preferredWidth: pageSearchForm.labelW; leftPadding: pageSearchForm.labelInset; wrapMode: Text.Wrap; Layout.alignment: Qt.AlignTop
                                 Layout.topMargin: Math.max(0, (search_TextField_FileNameText.firstRowHeight - implicitHeight) / 2) }
                SearchTermList {
                    id: search_TextField_FileNameText
                    Layout.fillWidth: true
                    // Same affordance as the exclude list: clearing is per-row, through
                    // the icon inside the field.
                    showInlineClear: true
                    onAccepted: pageSearchForm.searchRequested()
                }
                RowLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: Kirigami.Units.smallSpacing
                    IconButton {
                        id: search_Button_PasteClipboard
                        text: qsTr("Paste the text from the clipboard")
                        icon.name: "edit-paste"
                        onClicked: search_TextField_FileNameText.pasteReplaceAll()
                    }
                    IconButton {
                        id: search_Button_CleanText
                        text: qsTr("Clean the search Text from characters such as _ - . ,")
                        icon.name: "edit-clear-history"
                        onClicked: search_TextField_FileNameText.cleanAll()
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: search_FormLayout_FileNameCriteria.width
                Controls.Label { text: qsTr("with"); color: KTheme.text2; Layout.preferredWidth: pageSearchForm.labelW; leftPadding: pageSearchForm.labelInset; wrapMode: Text.Wrap }
                ComboBoxFitted {
                    id: search_ComboBox_TextCriteriaWith
                    textRole: "text"
                    valueRole: "value"
                    model: [
                        {text: qsTr("All Words"),     value: "AllWords"},
                        {text: qsTr("Exact Phrase"),  value: "ExactPhrase"},
                        {text: qsTr("Begins With"),   value: "BeginsWith"},
                        {text: qsTr("Any Word"),      value: "AnyWord"},
                        {text: qsTr("Regex"),         value: "Regex"}
                    ]
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: search_FormLayout_FileNameCriteria.width
                Controls.Label { text: qsTr("in"); color: KTheme.text2; Layout.preferredWidth: pageSearchForm.labelW; leftPadding: pageSearchForm.labelInset; wrapMode: Text.Wrap }
                ComboBoxFitted {
                    id: search_ComboBox_TextCriteriaIn
                    textRole: "text"
                    valueRole: "value"
                    // A binding, not an assignment from the "with" combo's handler: an
                    // assigned array keeps the words of the language it was built in,
                    // so a language switch left this list untranslated. "Begins With"
                    // matches file names only.
                    model: search_ComboBox_TextCriteriaWith.currentIndex === 2
                           ? [ {text: qsTr("File names only"),            value: "FileNamesOnly"} ]
                           : [ {text: qsTr("File names only"),            value: "FileNamesOnly"},
                               {text: qsTr("File names or Folder paths"), value: "FilesAndFolders"},
                               {text: qsTr("Folder path only"),           value: "FolderPathOnly"} ]
                }
            }
            RowLayout {
                Controls.Label { text: ""; Layout.preferredWidth: pageSearchForm.labelW }
                Controls.CheckBox {
                    id: search_CheckBox_FileNameCaseSensitive
                    checked: false
                    text: qsTr("case sensitive")
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: search_FormLayout_FileNameCriteria.width
                Controls.Label { text: qsTr("exclude"); color: KTheme.text2; Layout.preferredWidth: pageSearchForm.labelW; leftPadding: pageSearchForm.labelInset; wrapMode: Text.Wrap; Layout.alignment: Qt.AlignTop
                                 Layout.topMargin: Math.max(0, (search_TextField_FileNameExclude.firstRowHeight - implicitHeight) / 2) }
                SearchTermList {
                    id: search_TextField_FileNameExclude
                    Layout.fillWidth: true
                    showInlineClear: true
                    onAccepted: pageSearchForm.searchRequested()
                }
                RowLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: Kirigami.Units.smallSpacing
                    IconButton {
                        id: search_Button_ExcludePasteClipboard
                        text: qsTr("Paste the text from the clipboard")
                        icon.name: "edit-paste"
                        onClicked: search_TextField_FileNameExclude.pasteReplaceAll()
                    }
                    IconButton {
                        id: search_Button_ExcludeCleanText
                        text: qsTr("Clean the search Text from characters such as _ - . ,")
                        icon.name: "edit-clear-history"
                        onClicked: search_TextField_FileNameExclude.cleanAll()
                    }
                }
            }
        }

        // By meaning (fork, SpecMediaIntelligence.md MDI-F47)
        ColumnLayout {
            visible: search_checkBox_MeaningCriteria.checked
            Layout.fillWidth: true
            spacing: pageSearchForm.rowSpacing

            Controls.Label {
                visible: search_checkBox_FileNameCriteria.checked
                text: qsTr("By meaning")
                color: KTheme.text3
                font.pixelSize: KTheme.note
                font.weight: Font.DemiBold
            }
            RowLayout {
                Layout.fillWidth: true
                Controls.Label { text: qsTr("text"); color: KTheme.text2; Layout.preferredWidth: pageSearchForm.labelW; leftPadding: pageSearchForm.labelInset; wrapMode: Text.Wrap }
                Controls.TextField {
                    id: search_TextField_Meaning
                    Layout.fillWidth: true
                    placeholderText: qsTr("Describe what you are looking for")
                    onAccepted: pageSearchForm.searchRequested()
                }
            }
        }

        // The one primary action of the page: the same as the page's Search
        // action and the Enter key (Main.qml runSearch), enabled when it is.
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            KButton {
                id: search_Button_Search
                variant: "primary"
                icon.name: "edit-find"
                text: qsTr("Search")
                enabled: !appManager1.searchIsRunning
                         && !appManager1.deviceUpdateIsRunning
                         && !appManager1.catalogIsCreating
                         && !appManager1.exploreLoadIsRunning   // OPQ-F20
                onClicked: pageSearchForm.searchRequested()
            }
        }
    }

    // ── Where to search ──────────────────────────────────────────────────────
    KCard {
        Layout.fillWidth: true

        KSectionHeader {
            level: 3
            title: qsTr("Where to search")
            description: qsTr("In the catalogs of the drives chosen in My drives, or straight on a connected drive or folder.")
        }
        Flow {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Controls.RadioButton {
                id: search_radioButton_SearchInCatalogs
                text: qsTr("Catalogs")
                checked: true
                onCheckedChanged: {
                    if (checked) search_lineEdit_ConnectedDirectory.text = ""
                }
            }
            Controls.RadioButton {
                id: search_radioButton_SearchInConnected
                text: qsTr("Connected drive")
            }
        }
        RowLayout {
            id: search_RowLayout_ConnectedDirectory
            Layout.fillWidth: true
            visible: search_radioButton_SearchInConnected.checked

            Controls.TextField {
                id: search_lineEdit_ConnectedDirectory
                Layout.fillWidth: true
                placeholderText: qsTr("Path to connected drive or folder")
            }
            IconButton {
                icon.name: "folder-open"
                text: qsTr("Select the path")
                onClicked: connectedDirectoryDialog.open()
            }
        }
    }

    // ── Filters ──────────────────────────────────────────────────────────────
    // Each card opens its fields when switched on, as the section titles did.
    KSectionHeader {
        Layout.topMargin: Kirigami.Units.smallSpacing
        title: qsTr("Filters")
        description: qsTr("A filter counts only while it is on.")
    }

    // File attributes
    KCard {
        Layout.fillWidth: true

        CriteriaSwitch {
            id: checkBoxFileAttributesCriteria
            checked: false
            text: qsTr("Type, size and date")
            description: qsTr("Files of one type, within a size range or modified between two dates.")
            onCheckedChanged: {
                fileAtrributeCriteria.visible = checked
                // Convenience: enabling File attributes while Type is still the default
                // "All" auto-ticks Type so the combo is ready — one less click to pick
                // another type. "All" applies no type filter, so this changes nothing.
                if (checked && search_comboBox_FileType.currentValue === "All")
                    search_checkBox_Type.checked = true
            }
        }
        ColumnLayout {
            id: fileAtrributeCriteria
            visible: false
            Layout.fillWidth: true
            spacing: pageSearchForm.rowSpacing

            RowLayout {
                Layout.fillWidth: true
                Controls.CheckBox {
                    id: search_checkBox_Type
                    checked: false
                    text: qsTr("Type")
                    Layout.preferredWidth: pageSearchForm.labelW
                    onCheckedChanged: search_comboBox_FileType.enabled = checked
                }
                ComboBoxFitted {
                    id: search_comboBox_FileType
                    rowLeadingWidth: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing * 2  // row icon
                    enabled: false
                    textRole: "text"
                    valueRole: "value"
                    displayText: ""
                    model: [
                        {text: qsTr("All"),   value: "All",   iconName: "folder"},
                        {text: qsTr("Audio"), value: "Audio", iconName: "audio-x-mpeg"},
                        {text: qsTr("Image"), value: "Image", iconName: "image-jpeg"},
                        {text: qsTr("Text"),  value: "Text",  iconName: "view-list-text"},
                        {text: qsTr("Video"), value: "Video", iconName: "video-mp4"},
                        {text: qsTr("Other"), value: "Other", iconName: "document-open"},
                        {text: qsTr("None"),  value: "None",  iconName: "application-x-zerosize"}
                    ]
                    delegate: Controls.ItemDelegate {
                        width: ListView.view ? ListView.view.width : search_comboBox_FileType.width  // fills the list, which may be wider than the box
                        text: modelData.text
                        icon.name: modelData.iconName
                        highlighted: search_comboBox_FileType.highlightedIndex === index
                    }
                    contentItem: RowLayout {
                        function positionToRectangle(pos) { return Qt.rect(0, 0, 0, 0) }
                        property int selectionStart: 0
                        spacing: Kirigami.Units.smallSpacing
                        Item { width: Kirigami.Units.smallSpacing }
                        Kirigami.Icon {
                            source: search_comboBox_FileType.currentIndex >= 0
                                    ? search_comboBox_FileType.model[search_comboBox_FileType.currentIndex].iconName : ""
                            implicitWidth:  Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                        }
                        Controls.Label {
                            text: search_comboBox_FileType.currentIndex >= 0
                                  ? search_comboBox_FileType.model[search_comboBox_FileType.currentIndex].text : ""
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Controls.CheckBox {
                    id: search_checkBox_Size
                    checked: false
                    text: qsTr("Size")
                    Layout.preferredWidth: pageSearchForm.labelW
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: Math.max(0, (pageSearchForm.firstLineHeight(search_flow_Size) - implicitHeight) / 2)
                    onCheckedChanged: {
                        search_spinBox_MinimumSize.enabled = checked
                        search_comboBox_MinSizeUnit.enabled = checked
                        search_spinBox_MaximumSize.enabled = checked
                        search_comboBox_MaxSizeUnit.enabled = checked
                    }
                }
                Flow {
                    id: search_flow_Size
                    Layout.fillWidth: true
                    spacing: pageSearchForm.rowSpacing
                    RowLayout {
                        width: Math.min(implicitWidth, parent.width)  // lets its combo box shrink (CBX-F2)
                        Controls.Label { text: ">" }
                        Controls.SpinBox {
                            id: search_spinBox_MinimumSize
                            enabled: false
                            editable: true
                            from: 0; value: 0; to: 1000
                            implicitWidth: 110
                        }
                        ComboBoxFitted {
                            id: search_comboBox_MinSizeUnit
                            enabled: false
                            model: ["Bytes", "KiB", "MiB", "GiB", "TiB"]
                        }
                    }
                    RowLayout {
                        width: Math.min(implicitWidth, parent.width)  // lets its combo box shrink (CBX-F2)
                        Controls.Label { text: "<" }
                        Controls.SpinBox {
                            id: search_spinBox_MaximumSize
                            enabled: false
                            editable: true
                            from: 0; value: 1000; to: 1000
                            implicitWidth: 110
                        }
                        ComboBoxFitted {
                            id: search_comboBox_MaxSizeUnit
                            enabled: false
                            model: ["Bytes", "KiB", "MiB", "GiB", "TiB"]
                            currentIndex: 3
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Controls.CheckBox {
                    id: search_checkBox_Date
                    checked: false
                    text: qsTr("Date")
                    Layout.preferredWidth: pageSearchForm.labelW
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: Math.max(0, (pageSearchForm.firstLineHeight(search_flow_Date) - implicitHeight) / 2)
                    onCheckedChanged: {
                        search_dateTimeEdit_Min.enabled = checked
                        search_button_ShowMinDateCalendar.enabled = checked
                        search_dateTimeEdit_Max.enabled = checked
                        search_button_ShowMaxDateCalendar.enabled = checked
                    }
                }
                Flow {
                    id: search_flow_Date
                    Layout.fillWidth: true
                    spacing: pageSearchForm.rowSpacing
                    RowLayout {
                        Controls.Label { text: ">" }
                        Controls.TextField {
                            id: search_dateTimeEdit_Min
                            enabled: false
                            inputMask: "9999/99/99 99:99:99"
                            inputMethodHints: Qt.ImhDigitsOnly
                            text: "1970/01/01 00:00:00"
                            implicitWidth: 150
                        }
                        IconButton {
                            id: search_button_ShowMinDateCalendar
                            text: qsTr("Select a date")
                            enabled: false
                            icon.name: "view-calendar"
                            onClicked: { dateDialog.selectedDateField = "Min"; dateDialog.open() }
                        }
                    }
                    RowLayout {
                        Controls.Label { text: "<" }
                        Controls.TextField {
                            id: search_dateTimeEdit_Max
                            enabled: false
                            inputMask: "9999/99/99 99:99:99"
                            inputMethodHints: Qt.ImhDigitsOnly
                            text: "2030/01/01 00:00:00"
                            implicitWidth: 150
                        }
                        IconButton {
                            id: search_button_ShowMaxDateCalendar
                            text: qsTr("Select a date")
                            enabled: false
                            icon.name: "view-calendar"
                            onClicked: { dateDialog.selectedDateField = "Max"; dateDialog.open() }
                        }
                    }
                }
            }
        }
    }

    // File metadata
    KCard {
        Layout.fillWidth: true

        CriteriaSwitch {
            id: search_checkBox_FileMetadata
            checked: false
            text: qsTr("Information in the files")
            description: qsTr("What the files themselves record: text such as title, artist or album, picture size and duration.")
            onCheckedChanged: search_FormLayout_FileMetadata.visible = checked
        }
        ColumnLayout {
            id: search_FormLayout_FileMetadata
            visible: false
            Layout.fillWidth: true
            spacing: pageSearchForm.rowSpacing

            RowLayout {
                Layout.fillWidth: true
                Controls.CheckBox {
                    id: search_checkBox_MetadataText
                    checked: false
                    text: qsTr("Text")
                    Layout.preferredWidth: pageSearchForm.labelW
                    onCheckedChanged: search_lineEdit_MetadataText.enabled = checked
                }
                Controls.TextField {
                    id: search_lineEdit_MetadataText
                    enabled: false
                    Layout.fillWidth: true
                    leftPadding: Kirigami.Units.largeSpacing
                    rightPadding: text.length > 0
                                  ? Kirigami.Units.iconSizes.small + Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing
                                  : Kirigami.Units.smallSpacing
                    Kirigami.Icon {
                        anchors { right: parent.right; rightMargin: Kirigami.Units.smallSpacing * 2; verticalCenter: parent.verticalCenter }
                        source: parent.LayoutMirroring.enabled ? "edit-clear-locationbar-ltr" : "edit-clear-locationbar-rtl"
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: Kirigami.Units.iconSizes.small
                        visible: parent.text.length > 0
                        opacity: metaClearTap.pressed ? 0.5 : 1.0
                        Behavior on opacity { NumberAnimation { duration: Kirigami.Units.shortDuration } }
                        HoverHandler { id: metaClearHover; cursorShape: Qt.ArrowCursor }
                        Controls.ToolTip.text: qsTr("Clear")
                        Controls.ToolTip.visible: metaClearHover.hovered
                        TapHandler {
                            id: metaClearTap
                            // Focus follows the clear, so the user can retype straight
                            // away instead of clicking the field a second time.
                            onTapped: {
                                search_lineEdit_MetadataText.clear()
                                search_lineEdit_MetadataText.forceActiveFocus()
                            }
                        }
                    }
                }
                IconButton {
                    icon.name: "edit-paste"
                    enabled: search_checkBox_MetadataText.checked
                    text: qsTr("Paste the text from the clipboard")
                    onClicked: search_lineEdit_MetadataText.text = pageSearch1.returnClipboard()
                }
                IconButton {
                    icon.name: "edit-clear-history"
                    enabled: search_checkBox_MetadataText.checked
                    // The tooltip of the same button beside the term lists (ICB-F1)
                    text: qsTr("Clean the search Text from characters such as _ - . ,")
                    onClicked: search_lineEdit_MetadataText.text = pageSearch1.returnCleanedText(search_lineEdit_MetadataText.text)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Controls.CheckBox {
                    id: search_checkBox_MetadataSize
                    checked: false
                    text: qsTr("Size")
                    Layout.preferredWidth: pageSearchForm.labelW
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: Math.max(0, (pageSearchForm.firstLineHeight(search_flow_MetadataWidth) - implicitHeight) / 2)
                    onCheckedChanged: {
                        search_spinBox_MetadataMinimumHeight.enabled = checked
                        search_spinBox_MetadataMaximumHeight.enabled = checked
                        search_spinBox_MetadataMinimumWidth.enabled  = checked
                        search_spinBox_MetadataMaximumWidth.enabled  = checked
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: pageSearchForm.rowSpacing
                    // The label sits in front of the Flow, so a wrapped "<" group starts
                    // under the ">" group above it (PGL-F14)
                    RowLayout {
                        Layout.fillWidth: true
                        Controls.Label { text: qsTr("Width"); color: KTheme.text2; Layout.preferredWidth: 50; Layout.alignment: Qt.AlignTop
                                         Layout.topMargin: Math.max(0, (pageSearchForm.firstLineHeight(search_flow_MetadataWidth) - implicitHeight) / 2) }
                        Flow {
                            id: search_flow_MetadataWidth
                            Layout.fillWidth: true
                            spacing: pageSearchForm.rowSpacing
                            RowLayout {
                                Controls.Label { text: ">" }
                                Controls.SpinBox {
                                    id: search_spinBox_MetadataMinimumWidth
                                    enabled: false
                                    editable: true
                                    from: 0; value: 0; to: 30000
                                    implicitWidth: 110
                                }
                            }
                            RowLayout {
                                Controls.Label { text: "<" }
                                Controls.SpinBox {
                                    id: search_spinBox_MetadataMaximumWidth
                                    enabled: false
                                    editable: true
                                    from: 0; value: 30000; to: 30000
                                    implicitWidth: 110
                                }
                            }
                        }
                    }
                    // The label sits in front of the Flow, so a wrapped "<" group starts
                    // under the ">" group above it (PGL-F14)
                    RowLayout {
                        Layout.fillWidth: true
                        Controls.Label { text: qsTr("Height"); color: KTheme.text2; Layout.preferredWidth: 50; Layout.alignment: Qt.AlignTop
                                         Layout.topMargin: Math.max(0, (pageSearchForm.firstLineHeight(search_flow_MetadataHeight) - implicitHeight) / 2) }
                        Flow {
                            id: search_flow_MetadataHeight
                            Layout.fillWidth: true
                            spacing: pageSearchForm.rowSpacing
                            RowLayout {
                                Controls.Label { text: ">" }
                                Controls.SpinBox {
                                    id: search_spinBox_MetadataMinimumHeight
                                    enabled: false
                                    editable: true
                                    from: 0; value: 0; to: 30000
                                    implicitWidth: 110
                                }
                            }
                            RowLayout {
                                Controls.Label { text: "<" }
                                Controls.SpinBox {
                                    id: search_spinBox_MetadataMaximumHeight
                                    enabled: false
                                    editable: true
                                    from: 0; value: 30000; to: 30000
                                    implicitWidth: 110
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Controls.CheckBox {
                    id: search_checkBox_MetadataDuration
                    checked: false
                    text: qsTr("Duration")
                    Layout.preferredWidth: pageSearchForm.labelW
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: Math.max(0, (pageSearchForm.firstLineHeight(search_flow_MetadataDuration) - implicitHeight) / 2)
                    onCheckedChanged: {
                        search_dateTimeEdit_MetadataDurationMin.enabled = checked
                        search_dateTimeEdit_MetadataDurationMax.enabled = checked
                    }
                }
                Flow {
                    id: search_flow_MetadataDuration
                    Layout.fillWidth: true
                    spacing: pageSearchForm.rowSpacing
                    RowLayout {
                        Controls.Label { text: ">" }
                        Controls.TextField {
                            id: search_dateTimeEdit_MetadataDurationMin
                            enabled: false
                            inputMask: "99:99:99"
                            text: "00:00:00"
                            implicitWidth: 80
                        }
                    }
                    RowLayout {
                        Controls.Label { text: "<" }
                        Controls.TextField {
                            id: search_dateTimeEdit_MetadataDurationMax
                            enabled: false
                            inputMask: "99:99:99"
                            text: "23:59:59"
                            implicitWidth: 80
                        }
                    }
                }
            }
        }
    }

    // Folder criteria
    KCard {
        Layout.fillWidth: true

        CriteriaSwitch {
            id: search_checkBox_FolderCriteria
            checked: false
            text: qsTr("Folders and tags")
            description: qsTr("Only folders in the results, or only what lies in folders with a tag.")
            onCheckedChanged: search_FormLayout_folderCriteria.visible = checked
        }
        ColumnLayout {
            Layout.fillWidth: true
            id: search_FormLayout_folderCriteria
            visible: false
            spacing: pageSearchForm.rowSpacing

            Controls.CheckBox {
                id: search_checkBox_ShowFoldersOnly
                checked: false
                text: qsTr("only list folders in results")
                // Wraps rather than hold the card at its one-line width: in
                // French it was the widest item, and every row stretched to it,
                // under the scrollbar of a narrow Search column (PGL-F4).
                Layout.fillWidth: true
                Binding { target: search_checkBox_ShowFoldersOnly.contentItem; property: "wrapMode"; value: Text.WordWrap }
            }
            RowLayout {
                Layout.fillWidth: true
                Controls.CheckBox {
                    id: search_checkBox_SearchOnTags
                    checked: false
                    text: qsTr("Tag")
                    Layout.preferredWidth: pageSearchForm.labelW
                    onCheckedChanged: search_comboBox_FolderTag.enabled = checked
                }
                ComboBoxFitted {
                    id: search_comboBox_FolderTag
                    enabled: false
                    model: pageSearchForm.tagNames
                }
            }
        }
    }

    // ── Duplicates and differences ───────────────────────────────────────────
    KSectionHeader {
        Layout.topMargin: Kirigami.Units.smallSpacing
        title: qsTr("Duplicates and differences")
        description: qsTr("Files found more than once, or that differ between two drives. One of the two at a time.")
    }

    KCard {
        Layout.fillWidth: true

        CriteriaSwitch {
            id: search_checkBox_Duplicates
            checked: false
            text: qsTr("Duplicates")
            description: qsTr("The same file more than once, compared by name, size, date or checksum.")
            onCheckedChanged: {
                search_FormLayout_Duplicates.visible = checked
                if (checked && search_checkBox_Differences.checked) {
                    showPassiveNotification(qsTr("Duplicates and Differences cannot be used at the same time"))
                    search_checkBox_Differences.checked = false
                }
            }
        }
        ColumnLayout {
            id: search_FormLayout_Duplicates
            visible: false
            Layout.fillWidth: true
            spacing: pageSearchForm.rowSpacing

            RowLayout {
                Layout.fillWidth: true
                Controls.Label { text: qsTr("On"); color: KTheme.text2; Layout.preferredWidth: pageSearchForm.labelW; leftPadding: pageSearchForm.labelInset; wrapMode: Text.Wrap; Layout.alignment: Qt.AlignTop
                                 Layout.topMargin: Math.max(0, (pageSearchForm.firstLineHeight(search_flow_DuplicatesOn) - implicitHeight) / 2) }
                Flow {
                    id: search_flow_DuplicatesOn
                    Layout.fillWidth: true
                    spacing: pageSearchForm.rowSpacing
                    RowLayout {
                        Controls.CheckBox {
                            id: search_checkBox_DuplicatesOnName
                            checked: true
                            text: qsTr("Name")
                            onCheckedChanged: {
                                if (!search_checkBox_DuplicatesOnSize.checked && !search_checkBox_DuplicatesOnDate.checked
                                        && !search_checkBox_DuplicatesOnChecksum.checked)
                                    search_checkBox_DuplicatesOnName.checked = true
                            }
                        }
                        Controls.CheckBox {
                            id: search_checkBox_DuplicatesOnSize
                            checked: false
                            text: qsTr("Size")
                            onCheckedChanged: {
                                if (!search_checkBox_DuplicatesOnName.checked && !search_checkBox_DuplicatesOnDate.checked
                                        && !search_checkBox_DuplicatesOnChecksum.checked)
                                    search_checkBox_DuplicatesOnSize.checked = true
                            }
                        }
                        Controls.CheckBox {
                            id: search_checkBox_DuplicatesOnDate
                            checked: false
                            text: qsTr("Date")
                            onCheckedChanged: {
                                if (!search_checkBox_DuplicatesOnSize.checked && !search_checkBox_DuplicatesOnName.checked
                                        && !search_checkBox_DuplicatesOnChecksum.checked)
                                    search_checkBox_DuplicatesOnDate.checked = true
                            }
                        }
                    }
                    RowLayout {
                        width: Math.min(implicitWidth, parent.width)  // lets its combo box shrink (CBX-F2)
                        ComboBoxFitted {
                            id: search_comboBox_DuplicateChecksumSign
                            model: ["=", "≠"]
                            enabled: search_checkBox_DuplicatesOnChecksum.checked
                        }
                        Controls.CheckBox {
                            id: search_checkBox_DuplicatesOnChecksum
                            checked: false
                            text: qsTr("Checksum")
                            onCheckedChanged: {
                                search_comboBox_DuplicateChecksumSign.enabled = checked
                                if (!search_checkBox_DuplicatesOnName.checked && !search_checkBox_DuplicatesOnSize.checked
                                        && !search_checkBox_DuplicatesOnDate.checked)
                                    search_checkBox_DuplicatesOnChecksum.checked = true
                            }
                        }
                        // Fork: the checksum key is the quick hash when available;
                        // this confirms the groups with the full SHA-256
                        Controls.CheckBox {
                            id: search_checkBox_DuplicatesChecksumVerify
                            checked: false
                            text: qsTr("Verify with full checksum")
                            enabled: search_checkBox_DuplicatesOnChecksum.checked
                                     && search_comboBox_DuplicateChecksumSign.currentIndex === 0
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Controls.Label { text: qsTr("Scope"); color: KTheme.text2; Layout.preferredWidth: pageSearchForm.labelW; leftPadding: pageSearchForm.labelInset; wrapMode: Text.Wrap; Layout.alignment: Qt.AlignTop
                                 Layout.topMargin: Math.max(0, (pageSearchForm.firstLineHeight(search_flow_DuplicatesScope) - implicitHeight) / 2) }
                Flow {
                    id: search_flow_DuplicatesScope
                    Layout.fillWidth: true
                    spacing: pageSearchForm.rowSpacing
                    Controls.RadioButton {
                        id: search_radioButton_DuplicatesWithinSelectedDevice
                        checked: true
                        text: qsTr("Within selected device")
                    }
                    Controls.RadioButton {
                        id: search_radioButton_DuplicatesCompareTwoDevices
                        text: qsTr("Compare two devices")
                        onCheckedChanged: search_FormLayout_DuplicatesDevices.visible = checked
                    }
                }
            }

            Flow {
                id: search_FormLayout_DuplicatesDevices
                visible: false
                Layout.leftMargin: pageSearchForm.labelW + Kirigami.Units.largeSpacing
                Layout.fillWidth: true
                spacing: pageSearchForm.rowSpacing

                RowLayout {
                    width: Math.min(implicitWidth, parent.width)  // lets its combo box shrink (CBX-F2)
                    Controls.Label { text: qsTr("Device 1"); color: KTheme.text2 }
                    DeviceTreeComboBox { id: search_comboBox_DuplicatesDevice1 }
                }
                RowLayout {
                    width: Math.min(implicitWidth, parent.width)  // lets its combo box shrink (CBX-F2)
                    Controls.Label { text: qsTr("Device 2"); color: KTheme.text2 }
                    DeviceTreeComboBox { id: search_comboBox_DuplicatesDevice2 }
                }
            }
        }
    }

    KCard {
        Layout.fillWidth: true

        CriteriaSwitch {
            id: search_checkBox_Differences
            checked: false
            enabled: search_radioButton_SearchInCatalogs.checked
            text: qsTr("Differences")
            description: qsTr("What differs between two drives, compared by name, size, date or checksum.")
            onCheckedChanged: {
                search_FormLayout_Differences.visible = checked
                if (checked && search_checkBox_Duplicates.checked) {
                    showPassiveNotification(qsTr("Duplicates and Differences cannot be used at the same time"))
                    search_checkBox_Duplicates.checked = false
                }
            }
        }
        // Says why the switch is greyed, instead of leaving it unexplained
        Controls.Label {
            visible: !search_checkBox_Differences.enabled
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: KTheme.text3
            font.pixelSize: KTheme.note
            text: qsTr("Available when the search is in the catalogs.")
        }
        ColumnLayout {
            id: search_FormLayout_Differences
            visible: false
            Layout.fillWidth: true
            spacing: pageSearchForm.rowSpacing

            RowLayout {
                Layout.fillWidth: true
                Controls.Label { text: qsTr("On"); color: KTheme.text2; Layout.preferredWidth: pageSearchForm.labelW; leftPadding: pageSearchForm.labelInset; wrapMode: Text.Wrap; Layout.alignment: Qt.AlignTop
                                 Layout.topMargin: Math.max(0, (pageSearchForm.firstLineHeight(search_flow_DifferencesOn) - implicitHeight) / 2) }
                Flow {
                    id: search_flow_DifferencesOn
                    Layout.fillWidth: true
                    spacing: pageSearchForm.rowSpacing
                    RowLayout {
                        Controls.CheckBox {
                            id: search_checkBox_DifferencesOnName
                            checked: true
                            text: qsTr("Name")
                            onCheckedChanged: {
                                if (!search_checkBox_DifferencesOnSize.checked && !search_checkBox_DifferencesOnDate.checked
                                        && !search_checkBox_DifferencesOnChecksum.checked)
                                    search_checkBox_DifferencesOnName.checked = true
                            }
                        }
                        Controls.CheckBox {
                            id: search_checkBox_DifferencesOnSize
                            checked: false
                            text: qsTr("Size")
                            onCheckedChanged: {
                                if (!search_checkBox_DifferencesOnName.checked && !search_checkBox_DifferencesOnDate.checked
                                        && !search_checkBox_DifferencesOnChecksum.checked)
                                    search_checkBox_DifferencesOnSize.checked = true
                            }
                        }
                        Controls.CheckBox {
                            id: search_checkBox_DifferencesOnDate
                            checked: false
                            text: qsTr("Date")
                            onCheckedChanged: {
                                if (!search_checkBox_DifferencesOnSize.checked && !search_checkBox_DifferencesOnName.checked
                                        && !search_checkBox_DifferencesOnChecksum.checked)
                                    search_checkBox_DifferencesOnDate.checked = true
                            }
                        }
                    }
                    RowLayout {
                        width: Math.min(implicitWidth, parent.width)  // lets its combo box shrink (CBX-F2)
                        ComboBoxFitted {
                            id: search_comboBox_DifferenceChecksumSign
                            model: ["=", "≠"]
                            enabled: search_checkBox_DifferencesOnChecksum.checked
                        }
                        Controls.CheckBox {
                            id: search_checkBox_DifferencesOnChecksum
                            checked: false
                            text: qsTr("Checksum")
                            onCheckedChanged: {
                                search_comboBox_DifferenceChecksumSign.enabled = checked
                                if (!search_checkBox_DifferencesOnName.checked && !search_checkBox_DifferencesOnSize.checked
                                        && !search_checkBox_DifferencesOnDate.checked)
                                    search_checkBox_DifferencesOnChecksum.checked = true
                            }
                        }
                    }
                }
            }

            Flow {
                Layout.leftMargin: pageSearchForm.labelW + Kirigami.Units.largeSpacing
                Layout.fillWidth: true
                spacing: pageSearchForm.rowSpacing
                RowLayout {
                    width: Math.min(implicitWidth, parent.width)  // lets its combo box shrink (CBX-F2)
                    Controls.Label { text: qsTr("Between"); color: KTheme.text2 }
                    DeviceTreeComboBox { id: search_comboBox_DifferencesDevice1 }
                }
                RowLayout {
                    width: Math.min(implicitWidth, parent.width)  // lets its combo box shrink (CBX-F2)
                    Controls.Label { text: qsTr("And"); color: KTheme.text2 }
                    DeviceTreeComboBox { id: search_comboBox_DifferencesDevice2 }
                }
            }
        }
    }

    Item { Layout.fillHeight: true }
}
