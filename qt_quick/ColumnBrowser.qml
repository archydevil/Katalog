import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Column view of Explore (fork, owner's request 2026-10-29): Finder-style
// columns, one per folder level, fed by the same catalog entries as the
// Explore list (appManager1.getExploreEntries). Selecting a folder opens the
// next column; selecting a file shows it in the Inspector. Keyboard: Up/Down
// move, Right enters a folder, Left goes back a column, Space toggles the
// Inspector, Enter opens the file when its disk is online.
Item {
    id: browser

    property string rootPath: ""     // catalog source path
    property int    catalogId: 0
    property var    paths: []        // folder shown in each column
    property string selectedFullPath: ""
    property int    focusedColumn: 0
    signal inspectorToggleRequested()

    readonly property real columnWidth: Kirigami.Units.gridUnit * 15

    onRootPathChanged: reset()
    function reset() {
        paths = rootPath !== "" ? [rootPath] : []
        selectedFullPath = ""
        focusedColumn = 0
    }
    function openFolder(columnIndex, folderPath) {
        paths = paths.slice(0, columnIndex + 1).concat([folderPath])
        selectedFullPath = ""
        Qt.callLater(function() { columnsView.positionViewAtEnd() })
    }
    function selectFile(columnIndex, fullPath) {
        paths = paths.slice(0, columnIndex + 1)
        selectedFullPath = fullPath
    }
    // Folders first, then files, each by name
    function sortedEntries(folder) {
        const list = appManager1.getExploreEntries(folder, true, false)
        list.sort(function(a, b) {
            if (a.entryType !== b.entryType)
                return a.entryType === "folder" ? -1 : 1
            return String(a.name).localeCompare(String(b.name))
        })
        return list
    }

    Rectangle {
        anchors.fill: parent
        color: applicationWindow().listBackgroundColor
    }

    ListView {
        id: columnsView
        anchors.fill: parent
        orientation: ListView.Horizontal
        clip: true
        model: browser.paths
        boundsBehavior: Flickable.StopAtBounds
        Controls.ScrollBar.horizontal: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }

        delegate: Item {
            id: column
            required property int    index
            required property string modelData
            width: browser.columnWidth
            height: columnsView.height
            readonly property var entries: browser.sortedEntries(modelData)
            property int current: -1

            ListView {
                id: list
                anchors { fill: parent; rightMargin: 1 }
                clip: true
                model: column.entries
                currentIndex: column.current
                focus: browser.focusedColumn === column.index
                keyNavigationEnabled: false
                Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }

                function choose(i) {
                    if (i < 0 || i >= column.entries.length)
                        return
                    column.current = i
                    list.positionViewAtIndex(i, ListView.Contain)
                    const e = column.entries[i]
                    if (e.entryType === "folder")
                        browser.openFolder(column.index, e.fullPath)
                    else
                        browser.selectFile(column.index, e.fullPath)
                }

                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Down) {
                        choose(Math.min(column.entries.length - 1, column.current + 1)); event.accepted = true
                    } else if (event.key === Qt.Key_Up) {
                        choose(Math.max(0, column.current - 1)); event.accepted = true
                    } else if (event.key === Qt.Key_Right) {
                        if (column.current >= 0 && column.entries[column.current].entryType === "folder"
                                && browser.paths.length > column.index + 1)
                            browser.focusedColumn = column.index + 1
                        event.accepted = true
                    } else if (event.key === Qt.Key_Left) {
                        if (column.index > 0) {
                            browser.focusedColumn = column.index - 1
                            browser.paths = browser.paths.slice(0, column.index + 1)
                            browser.selectedFullPath = ""
                        }
                        event.accepted = true
                    } else if (event.key === Qt.Key_Space) {
                        browser.inspectorToggleRequested(); event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        const e = column.entries[column.current]
                        if (e && e.entryType === "file" && appManager1.inspectorInfo(browser.catalogId, e.fullPath).online)
                            Qt.openUrlExternally(appManager1.localFileUrl(e.fullPath))
                        event.accepted = true
                    }
                }
                onFocusChanged: if (focus) forceActiveFocus()

                delegate: Controls.ItemDelegate {
                    id: row
                    required property int index
                    required property var modelData
                    width: ListView.view.width
                    highlighted: column.current === index
                    onClicked: {
                        browser.focusedColumn = column.index
                        list.forceActiveFocus()
                        list.choose(index)
                    }
                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing
                        // The saved thumbnail when there is one, else the type icon
                        Item {
                            implicitWidth: Kirigami.Units.iconSizes.smallMedium
                            implicitHeight: implicitWidth
                            readonly property string thumb: row.modelData.entryType === "file"
                                ? appManager1.mdiPreviewUrl(browser.catalogId, row.modelData.fullPath, appManager1.mdiRevision) : ""
                            Image {
                                anchors.fill: parent
                                visible: parent.thumb !== ""
                                source: parent.thumb
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                            Kirigami.Icon {
                                anchors.fill: parent
                                visible: parent.thumb === ""
                                source: {
                                    if (row.modelData.entryType === "folder") return "folder"
                                    switch (row.modelData.fileType) {
                                        case "audio": return "audio-x-mpeg"
                                        case "image": return "image-jpeg"
                                        case "video": return "video-mp4"
                                        case "text":  return "view-list-text"
                                        default:      return "document-open"
                                    }
                                }
                            }
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            text: row.modelData.name
                            elide: Text.ElideMiddle
                        }
                        Kirigami.Icon {
                            visible: row.modelData.entryType === "folder"
                            source: "go-next-symbolic"
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: implicitWidth
                            opacity: 0.6
                        }
                    }
                }
            }
            Kirigami.Separator {
                anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
            }
        }
    }
}
