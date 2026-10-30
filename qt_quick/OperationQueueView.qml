import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Global activity panel: what is running now, and what is queued behind it.
// Catalog operations run strictly one at a time (SpecOperationQueue.md), so this
// is application-wide state and lives in the window footer. It reports the
// running job's progress alongside the queue, and hides itself when idle.
//
// Root is a ToolBar so the window footer gives it the full window width; a bare
// layout would size to its contents and sit against the left edge.
//
// Redesign phase R5 (D34): the panel keeps its compact height and takes the
// design system's colours, an info tag for the analysis state and its buttons.
Controls.ToolBar {
    id: root

    // Creation, update and search all report here and nowhere else (OPQ-C16).
    // The Memory-mode Explore load reports here too (OPQ-F19, EXP-F28).
    readonly property bool busy: appManager1.catalogIsCreating
                                 || appManager1.deviceUpdateIsRunning
                                 || appManager1.searchIsRunning
                                 || appManager1.exploreLoadIsRunning
    // The Explore load's final line stays readable through the linger: it is
    // the last thing that ran until another operation starts.
    property bool exploreWasLast: false
    readonly property string statusText: appManager1.catalogIsCreating
                                         ? appManager1.catalogStatusText
                                         : appManager1.searchIsRunning
                                           ? appManager1.searchStatusText
                                           : (appManager1.exploreLoadIsRunning || exploreWasLast)
                                             ? appManager1.exploreStatusText
                                             : appManager1.deviceUpdateStatusText

    // How long the panel stays up after the work ends. The catalog job runs on
    // the GUI thread and pumps the event loop by hand, so a small catalog can
    // finish before Qt Quick paints a single frame: without a tail the panel is
    // shown for zero frames and the completion message, which is built and
    // emitted, is never seen. 5000 ms is what K2's status bar uses; it is a
    // design choice, not a ratified rule.
    readonly property int completionLinger: 5000

    property bool lingering: false

    Timer {
        id: lingerTimer
        interval: root.completionLinger
        onTriggered: root.lingering = false
    }

    onBusyChanged: {
        if (root.busy) {
            root.exploreWasLast = appManager1.exploreLoadIsRunning
            // A new operation supersedes the previous one's tail.
            root.lingering = false
            lingerTimer.stop()
        } else {
            root.lingering = true
            lingerTimer.restart()
        }
    }

    // The tail only holds the panel open while there is something to read: a
    // stopped or failed operation clears the status text, and an empty bar
    // lingering for five seconds would say nothing.
    visible: busy || (lingering && root.statusText.length > 0)
             || appManager1.operationQueue.length > 0
             || appManager1.mdiIsRunning || root.mdiLingering

    // The enrichment runner is not a queue operation (MDI-C3): it has its own
    // row, shown alongside whatever the queue runs (MDI-F17), with the same tail.
    property bool mdiLingering: false
    Timer {
        id: mdiLingerTimer
        interval: root.completionLinger
        onTriggered: root.mdiLingering = false
    }
    Connections {
        target: appManager1
        function onMdiStateChanged() {
            if (appManager1.mdiIsRunning) {
                root.mdiLingering = false
                mdiLingerTimer.stop()
            } else {
                root.mdiLingering = true
                mdiLingerTimer.restart()
            }
        }
    }
    height: visible ? implicitHeight : 0

    // Positioned by the caller against the page area, so no drawer compensation
    // is needed here.
    leftPadding:  Kirigami.Units.smallSpacing
    rightPadding: Kirigami.Units.smallSpacing
    topPadding: 4
    bottomPadding: 4

    // An opaque surface with a hairline on top: the panel lies over the page
    background: Rectangle {
        color: KTheme.surface
        Rectangle {
            width: parent.width
            height: 1
            color: KTheme.line
        }
    }

    // Buttons of the panel: the design system's, kept low — but never lower
    // than an enlarged label needs (WCAG 1.4.4)
    component PanelButton: KButton {
        variant: "link"
        implicitHeight: Math.max(32, implicitContentHeight + topPadding + bottomPadding)
        leftPadding: 10
        rightPadding: 10
    }

    contentItem: ColumnLayout {
        spacing: 2

        // ── Running job ──────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            visible: root.busy || root.statusText.length > 0

            // Deliberately bound to the real running state, not to the linger
            // tail: a turning indicator must never sit beside a "Completed"
            // line. During the tail the row shows the message alone.
            Controls.BusyIndicator {
                // Not simply `busy`: a paused search is still "running", and an
                // indicator that keeps turning would say work is happening when
                // none is (OPQ-C12).
                running: root.busy && !appManager1.searchIsPaused
                // Hidden but keeping its slot, so the text does not jump left
                // when the work ends and the completion line lingers
                opacity: root.busy ? 1 : 0
                // The space goes before the indicator, the text right after it
                Layout.leftMargin: Kirigami.Units.largeSpacing
                implicitWidth:  Kirigami.Units.gridUnit * 1.2
                implicitHeight: Kirigami.Units.gridUnit * 1.2
            }
            Controls.Label {
                id: jobStatusLabel
                Layout.fillWidth: true
                // The builder pads the operation with leading &nbsp; for K2's
                // status bar; here the indicator already separates it
                text: root.statusText.replace(/^(&nbsp;)+/, "")
                textFormat: Text.StyledText
                // Cut in the middle: the operation and the file being read
                // both stay readable; the whole line is in the tooltip
                elide: Text.ElideMiddle
                color: KTheme.text
                HoverHandler { id: jobStatusHover }
                Controls.ToolTip.text: jobStatusLabel.text
                Controls.ToolTip.visible: jobStatusHover.hovered && jobStatusLabel.truncated
                Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
            PanelButton {
                text: qsTr("Stop")
                icon.name: "process-stop"
                visible: root.busy
                onClicked: {
                    if (appManager1.exploreLoadIsRunning)
                        appManager1.stopExploreLoad()
                    else if (appManager1.catalogIsCreating)
                        appManager1.stopCatalogCreation()
                    else if (appManager1.searchIsRunning)
                        appManager1.stopSearch()
                    else
                        appManager1.stopDeviceUpdate()
                }
            }
        }

        // ── Enrichment runner (MDI-F17) ──────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            visible: appManager1.mdiIsRunning || root.mdiLingering

            Controls.BusyIndicator {
                running: appManager1.mdiIsRunning && !appManager1.mdiIsPaused
                         && !appManager1.mdiJobInfo.waitingForSource
                opacity: appManager1.mdiIsRunning ? 1 : 0
                Layout.leftMargin: Kirigami.Units.largeSpacing
                implicitWidth:  Kirigami.Units.gridUnit * 1.2
                implicitHeight: Kirigami.Units.gridUnit * 1.2
            }
            // A state that needs attention as a tag; "In Progress" only in the
            // line beside it, which already says it
            KInfoTag {
                visible: appManager1.mdiIsRunning
                         && (appManager1.mdiIsPaused || appManager1.mdiJobInfo.waitingForSource === true)
                text: appManager1.mdiIsPaused ? qsTr("Paused") : qsTr("Waiting for the drive")
                tone: "warn"
            }
            Controls.Label {
                id: mdiStatusLabel
                Layout.fillWidth: true
                text: appManager1.mdiStatusText.replace(/^(&nbsp;)+/, "")
                textFormat: Text.StyledText
                // As the line above: cut in the middle, whole in the tooltip
                elide: Text.ElideMiddle
                color: KTheme.text
                HoverHandler { id: mdiStatusHover }
                Controls.ToolTip.text: mdiStatusLabel.text
                Controls.ToolTip.visible: mdiStatusHover.hovered && mdiStatusLabel.truncated
                Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
            PanelButton {
                // The compact summary opens the full Activity page (MDI-F61)
                text: qsTr("Work in progress")
                icon.name: "utilities-system-monitor"
                onClicked: applicationWindow().showActivityPage()
            }
            PanelButton {
                visible: appManager1.mdiIsRunning
                text: appManager1.mdiIsPaused ? qsTr("Resume") : qsTr("Pause")
                icon.name: appManager1.mdiIsPaused ? "media-playback-start" : "media-playback-pause"
                onClicked: appManager1.setEnrichmentPaused(!appManager1.mdiIsPaused)
            }
            PanelButton {
                visible: appManager1.mdiIsRunning
                text: qsTr("Stop")
                icon.name: "process-stop"
                onClicked: appManager1.stopEnrichment()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: KTheme.line
            visible: appManager1.operationQueueWaitingCount > 0
        }

        // ── Queue ────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            visible: appManager1.operationQueueWaitingCount > 0

            Controls.Label {
                Layout.leftMargin: Kirigami.Units.largeSpacing
                text: qsTr("Queue")
                font.weight: Font.DemiBold
                color: KTheme.text2
            }
            KInfoTag {
                text: qsTr("%1 waiting").arg(appManager1.operationQueueWaitingCount)
            }
            Item { Layout.fillWidth: true }
            PanelButton {
                text: qsTr("Clear")
                icon.name: "edit-clear"
                onClicked: clearQueueDialog.open()
            }
        }

        Repeater {
            model: appManager1.operationQueue

            RowLayout {
                id: entryRow
                required property var modelData
                required property int index

                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.smallSpacing
                // The running entry is already described by the status row above.
                visible: !entryRow.modelData.running
                height: visible ? implicitHeight : 0

                Kirigami.Icon {
                    source: entryRow.modelData.isCreate ? "list-add" : "view-refresh"
                    implicitWidth:  Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                    isMask: true
                    color: KTheme.text3
                    Accessible.ignored: true   // the label says Create or Update
                }
                Controls.Label {
                    text: (entryRow.modelData.isCreate ? qsTr("Create") : qsTr("Update"))
                          + " · " + entryRow.modelData.deviceName
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    color: KTheme.text2
                }
                IconButton {
                    icon.name: "list-remove"
                    text: qsTr("Remove")
                    flat: true
                    // Waiting entries only; the running one is stopped above.
                    onClicked: appManager1.removeQueuedOperation(
                                   entryRow.index - (appManager1.operationQueue.length
                                                     > appManager1.operationQueueWaitingCount ? 1 : 0))
                }
            }
        }
    }

    // Emptying the queue asks first, and says what goes on
    Kirigami.PromptDialog {
        id: clearQueueDialog
        background: Rectangle { color: KTheme.surface; radius: KTheme.radiusCard; border.width: 1; border.color: KTheme.line }
        padding: Kirigami.Units.largeSpacing * 2
        title: qsTr("Clear the queue?")
        subtitle: qsTr("The %1 waiting operations are taken off the queue and do not run. What is running now goes on, and nothing on the drives changes.")
                  .arg(appManager1.operationQueueWaitingCount)
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Clear")
                icon.name: "edit-clear"
                onTriggered: {
                    appManager1.clearOperationQueue()
                    clearQueueDialog.close()
                }
            },
            Kirigami.Action {
                text: qsTranslate("Main", "Cancel")
                icon.name: "dialog-cancel"
                onTriggered: clearQueueDialog.close()
            }
        ]
    }
}
