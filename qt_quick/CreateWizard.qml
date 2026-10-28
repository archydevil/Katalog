import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import QtQuick.Dialogs

// Wizard mode of the Create page (fork, SpecMediaIntelligence.md MDI-F1..F6).
// Step 0 (Source) is the "Catalog definition" section of PageCreateForm, shown
// by the page itself; this component holds the steps after it. It only collects
// choices: the page validates, navigates and creates through the standard entry
// point.
ColumnLayout {
    id: createWizard
    spacing: Kirigami.Units.smallSpacing

    // Filled by PageCreateForm for the summary
    property string sourcePath:  ""
    property string storageName: ""
    property string catalogName: ""

    property int step: 0                 // 0 = Source (shown by the page)
    readonly property int stepCount: 5   // Source, Destination, File types, Previews, Summary
    readonly property var stepTitles: [
        qsTr("Source"), qsTr("Destination path"), qsTr("File types"), qsTr("Previews"), qsTr("Summary")
    ]

    signal choiceEdited()                // hides the page's validation message (MDI-F6)

    // ── Choices ───────────────────────────────────────────────────────────────
    property alias destinationPath: wizard_lineEdit_Destination.text

    // MDI-F3: Image, Video, Audio, Text (documents) and Other, independently
    property bool typeImage: true
    property bool typeVideo: true
    property bool typeAudio: true
    property bool typeText:  true
    property bool typeOther: false

    // MDI-F4 defaults
    property bool imageThumbnail:      true
    property bool videoPosterFrame:    true
    property bool videoKeyframeStrip:  false
    property int  videoKeyframeCount:  8
    property string videoClip:         "off"   // off / full / partial
    property int  videoClipStart:      0
    property int  videoClipDuration:   30
    property string audioClip:         "off"
    property int  audioClipStart:      0
    property int  audioClipDuration:   30
    property bool audioWaveform:       false

    readonly property var clipModel: [
        { text: qsTr("Off"),     value: "off"     },
        { text: qsTr("Full"),    value: "full"    },
        { text: qsTr("Partial"), value: "partial" }
    ]

    function anyFileType() {
        return typeImage || typeVideo || typeAudio || typeText || typeOther
    }
    function anyPreview() {
        return imageThumbnail || videoPosterFrame || videoKeyframeStrip
            || videoClip !== "off" || audioClip !== "off" || audioWaveform
    }
    function selectedTypes() {
        var t = []
        if (typeImage) t.push("Image")
        if (typeVideo) t.push("Video")
        if (typeAudio) t.push("Audio")
        if (typeText)  t.push("Text")
        if (typeOther) t.push("Other")
        return t
    }
    function clipText(value) {
        for (var i = 0; i < clipModel.length; i++)
            if (clipModel[i].value === value) return clipModel[i].text
        return value
    }
    function onOff(flag) { return flag ? qsTr("On") : qsTr("Off") }

    // Profile saved with the catalog once its creation completes (MDI-F7, F8)
    function buildProfile() {
        return {
            destinationPath: appManager1.normalizeSourcePath(destinationPath),
            settings: {
                version: 1,
                fileTypes: selectedTypes(),
                image: { thumbnail: imageThumbnail },
                video: {
                    posterFrame:   videoPosterFrame,
                    keyframeStrip: videoKeyframeStrip,
                    keyframeCount: videoKeyframeCount,
                    clip:          videoClip,
                    clipStart:     videoClipStart,
                    clipDuration:  videoClipDuration
                },
                audio: {
                    clip:         audioClip,
                    clipStart:    audioClipStart,
                    clipDuration: audioClipDuration,
                    waveform:     audioWaveform
                }
            }
        }
    }

    function restoreSettings() {
        var d = appManager1.getCreateSetting("WizardDestinationPath")
        if (d !== "") destinationPath = d
    }
    function rememberSettings() {
        appManager1.setCreateSetting("WizardDestinationPath", destinationPath)
    }

    Component {   // a fresh dialog per open (openFreshDialog)
        id: destinationDialogComponent
        FolderDialog {
            onAccepted: wizard_lineEdit_Destination.text = appManager1.normalizeSourcePath(appManager1.pathFromFileUrl(selectedFolder.toString()))
        }
    }

    // ── Step header ───────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.largeSpacing
        Controls.Label {
            text: createWizard.stepTitles[createWizard.step]
            color: applicationWindow().titleColor
        }
        Kirigami.Separator { Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter }
        Controls.Label {
            text: qsTr("Step %1 of %2").arg(createWizard.step + 1).arg(createWizard.stepCount)
            opacity: 0.7
        }
    }

    // ── Step 1: Destination path ──────────────────────────────────────────────
    GridLayout {
        visible: createWizard.step === 1
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: Kirigami.Units.smallSpacing

        Controls.Label { text: qsTr("Destination path"); opacity: 0.7 }
        RowLayout {
            Layout.fillWidth: true
            Controls.TextField {
                id: wizard_lineEdit_Destination
                Layout.fillWidth: true
                placeholderText: qsTr("Path for previews and derived data")
                onTextChanged: createWizard.choiceEdited()
                onEditingFinished: text = appManager1.normalizeSourcePath(text)
            }
            IconButton {
                icon.name: "folder-open"
                text: qsTr("Select the path")
                onClicked: applicationWindow().openFreshDialog(destinationDialogComponent,
                                                               wizard_lineEdit_Destination.text.trim())
            }
        }
    }

    // ── Step 2: File types ────────────────────────────────────────────────────
    ColumnLayout {
        visible: createWizard.step === 2
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing
        Controls.CheckBox { text: qsTr("Image"); checked: createWizard.typeImage; onToggled: { createWizard.typeImage = checked; createWizard.choiceEdited() } }
        Controls.CheckBox { text: qsTr("Video"); checked: createWizard.typeVideo; onToggled: { createWizard.typeVideo = checked; createWizard.choiceEdited() } }
        Controls.CheckBox { text: qsTr("Audio"); checked: createWizard.typeAudio; onToggled: { createWizard.typeAudio = checked; createWizard.choiceEdited() } }
        Controls.CheckBox { text: qsTr("Text");  checked: createWizard.typeText;  onToggled: { createWizard.typeText  = checked; createWizard.choiceEdited() } }
        Controls.CheckBox { text: qsTr("Other"); checked: createWizard.typeOther; onToggled: { createWizard.typeOther = checked; createWizard.choiceEdited() } }
    }

    // ── Step 3: Previews ──────────────────────────────────────────────────────
    GridLayout {
        visible: createWizard.step === 3
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: Kirigami.Units.smallSpacing

        // Image
        Controls.Label { text: qsTr("Image"); color: applicationWindow().titleColor; Layout.columnSpan: 2 }
        Controls.Label { text: qsTr("Thumbnail"); opacity: 0.7 }
        Controls.CheckBox { checked: createWizard.imageThumbnail; onToggled: { createWizard.imageThumbnail = checked; createWizard.choiceEdited() } }

        // Video
        Controls.Label { text: qsTr("Video"); color: applicationWindow().titleColor; Layout.columnSpan: 2; Layout.topMargin: Kirigami.Units.smallSpacing }
        Controls.Label { text: qsTr("Poster frame"); opacity: 0.7 }
        Controls.CheckBox { checked: createWizard.videoPosterFrame; onToggled: { createWizard.videoPosterFrame = checked; createWizard.choiceEdited() } }
        Controls.Label { text: qsTr("Keyframe strip"); opacity: 0.7 }
        Controls.CheckBox { checked: createWizard.videoKeyframeStrip; onToggled: { createWizard.videoKeyframeStrip = checked; createWizard.choiceEdited() } }
        Controls.Label { text: qsTr("Number of frames"); opacity: 0.7; visible: createWizard.videoKeyframeStrip }
        Controls.SpinBox {
            visible: createWizard.videoKeyframeStrip
            from: 2; to: 32
            value: createWizard.videoKeyframeCount
            onValueModified: createWizard.videoKeyframeCount = value
        }
        Controls.Label { text: qsTr("Clip"); opacity: 0.7 }
        ComboBoxFitted {
            textRole: "text"; valueRole: "value"
            model: createWizard.clipModel
            currentIndex: indexOfValue(createWizard.videoClip)
            onActivated: { createWizard.videoClip = currentValue; createWizard.choiceEdited() }
        }
        Controls.Label { text: qsTr("Start (seconds)"); opacity: 0.7; visible: createWizard.videoClip === "partial" }
        Controls.SpinBox {
            visible: createWizard.videoClip === "partial"
            from: 0; to: 86400; editable: true
            value: createWizard.videoClipStart
            onValueModified: createWizard.videoClipStart = value
        }
        Controls.Label { text: qsTr("Duration (seconds)"); opacity: 0.7; visible: createWizard.videoClip === "partial" }
        Controls.SpinBox {
            visible: createWizard.videoClip === "partial"
            from: 1; to: 86400; editable: true
            value: createWizard.videoClipDuration
            onValueModified: createWizard.videoClipDuration = value
        }

        // Audio
        Controls.Label { text: qsTr("Audio"); color: applicationWindow().titleColor; Layout.columnSpan: 2; Layout.topMargin: Kirigami.Units.smallSpacing }
        Controls.Label { text: qsTr("Clip"); opacity: 0.7 }
        ComboBoxFitted {
            textRole: "text"; valueRole: "value"
            model: createWizard.clipModel
            currentIndex: indexOfValue(createWizard.audioClip)
            onActivated: { createWizard.audioClip = currentValue; createWizard.choiceEdited() }
        }
        Controls.Label { text: qsTr("Start (seconds)"); opacity: 0.7; visible: createWizard.audioClip === "partial" }
        Controls.SpinBox {
            visible: createWizard.audioClip === "partial"
            from: 0; to: 86400; editable: true
            value: createWizard.audioClipStart
            onValueModified: createWizard.audioClipStart = value
        }
        Controls.Label { text: qsTr("Duration (seconds)"); opacity: 0.7; visible: createWizard.audioClip === "partial" }
        Controls.SpinBox {
            visible: createWizard.audioClip === "partial"
            from: 1; to: 86400; editable: true
            value: createWizard.audioClipDuration
            onValueModified: createWizard.audioClipDuration = value
        }
        Controls.Label { text: qsTr("Waveform"); opacity: 0.7 }
        Controls.CheckBox { checked: createWizard.audioWaveform; onToggled: { createWizard.audioWaveform = checked; createWizard.choiceEdited() } }
    }

    // ── Step 4: Summary ───────────────────────────────────────────────────────
    GridLayout {
        visible: createWizard.step === 4
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: Kirigami.Units.smallSpacing

        Controls.Label { text: qsTr("Source path"); opacity: 0.7 }
        Controls.Label { text: createWizard.sourcePath; elide: Text.ElideMiddle; Layout.fillWidth: true }
        Controls.Label { text: qsTr("Storage"); opacity: 0.7 }
        Controls.Label { text: createWizard.storageName; elide: Text.ElideRight; Layout.fillWidth: true }
        Controls.Label { text: qsTr("Catalog name"); opacity: 0.7 }
        Controls.Label { text: createWizard.catalogName; elide: Text.ElideRight; Layout.fillWidth: true }
        Controls.Label { text: qsTr("Destination path"); opacity: 0.7 }
        Controls.Label { text: createWizard.destinationPath; elide: Text.ElideMiddle; Layout.fillWidth: true }

        Controls.Label { text: qsTr("File types"); opacity: 0.7 }
        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: {
                var names = []
                if (createWizard.typeImage) names.push(qsTr("Image"))
                if (createWizard.typeVideo) names.push(qsTr("Video"))
                if (createWizard.typeAudio) names.push(qsTr("Audio"))
                if (createWizard.typeText)  names.push(qsTr("Text"))
                if (createWizard.typeOther) names.push(qsTr("Other"))
                return names.join(", ")
            }
        }

        Controls.Label { text: qsTr("Previews"); color: applicationWindow().titleColor; Layout.columnSpan: 2; Layout.topMargin: Kirigami.Units.smallSpacing }
        Controls.Label { text: qsTr("Image") + " · " + qsTr("Thumbnail"); opacity: 0.7 }
        Controls.Label { text: createWizard.onOff(createWizard.imageThumbnail) }
        Controls.Label { text: qsTr("Video") + " · " + qsTr("Poster frame"); opacity: 0.7 }
        Controls.Label { text: createWizard.onOff(createWizard.videoPosterFrame) }
        Controls.Label { text: qsTr("Video") + " · " + qsTr("Keyframe strip"); opacity: 0.7 }
        Controls.Label {
            text: createWizard.videoKeyframeStrip
                  ? createWizard.onOff(true) + " · " + qsTr("Number of frames") + " " + createWizard.videoKeyframeCount
                  : createWizard.onOff(false)
        }
        Controls.Label { text: qsTr("Video") + " · " + qsTr("Clip"); opacity: 0.7 }
        Controls.Label {
            text: createWizard.clipText(createWizard.videoClip)
                  + (createWizard.videoClip === "partial"
                     ? " · " + qsTr("Start (seconds)") + " " + createWizard.videoClipStart
                       + " · " + qsTr("Duration (seconds)") + " " + createWizard.videoClipDuration
                     : "")
        }
        Controls.Label { text: qsTr("Audio") + " · " + qsTr("Clip"); opacity: 0.7 }
        Controls.Label {
            text: createWizard.clipText(createWizard.audioClip)
                  + (createWizard.audioClip === "partial"
                     ? " · " + qsTr("Start (seconds)") + " " + createWizard.audioClipStart
                       + " · " + qsTr("Duration (seconds)") + " " + createWizard.audioClipDuration
                     : "")
        }
        Controls.Label { text: qsTr("Audio") + " · " + qsTr("Waveform"); opacity: 0.7 }
        Controls.Label { text: createWizard.onOff(createWizard.audioWaveform) }
    }
}
