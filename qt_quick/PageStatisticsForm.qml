import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import QtCharts

// Archive in numbers (fork, D34 phase R8): how the selected drive changed over
// time — its key numbers as metric cards, the chart options and the chart in
// cards. Data, options and their persistence are unchanged; only the layout and
// the look follow the design system. A scroll view, so a short window scrolls
// instead of squeezing the chart.
Controls.ScrollView {
    id: root
    contentWidth: availableWidth
    contentHeight: pageColumn.implicitHeight
    clip: true

    // Internal keys: source = "all"|"updates"|"snapshots", dataType = "size"|"count"
    property string selectedSource:   "all"
    property string selectedDataType: "size"
    property bool   displayEachValue: false
    property string startDateText:    ""
    // The data last drawn, read by the key numbers
    property var    stats:            ({})

    readonly property bool narrow: width < Kirigami.Units.gridUnit * 36

    // Unit label map used in axis title and chart title
    function unitLabel(key) {
        if (key === "TiB") return qsTr("TiB")
        if (key === "GiB") return qsTr("GiB")
        if (key === "MiB") return qsTr("MiB")
        if (key === "KiB") return qsTr("KiB")
        return ""
    }

    // ── Key numbers, read from the data drawn ────────────────────────────────
    function lastPoint(list)  { return list && list.length > 0 ? list[list.length - 1] : null }
    function firstPoint(list) { return list && list.length > 0 ? list[0] : null }
    function formatValue(v) {
        if (v === undefined || v === null) return "—"
        var unit = root.stats.unitKey ? " " + unitLabel(root.stats.unitKey) : ""
        var a = Math.abs(v)
        var decimals = root.stats.unitKey ? (a < 10 ? 2 : a < 100 ? 1 : 0) : 0
        return Number(v).toLocaleString(Qt.locale(), "f", decimals) + unit
    }
    function formatDate(ms) { return Qt.formatDate(new Date(ms), "yyyy-MM-dd") }
    function changeText() {
        var a = firstPoint(root.stats.series1), b = lastPoint(root.stats.series1)
        if (!a || !b) return "—"
        var d = b.y - a.y
        return (d > 0 ? "+" : d < 0 ? "−" : "") + formatValue(Math.abs(d))
    }
    function sourceText(key) {
        if (key === "updates")   return qsTr("updates only")
        if (key === "snapshots") return qsTr("snapshots only")
        return qsTr("all records")
    }

    function loadSettings() {
        var src = appManager1.getStatisticsSetting("SelectedSource")
        if (src !== "") root.selectedSource = src
        var dt  = appManager1.getStatisticsSetting("TypeOfData")
        if (dt  !== "") root.selectedDataType = dt
        root.displayEachValue = appManager1.getStatisticsSetting("DisplayEachValue") === "true"
        root.startDateText    = appManager1.getStatisticsSetting("graphStartDate")
    }

    function refresh() {
        var data = appManager1.getStatisticsData(root.selectedSource, root.selectedDataType, root.startDateText)
        updateChart(data)
    }

    // Off-screen refreshes are deferred: running getStatisticsData() + rebuilding the
    // chart series while this page is hidden ran synchronously on every Selection card
    // tap (via onSelectedDeviceChanged), delaying the card-highlight repaint. Mark dirty
    // when hidden and refresh once, on becoming visible.
    property bool needsRefresh: false
    function requestRefresh() {
        if (root.visible) root.refresh()
        else root.needsRefresh = true
    }
    onVisibleChanged: if (visible && needsRefresh) { needsRefresh = false; refresh() }

    function updateChart(data) {
        root.stats = data
        series1Line.clear()
        series2Line.clear()
        series3Line.clear()

        if (!data.hasData) {
            chart.title = qsTr("No data")
            return
        }

        var i
        if (data.loadSeries1)
            for (i = 0; i < data.series1.length; i++)
                series1Line.append(data.series1[i].x, data.series1[i].y)
        if (data.loadSeries2)
            for (i = 0; i < data.series2.length; i++)
                series2Line.append(data.series2[i].x, data.series2[i].y)
        if (data.loadSeries3)
            for (i = 0; i < data.series3.length; i++)
                series3Line.append(data.series3[i].x, data.series3[i].y)

        // Series visibility and point markers
        series1Line.visible       = data.loadSeries1
        series2Line.visible       = data.loadSeries2
        series3Line.visible       = data.loadSeries3
        series1Line.pointsVisible = data.loadSeries1 && root.displayEachValue
        series2Line.pointsVisible = data.loadSeries2 && root.displayEachValue
        series3Line.pointsVisible = data.loadSeries3 && root.displayEachValue

        // Y axis: round up to next leading digit (e.g. 848 365 → 900 000)
        var maxVal = data.maxValue
        var yMax = 10
        if (maxVal > 0) {
            var digits     = Math.floor(Math.log10(maxVal)) + 1
            var firstDigit = Math.floor(maxVal / Math.pow(10, digits - 1))
            yMax = (firstDigit + 1) * Math.pow(10, digits - 1)
        }
        var stepSize    = yMax / 10
        axisY.max       = yMax + stepSize
        axisY.min       = 0
        axisY.tickCount = 10
        var ul = data.unitKey !== "" ? " (" + unitLabel(data.unitKey) + ")" : ""
        axisY.titleText = qsTr("Total") + ul

        // X axis: always set range explicitly — DateTimeAxis has no auto-range
        var pts = data.series1.length > 0 ? data.series1 : data.series2.length > 0 ? data.series2 : data.series3
        if (pts.length === 1) {
            axisX.min = new Date(pts[0].x - 86400000)   // ±1 day padding
            axisX.max = new Date(pts[0].x + 86400000)
        } else if (pts.length > 1) {
            var firstX   = pts[0].x
            var lastX    = pts[pts.length - 1].x
            var padding  = Math.max((lastX - firstX) * 0.05, 86400000)
            axisX.min    = new Date(firstX - padding)
            axisX.max    = new Date(lastX  + padding)
        }

        // Y axis label format
        axisY.labelFormat = maxVal < 10.0 ? "%.1f" : "%.0f"

        // Series names (reflect current data type)
        var dtLabel = root.selectedDataType === "size" ? qsTr("Total File Size") : qsTr("Number of Files")
        series1Line.name = qsTr("Catalogs") + " / " + dtLabel

        // Chart title
        chart.title = data.deviceName + " — " + dtLabel + ul
    }

    Component.onCompleted: {
        loadSettings()
        // Sync comboboxes to restored settings
        var si = sourceCombo.indexOfValue(root.selectedSource)
        if (si >= 0) sourceCombo.currentIndex = si
        var di = dataTypeCombo.indexOfValue(root.selectedDataType)
        if (di >= 0) dataTypeCombo.currentIndex = di
        displayEachValueCheck.checked = root.displayEachValue
        startDateField.text = root.startDateText
        refresh()
    }

    Connections {
        target: appManager1
        function onSelectedDeviceChanged() { root.requestRefresh() }
        function onStatisticsRefreshed()   { root.requestRefresh() }
    }

    Kirigami.Dialog {
        id: statsDateDialog
        title: qsTr("Select start date")

        function applyDate(d) {
            var mm = d.getMonth() + 1
            var dd = d.getDate()
            var text = d.getFullYear() + '-'
                + (mm < 10 ? '0' : '') + mm + '-'
                + (dd < 10 ? '0' : '') + dd
            startDateField.text = text
            root.startDateText  = text
            appManager1.setStatisticsSetting("graphStartDate", text)
            root.refresh()
            statsDateDialog.close()
        }

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing
            KButton {
                text: qsTr("1 day ago")
                onClicked: { var d = new Date(); d.setDate(d.getDate() - 1);   statsDateDialog.applyDate(d) }
            }
            KButton {
                text: qsTr("1 week ago")
                onClicked: { var d = new Date(); d.setDate(d.getDate() - 7);   statsDateDialog.applyDate(d) }
            }
            KButton {
                text: qsTr("1 month ago")
                onClicked: { var d = new Date(); d.setMonth(d.getMonth() - 1); statsDateDialog.applyDate(d) }
            }
            KButton {
                text: qsTr("1 year ago")
                onClicked: { var d = new Date(); d.setFullYear(d.getFullYear() - 1); statsDateDialog.applyDate(d) }
            }
        }
    }

    ColumnLayout {
        id: pageColumn
        x: root.narrow ? KTheme.gap : KTheme.pagePadding
        width: root.availableWidth - 2 * x
        spacing: KTheme.gap * 2

        // ── Intro ─────────────────────────────────────────────────────────────
        KSectionHeader {
            Layout.topMargin: pageColumn.x
            level: 1
            title: qsTr("Archive in numbers")
            description: qsTr("How the selected drive has changed over time: its files, their size and its space.")
        }

        // ── Options ───────────────────────────────────────────────────────────
        KCard {
            Layout.fillWidth: true
            KSectionHeader {
                level: 3
                title: qsTr("What is shown")
                description: qsTr("The drive selected in My drives, which records to use and from which date.")
            }

            // The drive the numbers are about
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Controls.Label {
                    text: qsTr("Device")
                    color: KTheme.text3
                    font.pixelSize: KTheme.note
                    font.weight: Font.Medium
                    height: deviceNameLabel.height
                    verticalAlignment: Text.AlignVCenter
                }
                Controls.Label {
                    id: deviceNameLabel
                    // Guard the selectedDeviceId so the untranslated "No device selected"
                    // fallback in getSelectedDeviceName() never reaches the UI.
                    visible: appManager1.selectedDeviceId > 0
                    text: appManager1.selectedDeviceId > 0 ? appManager1.selectedDeviceName : ""
                    width: Math.min(implicitWidth, parent.width)
                    wrapMode: Text.WrapAnywhere
                    color: KTheme.text
                    font.weight: Font.Bold
                }
                Controls.Label {
                    visible: appManager1.selectedDeviceId <= 0
                    width: parent.width
                    text: qsTr("The numbers appear once a drive is selected in My drives.")
                    wrapMode: Text.WordWrap
                    color: KTheme.text2
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: KTheme.gap

                ColumnLayout {
                    spacing: 4
                    Controls.Label { text: qsTr("Source"); color: KTheme.text3; font.pixelSize: KTheme.note; font.weight: Font.Medium }
                    ComboBoxFitted {
                        id: sourceCombo
                        Layout.preferredHeight: 40
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { value: "all",       text: qsTr("all records")    },
                            { value: "updates",   text: qsTr("updates only")   },
                            { value: "snapshots", text: qsTr("snapshots only") }
                        ]
                        onActivated: {
                            root.selectedSource = currentValue
                            appManager1.setStatisticsSetting("SelectedSource", currentValue)
                            root.refresh()
                        }
                    }
                }

                ColumnLayout {
                    spacing: 4
                    Controls.Label { text: qsTr("Data"); color: KTheme.text3; font.pixelSize: KTheme.note; font.weight: Font.Medium }
                    ComboBoxFitted {
                        id: dataTypeCombo
                        Layout.preferredHeight: 40
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { value: "size",  text: qsTr("Total File Size")   },
                            { value: "count", text: qsTr("Number of Files") }
                        ]
                        onActivated: {
                            root.selectedDataType = currentValue
                            appManager1.setStatisticsSetting("TypeOfData", currentValue)
                            root.refresh()
                        }
                    }
                }

                ColumnLayout {
                    spacing: 4
                    Controls.Label { text: qsTr("From date"); color: KTheme.text3; font.pixelSize: KTheme.note; font.weight: Font.Medium }
                    RowLayout {
                        spacing: 6
                        Controls.TextField {
                            id: startDateField
                            placeholderText: "yyyy-MM-dd"
                            readOnly: true
                            implicitWidth: 110
                            Layout.preferredHeight: 40
                        }
                        IconButton {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            icon.name: "view-calendar"
                            text: qsTr("Select a date")
                            onClicked: statsDateDialog.open()
                        }
                        IconButton {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            icon.name: "edit-clear-history"
                            text: qsTr("Clear")
                            onClicked: {
                                startDateField.text = ""
                                root.startDateText  = ""
                                appManager1.setStatisticsSetting("graphStartDate", "")
                                root.refresh()
                            }
                        }
                    }
                }

                ColumnLayout {
                    spacing: 4
                    // Keeps the checkbox level with the fields beside it
                    Controls.Label { text: " "; font.pixelSize: KTheme.note }
                    Controls.CheckBox {
                        id: displayEachValueCheck
                        Layout.preferredHeight: 40
                        text: qsTr("Display each value")
                        onCheckedChanged: {
                            root.displayEachValue = checked
                            appManager1.setStatisticsSetting("DisplayEachValue", checked ? "true" : "false")
                            series1Line.pointsVisible = checked && series1Line.visible
                            series2Line.pointsVisible = checked && series2Line.visible
                            series3Line.pointsVisible = checked && series3Line.visible
                        }
                    }
                }
            }
        }

        // ── Key numbers ───────────────────────────────────────────────────────
        GridLayout {
            Layout.fillWidth: true
            visible: root.stats.hasData === true
            columns: Math.max(1, Math.min(4, Math.floor(width / 220)))
            columnSpacing: KTheme.gap
            rowSpacing: KTheme.gap
            KMetricCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                label: root.selectedDataType === "size" ? qsTr("Total File Size") : qsTr("Number of Files")
                value: root.formatValue((root.lastPoint(root.stats.series1) || {}).y)
                tagText: root.lastPoint(root.stats.series1)
                         ? qsTr("On %1").arg(root.formatDate(root.lastPoint(root.stats.series1).x)) : ""
            }
            KMetricCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                label: qsTr("Change over the period")
                value: root.changeText()
                tagText: root.firstPoint(root.stats.series1)
                         ? qsTr("Since %1").arg(root.formatDate(root.firstPoint(root.stats.series1).x)) : ""
                tagTone: "info"
            }
            KMetricCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                label: qsTr("Records")
                value: Number((root.stats.series1 || []).length).toLocaleString(Qt.locale(), "f", 0)
                tagText: root.sourceText(root.selectedSource)
            }
            KMetricCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                visible: root.stats.loadSeries2 === true && root.stats.loadSeries3 === true
                label: qsTr("Used Space")
                value: root.formatValue((root.lastPoint(root.stats.series2) || {}).y)
                tagText: qsTr("Total Space") + ": " + root.formatValue((root.lastPoint(root.stats.series3) || {}).y)
            }
        }

        // ── Chart ─────────────────────────────────────────────────────────────
        KCard {
            Layout.fillWidth: true
            Layout.bottomMargin: pageColumn.x
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                KSectionHeader {
                    level: 3
                    title: qsTr("Over time")
                    description: qsTr("Dragging across the chart zooms in; a double-click or a right-click shows it whole again.")
                }
                KInfoTag {
                    Layout.alignment: Qt.AlignTop
                    visible: root.stats.hasData !== true
                    text: qsTr("No data")
                }
            }

            ChartView {
                id: chart
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Kirigami.Units.gridUnit * 20, root.availableHeight * 0.6)
                antialiasing: true
                legend.visible:    true
                legend.alignment:  Qt.AlignBottom
                // Fixed design-system sizes, as on every redesigned page (D34,
                // MDI-F115): they no longer follow the text-size setting.
                legend.font.pixelSize: KTheme.note
                titleFont.pixelSize:   KTheme.body
                titleFont.bold:        true
                backgroundRoundness: 0

                // Follows the system's light/dark scheme, as the design system and
                // the card it sits on do (THM-F14 within D34). A theme switch resets
                // the series colours and the background, so both are re-applied
                // after every switch.
                readonly property bool darkDesktop: KTheme.dark
                theme: darkDesktop ? ChartView.ChartThemeDark : ChartView.ChartThemeLight
                onDarkDesktopChanged: Qt.callLater(applySeriesColors)
                Component.onCompleted: Qt.callLater(applySeriesColors)

                // K2-matching series colours; kept across theme switches (THM-C17).
                // The chart is drawn on the card itself.
                function applySeriesColors() {
                    series1Line.color = "#209fdf"
                    series2Line.color = "#f6a625"
                    series3Line.color = "#99ca53"
                    chart.backgroundColor = "transparent"
                }

                DateTimeAxis {
                    id: axisX
                    format:    "yyyy-MM-dd"
                    titleText: qsTr("Date")
                    labelsFont.pixelSize: KTheme.note
                    titleFont.pixelSize:  KTheme.note
                }
                ValueAxis {
                    id: axisY
                    titleText: qsTr("Total")
                    labelsFont.pixelSize: KTheme.note
                    titleFont.pixelSize:  KTheme.note
                    min: 0
                    max: 100
                }

                LineSeries {
                    id: series1Line
                    name:          qsTr("Catalogs") + " / " + qsTr("Total File Size")
                    color:         "#209fdf"
                    width:         2
                    pointsVisible: false
                    axisX: axisX
                    axisY: axisY
                }

                LineSeries {
                    id: series2Line
                    name:          qsTr("Storage") + " / " + qsTr("Used Space")
                    color:         "#f6a625"
                    width:         2
                    pointsVisible: false
                    axisX: axisX
                    axisY: axisY
                }

                LineSeries {
                    id: series3Line
                    name:          qsTr("Storage") + " / " + qsTr("Total Space")
                    color:         "#99ca53"
                    width:         2
                    pointsVisible: false
                    axisX: axisX
                    axisY: axisY
                }

                // Rubber-band zoom: drag a rectangle to zoom in (K2 RectangleRubberBand).
                // Right-click or double-click resets the zoom.
                Rectangle {
                    id: zoomSelection
                    visible: false
                    color:        Qt.rgba(KTheme.accent.r, KTheme.accent.g, KTheme.accent.b, 0.2)
                    border.color: KTheme.accent
                    border.width: 1
                }

                MouseArea {
                    id: zoomArea
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    // The page scrolls: the drag belongs to the zoom, not the scroll
                    preventStealing: true
                    property real startX: 0
                    property real startY: 0

                    onPressed: (mouse) => {
                        if (mouse.button === Qt.LeftButton) {
                            startX = mouse.x
                            startY = mouse.y
                            zoomSelection.x = mouse.x
                            zoomSelection.y = mouse.y
                            zoomSelection.width = 0
                            zoomSelection.height = 0
                            zoomSelection.visible = true
                        }
                    }
                    onPositionChanged: (mouse) => {
                        if (zoomSelection.visible) {
                            zoomSelection.x = Math.min(startX, mouse.x)
                            zoomSelection.y = Math.min(startY, mouse.y)
                            zoomSelection.width  = Math.abs(mouse.x - startX)
                            zoomSelection.height = Math.abs(mouse.y - startY)
                        }
                    }
                    onReleased: (mouse) => {
                        if (mouse.button === Qt.RightButton) {
                            chart.zoomReset()
                            return
                        }
                        zoomSelection.visible = false
                        if (zoomSelection.width > 5 && zoomSelection.height > 5)
                            chart.zoomIn(Qt.rect(zoomSelection.x, zoomSelection.y,
                                                 zoomSelection.width, zoomSelection.height))
                    }
                    onDoubleClicked: chart.zoomReset()
                }
            }
        }
    }
}
