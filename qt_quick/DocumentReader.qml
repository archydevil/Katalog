import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami

// Reading a document (fork, D39, owner 2026-10-30): what the study of the
// document found, in reading order — the synthesis, the key facts, the tags,
// the links to the archive's context, then the document fragment by fragment,
// each with its summary. Exported as an HTML page to share.
Kirigami.ScrollablePage {
    id: reader
    property int    catalogId: 0
    property string fullPath: ""
    property string fileName: fullPath.slice(fullPath.lastIndexOf("/") + 1)
    property var    study: ({})
    property Kirigami.Action escapeAction: readerCloseAction   // Esc closes the reading (KBS-F1)

    title: qsTr("Reading: %1").arg(fileName)
    background: Rectangle { color: KTheme.ground }

    function load() { reader.study = appManager1.documentStudy(reader.catalogId, reader.fullPath) }
    Component.onCompleted: load()
    Connections {
        target: appManager1
        function onMdiRevisionChanged() { reader.load() }
    }

    actions: [
        Kirigami.Action {
            text: qsTr("Export HTML")
            icon.name: "document-export"
            enabled: reader.study.available === true
            onTriggered: exportDialog.open()
        },
        Kirigami.Action {
            id: readerCloseAction
            text: qsTr("Close")
            icon.name: "view-close"
            onTriggered: applicationWindow().pageStack.layers.pop()
        }
    ]

    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("HTML page (*.html)")]
        currentFile: "file:///" + reader.fileName.replace(/\.[^.]*$/, "") + ".html"
        onAccepted: {
            const error = appManager1.exportHtml("file", reader.catalogId, reader.fullPath,
                                                 appManager1.pathFromFileUrl(selectedFile.toString()))
            applicationWindow().showPassiveNotification(error !== "" ? error : qsTr("Exported: %1").arg(appManager1.pathFromFileUrl(selectedFile.toString())))
        }
    }

    // A line of facts of one kind: its name, then each fact as a tag
    component FactRow: RowLayout {
        id: factRow
        property string label
        property var values: []
        property string tone: "neutral"
        visible: values.length > 0
        Layout.fillWidth: true
        spacing: 10
        Controls.Label {
            Layout.preferredWidth: 120
            Layout.alignment: Qt.AlignTop
            text: factRow.label
            color: KTheme.text3
            font.pixelSize: KTheme.note
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: factRow.values
                delegate: KInfoTag {
                    required property var modelData
                    text: typeof modelData === "string" ? modelData : (modelData.name || modelData.title || "")
                    tone: factRow.tone
                }
            }
        }
    }

    ColumnLayout {
        spacing: KTheme.gap

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: reader.study.available !== true
            type: Kirigami.MessageType.Information
            text: qsTr("This document has not been studied yet. The study runs with the AI analysis of its drive (Study of the documents), on this computer.")
        }

        // What it is
        KCard {
            visible: reader.study.available === true
            Layout.fillWidth: true
            KSectionHeader {
                level: 1
                title: reader.fileName
                description: [reader.study.type, reader.study.date, reader.study.author].filter(x => x).join(" · ")
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                KInfoTag { visible: reader.study.ocr === true; text: qsTr("Read with OCR"); tone: "info" }
                KInfoTag { visible: (reader.study.language || "") !== ""; text: reader.study.language || "" }
                KInfoTag { text: qsTr("%1 fragments").arg((reader.study.fragments || []).length) }
            }
            KSectionHeader { level: 2; title: qsTr("Synthesis") }
            Controls.Label {
                Layout.fillWidth: true
                text: reader.study.synthesis || ""
                wrapMode: Text.Wrap
                color: KTheme.text
                font.pixelSize: KTheme.body + 1
                lineHeight: 1.25
            }
        }

        // The facts it states
        KCard {
            visible: reader.study.available === true
            Layout.fillWidth: true
            KSectionHeader { level: 2; title: qsTr("Key facts") }
            FactRow { label: qsTr("Dates");         values: (reader.study.keyFacts || {}).dates || [] }
            FactRow { label: qsTr("People");        values: (reader.study.keyFacts || {}).people || []; tone: "ok" }
            FactRow { label: qsTr("Places");        values: (reader.study.keyFacts || {}).places || [] }
            FactRow { label: qsTr("Organisations"); values: (reader.study.keyFacts || {}).organisations || [] }
            FactRow { label: qsTr("Events");        values: (reader.study.keyFacts || {}).events || [] }
            FactRow { label: qsTr("Recipients");    values: reader.study.recipients || [] }
            FactRow { label: qsTr("Tags");          values: reader.study.tags || []; tone: "ai" }
        }

        // How it meets the archive's context
        KCard {
            visible: reader.study.available === true
                     && ((reader.study.glossary || []).length + ((reader.study.related || {}).kb || []).length
                         + ((reader.study.related || {}).people || []).length) > 0
            Layout.fillWidth: true
            KSectionHeader {
                level: 2
                title: qsTr("In the archive's context")
                description: qsTr("The glossary terms it uses, the reference documents and the people of the archive it relates to.")
            }
            FactRow { label: qsTr("Glossary");            values: reader.study.glossary || []; tone: "info" }
            FactRow { label: qsTr("Reference documents"); values: (reader.study.related || {}).kb || [] }
            FactRow { label: qsTr("People of the archive"); values: (reader.study.related || {}).people || []; tone: "ok" }
        }

        // The document, fragment by fragment
        KSectionHeader {
            visible: reader.study.available === true
            level: 2
            title: qsTr("The document, fragment by fragment")
            description: qsTr("Each part with its summary; its text opens below it.")
        }
        Repeater {
            model: reader.study.fragments || []
            delegate: KCard {
                id: fragment
                required property var modelData
                property bool open: false
                Layout.fillWidth: true
                RowLayout {
                    Layout.fillWidth: true
                    KSectionHeader {
                        level: 3
                        title: fragment.modelData.title || qsTr("Part %1").arg(fragment.modelData.index + 1)
                        description: fragment.modelData.pages ? qsTr("Pages %1").arg(fragment.modelData.pages) : ""
                    }
                }
                Controls.Label {
                    Layout.fillWidth: true
                    text: fragment.modelData.summary || ""
                    wrapMode: Text.Wrap
                    color: KTheme.text
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: (fragment.modelData.tags || []).concat(fragment.modelData.entities || [])
                        delegate: KInfoTag {
                            required property var modelData
                            text: typeof modelData === "string" ? modelData : (modelData.name || "")
                            tone: "ai"
                        }
                    }
                }
                KButton {
                    variant: "link"
                    icon.name: fragment.open ? "go-up" : "go-down"
                    text: fragment.open ? qsTr("Hide the text") : qsTr("Show the text")
                    onClicked: fragment.open = !fragment.open
                }
                Controls.TextArea {
                    visible: fragment.open
                    Layout.fillWidth: true
                    readOnly: true
                    wrapMode: TextEdit.Wrap
                    text: fragment.modelData.text || ""
                    font.family: KTheme.mono
                    Accessible.name: qsTr("Text of the fragment")
                }
            }
        }
    }
}
