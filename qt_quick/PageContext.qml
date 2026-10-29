import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Dialogs as Dialogs
import org.kde.kirigami as Kirigami

// Context page (fork, SpecMediaIntelligence.md D33, MDI-F94..F107): the domain
// the AI interprets the collection in — the archive, a glossary, the owner's
// categories, known names, notes per catalog and reference documents. Saving
// re-runs nothing: new files use the context, the others are re-interpreted
// on request (MDI-F99, MDI-F103).
ColumnLayout {
    id: pageContext
    spacing: Kirigami.Units.largeSpacing

    property bool dirty: false
    property var catalogs: []
    property var documents: []
    property bool drafting: false
    readonly property int catalogDeviceId: catalogBox.currentIndex >= 0 && catalogs.length > 0
                                           ? catalogs[catalogBox.currentIndex].deviceId : -1
    readonly property bool hasSavedContext: savedContextEmpty === false
    property var savedContextEmpty: true

    // Called by Main.qml when the page is shown
    function setShown(shown) {
        if (shown)
            load()
    }

    function splitList(text) {
        return String(text || "").split(",").map(function (s) { return s.trim() })
                                 .filter(function (s) { return s.length > 0 })
    }
    function contextIsEmpty(c) {
        return !(c.organisation || c.period || c.domain || c.notes
                 || (c.glossary || []).length || (c.categories || []).length || (c.entities || []).length)
    }

    function load() {
        var c = appManager1.getContext()
        organisationField.text = c.organisation || ""
        periodField.text       = c.period || ""
        domainField.text       = c.domain || ""
        languageField.text     = c.language || ""
        styleField.text        = c.style || ""
        notesField.text        = c.notes || ""
        transcriptHintBox.checked = c.transcriptHint === true
        glossaryModel.clear()
        for (var g of (c.glossary || []))
            glossaryModel.append({ term: g.term || "", preferred: g.preferred || "",
                                   aliases: (g.aliases || []).join(", "), definition: g.definition || "" })
        categoryModel.clear()
        for (var k of (c.categories || []))
            categoryModel.append({ name: k.name || "", description: k.description || "",
                                   values: (k.values || []).join(", "), closed: k.closed === true,
                                   multiple: k.multiple !== false })
        entityModel.clear()
        for (var e of (c.entities || []))
            entityModel.append({ name: e.name || "", kind: e.kind || "other",
                                 aliases: (e.aliases || []).join(", "), notes: e.notes || "" })
        savedContextEmpty = contextIsEmpty(c)
        catalogs = appManager1.contextCatalogs()
        documents = appManager1.knowledgeDocuments()
        loadCatalogNotes()
        dirty = false
    }
    function loadCatalogNotes() {
        catalogNotesField.text = catalogDeviceId >= 0 ? appManager1.getCatalogNotes(catalogDeviceId) : ""
        catalogNotesField.edited = false
    }

    function collect() {
        var glossary = [], categories = [], entities = []
        for (var i = 0; i < glossaryModel.count; ++i) {
            var g = glossaryModel.get(i)
            if (g.term.trim() || g.preferred.trim())
                glossary.push({ term: g.term.trim(), preferred: g.preferred.trim(),
                                aliases: splitList(g.aliases), definition: g.definition.trim() })
        }
        for (i = 0; i < categoryModel.count; ++i) {
            var k = categoryModel.get(i)
            if (k.name.trim())
                categories.push({ name: k.name.trim(), description: k.description.trim(),
                                  values: splitList(k.values), closed: k.closed, multiple: k.multiple })
        }
        for (i = 0; i < entityModel.count; ++i) {
            var e = entityModel.get(i)
            if (e.name.trim())
                entities.push({ name: e.name.trim(), kind: e.kind, aliases: splitList(e.aliases), notes: e.notes.trim() })
        }
        return { organisation: organisationField.text.trim(), period: periodField.text.trim(),
                 domain: domainField.text.trim(), language: languageField.text.trim(),
                 style: styleField.text.trim(), notes: notesField.text.trim(),
                 transcriptHint: transcriptHintBox.checked,
                 glossary: glossary, categories: categories, entities: entities }
    }

    function save() {
        var c = collect()
        var ok = appManager1.saveContext(c)
        if (ok && catalogDeviceId >= 0 && catalogNotesField.edited)
            ok = appManager1.saveCatalogNotes(catalogDeviceId, catalogNotesField.text)
        if (ok) {
            savedContextEmpty = contextIsEmpty(c)
            dirty = false
            catalogNotesField.edited = false
            applicationWindow().showPassiveNotification(qsTr("Context saved"))
        }
    }

    // The draft fills what is empty and adds what is missing; nothing is saved
    function applyDraft(d) {
        if (!organisationField.text && d.organisation) organisationField.text = d.organisation
        if (!periodField.text && d.period)             periodField.text = d.period
        if (!domainField.text && d.domain)             domainField.text = d.domain
        if (!styleField.text && d.style)               styleField.text = d.style
        function has(model, role, value) {
            for (var i = 0; i < model.count; ++i)
                if (String(model.get(i)[role]).toLowerCase() === String(value).toLowerCase())
                    return true
            return false
        }
        for (var g of (d.glossary || []))
            if (!has(glossaryModel, "term", g.term))
                glossaryModel.append({ term: g.term || "", preferred: g.preferred || "",
                                       aliases: (g.aliases || []).join(", "), definition: g.definition || "" })
        for (var k of (d.categories || []))
            if (!has(categoryModel, "name", k.name))
                categoryModel.append({ name: k.name || "", description: k.description || "",
                                       values: (k.values || []).join(", "), closed: k.closed === true,
                                       multiple: k.multiple !== false })
        for (var e of (d.entities || []))
            if (!has(entityModel, "name", e.name))
                entityModel.append({ name: e.name || "", kind: e.kind || "other",
                                     aliases: (e.aliases || []).join(", "), notes: e.notes || "" })
        dirty = true
    }
    function draft() {
        if (catalogDeviceId < 0)
            return
        drafting = true
        appManager1.draftContext(catalogDeviceId)
    }

    Connections {
        target: appManager1
        function onContextDraftReady(draft, error) {
            pageContext.drafting = false
            if (error !== "" || Object.keys(draft).length === 0) {
                applicationWindow().showPassiveNotification(qsTr("The draft could not be made: %1").arg(error))
                return
            }
            pageContext.applyDraft(draft)
            applicationWindow().showPassiveNotification(qsTr("Draft ready: review it, then save."))
        }
        function onKnowledgeStateChanged() {
            pageContext.documents = appManager1.knowledgeDocuments()
        }
    }

    ListModel { id: glossaryModel }
    ListModel { id: categoryModel }
    ListModel { id: entityModel }

    // ── The archive ─────────────────────────────────────────────────────────
    Kirigami.Heading { text: qsTr("Archive"); level: 3 }
    Kirigami.FormLayout {
        Layout.fillWidth: true
        Controls.TextField {
            id: organisationField
            Kirigami.FormData.label: qsTr("Organisation")
            Layout.fillWidth: true
            onTextEdited: pageContext.dirty = true
        }
        Controls.TextField {
            id: periodField
            Kirigami.FormData.label: qsTr("Period")
            Layout.fillWidth: true
            onTextEdited: pageContext.dirty = true
        }
        Controls.TextArea {
            id: domainField
            Kirigami.FormData.label: qsTr("Domain")
            Layout.fillWidth: true
            wrapMode: TextEdit.Wrap
            onTextChanged: if (activeFocus) pageContext.dirty = true
        }
        Controls.TextField {
            id: languageField
            Kirigami.FormData.label: qsTr("Language")
            Layout.fillWidth: true
            onTextEdited: pageContext.dirty = true
        }
        Controls.TextArea {
            id: styleField
            Kirigami.FormData.label: qsTr("Narrative style and terminology")
            Layout.fillWidth: true
            wrapMode: TextEdit.Wrap
            onTextChanged: if (activeFocus) pageContext.dirty = true
        }
        Controls.TextArea {
            id: notesField
            Kirigami.FormData.label: qsTr("Notes")
            Layout.fillWidth: true
            wrapMode: TextEdit.Wrap
            onTextChanged: if (activeFocus) pageContext.dirty = true
        }
        Controls.CheckBox {
            id: transcriptHintBox
            Kirigami.FormData.label: ""
            text: qsTr("Suggest the glossary terms to the transcription")
            onToggled: pageContext.dirty = true
        }
    }

    // ── Glossary ───────────────────────────────────────────────────────────
    Kirigami.Heading { text: qsTr("Glossary"); level: 3 }
    Repeater {
        model: glossaryModel
        delegate: RowLayout {
            required property int index
            required property string term
            required property string preferred
            required property string aliases
            required property string definition
            Layout.fillWidth: true
            Controls.TextField {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                placeholderText: qsTr("Term")
                text: term
                onTextEdited: { glossaryModel.setProperty(index, "term", text); pageContext.dirty = true }
            }
            Controls.TextField {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 9
                placeholderText: qsTr("Preferred form")
                text: preferred
                onTextEdited: { glossaryModel.setProperty(index, "preferred", text); pageContext.dirty = true }
            }
            Controls.TextField {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                placeholderText: qsTr("Also written as")
                text: aliases
                onTextEdited: { glossaryModel.setProperty(index, "aliases", text); pageContext.dirty = true }
            }
            Controls.TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("Definition")
                text: definition
                onTextEdited: { glossaryModel.setProperty(index, "definition", text); pageContext.dirty = true }
            }
            Controls.ToolButton {
                icon.name: "list-remove"
                Controls.ToolTip.text: qsTr("Remove")
                Controls.ToolTip.visible: hovered
                onClicked: { glossaryModel.remove(index); pageContext.dirty = true }
            }
        }
    }
    Controls.Button {
        icon.name: "list-add"
        text: qsTr("Add")
        onClicked: { glossaryModel.append({ term: "", preferred: "", aliases: "", definition: "" }); pageContext.dirty = true }
    }

    // ── Categories ─────────────────────────────────────────────────────────
    Kirigami.Heading { text: qsTr("Categories"); level: 3 }
    Repeater {
        model: categoryModel
        delegate: RowLayout {
            required property int index
            required property string name
            required property string description
            required property string values
            required property bool closed
            required property bool multiple
            Layout.fillWidth: true
            Controls.TextField {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                placeholderText: qsTr("Name")
                text: name
                onTextEdited: { categoryModel.setProperty(index, "name", text); pageContext.dirty = true }
            }
            Controls.TextField {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                placeholderText: qsTr("Definition")
                text: description
                onTextEdited: { categoryModel.setProperty(index, "description", text); pageContext.dirty = true }
            }
            Controls.TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("Values")
                text: values
                onTextEdited: { categoryModel.setProperty(index, "values", text); pageContext.dirty = true }
            }
            Controls.CheckBox {
                text: qsTr("Only these values")
                checked: closed
                onToggled: { categoryModel.setProperty(index, "closed", checked); pageContext.dirty = true }
            }
            Controls.CheckBox {
                text: qsTr("More than one value")
                checked: multiple
                onToggled: { categoryModel.setProperty(index, "multiple", checked); pageContext.dirty = true }
            }
            Controls.ToolButton {
                icon.name: "list-remove"
                Controls.ToolTip.text: qsTr("Remove")
                Controls.ToolTip.visible: hovered
                onClicked: { categoryModel.remove(index); pageContext.dirty = true }
            }
        }
    }
    Controls.Button {
        icon.name: "list-add"
        text: qsTr("Add")
        onClicked: {
            categoryModel.append({ name: "", description: "", values: "", closed: false, multiple: true })
            pageContext.dirty = true
        }
    }

    // ── Known names: people, places, events, bodies ────────────────────────
    Kirigami.Heading { text: qsTr("Known names"); level: 3 }
    Repeater {
        model: entityModel
        delegate: RowLayout {
            required property int index
            required property string name
            required property string kind
            required property string aliases
            required property string notes
            Layout.fillWidth: true
            Controls.TextField {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                placeholderText: qsTr("Name")
                text: name
                onTextEdited: { entityModel.setProperty(index, "name", text); pageContext.dirty = true }
            }
            Controls.ComboBox {
                readonly property var kinds: ["person", "place", "event", "organisation", "other"]
                model: [qsTr("Person"), qsTr("Place"), qsTr("Event"), qsTr("Organisation"), qsTr("Other")]
                currentIndex: Math.max(0, kinds.indexOf(kind))
                onActivated: { entityModel.setProperty(index, "kind", kinds[currentIndex]); pageContext.dirty = true }
            }
            Controls.TextField {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                placeholderText: qsTr("Also written as")
                text: aliases
                onTextEdited: { entityModel.setProperty(index, "aliases", text); pageContext.dirty = true }
            }
            Controls.TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("Notes")
                text: notes
                onTextEdited: { entityModel.setProperty(index, "notes", text); pageContext.dirty = true }
            }
            Controls.ToolButton {
                icon.name: "list-remove"
                Controls.ToolTip.text: qsTr("Remove")
                Controls.ToolTip.visible: hovered
                onClicked: { entityModel.remove(index); pageContext.dirty = true }
            }
        }
    }
    Controls.Button {
        icon.name: "list-add"
        text: qsTr("Add")
        onClicked: { entityModel.append({ name: "", kind: "person", aliases: "", notes: "" }); pageContext.dirty = true }
    }

    // ── One catalog: its notes, its draft ──────────────────────────────────
    Kirigami.Heading { text: qsTr("Catalog"); level: 3 }
    RowLayout {
        Layout.fillWidth: true
        Controls.ComboBox {
            id: catalogBox
            Layout.fillWidth: true
            model: pageContext.catalogs.map(function (c) { return c.name })
            onCurrentIndexChanged: pageContext.loadCatalogNotes()
        }
        Controls.Button {
            icon.name: "tools-wizard"
            text: qsTr("Draft from the catalog")
            enabled: pageContext.catalogDeviceId >= 0 && !pageContext.drafting
            onClicked: pageContext.draft()
        }
        Controls.BusyIndicator {
            visible: pageContext.drafting
            running: visible
            implicitWidth: Kirigami.Units.gridUnit * 1.5
            implicitHeight: implicitWidth
        }
    }
    Controls.TextArea {
        id: catalogNotesField
        property bool edited: false
        Layout.fillWidth: true
        placeholderText: qsTr("Notes for this catalog")
        wrapMode: TextEdit.Wrap
        enabled: pageContext.catalogDeviceId >= 0
        onTextChanged: if (activeFocus) { edited = true; pageContext.dirty = true }
    }

    // ── Reference documents (local knowledge base) ─────────────────────────
    Kirigami.Heading { text: qsTr("Reference documents"); level: 3 }
    Controls.Label {
        visible: appManager1.knowledgeIsRunning || appManager1.knowledgeStatusText !== ""
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        textFormat: Text.StyledText
        opacity: 0.8
        text: appManager1.knowledgeStatusText.replace(/^(&nbsp;)+/, "")
    }
    Repeater {
        model: pageContext.documents
        delegate: RowLayout {
            id: documentRow
            required property var modelData
            Layout.fillWidth: true
            Kirigami.Icon {
                source: documentRow.modelData.status === "indexed" ? "dialog-ok-apply"
                        : documentRow.modelData.status === "failed" ? "dialog-error" : "view-refresh"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: implicitWidth
            }
            Controls.Label {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: documentRow.modelData.title
                Controls.ToolTip.text: documentRow.modelData.path + (documentRow.modelData.reason ? "\n" + documentRow.modelData.reason : "")
                Controls.ToolTip.visible: hoverArea.containsMouse
                MouseArea { id: hoverArea; anchors.fill: parent; hoverEnabled: true }
            }
            Controls.Label {
                opacity: 0.7
                text: documentRow.modelData.status === "indexed" ? qsTr("%1 passages").arg(documentRow.modelData.passages)
                      : documentRow.modelData.status === "failed" ? qsTr("Error") : qsTr("To be indexed")
            }
            Controls.ToolButton {
                icon.name: "list-remove"
                Controls.ToolTip.text: qsTr("Remove")
                Controls.ToolTip.visible: hovered
                onClicked: appManager1.removeKnowledgeDocument(documentRow.modelData.id)
            }
        }
    }
    RowLayout {
        Controls.Button {
            icon.name: "document-open"
            text: qsTr("Add files")
            onClicked: documentsDialog.open()
        }
        Controls.Button {
            icon.name: "folder-open"
            text: qsTr("Add folder")
            onClicked: documentsFolderDialog.open()
        }
    }
    Dialogs.FileDialog {
        id: documentsDialog
        fileMode: Dialogs.FileDialog.OpenFiles
        onAccepted: appManager1.addKnowledgePaths(selectedFiles)
    }
    Dialogs.FolderDialog {
        id: documentsFolderDialog
        onAccepted: appManager1.addKnowledgePaths([selectedFolder])
    }

    // ── Re-interpretation of files already analysed (MDI-F103) ─────────────
    property int reinterpretFiles: 0
    property int reinterpretSeconds: 0
    function durationText(seconds) {
        var h = Math.floor(seconds / 3600), m = Math.round((seconds % 3600) / 60)
        return h > 0 ? h + " h " + m + " min" : Math.max(1, m) + " min"
    }
    function openReinterpret() {
        reinterpretFolder.text = ""
        reinterpretType.currentIndex = 0
        updateEstimate()
        reinterpretDialog.open()
    }
    function updateEstimate() {
        var e = appManager1.reinterpretEstimate(catalogDeviceId, reinterpretFolder.text,
                                                reinterpretType.types[reinterpretType.currentIndex])
        reinterpretFiles = e.files || 0
        reinterpretSeconds = e.seconds || 0
    }
    Controls.Dialog {
        id: reinterpretDialog
        title: qsTr("Re-interpret with the context")
        modal: true
        anchors.centerIn: Controls.Overlay.overlay
        standardButtons: Controls.Dialog.Ok | Controls.Dialog.Cancel
        onAccepted: {
            appManager1.startReinterpretation(pageContext.catalogDeviceId, reinterpretFolder.text,
                                              reinterpretType.types[reinterpretType.currentIndex])
            applicationWindow().showActivityPage()
        }
        contentItem: Kirigami.FormLayout {
            Controls.Label {
                Kirigami.FormData.label: qsTr("Catalog")
                text: catalogBox.currentText
            }
            RowLayout {
                Kirigami.FormData.label: qsTr("Folder")
                Controls.TextField {
                    id: reinterpretFolder
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                    onEditingFinished: pageContext.updateEstimate()
                }
                Controls.Button {
                    icon.name: "folder-open"
                    Controls.ToolTip.text: qsTr("Select the path")
                    Controls.ToolTip.visible: hovered
                    onClicked: reinterpretFolderDialog.open()
                }
            }
            Controls.ComboBox {
                id: reinterpretType
                Kirigami.FormData.label: qsTr("File type")
                readonly property var types: ["", "image", "video", "audio", "text"]
                model: [qsTr("All"), qsTr("Image"), qsTr("Video"), qsTr("Audio"), qsTr("Text")]
                onActivated: pageContext.updateEstimate()
            }
            Controls.Label {
                Kirigami.FormData.label: qsTr("Files")
                text: qsTr("%1, about %2").arg(pageContext.reinterpretFiles)
                                          .arg(pageContext.durationText(pageContext.reinterpretSeconds))
            }
        }
    }
    Dialogs.FolderDialog {
        id: reinterpretFolderDialog
        onAccepted: {
            reinterpretFolder.text = appManager1.normalizeSourcePath(selectedFolder.toString())
            pageContext.updateEstimate()
        }
    }
}
