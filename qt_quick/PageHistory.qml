import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// History of the actions on the catalogs (fork, D36, owner 2026-10-30:
// "Punti di ripristino"). Every update, split, recomposition, edit or deletion
// of a catalog leaves a restore point: the page lists them, newest first, and
// takes a catalog back to how it was before, or opens an earlier version as a
// catalog of its own. Catalogs split earlier can be joined back into one.
ColumnLayout {
    id: history
    spacing: KTheme.gap

    property var entries: []
    property var groups: []          // drives or groups holding two catalogs or more
    readonly property bool available: appManager1.historyAvailable()

    function refresh() {
        history.entries = history.available ? appManager1.historyEntries() : []
        // Catalogs grouped by the drive or group they sit in: the candidates to join
        let byParent = {}
        for (const d of (appManager1.homeOverview().drives || [])) {
            const parentId = (d.ancestors || [])[0] || 0
            if (parentId <= 0 || !d.isDrive)
                continue
            if (!byParent[parentId])
                byParent[parentId] = []
            byParent[parentId].push(d)
        }
        let groups = []
        for (const parentId in byParent)
            if (byParent[parentId].length > 1) {
                const proposal = appManager1.recomposeProposal(Number(parentId))
                if (proposal.possible)
                    groups.push({ deviceId: Number(parentId), proposal: proposal })
            }
        history.groups = groups
    }
    function count(n) { return Number(n || 0).toLocaleString(Qt.locale(), "f", 0) }
    function actionTone(action) {
        return action === "delete" ? "bad" : action === "split" || action === "recompose" ? "info"
             : action === "restore" ? "ok" : "neutral"
    }

    // Read while shown only (the grouping asks every drive for a proposal);
    // a change while hidden is read when the page shows again
    property bool stale: true
    function setShown(shown) {
        if (shown && history.stale) {
            history.stale = false
            history.refresh()
        }
    }
    function changed() {
        if (history.visible)
            history.refresh()
        else
            history.stale = true
    }
    Connections {
        target: appManager1
        function onHistoryChanged()     { history.changed() }
        function onDeviceListChanged()  { history.changed() }
    }

    // What the page is for
    Controls.Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: KTheme.text2
        text: qsTr("Every change to a catalog leaves a restore point: here you see what happened and can go back to how it was before. The last 10 versions of each catalog are kept.")
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: !history.available
        type: Kirigami.MessageType.Information
        text: qsTr("The history needs the collection's folder on this computer, so it is not kept for this collection.")
    }

    // Catalogs that were split: joined back into one
    KSectionHeader {
        visible: history.groups.length > 0
        level: 2
        title: qsTr("Split catalogs")
        description: qsTr("These drives hold several catalogs, one per folder. Joining them makes one catalog again, with every file, folder and AI result; a restore point is saved first.")
    }
    Repeater {
        model: history.groups
        delegate: KCard {
            id: groupCard
            required property var modelData
            Layout.fillWidth: true
            KSectionHeader {
                level: 3
                title: groupCard.modelData.proposal.name
                description: qsTr("%1 catalogs, in %2").arg(groupCard.modelData.proposal.catalogs.length)
                                                           .arg(groupCard.modelData.proposal.path)
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: groupCard.modelData.proposal.catalogs
                    delegate: KInfoTag {
                        required property var modelData
                        text: modelData.name
                    }
                }
            }
            KButton {
                variant: "primary"
                icon.name: "folder-sync"
                text: qsTr("Join into one catalog")
                onClicked: recomposeDialog.ask(groupCard.modelData.proposal)
            }
        }
    }

    // The actions, newest first
    KSectionHeader {
        level: 2
        title: qsTr("Actions")
        description: history.entries.length > 0
                     ? qsTr("Newest first. Restoring saves the current state too, so it can be undone in turn.")
                     : qsTr("Nothing has changed yet. When a catalog is updated, split, joined, edited or deleted, it shows here and can be undone.")
    }
    Repeater {
        model: history.entries
        delegate: KCard {
            id: entryCard
            required property var modelData
            Layout.fillWidth: true
            Accessible.name: modelData.title

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                KSectionHeader {
                    level: 3
                    title: entryCard.modelData.title
                    description: entryCard.modelData.details
                }
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: 4
                    Controls.Label {
                        Layout.alignment: Qt.AlignRight
                        text: entryCard.modelData.dateText
                        color: KTheme.text3
                        font.pixelSize: KTheme.note
                    }
                    KInfoTag {
                        Layout.alignment: Qt.AlignRight
                        visible: entryCard.modelData.undone
                        text: qsTr("Undone")
                        tone: "warn"
                    }
                }
            }
            // The catalogs as they were, each openable on its own
            Repeater {
                model: entryCard.modelData.catalogs || []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8
                    Kirigami.Icon {
                        source: "drive-harddisk"
                        implicitWidth: 18
                        implicitHeight: 18
                        Accessible.ignored: true
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        text: modelData.name
                        color: KTheme.text
                        wrapMode: Text.Wrap
                    }
                    KInfoTag { text: qsTr("%1 files").arg(history.count(modelData.files)) }
                    KButton {
                        variant: "link"
                        text: qsTr("Open as a separate catalog")
                        Accessible.description: modelData.name
                        onClicked: {
                            const error = appManager1.openHistoryVersion(entryCard.modelData.id, modelData.catalogId,
                                                                         qsTr("(version of %1)").arg(entryCard.modelData.dateText))
                            applicationWindow().showPassiveNotification(error !== "" ? error
                                : qsTr("The earlier version is now a catalog of its own, in My drives."))
                        }
                    }
                }
            }
            KButton {
                visible: entryCard.modelData.canRestore && !entryCard.modelData.undone
                variant: "secondary"
                icon.name: "view-history"
                text: qsTr("Restore how it was before")
                Accessible.description: entryCard.modelData.title
                onClicked: restoreDialog.ask(entryCard.modelData)
            }
        }
    }

    // Restoring changes the catalogs: it asks first and says what stays
    Kirigami.PromptDialog {
        id: restoreDialog
        property var entry: ({})
        function ask(e) { entry = e; open() }
        background: Rectangle {   // the design system surface (D34)
            color: KTheme.surface
            radius: KTheme.radiusCard
            border.width: 1
            border.color: KTheme.line
        }
        title: qsTr("Restore how it was before")
        subtitle: qsTr("The catalogs go back to how they were before «%1». The files on the drives are not touched, the AI results follow, and the current state is saved first, so this can be undone.")
                  .arg(restoreDialog.entry.title || "")
        standardButtons: Kirigami.Dialog.NoButton
        padding: Kirigami.Units.largeSpacing * 2   // room inside the design-system surface
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Restore")
                icon.name: "view-history"
                onTriggered: {
                    const error = appManager1.restoreHistoryEntry(restoreDialog.entry.id)
                    restoreDialog.close()
                    applicationWindow().showPassiveNotification(error !== "" ? error : qsTr("Restored."))
                }
            },
            Kirigami.Action {
                text: qsTr("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: restoreDialog.close()
            }
        ]
    }

    // Joining asks for the name, and says what happens
    Kirigami.PromptDialog {
        id: recomposeDialog
        property var proposal: ({})
        function ask(p) {
            proposal = p
            recomposeName.text = p.name || ""
            open()
        }
        background: Rectangle {
            color: KTheme.surface
            radius: KTheme.radiusCard
            border.width: 1
            border.color: KTheme.line
        }
        title: qsTr("Join into one catalog")
        subtitle: qsTr("%1 catalogs become one, with every file, folder and AI result. Nothing on the drive is touched, and a restore point is saved first.")
                  .arg((recomposeDialog.proposal.catalogs || []).length)
        standardButtons: Kirigami.Dialog.NoButton
        padding: Kirigami.Units.largeSpacing * 2   // room inside the design-system surface
        ColumnLayout {
            spacing: 6
            Controls.Label {
                text: qsTr("Name of the catalog")
                color: KTheme.text2
            }
            Controls.TextField {
                id: recomposeName
                Layout.fillWidth: true
                Accessible.name: qsTr("Name of the catalog")
            }
        }
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Join")
                icon.name: "folder-sync"
                enabled: recomposeName.text.trim() !== ""
                onTriggered: {
                    const ids = (recomposeDialog.proposal.catalogs || []).map(c => c.deviceId)
                    const error = appManager1.recomposeCatalogs(ids, recomposeName.text.trim())
                    recomposeDialog.close()
                    applicationWindow().showPassiveNotification(error !== "" ? error
                        : qsTr("The catalogs are one again: %1").arg(recomposeName.text.trim()))
                }
            },
            Kirigami.Action {
                text: qsTr("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: recomposeDialog.close()
            }
        ]
    }
}
