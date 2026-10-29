import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import QtQuick.Dialogs

// Enrichment options (fork, SpecMediaIntelligence.md MDI-F2..F6): where the
// derived files go, which file types, which previews and which AI analyses.
// One page, no steps. Used by the Create page in wizard mode, below the
// standard form's "Catalog definition", and by the enrichment dialog for a
// catalog that already exists. It only collects choices; the caller validates
// and starts the work.
ColumnLayout {
    id: createWizard
    spacing: Kirigami.Units.smallSpacing

    signal choiceEdited()                // hides the caller's validation message (MDI-F6)

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
    property int  videoKeyframeCount:  4
    property string videoClip:         "off"   // off / full / partial
    property int  videoClipStart:      0
    property int  videoClipDuration:   30
    property string audioClip:         "off"
    property int  audioClipStart:      0
    property int  audioClipDuration:   30
    property bool audioWaveform:       false

    // MDI-F5: local provider analyses
    property bool aiCaption: true          // images and video poster frames
    property bool aiSummary: true          // documents and recording transcripts
    property bool aiTranscript: true       // audio and video, local whisper.cpp
    property bool aiSegments:   true       // timecoded topic segments; needs the transcript
    property bool aiClassification: true   // in the collection's context, when it has one (D33)

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
            || aiCaption || aiSummary || aiTranscript || aiSegments
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

    // Profile saved with the catalog (MDI-F7, F8)
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
                },
                ai: { caption: aiCaption, summary: aiSummary,
                      transcript: aiTranscript || aiSegments, segments: aiSegments,
                      classification: aiClassification }
            }
        }
    }

    // Fills the form from a saved profile (enrichment dialog)
    function loadProfile(profile) {
        if (!profile || !profile.settings) return
        var s = profile.settings
        if (profile.destinationPath) destinationPath = profile.destinationPath
        var types = s.fileTypes || []
        typeImage = types.indexOf("Image") >= 0
        typeVideo = types.indexOf("Video") >= 0
        typeAudio = types.indexOf("Audio") >= 0
        typeText  = types.indexOf("Text")  >= 0
        typeOther = types.indexOf("Other") >= 0
        var im = s.image || {}, v = s.video || {}, a = s.audio || {}, ai = s.ai || {}
        imageThumbnail     = im.thumbnail !== false
        videoPosterFrame   = v.posterFrame !== false
        videoKeyframeStrip = v.keyframeStrip === true
        videoKeyframeCount = v.keyframeCount || 4
        videoClip          = v.clip || "off"
        videoClipStart     = v.clipStart || 0
        videoClipDuration  = v.clipDuration || 30
        audioClip          = a.clip || "off"
        audioClipStart     = a.clipStart || 0
        audioClipDuration  = a.clipDuration || 30
        audioWaveform      = a.waveform === true
        aiCaption          = ai.caption !== false
        aiSummary          = ai.summary !== false
        aiTranscript       = ai.transcript !== false
        aiSegments         = ai.segments !== false
        aiClassification   = ai.classification !== false
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

    component SectionHeader: RowLayout {
        property alias text: sectionLabel.text
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing
        Controls.Label { id: sectionLabel; color: applicationWindow().titleColor }
        Kirigami.Separator { Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter }
    }

    // ── Destination path ──────────────────────────────────────────────────────
    SectionHeader { text: qsTr("Destination path"); Layout.topMargin: 0 }
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

    // ── File types ────────────────────────────────────────────────────────────
    SectionHeader { text: qsTr("File types") }
    Flow {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing
        Repeater {
            model: [
                { key: "typeImage", text: qsTr("Image"), icon: "image-jpeg"      },
                { key: "typeVideo", text: qsTr("Video"), icon: "video-mp4"       },
                { key: "typeAudio", text: qsTr("Audio"), icon: "audio-x-mpeg"    },
                { key: "typeText",  text: qsTr("Text"),  icon: "view-list-text"  },
                { key: "typeOther", text: qsTr("Other"), icon: "document-open"   }
            ]
            delegate: Controls.Button {
                required property var modelData
                text: modelData.text
                icon.name: modelData.icon
                checkable: true
                checked: createWizard[modelData.key]
                onToggled: { createWizard[modelData.key] = checked; createWizard.choiceEdited() }
            }
        }
    }

    // ── Previews ──────────────────────────────────────────────────────────────
    SectionHeader { text: qsTr("Previews") }
    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: Kirigami.Units.smallSpacing

        // Image
        Kirigami.Icon { source: "image-jpeg"; implicitWidth: Kirigami.Units.iconSizes.small; implicitHeight: implicitWidth; visible: createWizard.typeImage }
        Controls.Switch {
            visible: createWizard.typeImage
            text: qsTr("Thumbnail")
            checked: createWizard.imageThumbnail
            onToggled: { createWizard.imageThumbnail = checked; createWizard.choiceEdited() }
        }

        // Video
        Kirigami.Icon { source: "video-mp4"; implicitWidth: Kirigami.Units.iconSizes.small; implicitHeight: implicitWidth; visible: createWizard.typeVideo; Layout.alignment: Qt.AlignTop; Layout.topMargin: Kirigami.Units.smallSpacing }
        Flow {
            visible: createWizard.typeVideo
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing
            Controls.Switch {
                text: qsTr("Poster frame")
                checked: createWizard.videoPosterFrame
                onToggled: { createWizard.videoPosterFrame = checked; createWizard.choiceEdited() }
            }
            Row {
                spacing: Kirigami.Units.smallSpacing
                Controls.Switch {
                    text: qsTr("Keyframe strip")
                    checked: createWizard.videoKeyframeStrip
                    onToggled: { createWizard.videoKeyframeStrip = checked; createWizard.choiceEdited() }
                }
                Controls.SpinBox {
                    visible: createWizard.videoKeyframeStrip
                    from: 2; to: 32
                    value: createWizard.videoKeyframeCount
                    onValueModified: createWizard.videoKeyframeCount = value
                    Controls.ToolTip.text: qsTr("Number of frames")
                    Controls.ToolTip.visible: hovered
                }
            }
            Row {
                spacing: Kirigami.Units.smallSpacing
                Controls.Label { text: qsTr("Clip"); anchors.verticalCenter: parent.verticalCenter }
                ComboBoxFitted {
                    textRole: "text"; valueRole: "value"
                    model: createWizard.clipModel
                    currentIndex: indexOfValue(createWizard.videoClip)
                    onActivated: { createWizard.videoClip = currentValue; createWizard.choiceEdited() }
                }
                Controls.SpinBox {
                    visible: createWizard.videoClip === "partial"
                    from: 0; to: 86400; editable: true
                    value: createWizard.videoClipStart
                    onValueModified: createWizard.videoClipStart = value
                    Controls.ToolTip.text: qsTr("Start (seconds)")
                    Controls.ToolTip.visible: hovered
                }
                Controls.SpinBox {
                    visible: createWizard.videoClip === "partial"
                    from: 1; to: 86400; editable: true
                    value: createWizard.videoClipDuration
                    onValueModified: createWizard.videoClipDuration = value
                    Controls.ToolTip.text: qsTr("Duration (seconds)")
                    Controls.ToolTip.visible: hovered
                }
            }
        }

        // Audio
        Kirigami.Icon { source: "audio-x-mpeg"; implicitWidth: Kirigami.Units.iconSizes.small; implicitHeight: implicitWidth; visible: createWizard.typeAudio; Layout.alignment: Qt.AlignTop; Layout.topMargin: Kirigami.Units.smallSpacing }
        Flow {
            visible: createWizard.typeAudio
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing
            Controls.Switch {
                text: qsTr("Waveform")
                checked: createWizard.audioWaveform
                onToggled: { createWizard.audioWaveform = checked; createWizard.choiceEdited() }
            }
            Row {
                spacing: Kirigami.Units.smallSpacing
                Controls.Label { text: qsTr("Clip"); anchors.verticalCenter: parent.verticalCenter }
                ComboBoxFitted {
                    textRole: "text"; valueRole: "value"
                    model: createWizard.clipModel
                    currentIndex: indexOfValue(createWizard.audioClip)
                    onActivated: { createWizard.audioClip = currentValue; createWizard.choiceEdited() }
                }
                Controls.SpinBox {
                    visible: createWizard.audioClip === "partial"
                    from: 0; to: 86400; editable: true
                    value: createWizard.audioClipStart
                    onValueModified: createWizard.audioClipStart = value
                    Controls.ToolTip.text: qsTr("Start (seconds)")
                    Controls.ToolTip.visible: hovered
                }
                Controls.SpinBox {
                    visible: createWizard.audioClip === "partial"
                    from: 1; to: 86400; editable: true
                    value: createWizard.audioClipDuration
                    onValueModified: createWizard.audioClipDuration = value
                    Controls.ToolTip.text: qsTr("Duration (seconds)")
                    Controls.ToolTip.visible: hovered
                }
            }
        }
    }

    // ── AI analysis (local model, MDI-F5) ─────────────────────────────────────
    SectionHeader { text: qsTr("AI analysis") }
    Controls.Switch {
        enabled: createWizard.typeImage || createWizard.typeVideo
        text: qsTr("Caption and tags")
        checked: createWizard.aiCaption
        onToggled: { createWizard.aiCaption = checked; createWizard.choiceEdited() }
    }
    Controls.Switch {
        enabled: createWizard.typeText || createWizard.typeAudio || createWizard.typeVideo
        text: qsTr("Summary and excerpts")
        checked: createWizard.aiSummary
        onToggled: { createWizard.aiSummary = checked; createWizard.choiceEdited() }
    }
    Controls.Switch {
        enabled: createWizard.typeAudio || createWizard.typeVideo
        text: qsTr("Transcript")
        checked: createWizard.aiTranscript || createWizard.aiSegments
        onToggled: {
            createWizard.aiTranscript = checked
            if (!checked) createWizard.aiSegments = false   // segments are cut from the transcript
            createWizard.choiceEdited()
        }
    }
    Controls.Switch {
        // Uses the Context page; off while the collection has none (MDI-F100)
        enabled: Object.keys(appManager1.getContext()).length > 0
        text: qsTr("Classification")
        checked: createWizard.aiClassification
        onToggled: { createWizard.aiClassification = checked; createWizard.choiceEdited() }
    }
    Controls.Switch {
        enabled: createWizard.typeAudio || createWizard.typeVideo
        text: qsTr("Topic segments")
        checked: createWizard.aiSegments
        onToggled: {
            createWizard.aiSegments = checked
            if (checked) createWizard.aiTranscript = true
            createWizard.choiceEdited()
        }
    }
    Controls.Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        opacity: 0.7
        text: qsTr("Analysed on this computer by the local model (%1). Nothing is sent to the internet.")
                  .arg(appManager1.getEnrichmentTools().visionModel)
    }
}
