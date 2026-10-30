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
//
// Redesign phase R4 (D34), "Archive knowledge": the page intro, a segmented
// choice of three parts made of cards, and beside them (below them when the
// window is narrow) the two actions, re-interpret and draft. Every editing
// behaviour is the one of Context 1; only the presentation changed.
ColumnLayout {
    id: pageContext
    spacing: 20

    property bool dirty: false
    property var catalogs: []
    property var documents: []
    property bool drafting: false
    readonly property int catalogDeviceId: catalogBox.currentIndex >= 0 && catalogs.length > 0
                                           ? catalogs[catalogBox.currentIndex].deviceId : -1
    readonly property bool hasSavedContext: savedContextEmpty === false
    property var savedContextEmpty: true

    // The part shown: 0 archive and categories, 1 glossary and known names,
    // 2 reference documents. Every part stays built, so load() and collect()
    // always reach every field.
    property int part: 0
    // The two action cards sit beside the parts when there is room
    readonly property bool wide: width >= 960
    readonly property int passageCount: {
        var n = 0
        for (var d of documents)
            n += d.passages || 0
        return n
    }

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
    // A catalog chosen elsewhere (a drive card on Home): shown in both the
    // re-interpret and the draft cards, which share one catalog choice
    function selectCatalog(deviceId) {
        for (var i = 0; i < catalogs.length; ++i) {
            if (catalogs[i].deviceId === deviceId) {
                catalogBox.currentIndex = i
                return true
            }
        }
        return false
    }
    // ...and its re-interpretation asked for at once, when it can run
    function reinterpretCatalog(deviceId) {
        // After the page's own load, once the catalog list fills the choice
        Qt.callLater(function() {
            if (pageContext.selectCatalog(deviceId) && pageContext.hasSavedContext && !pageContext.dirty)
                pageContext.openReinterpret()
        })
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
            applicationWindow().showPassiveNotification(qsTr("Draft ready, not saved yet."))
        }
        function onKnowledgeStateChanged() {
            pageContext.documents = appManager1.knowledgeDocuments()
        }
    }

    ListModel { id: glossaryModel }
    ListModel { id: categoryModel }
    ListModel { id: entityModel }

    // ── Building blocks of this page ───────────────────────────────────────
    // The name of a field, above it or on its left
    component FieldLabel: Controls.Label {
        color: KTheme.text3
        font.pixelSize: KTheme.note
        font.weight: Font.DemiBold
        wrapMode: Text.WordWrap
    }
    // A line of explanation inside a card
    component Explanation: Controls.Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: KTheme.text2
    }
    // One row of a list being edited (a term, a category, a name)
    component EntryBox: Rectangle {
        default property alias content: entryColumn.data
        Layout.fillWidth: true
        implicitHeight: entryColumn.implicitHeight + 20
        radius: KTheme.radiusItem
        color: KTheme.surface2
        border.color: KTheme.line
        border.width: 1
        ColumnLayout {
            id: entryColumn
            x: 10
            y: 10
            width: parent.width - 20
            spacing: 8
        }
    }
    // Removes one row; an icon-only button, so its text is the tooltip
    component RemoveButton: IconButton {
        icon.name: "list-remove"
        text: qsTr("Remove")
        flat: true
        implicitWidth: 40
        implicitHeight: 40
        Layout.alignment: Qt.AlignTop
    }

    // ── Page intro ─────────────────────────────────────────────────────────
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10
        KSectionHeader {
            level: 1
            title: qsTr("Archive knowledge")
            description: qsTr("What the AI needs to know to describe and classify the files in the archive's own language. Nothing is redone until you ask for it.")
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            KInfoTag {
                text: pageContext.hasSavedContext ? qsTr("Context active") : qsTr("No context yet")
                tone: pageContext.hasSavedContext ? "ok" : "neutral"
            }
            KInfoTag {
                visible: pageContext.dirty
                text: qsTr("Changes not saved")
                tone: "warn"
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: pageContext.wide ? 2 : 1
        columnSpacing: 20
        rowSpacing: 20

        // ── The three parts ────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: KTheme.gap

            // Segmented choice of the part shown
            Rectangle {
                id: partBar
                // One row when the three fit, else one under the other
                readonly property real oneRowWidth: {
                    var w = 8 + 4 * (partRepeater.count - 1)
                    for (var i = 0; i < partBar.partsBuilt; ++i) {
                        var b = partRepeater.itemAt(i)
                        if (b)
                            w += b.implicitWidth
                    }
                    return w
                }
                property int partsBuilt: 0
                Layout.fillWidth: partGrid.columns === 1
                implicitWidth: partGrid.implicitWidth + 8
                implicitHeight: partGrid.implicitHeight + 8
                radius: KTheme.radiusControl
                color: KTheme.surface
                border.color: KTheme.line
                border.width: 1

                GridLayout {
                    id: partGrid
                    x: 4
                    y: 4
                    width: parent.width - 8
                    columns: partBar.parent.width >= partBar.oneRowWidth ? 3 : 1
                    columnSpacing: 4
                    rowSpacing: 4

                    Repeater {
                        id: partRepeater
                        model: [qsTr("Archive and categories"), qsTr("Glossary and known names"), qsTr("Reference documents")]
                        onItemAdded: partBar.partsBuilt++
                        delegate: Controls.AbstractButton {
                            id: partButton
                            required property int index
                            required property string modelData
                            readonly property bool selected: pageContext.part === index
                            Layout.fillWidth: true
                            implicitHeight: 40
                            leftPadding: 14
                            rightPadding: 14
                            hoverEnabled: true
                            text: modelData
                            Accessible.role: Accessible.PageTab
                            Accessible.selectable: true
                            Accessible.selected: partButton.selected
                            onClicked: pageContext.part = index
                            background: Rectangle {
                                radius: KTheme.radiusItem
                                color: partButton.selected ? KTheme.accentSoft
                                     : partButton.hovered ? KTheme.hover : "transparent"
                                border.width: partButton.visualFocus ? 2 : 0
                                border.color: KTheme.accent
                            }
                            contentItem: Controls.Label {
                                text: partButton.text
                                color: partButton.selected ? KTheme.accentText : KTheme.text2
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            // ── Part 1: archive and categories ─────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                visible: pageContext.part === 0
                spacing: KTheme.gap

                KCard {
                    Layout.fillWidth: true
                    KSectionHeader {
                        level: 3
                        title: qsTr("Archive")
                        description: qsTr("Who produced the archive, when, about what, and how it is written.")
                    }
                    GridLayout {
                        id: archiveGrid
                        readonly property bool twoColumns: width >= 520
                        Layout.fillWidth: true
                        columns: twoColumns ? 2 : 1
                        columnSpacing: 16
                        rowSpacing: twoColumns ? 10 : 4

                        FieldLabel {
                            text: qsTr("Organisation")
                            Layout.preferredWidth: archiveGrid.twoColumns ? 170 : -1
                            Layout.fillWidth: !archiveGrid.twoColumns
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Controls.TextField {
                            id: organisationField
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Organisation")
                            onTextEdited: pageContext.dirty = true
                        }
                        FieldLabel {
                            text: qsTr("Period")
                            Layout.preferredWidth: archiveGrid.twoColumns ? 170 : -1
                            Layout.fillWidth: !archiveGrid.twoColumns
                            Layout.topMargin: archiveGrid.twoColumns ? 0 : 6
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Controls.TextField {
                            id: periodField
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Period")
                            onTextEdited: pageContext.dirty = true
                        }
                        FieldLabel {
                            text: qsTr("Domain")
                            Layout.preferredWidth: archiveGrid.twoColumns ? 170 : -1
                            Layout.fillWidth: !archiveGrid.twoColumns
                            Layout.topMargin: 6
                            Layout.alignment: Qt.AlignTop
                        }
                        Controls.TextArea {
                            id: domainField
                            Layout.fillWidth: true
                            wrapMode: TextEdit.Wrap
                            Accessible.name: qsTr("Domain")
                            onTextChanged: if (activeFocus) pageContext.dirty = true
                        }
                        FieldLabel {
                            text: qsTr("Language")
                            Layout.preferredWidth: archiveGrid.twoColumns ? 170 : -1
                            Layout.fillWidth: !archiveGrid.twoColumns
                            Layout.topMargin: archiveGrid.twoColumns ? 0 : 6
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Controls.TextField {
                            id: languageField
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Language")
                            onTextEdited: pageContext.dirty = true
                        }
                        FieldLabel {
                            text: qsTr("Narrative style and terminology")
                            Layout.preferredWidth: archiveGrid.twoColumns ? 170 : -1
                            Layout.fillWidth: !archiveGrid.twoColumns
                            Layout.topMargin: 6
                            Layout.alignment: Qt.AlignTop
                        }
                        Controls.TextArea {
                            id: styleField
                            Layout.fillWidth: true
                            wrapMode: TextEdit.Wrap
                            Accessible.name: qsTr("Narrative style and terminology")
                            onTextChanged: if (activeFocus) pageContext.dirty = true
                        }
                        FieldLabel {
                            text: qsTr("Notes")
                            Layout.preferredWidth: archiveGrid.twoColumns ? 170 : -1
                            Layout.fillWidth: !archiveGrid.twoColumns
                            Layout.topMargin: 6
                            Layout.alignment: Qt.AlignTop
                        }
                        Controls.TextArea {
                            id: notesField
                            Layout.fillWidth: true
                            wrapMode: TextEdit.Wrap
                            Accessible.name: qsTr("Notes")
                            onTextChanged: if (activeFocus) pageContext.dirty = true
                        }
                    }
                }

                KCard {
                    Layout.fillWidth: true
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        KSectionHeader {
                            level: 3
                            title: qsTr("Categories")
                            description: qsTr("The AI chooses among these values; where only these values are allowed, it adds no others.")
                        }
                        KInfoTag {
                            Layout.alignment: Qt.AlignTop
                            text: qsTr("%1 categories").arg(categoryModel.count)
                        }
                    }
                    Repeater {
                        model: categoryModel
                        delegate: EntryBox {
                            id: categoryEntry
                            required property int index
                            required property string name
                            required property string description
                            required property string values
                            required property bool closed
                            required property bool multiple
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: width >= 560 ? 3 : 1
                                    columnSpacing: 8
                                    rowSpacing: 8
                                    Controls.TextField {
                                        Layout.fillWidth: true
                                        placeholderText: qsTr("Name")
                                        Accessible.name: qsTr("Name")
                                        text: categoryEntry.name
                                        onTextEdited: { categoryModel.setProperty(categoryEntry.index, "name", text); pageContext.dirty = true }
                                    }
                                    Controls.TextField {
                                        Layout.fillWidth: true
                                        placeholderText: qsTr("Definition")
                                        Accessible.name: qsTr("Definition")
                                        text: categoryEntry.description
                                        onTextEdited: { categoryModel.setProperty(categoryEntry.index, "description", text); pageContext.dirty = true }
                                    }
                                    Controls.TextField {
                                        Layout.fillWidth: true
                                        placeholderText: qsTr("Values")
                                        Accessible.name: qsTr("Values")
                                        text: categoryEntry.values
                                        onTextEdited: { categoryModel.setProperty(categoryEntry.index, "values", text); pageContext.dirty = true }
                                    }
                                }
                                RemoveButton {
                                    onClicked: { categoryModel.remove(categoryEntry.index); pageContext.dirty = true }
                                }
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 12
                                Controls.CheckBox {
                                    text: qsTr("Only these values")
                                    checked: categoryEntry.closed
                                    onToggled: { categoryModel.setProperty(categoryEntry.index, "closed", checked); pageContext.dirty = true }
                                }
                                Controls.CheckBox {
                                    text: qsTr("More than one value")
                                    checked: categoryEntry.multiple
                                    onToggled: { categoryModel.setProperty(categoryEntry.index, "multiple", checked); pageContext.dirty = true }
                                }
                            }
                        }
                    }
                    KButton {
                        variant: "tonal"
                        icon.name: "list-add"
                        text: qsTr("Add")
                        onClicked: {
                            categoryModel.append({ name: "", description: "", values: "", closed: false, multiple: true })
                            pageContext.dirty = true
                        }
                    }
                }
            }

            // ── Part 2: glossary and known names ───────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                visible: pageContext.part === 1
                spacing: KTheme.gap

                KCard {
                    Layout.fillWidth: true
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        KSectionHeader {
                            level: 3
                            title: qsTr("Glossary")
                            description: qsTr("How the archive writes its terms: the AI uses the preferred form and recognises the other spellings.")
                        }
                        KInfoTag {
                            Layout.alignment: Qt.AlignTop
                            text: qsTr("%1 terms").arg(glossaryModel.count)
                        }
                    }
                    Repeater {
                        model: glossaryModel
                        delegate: EntryBox {
                            id: glossaryEntry
                            required property int index
                            required property string term
                            required property string preferred
                            required property string aliases
                            required property string definition
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: width >= 720 ? 4 : width >= 380 ? 2 : 1
                                    columnSpacing: 8
                                    rowSpacing: 8
                                    Controls.TextField {
                                        Layout.fillWidth: true
                                        placeholderText: qsTr("Term")
                                        Accessible.name: qsTr("Term")
                                        text: glossaryEntry.term
                                        onTextEdited: { glossaryModel.setProperty(glossaryEntry.index, "term", text); pageContext.dirty = true }
                                    }
                                    Controls.TextField {
                                        Layout.fillWidth: true
                                        placeholderText: qsTr("Preferred form")
                                        Accessible.name: qsTr("Preferred form")
                                        text: glossaryEntry.preferred
                                        onTextEdited: { glossaryModel.setProperty(glossaryEntry.index, "preferred", text); pageContext.dirty = true }
                                    }
                                    Controls.TextField {
                                        Layout.fillWidth: true
                                        placeholderText: qsTr("Also written as")
                                        Accessible.name: qsTr("Also written as")
                                        text: glossaryEntry.aliases
                                        onTextEdited: { glossaryModel.setProperty(glossaryEntry.index, "aliases", text); pageContext.dirty = true }
                                    }
                                    Controls.TextField {
                                        Layout.fillWidth: true
                                        placeholderText: qsTr("Definition")
                                        Accessible.name: qsTr("Definition")
                                        text: glossaryEntry.definition
                                        onTextEdited: { glossaryModel.setProperty(glossaryEntry.index, "definition", text); pageContext.dirty = true }
                                    }
                                }
                                RemoveButton {
                                    onClicked: { glossaryModel.remove(glossaryEntry.index); pageContext.dirty = true }
                                }
                            }
                        }
                    }
                    KButton {
                        variant: "tonal"
                        icon.name: "list-add"
                        text: qsTr("Add")
                        onClicked: { glossaryModel.append({ term: "", preferred: "", aliases: "", definition: "" }); pageContext.dirty = true }
                    }
                    Controls.CheckBox {
                        id: transcriptHintBox
                        Layout.fillWidth: true
                        text: qsTr("Suggest the glossary terms to the transcription")
                        onToggled: pageContext.dirty = true
                    }
                }

                // Known names: people, places, events, bodies
                KCard {
                    Layout.fillWidth: true
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        KSectionHeader {
                            level: 3
                            title: qsTr("Known names")
                            description: qsTr("People, places, events and organisations: the AI recognises them and always writes them the same way.")
                        }
                        KInfoTag {
                            Layout.alignment: Qt.AlignTop
                            text: qsTr("%1 known names").arg(entityModel.count)
                        }
                    }
                    Repeater {
                        model: entityModel
                        delegate: EntryBox {
                            id: entityEntry
                            required property int index
                            required property string name
                            required property string kind
                            required property string aliases
                            required property string notes
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: width >= 720 ? 4 : width >= 380 ? 2 : 1
                                    columnSpacing: 8
                                    rowSpacing: 8
                                    Controls.TextField {
                                        Layout.fillWidth: true
                                        placeholderText: qsTr("Name")
                                        Accessible.name: qsTr("Name")
                                        text: entityEntry.name
                                        onTextEdited: { entityModel.setProperty(entityEntry.index, "name", text); pageContext.dirty = true }
                                    }
                                    ComboBoxFitted {
                                        readonly property var kinds: ["person", "place", "event", "organisation", "other"]
                                        model: [qsTr("Person"), qsTr("Place"), qsTr("Event"), qsTr("Organisation"), qsTr("Other")]
                                        currentIndex: Math.max(0, kinds.indexOf(entityEntry.kind))
                                        onActivated: { entityModel.setProperty(entityEntry.index, "kind", kinds[currentIndex]); pageContext.dirty = true }
                                    }
                                    Controls.TextField {
                                        Layout.fillWidth: true
                                        placeholderText: qsTr("Also written as")
                                        Accessible.name: qsTr("Also written as")
                                        text: entityEntry.aliases
                                        onTextEdited: { entityModel.setProperty(entityEntry.index, "aliases", text); pageContext.dirty = true }
                                    }
                                    Controls.TextField {
                                        Layout.fillWidth: true
                                        placeholderText: qsTr("Notes")
                                        Accessible.name: qsTr("Notes")
                                        text: entityEntry.notes
                                        onTextEdited: { entityModel.setProperty(entityEntry.index, "notes", text); pageContext.dirty = true }
                                    }
                                }
                                RemoveButton {
                                    onClicked: { entityModel.remove(entityEntry.index); pageContext.dirty = true }
                                }
                            }
                        }
                    }
                    KButton {
                        variant: "tonal"
                        icon.name: "list-add"
                        text: qsTr("Add")
                        onClicked: { entityModel.append({ name: "", kind: "person", aliases: "", notes: "" }); pageContext.dirty = true }
                    }
                }
            }

            // ── Part 3: reference documents (local knowledge base) ─────────
            KCard {
                Layout.fillWidth: true
                visible: pageContext.part === 2

                KSectionHeader {
                    level: 3
                    title: qsTr("Reference documents")
                    description: qsTr("Documents are only read, never changed. For each file it classifies, the AI uses the passages closest to it.")
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    KInfoTag { text: qsTr("%1 documents").arg(pageContext.documents.length) }
                    KInfoTag { text: qsTr("%1 passages").arg(pageContext.passageCount) }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    KButton {
                        variant: "tonal"
                        icon.name: "document-open"
                        text: qsTr("Add files")
                        onClicked: documentsDialog.open()
                    }
                    KButton {
                        variant: "secondary"
                        icon.name: "folder-open"
                        text: qsTr("Add folder")
                        onClicked: documentsFolderDialog.open()
                    }
                }
                Controls.Label {
                    visible: appManager1.knowledgeIsRunning || appManager1.knowledgeStatusText !== ""
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    textFormat: Text.StyledText
                    color: KTheme.text2
                    text: appManager1.knowledgeStatusText.replace(/^(&nbsp;)+/, "")
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Repeater {
                        model: pageContext.documents
                        delegate: ColumnLayout {
                            id: documentRow
                            required property var modelData
                            required property int index
                            readonly property string status: modelData.status
                            Layout.fillWidth: true
                            spacing: 0

                            Rectangle {
                                // A hairline between documents
                                visible: documentRow.index > 0
                                Layout.fillWidth: true
                                implicitHeight: 1
                                color: KTheme.line
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 8
                                Layout.bottomMargin: 8
                                spacing: 10
                                Kirigami.Icon {
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: 2
                                    source: documentRow.status === "indexed" ? "dialog-ok-apply"
                                            : documentRow.status === "failed" ? "dialog-error" : "view-refresh"
                                    isMask: true
                                    color: documentRow.status === "indexed" ? KTheme.ok
                                           : documentRow.status === "failed" ? KTheme.bad : KTheme.text3
                                    implicitWidth: 16
                                    implicitHeight: 16
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Controls.Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                        font.weight: Font.DemiBold
                                        color: KTheme.text
                                        text: documentRow.modelData.title
                                    }
                                    Controls.Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WrapAnywhere
                                        font.family: KTheme.mono
                                        font.pixelSize: KTheme.note
                                        color: KTheme.text3
                                        text: documentRow.modelData.path
                                    }
                                    Controls.Label {
                                        visible: !!documentRow.modelData.reason
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        color: KTheme.text2
                                        text: documentRow.modelData.reason || ""
                                    }
                                    Flow {
                                        Layout.fillWidth: true
                                        Layout.topMargin: 4
                                        spacing: 6
                                        KInfoTag {
                                            text: documentRow.status === "indexed" ? qsTr("Indexed")
                                                  : documentRow.status === "failed" ? qsTr("Error") : qsTr("To be indexed")
                                            tone: documentRow.status === "indexed" ? "ok"
                                                  : documentRow.status === "failed" ? "bad" : "neutral"
                                        }
                                        KInfoTag {
                                            visible: documentRow.status === "indexed"
                                            text: qsTr("%1 passages").arg(documentRow.modelData.passages)
                                        }
                                    }
                                }
                                RemoveButton {
                                    onClicked: {
                                        removeDocumentDialog.documentId    = documentRow.modelData.id
                                        removeDocumentDialog.documentTitle = documentRow.modelData.title
                                        removeDocumentDialog.open()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Actions: re-interpret and draft ────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: !pageContext.wide
            Layout.preferredWidth: pageContext.wide ? 340 : -1
            Layout.maximumWidth: pageContext.wide ? 360 : -1
            Layout.alignment: Qt.AlignTop
            spacing: KTheme.gap

            KCard {
                Layout.fillWidth: true
                KSectionHeader {
                    level: 3
                    title: qsTr("Re-interpret with the context")
                    description: qsTr("Redoes only the classification of the files already analysed, from their descriptions and transcripts. Files already classified with this context are skipped.")
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: pageContext.catalogDeviceId >= 0
                    spacing: 2
                    FieldLabel { text: qsTr("Catalog") }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        font.weight: Font.DemiBold
                        color: KTheme.text
                        text: catalogBox.currentText
                    }
                }
                // Why the action is not available yet, in words
                Explanation {
                    visible: !reinterpretButton.enabled
                    color: KTheme.text3
                    text: !pageContext.hasSavedContext ? qsTr("A saved context is needed first.")
                          : pageContext.dirty ? qsTr("The changes need to be saved first.")
                          : qsTr("A catalog is needed first.")
                }
                KButton {
                    id: reinterpretButton
                    variant: "primary"
                    icon.name: "view-refresh"
                    text: qsTr("Re-interpret")
                    // Same condition as the page action in Main.qml
                    enabled: pageContext.hasSavedContext && !pageContext.dirty && pageContext.catalogDeviceId >= 0
                    onClicked: pageContext.openReinterpret()
                }
            }

            KCard {
                Layout.fillWidth: true
                KSectionHeader {
                    level: 3
                    title: qsTr("Propose a draft")
                    description: qsTr("The AI reads the catalog's folder names and a sample of its descriptions, and proposes terms, categories and names. It fills the empty fields, adds what is missing and saves nothing by itself.")
                }
                FieldLabel { text: qsTr("Catalog") }
                ComboBoxFitted {
                    id: catalogBox
                    Accessible.name: qsTr("Catalog")
                    model: pageContext.catalogs.map(function (c) { return c.name })
                    onCurrentIndexChanged: pageContext.loadCatalogNotes()
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    KButton {
                        variant: "secondary"
                        icon.name: "tools-wizard"
                        text: qsTr("Draft from the catalog")
                        enabled: pageContext.catalogDeviceId >= 0 && !pageContext.drafting
                        onClicked: pageContext.draft()
                    }
                    Controls.BusyIndicator {
                        visible: pageContext.drafting
                        running: visible
                        implicitWidth: 32
                        implicitHeight: 32
                    }
                }
                FieldLabel {
                    Layout.topMargin: 6
                    text: qsTr("Notes for this catalog")
                }
                Controls.TextArea {
                    id: catalogNotesField
                    property bool edited: false
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Notes for this catalog")
                    wrapMode: TextEdit.Wrap
                    enabled: pageContext.catalogDeviceId >= 0
                    onTextChanged: if (activeFocus) { edited = true; pageContext.dirty = true }
                }
            }
        }
    }

    Item { Layout.preferredHeight: KTheme.gap }

    // Removing a reference document asks first, and says what stays
    Kirigami.PromptDialog {
        id: removeDocumentDialog
        property int    documentId: -1
        property string documentTitle: ""
        background: Rectangle { color: KTheme.surface; radius: KTheme.radiusCard; border.width: 1; border.color: KTheme.line }
        padding: Kirigami.Units.largeSpacing * 2
        title: qsTr("Remove this reference document?")
        subtitle: qsTr("%1 leaves the archive knowledge: the AI no longer uses its passages. The file itself stays where it is, unchanged.")
                  .arg(removeDocumentDialog.documentTitle)
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Remove")
                icon.name: "list-remove"
                onTriggered: {
                    appManager1.removeKnowledgeDocument(removeDocumentDialog.documentId)
                    removeDocumentDialog.close()
                }
            },
            Kirigami.Action {
                text: qsTranslate("Main", "Cancel")
                icon.name: "dialog-cancel"
                onTriggered: removeDocumentDialog.close()
            }
        ]
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
        reinterpretSeconds = e.seconds === undefined ? -1 : e.seconds
    }
    Controls.Dialog {
        id: reinterpretDialog
        // The design system surface and line (D34)
        palette.window: KTheme.surface
        palette.mid:    KTheme.line
        title: qsTr("Re-interpret with the context")
        modal: true
        width: Kirigami.Units.gridUnit * 30
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
                    Accessible.name: qsTr("Select the path")   // icon only (WCAG 4.1.2)
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
                // The time only once a classification has been measured
                text: pageContext.reinterpretSeconds < 0 ? String(pageContext.reinterpretFiles)
                      : qsTr("%1, about %2").arg(pageContext.reinterpretFiles)
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
