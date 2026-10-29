import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import QtQuick.Dialogs as Dialogs
import Qt.labs.platform
import QtCore
import QtQml.Models
import Katalog 3.0

// Provides basic features needed for all kirigami applications
Kirigami.ApplicationWindow {
    // Unique identifier to reference this object
    id: root

    // Kirigami's own surfaces (page header bar, menus, dialogs, fields) take
    // the design system's palette, so what is not rebuilt still matches (D34)
    Kirigami.Theme.backgroundColor: KTheme.ground
    Kirigami.Theme.alternateBackgroundColor: KTheme.surface2
    Kirigami.Theme.textColor: KTheme.text
    Kirigami.Theme.disabledTextColor: KTheme.text3
    Kirigami.Theme.highlightColor: KTheme.accent
    Kirigami.Theme.highlightedTextColor: KTheme.onAccent
    Kirigami.Theme.linkColor: KTheme.accentText
    Kirigami.Theme.positiveTextColor: KTheme.ok
    Kirigami.Theme.neutralTextColor: KTheme.warn
    Kirigami.Theme.negativeTextColor: KTheme.bad
    // The page header bar uses the window's colours (above), not the
    // platform's header set, so it sits on the same ground as the pages
    pageStack.globalToolBar.colorSet: Kirigami.Theme.Window
    pageStack.globalToolBar.separatorVisible: false
    // The Qt Quick style (Fusion on macOS) paints toolbars, buttons, fields
    // and menus from the window palette: the same tokens
    palette.window:          KTheme.ground
    palette.windowText:      KTheme.text
    palette.base:            KTheme.surface
    palette.alternateBase:   KTheme.surface2
    palette.text:            KTheme.text
    palette.button:          KTheme.surface
    palette.buttonText:      KTheme.text
    palette.highlight:       KTheme.accent
    palette.highlightedText: KTheme.onAccent
    palette.placeholderText: KTheme.text3
    palette.link:            KTheme.accentText
    palette.toolTipBase:     KTheme.surface2
    palette.toolTipText:     KTheme.text

    // Global activity panel: running job progress plus the queue behind it.
    // Anchored to pageStack rather than used as the window footer: the window
    // footer spans the whole window, so a pinned (non-modal) global drawer sits
    // on top of its left edge. pageStack already excludes the drawer, so
    // matching its geometry puts the panel beside the drawer, not under it.
    OperationQueueView {
        id: activityPanel
        anchors.left:   root.pageStack.left
        anchors.right:  root.pageStack.right
        anchors.bottom: root.pageStack.bottom
        z: 1
    }

    // Re-probe device active status when the user comes back to Katalog, so a
    // drive connected or removed meanwhile shows correctly. Off by default and
    // gated on the mount table, so this is normally a no-op — see
    // SpecDeviceActiveStatus.md. Application-level rather than per-window, so a
    // future secondary window would not change the behaviour.
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive)
                appManager1.refreshDeviceActiveOnActivation()
        }
        //Quitting the application does not always close the window first —
        //Cmd+Q on macOS does not — so the geometry is saved here too.
        function onAboutToQuit() {
            root.saveWindowGeometry()
        }
    }

    Settings {
        id: windowSettings
        property bool drawerPinned:   false
        property real savedCardScale: 1.0
    }

    // Window geometry ---------------------------------------------------------
    // Kept in Katalog's own settings file through AppManager, not in the
    // Settings block above: that one writes to the platform-native store, which
    // a portable build has no business using. The tracked values are the
    // windowed ones, so a maximized window comes back maximized and
    // un-maximizes to the size it had before.
    property bool _geometryRestored: false
    property int  _windowedX:      0
    property int  _windowedY:      0
    property int  _windowedWidth:  0   // set from getWindowGeometry() at start (WIN-C1: one default)
    property int  _windowedHeight: 0
    property bool _resetSizePending: false

    function trackWindowedGeometry() {
        //Only once the saved geometry has been applied: the sizes the platform
        //reports while the window is still being set up are transient, and
        //recording them used to overwrite the stored value before it was read.
        if (!root._geometryRestored || root.visibility !== Window.Windowed)
            return

        root._windowedX      = root.x
        root._windowedY      = root.y
        root._windowedWidth  = root.width
        root._windowedHeight = root.height
    }

    // Reset window size (WIN-F4): back to the default size, windowed. A maximized
    // window is restored first and resized once it is actually windowed: leaving
    // the maximized state is asynchronous on X11 and Wayland, and a size set
    // before that would be replaced by the previous windowed size.
    function resetWindowSize() {
        if (root.visibility === Window.Maximized || root.visibility === Window.FullScreen) {
            root._resetSizePending = true
            root.showNormal()
        } else {
            root.applyDefaultWindowSize()
        }
    }
    function applyDefaultWindowSize() {
        var size = appManager1.getDefaultWindowSize(root.screen ? root.screen.name : "")
        root.width  = size.width
        root.height = size.height
    }

    function saveWindowGeometry() {
        appManager1.saveWindowGeometry(root._windowedX, root._windowedY,
                                       root._windowedWidth, root._windowedHeight,
                                       root.visibility === Window.Maximized)
    }

    signal searchTriggered()
    property real cardScale: 1.0

    // App-wide text size (TYP-F7): scales the application font. QML sizes read
    // appManager1.textPointSize (already scaled), never Kirigami.Theme.defaultFont,
    // which only follows the application font on Linux.
    // The window font carries it to the controls too: Qt Quick Controls take
    // their default font per control type from the platform theme, not from
    // the application font, so buttons would not follow otherwise.
    onCardScaleChanged: appManager1.setTextScale(cardScale)

    // A page hidden while the text size changes gets the new font values but
    // keeps drawing with the old ones (style-drawn controls do not repaint while
    // hidden). When such a page is shown again, the size is nudged and restored
    // in the same frame, which makes every item redraw at the current size.
    property var _pageTextScale: new Map()
    function refreshTextIfStale(page) {
        let shownAt = _pageTextScale.get(page)
        _pageTextScale.set(page, cardScale)
        // A page never shown yet counts as stale too: hidden pages are drawn
        // at creation, before the saved or changed size reaches them.
        if (shownAt !== cardScale) {
            appManager1.setTextScale(cardScale + 0.01)
            appManager1.setTextScale(cardScale)
        }
    }

    // The top line of the window (drawer collection name, page title, page
    // toolbar buttons) keeps the system size: the text-size setting does not
    // apply to it. Kirigami builds the page header itself, rebuilds it every
    // time the page is shown (its loader is only active while the page is
    // visible), and creates the toolbar buttons asynchronously afterwards. So
    // the header is pinned each time Kirigami creates it. Items get the system
    // size explicitly (whole-font
    // assignment, replacing Kirigami's own font binding); headings keep
    // Kirigami's level factors.
    Instantiator {
        model: [pageHome, pageSelection, pageSearch, pageSearchResults, pageDevices, pageExplore,
                pageActivity, pageContext, pageCreate, pageDeviceEdit, pageStatistics, pageTags,
                pageBackup]
        delegate: Connections {
            required property var modelData
            target: modelData
            function onGlobalToolBarItemChanged() { root.styleHeader(modelData.globalToolBarItem) }
        }
    }
    Connections {
        target: pageStack.layers.currentItem
        ignoreUnknownSignals: true
        function onGlobalToolBarItemChanged() { root.styleHeader(pageStack.layers.currentItem.globalToolBarItem) }
    }
    // A layer's header is created during the push, before the connection above
    // retargets to it, so it is also pinned once the layer becomes current.
    Connections {
        target: pageStack.layers
        function onCurrentItemChanged() {
            Qt.callLater(function() {
                let layer = pageStack.layers.currentItem
                if (layer && pageStack.layers.depth > 1)
                    root.styleHeader(layer.globalToolBarItem)
            })
        }
    }
    // Kirigami draws the page header with the platform's own colours (it does
    // not inherit the window's), so its background is set to the design
    // system ground each time the header is created (D34).
    function styleHeader(header) {
        if (!header)
            return
        if (header.background)
            header.background.color = Qt.binding(function() { return KTheme.ground })
        pinHeaderFont(header)
    }
    function pinHeaderFont(item) {
        if (!item)
            return
        const base = appManager1.systemTextPointSize
        const headingFactor = {1: 1.35, 2: 1.20, 3: 1.15, 4: 1.10}
        if (item.font !== undefined) {
            let factor = ("level" in item) ? (headingFactor[item.level] || 1.0) : 1.0
            if (item.font.pointSize !== base * factor) {
                let f = item.font
                f.pointSize = base * factor
                item.font = f
            }
        }
        for (let i = 0; i < item.children.length; ++i)
            pinHeaderFont(item.children[i])
    }

    font.pointSize: appManager1.textPointSize

    // The height the platform's own toolbar takes, measured rather than assumed:
    // Breeze and Fusion pad differently, so any fixed number is right on one
    // platform and wrong on the other. Used by the drawer's collection row so it
    // matches the page toolbar beside it on every platform.
    readonly property real toolBarHeight: toolBarProbe.implicitHeight

    Controls.ToolBar {
        id: toolBarProbe
        visible: false
        enabled: false
        Controls.ToolButton {
            icon.name: "go-up"
            display: Controls.AbstractButton.IconOnly
        }
    }

    // Height of a header strip row. Selection, the Explore directory tree and the
    // Explore file list each have two of them, and they are defined here rather
    // than per page so the three line up when they share the screen.
    readonly property real headerRowHeight: Kirigami.Units.gridUnit * 2

    // The desktop's own background tells us which way round we are.
    //
    // Read inside each colour binding rather than through a shared property. As
    // its own property it was a second hop: changing the Plasma scheme updated
    // highlightColor, which re-ran the colour bindings before this had caught up
    // with the new backgroundColor, so they used the previous light/dark answer
    // and nothing re-triggered them — the band stayed wrong until a restart.
    // Called from a binding, it makes both colours a direct dependency.
    function isDarkDesktop() {
        return Kirigami.Theme.backgroundColor.hslLightness < 0.5
    }

    // The desktop's View background — the surface its own file managers and list
    // views paint on. It needs a probe of its own because everything else here
    // inherits the Window colour set, which is the grey; the View set is the
    // near-white (#fcfcfc under Breeze light) or near-black (#1b1e20 under Breeze
    // dark) one row of a file list is meant to sit on. Taken from the scheme
    // rather than hardcoded to white/black so a scheme that uses neither still
    // gets its own surface (SpecTheme THM-F1, THM-C6).
    readonly property color viewBackgroundColor:
        viewColorProbe.Kirigami.Theme.backgroundColor

    // Must NOT be visible: false. A hidden item gets no resolved palette from
    // the KDE platform theme and Kirigami.Theme.backgroundColor then reads back
    // opaque black, which painted the first row of every file list black on a
    // light desktop. It is a bare 0x0 Item with no children, so it paints
    // nothing regardless. Note this only misbehaves under the real platform
    // theme — with QT_QPA_PLATFORM=offscreen a hidden probe still returns the
    // right colour, so it cannot be caught by an offscreen run.
    Item {
        id: viewColorProbe
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false
    }

    // The two tinted surfaces, defined once for every theme so they always agree.
    // In both cases the logo band is the darker of the two, so it reads as a
    // header above the page.
    // Katalog Colors palette (theme 1), copied from K2: tabwidget_blue_light.css /
    // tabwidget_blue_dark.css and loadCustomThemeLight/Dark (SpecTheme THM-C20).
    // The only hex literals of the theme colours; nothing else may re-declare them.
    readonly property color katalogBlueLight:     "#39b2e5"
    readonly property color katalogBlue:          "#10a2df"
    readonly property color katalogBlueDark:      "#0D79A6"
    readonly property color katalogBlueDarkest:   "#095676"
    readonly property color katalogBlueLightest:  "#e9f7fc"
    readonly property color katalogAlternateDark: "#161b1d"

    // Section titles and in-text links (THM-F17, PGL-C6): K2's title blue under
    // Katalog Colors, the plain text colour under Desktop Theme (gray), the
    // desktop link colour under Desktop Theme.
    readonly property color titleColor:
        appManager1.themeId === 1
        ? (root.isDarkDesktop() ? root.katalogBlueLight : root.katalogBlueDarkest)
        : appManager1.themeId === 2 ? Kirigami.Theme.textColor
        : Kirigami.Theme.linkColor

    // Both surfaces are built from the desktop's ACCENT
    // (Kirigami.Theme.highlightColor), not from its background. The background is
    // a grey, so lightening or darkening it only ever yields more grey; the
    // accent is what carries the desktop's hue — blue under Breeze, green under
    // the openSUSE set — and that is what should show through here.
    //
    // Light desktop: the accent is laid over a near-white background at two
    // strengths, so more accent reads as deeper. Dark desktop: alpha over
    // near-black would make more accent read as *lighter*, so the two are
    // darkened solids of the accent instead, which keeps the band the darker of
    // the pair either way.
    // Theme 2, "Desktop Theme (gray)", takes the same two surfaces from the
    // desktop's BACKGROUND rather than its accent: no hue at all, just a step
    // away from the window colour, for anyone who finds the tinted version too
    // strong.
    // Theme 1, "Katalog Colors", paints these surfaces from K2's own palette
    // (THM-C20), light or dark by isDarkDesktop() as K2 picks it; everything it
    // does not name renders as theme 0 (THM-C2).
    readonly property color selectionPageColor:
        appManager1.themeId === 1
        ? (root.isDarkDesktop() ? root.katalogBlueDarkest : root.katalogBlueLightest)
        : appManager1.themeId === 2
        ? (root.isDarkDesktop() ? Qt.lighter(Kirigami.Theme.backgroundColor, 1.25)
                                : Qt.darker(Kirigami.Theme.backgroundColor, 1.06))
        : (root.isDarkDesktop() ? Qt.darker(Kirigami.Theme.highlightColor, 2.6)
                            : Qt.rgba(Kirigami.Theme.highlightColor.r,
                                      Kirigami.Theme.highlightColor.g,
                                      Kirigami.Theme.highlightColor.b, 0.12))

    // Selection highlight. One definition for every list in the application —
    // Selection cards, search results, explore files and folders, device combos.
    // Desktop Theme: the desktop accent. Katalog Colors: K2's dark blue
    // (THM-C2). Desktop Theme (gray): a grey from the desktop's own
    // text/background colours, the lightest that keeps the highlighted (white)
    // text readable (THM-F18, THM-C22); also the gray drawer band (THM-F20).
    readonly property color selectionHighlightColor:
        appManager1.themeId === 1 ? root.katalogBlueDark
        : appManager1.themeId === 2
          ? (root.isDarkDesktop() ? Qt.lighter(Kirigami.Theme.backgroundColor, 1.9)
                                  : Qt.lighter(Kirigami.Theme.textColor, 3.0))
        : Kirigami.Theme.highlightColor

    // The two list row surfaces. Even rows take the desktop's View background —
    // its brightest surface on a light scheme, its darkest on a dark one — under
    // every Theme value (SpecTheme THM-F1/F2); odd rows take the stripe below,
    // which depends on the Theme setting (THM-F4). Defined here and consumed by
    // three delegates - the Search results list, the Explore file list and the
    // Explore folder list (THM-F8) - none of which may re-derive either colour
    // inline (THM-C1 / THM-C11).
    readonly property color rowBaseColor:   root.viewBackgroundColor
    // The odd-row stripe follows the Theme setting (THM-F4, THM-C19):
    // Desktop Theme lays the desktop accent at 11% over the View surface (K2's
    // #e9f7fc is the Breeze accent at about that strength over white); Desktop
    // Theme (gray) keeps the Window background; Katalog Colors uses K2's own
    // hardcoded stripe, light or dark as K2 picks it.
    readonly property color rowStripeColor:
        appManager1.themeId === 1
          ? (root.isDarkDesktop() ? root.katalogAlternateDark : root.katalogBlueLightest)
          : appManager1.themeId === 2
            ? Kirigami.Theme.backgroundColor
            : Qt.tint(root.viewBackgroundColor,
                      Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g,
                              Kirigami.Theme.highlightColor.b, 0.11))

    // Strength of the 1px line between table cells. K2's tree views draw no
    // column lines at all, so K3 keeps them faint: enough to guide the eye
    // along a row, not enough to read as a grid. One value for every table.
    readonly property real columnSeparatorOpacity: 0.05

    // Double-clicking the divider between two header cells fits the column to
    // its left, as K2's QHeaderView does (SpecCardsAndTables CDT-F4). Every
    // table header routes its taps through headerTapped(): a tap within
    // headerDividerZone of a cell edge belongs to the divider and never sorts
    // (CDT-C6). The double-click is timed here rather than with TapHandler's
    // tapCount, which was measured to stay at 1 on these header cells.
    readonly property real headerDividerZone: 5
    property var  _dividerTable:  null
    property int  _dividerColumn: -1
    property real _dividerTime:   0

    // Returns true when the tap was on a divider (the caller must not sort).
    function headerTapped(table, header, column, cellWidth, x) {
        let left
        if (x >= cellWidth - headerDividerZone)
            left = column
        else if (column > 0 && x <= headerDividerZone)
            left = column - 1
        else
            return false
        // Hidden (zero-width) columns are not loaded: the divider belongs to
        // the nearest shown column on its left.
        while (left >= 0 && table.columnWidth(left) <= 0)
            left--
        if (left < 0)
            return true

        let now = Date.now()
        if (table === _dividerTable && left === _dividerColumn
                && now - _dividerTime <= Qt.styleHints.mouseDoubleClickInterval) {
            _dividerTable  = null
            _dividerColumn = -1
            fitTableColumn(table, header, left)
        } else {
            _dividerTable  = table
            _dividerColumn = left
            _dividerTime   = now
        }
        return true
    }

    // Persisted widths of selected columns (SpecCardsAndTables CDT-F12/C9).
    // `keys` maps a column to its settings key. The saved width becomes the
    // column's default in the page's columnWidthProvider; loading also drops
    // any width set in this session, so a collection switch shows the new
    // collection's widths. Returns { column: width } for the saved ones.
    function loadPersistedColumnWidths(table, keys) {
        let saved = {}
        for (const c in keys) {
            const w = appManager1.getColumnWidthSetting(keys[c])
            if (w > 0)
                saved[c] = w
            table.setColumnWidth(Number(c), -1)
        }
        table.forceLayout()
        return saved
    }

    // Saves each persisted column whose width the user set (by drag or by the
    // CDT-F4 fit) and that differs from the stored one. Only positive explicit
    // widths count: Qt's transient reset on a divider double-click (-1) and a
    // hidden column (0) are never saved. Returns the updated map.
    // Opens a folder or file picker where SpecValidationRules P2-P4 say: on
    // the field's folder, else on the last folder picked in that same dialog
    // this session, else on the collection browse folder. A fresh instance is
    // created each time, because Qt Quick's FolderDialog and FileDialog apply
    // currentFolder only on their first show. `fieldFolder` is a plain folder
    // path, or "" when the field is empty or the dialog has none. Nothing is
    // persisted (P5); a cancel leaves the remembered folder unchanged (P3).
    property var _lastPickedFolders: new Map()
    function openFreshDialog(component, fieldFolder) {
        const start = fieldFolder ? appManager1.pathToFileUrl(fieldFolder)
                    : _lastPickedFolders.get(component)
                      ?? appManager1.pathToFileUrl(appManager1.getCollectionBrowsePath())
        const dialog = component.createObject(root, { currentFolder: start })
        dialog.accepted.connect(function() {
            // A file picker remembers the folder holding the picked file.
            const picked = dialog.selectedFolder !== undefined
                         ? String(dialog.selectedFolder)
                         : String(dialog.selectedFile).replace(/\/[^\/]*$/, "")
            if (picked) _lastPickedFolders.set(component, picked)
            dialog.destroy()
        })
        dialog.rejected.connect(function() { dialog.destroy() })
        dialog.open()
    }

    function savePersistedColumnWidths(table, keys, saved) {
        let updated = Object.assign({}, saved)
        for (const c in keys) {
            const w = Math.round(table.explicitColumnWidth(Number(c)))
            if (w > 0 && w !== updated[c]) {
                appManager1.setColumnWidthSetting(keys[c], w)
                updated[c] = w
            }
        }
        return updated
    }

    // Keyboard navigation shared by every table (SpecCardsAndTables
    // CDT-F5–F11). A table forwards its Keys.onPressed here with its page's
    // actions; whatever is not handled - Esc above all - is left unaccepted so
    // it reaches the page (CDT-C7). `actions` may carry:
    //   activate(row)          - the row's left-click action (CDT-F7)
    //   openMenu(row)          - the row's context menu (CDT-F8)
    //   nameAt(row)            - the text type-ahead matches (CDT-F9)
    //   treeInfo(row), toggle(row) - the Devices tree (CDT-F10); when absent,
    //                            Left/Right scroll horizontally (CDT-F11)
    property var    _typeAheadTable: null
    property string _typeAheadText:  ""
    property real   _typeAheadTime:  0

    function selectTableRow(table, row) {
        if (table.rows <= 0)
            return
        row = Math.max(0, Math.min(table.rows - 1, row))
        table.selectedRow = row
        table.positionViewAtRow(row, TableView.Contain)
    }

    // Fork: Space toggles the Inspector only outside a type-ahead (CDT-F9),
    // where a space can be part of the name being typed
    function typeAheadIdle(table) {
        return _typeAheadTable !== table || _typeAheadText === ""
               || Date.now() - _typeAheadTime > Qt.styleHints.keyboardInputInterval
    }
    property bool inspectorVisible: appManager1.getViewSetting("InspectorVisible")
    property bool exploreColumns: appManager1.getViewSetting("ExploreColumns")   // column view instead of tree + list (fork)
    onInspectorVisibleChanged: appManager1.setViewSetting("InspectorVisible", inspectorVisible)
    onExploreColumnsChanged:   appManager1.setViewSetting("ExploreColumns", exploreColumns)

    function tableKeyPressed(table, event, actions) {
        const rows    = table.rows
        const current = table.selectedRow
        const page    = Math.max(1, Math.floor(table.height / Math.max(1, table.rowHeightProvider(0))))
        const step    = Kirigami.Units.gridUnit * 2

        event.accepted = true
        switch (event.key) {
        case Qt.Key_Up:       selectTableRow(table, current < 0 ? 0 : current - 1);    return
        case Qt.Key_Down:     selectTableRow(table, current < 0 ? 0 : current + 1);    return
        case Qt.Key_PageUp:   selectTableRow(table, current < 0 ? 0 : current - page); return
        case Qt.Key_PageDown: selectTableRow(table, current < 0 ? 0 : current + page); return
        case Qt.Key_Home:     selectTableRow(table, 0);                                return
        case Qt.Key_End:      selectTableRow(table, rows - 1);                         return
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (actions.activate && current >= 0 && current < rows)
                actions.activate(current)
            return
        case Qt.Key_Menu:
            if (actions.openMenu && current >= 0 && current < rows)
                actions.openMenu(current)
            return
        case Qt.Key_F10:
            if (event.modifiers & Qt.ShiftModifier) {
                if (actions.openMenu && current >= 0 && current < rows)
                    actions.openMenu(current)
                return
            }
            break
        case Qt.Key_Left:
        case Qt.Key_Right:
            if (actions.treeInfo) {
                if (current < 0) { selectTableRow(table, 0); return }
                const info = actions.treeInfo(current)
                if (event.key === Qt.Key_Left) {
                    if (info.hasChildren && info.expanded)   actions.toggle(current)
                    else if (info.parentRow >= 0)            selectTableRow(table, info.parentRow)
                } else {
                    if (info.hasChildren && !info.expanded)  actions.toggle(current)
                    else if (info.hasChildren)               selectTableRow(table, current + 1)
                }
            } else {
                const maxX = Math.max(0, table.contentWidth - table.width)
                table.contentX = Math.max(0, Math.min(maxX,
                    table.contentX + (event.key === Qt.Key_Left ? -step : step)))
            }
            return
        }

        // Type-ahead (CDT-F9): printable text without Ctrl/Alt/Meta.
        const text = event.text
        if (actions.nameAt && rows > 0 && text.length > 0 && text >= " "
                && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) {
            const now = Date.now()
            if (_typeAheadTable !== table || now - _typeAheadTime > Qt.styleHints.keyboardInputInterval)
                _typeAheadText = ""
            if (_typeAheadText === "" && text === " ") {   // a lone space is not a search
                event.accepted = false
                return
            }
            _typeAheadTable = table
            _typeAheadTime  = now
            _typeAheadText += text.toLowerCase()
            // A new search starts after the current row; a growing one may stay on it.
            const start = _typeAheadText.length === 1 ? current + 1 : Math.max(0, current)
            for (let i = 0; i < rows; i++) {
                const r = (start + i) % rows
                if (String(actions.nameAt(r) ?? "").toLowerCase().startsWith(_typeAheadText)) {
                    selectTableRow(table, r)
                    break
                }
            }
            return
        }
        event.accepted = false
    }

    // Widest of the header cell and the rows currently loaded; each delegate's
    // implicitWidth is its content's full, unelided width.
    // Applied once the double-click has been handled: Qt's own header handling
    // resets the column to its default width on a divider double-click, after
    // our tap handler has run (measured 2026-10-03: a width set directly was
    // back to the default by the next event-loop pass).
    function fitTableColumn(table, header, column) {
        let w = Math.max(table.implicitColumnWidth(column), header.implicitColumnWidth(column))
        if (w > 0)
            Qt.callLater(function() { table.setColumnWidth(column, Math.ceil(w)) })
    }

    // Background of the empty area of the file/folder lists (below the
    // last row): the alternate row colour under Katalog Colors, the even-row
    // (View) colour under both Desktop Themes.
    readonly property color listBackgroundColor:
        appManager1.themeId === 1 ? root.rowStripeColor : root.rowBaseColor

    // Always at least as dark as a selected card, and always darker than the
    // page beneath it. On a light desktop that is the full accent — the very
    // colour a selected card is filled with; on a dark one the page is already a
    // darkened accent, so the band is darkened further still.
    readonly property color logoBandColor:
        appManager1.themeId === 1
        ? (root.isDarkDesktop() ? Qt.darker(root.katalogBlueDarkest, 1.6) : root.katalogBlueDark)
        : appManager1.themeId === 2
          // Grey: on a dark desktop the page was lightened, so the band stays at
          // the plain background; on a light one it is the selection grey, which
          // carries the white collection name.
          ? (root.isDarkDesktop() ? Kirigami.Theme.backgroundColor
                                  : root.selectionHighlightColor)
          : (root.isDarkDesktop() ? Qt.darker(Kirigami.Theme.highlightColor, 3.8)
                              : Kirigami.Theme.highlightColor)

    // One metric for every small icon button on the Selection page — the panel
    // header and the device cards alike — so the two read as one set and the
    // buttons never set the card height.
    readonly property real selectionButton: Kirigami.Units.iconSizes.small
                                            + Kirigami.Units.smallSpacing
    property bool _firstRunCreateNew: false

    // Selection ("Filters" panel in K2) can be collapsed beside an open feature
    // page — the K2 ShowHideFilters portage (mainwindow_tab_filters.cpp). It is
    // removed from the stack when hidden so the feature page becomes column 0 and
    // keeps Kirigami's global-drawer button; it is forced back in whenever no
    // feature page is open, so the window is never blank.
    // featureOpen = is any non-Selection page currently in the stack.
    property bool featureOpen: false

    // The feature page currently open (null when only Selection is shown). Kept
    // reactive so context menus can adapt to the active page — e.g. the Selection
    // card menu hides its "Search" shortcut while Search/Results is already open.
    property var openFeaturePage: null

    function indexOfPage(page) {
        for (var i = 0; i < pageStack.depth; i++)
            if (pageStack.get(i) === page) return i
        return -1
    }

    // Removes (and hides) every page after `page` in the main stack.
    function removePagesAfter(page) {
        let idx = indexOfPage(page)
        if (idx < 0)
            return
        while (pageStack.depth - 1 > idx) {
            let p = pageStack.get(pageStack.depth - 1)
            pageStack.removePage(p)
            p.visible = false
        }
    }

    // "Explore folder" from Search results (SpecSearchResults SRS-F8): Explore
    // goes AFTER Results instead of replacing the feature pages, so its Close
    // (and Esc, KBS-F1) can return to the same Results, as K2 keeps its Search
    // tab. The catalog is opened by the caller through the usual path (SRS-C3).
    function showExploreAfterResults() {
        if (pageStack.layers.depth > 1)
            pageStack.layers.pop()
        let resultsIdx = indexOfPage(pageSearchResults)
        if (resultsIdx < 0) {           // no Results to come back to
            showPage(pageExplore)
            return
        }
        let existing = indexOfPage(pageExplore)
        if (existing < 0) {
            removePagesAfter(pageSearchResults)
            // push() truncates the pages forward of currentIndex: stand on Results.
            pageStack.currentIndex = resultsIdx
            pageExplore.visible = true
            refreshTextIfStale(pageExplore)
            pageStack.push(pageExplore)
            featureOpen = true
        }
        pageStack.currentIndex = indexOfPage(pageExplore)
        openFeaturePage = pageExplore
    }

    // A catalog card tapped on Selection while Explore is open opens there
    // too; one already on show is not reloaded (EXP-F27).
    // The open is held back so the newly selected card is painted first: the
    // load blocks the window (OPQ-F11 delay). A later tap replaces a pending one.
    function followSelectionInExplore(deviceId) {
        if (indexOfPage(pageExplore) < 0 || exploreFolders.openedDeviceId === deviceId)
            return
        appManager1.setBusyCursor(true)
        exploreFollowTimer.deviceId = deviceId
        exploreFollowTimer.restart()
    }
    Timer {
        id: exploreFollowTimer
        property int deviceId: -1
        interval: appManager1.startDeferralMs()
        onTriggered: exploreFolders.openByDeviceId(deviceId)
    }

    // True when Explore was opened after Results (SRS-F8).
    function exploreIsAfterResults() {
        let resultsIdx = indexOfPage(pageSearchResults)
        return resultsIdx >= 0 && indexOfPage(pageExplore) > resultsIdx
    }

    // Explore's Close in that case: remove only Explore, back to Results.
    function closeExploreBackToResults() {
        pageStack.removePage(pageExplore)
        pageExplore.visible = false
        pageStack.currentIndex = indexOfPage(pageSearchResults)
        openFeaturePage = pageSearchResults
    }

    // While Explore sits after Results it is the only page shown, as K2
    // switches to its Explore tab (SRS-F8). Whatever path removes Explore
    // (Close, Esc, a new search), the Binding lets go and Kirigami's own
    // width-driven column mode comes back as a binding, not a frozen value
    // (SRS-C5). Reevaluated on every stack change: indexOfPage reads depth.
    Binding {
        target: pageStack.columnView
        property: "columnResizeMode"
        value: Kirigami.ColumnView.SingleColumn
        when: root.exploreIsAfterResults()
        restoreMode: Binding.RestoreBindingOrValue
    }
    // The switch widens every page to the window at once while the view still
    // slid there from the old position: Selection showed full width for the
    // first frames (measured). No slide during the switch: Explore shows at once.
    Binding {
        target: pageStack.columnView
        property: "scrollDuration"
        value: 0
        when: root.exploreIsAfterResults()
        restoreMode: Binding.RestoreBindingOrValue
    }

    // "Tag this folder" (EXP-F23): Tags opens as a layer covering Explore, as
    // Settings covers the pages, so its Close (and Esc) returns to the same Explore.
    function showTagsAfterExplore() {
        if (pageStack.layers.depth > 1)
            pageStack.layers.pop()
        if (indexOfPage(pageExplore) < 0) {   // no Explore to come back to
            showPage(pageTags)
            return
        }
        if (indexOfPage(pageTags) >= 0)
            pageStack.removePage(pageTags)
        pageTags.visible = true
        refreshTextIfStale(pageTags)
        pageStack.layers.push(pageTags)
    }

    // True when Tags is the layer opened over Explore (EXP-F23).
    function tagsIsAfterExplore() {
        return pageStack.layers.depth > 1 && pageStack.layers.currentItem === pageTags
    }

    // Tags' Close in that case: close the layer, back to Explore.
    function closeTagsBackToExplore() {
        pageStack.layers.pop()
        pageStack.currentIndex = indexOfPage(pageExplore)
        openFeaturePage = pageExplore
    }

    function selectionInStack() {
        return pageStack.depth > 0 && pageStack.get(0) === pageSelection
    }

    function recomputeFeatureOpen() {
        for (var i = 0; i < pageStack.depth; i++)
            if (pageStack.get(i) !== pageSelection) { featureOpen = true; return }
        featureOpen = false
    }

    // Remove every page except Selection (static page objects are reused, so we
    // also hide them — matches the existing close/re-push pattern).
    function removeAllFeaturePages() {
        for (var i = pageStack.depth - 1; i >= 0; i--) {
            let p = pageStack.get(i)
            if (p !== pageSelection) {
                pageStack.removePage(p)
                p.visible = false
            }
        }
        featureOpen = false
    }

    // Enforce the invariant: Selection in stack at index 0 iff shown OR no feature
    // page open. Call after any change to the stack or to showSelectionPage.
    function syncSelectionVisibility() {
        let shouldShow = appManager1.showSelectionPage || !featureOpen
        let inStack = selectionInStack()
        if (shouldShow && !inStack) {
            // Selection must occupy column 0, before the feature page(s). Inserting
            // at index 0 with insertPage() drops the pages after it on this Kirigami
            // build (the feature page would vanish), so rebuild the stack from the
            // existing page objects — they are static/reused, so state is preserved.
            let pages = []
            for (var i = 0; i < pageStack.depth; i++)
                pages.push(pageStack.get(i))
            for (var j = pages.length - 1; j >= 0; j--)
                pageStack.removePage(pages[j])
            pageSelection.visible = true
            refreshTextIfStale(pageSelection)
            pageStack.push(pageSelection)
            for (var k = 0; k < pages.length; k++) {
                pages[k].visible = true
                refreshTextIfStale(pages[k])
                pageStack.push(pages[k])
            }
            pageStack.currentIndex = pageStack.depth - 1
        } else if (!shouldShow && inStack) {
            pageStack.removePage(pageSelection)
            pageSelection.visible = false
        }
    }

    // ── Page navigation ────────────────────────────────────────────────────
    //
    // Stack layout:
    //   col 0 : pageSelection — the home column; collapsible (see syncSelectionVisibility).
    //           When hidden with a feature page open it is removed from the stack,
    //           so the feature page takes col 0. No Close button.
    //   col 1 : one active feature page at a time (Search by default)
    //           Switching feature pages replaces col 1, so Search is hidden
    //           when Devices / Explore / etc. is open — matching K2 tab behaviour.
    //   col 2 : SearchResults — only when Search is at col 1
    //
    // Layer stack (pageStack.layers, overlay):
    //   Settings, About — one at a time via showLayer().
    //   layers.replace() breaks Kirigami's page header, so we pop then push
    //   after the pop animation completes (tracked via layers.onBusyChanged).

    // Pending layer state — used to sequence pop → push
    property var  _pendingLayerComponent:   null
    property var  _pendingLayerProperties:  null

    Connections {
        target: pageStack.layers
        function onBusyChanged() {
            if (!pageStack.layers.busy && root._pendingLayerComponent !== null) {
                let comp  = root._pendingLayerComponent
                let props = root._pendingLayerProperties
                root._pendingLayerComponent  = null
                root._pendingLayerProperties = null
                if (props)
                    pageStack.layers.push(comp, props)
                else
                    pageStack.layers.push(comp)
            }
            // Re-layout backup cards after the layer animation fully completes.
            // Having two Connections blocks with the same signal can cause one to be
            // silently dropped, so this is merged here rather than in a second block.
            if (!pageStack.layers.busy && pageStack.layers.depth === 1 && pageStack.currentItem === pageBackup)
                Qt.callLater(function() { backupPageForm.refresh() })
        }
    }

    // Navigate to a main-stack page.
    // Always closes any open layer overlay first.
    // pageSelection : clears col 1+, goes to col 0.
    // Any other page: if already in stack → navigate there (preserving pages above it,
    //                 e.g. SearchResults stays when navigating to Search).
    //                 If not in stack → clear col 1+, push at col 1.
    // From the activity panel at the bottom (fork, MDI-F61)
    function showActivityPage() {
        appManager1.setLastPage("Activity")
        root.showPage(pageActivity)
    }

    function showPage(page) {
        // Close any open layer so main content is visible
        if (pageStack.layers.depth > 1)
            pageStack.layers.pop()

        if (page === pageSelection) {
            // Navigate home: drop feature pages. Selection reappears (it is now the
            // only page) without changing the user's show/hide preference.
            removeAllFeaturePages()
            syncSelectionVisibility()
            pageStack.currentIndex = 0
            openFeaturePage = null
            return
        }

        // If the feature page is already in the stack, navigate to it
        // (preserves pages above it, e.g. SearchResults stays when going to Search).
        let existing = indexOfPage(page)
        if (existing >= 0) {
            pageStack.currentIndex = existing
            openFeaturePage = page
            return
        }

        // Not in stack: clear other feature pages, push this one, then collapse
        // Selection if it is hidden so the feature page becomes column 0.
        removeAllFeaturePages()
        page.visible = true
        refreshTextIfStale(page)
        pageStack.push(page)
        featureOpen = true
        syncSelectionVisibility()
        pageStack.currentIndex = indexOfPage(page)
        openFeaturePage = page
    }

    // Show a layer overlay (Settings, About).
    // If another layer is already open: pop it first, then push the new one
    // once the pop animation ends (avoids the broken-header bug from replace()).
    function showLayer(component, properties) {
        if (pageStack.layers.depth > 1) {
            _pendingLayerComponent  = component
            _pendingLayerProperties = properties || null
            pageStack.layers.pop()
        } else {
            if (properties)
                pageStack.layers.push(component, properties)
            else
                pageStack.layers.push(component)
        }
    }

    // Close a col-1 feature page: remove it and return to Selection only.
    // Called from the Close button of every col-1 page for consistent behaviour.
    function closeFeaturePage(page) {
        pageStack.removePage(page)
        page.visible = false
        // Clear any remaining feature pages (e.g. SearchResults if Search was closed)
        removeAllFeaturePages()
        // No feature page left → Selection must reappear so the window is not blank.
        syncSelectionVisibility()
        pageStack.currentIndex = Math.max(0, pageStack.depth - 1)
    }

    // Esc goes back one step (SpecKeyboardShortcuts.md): it triggers the page's
    // own Close/Cancel action (its escapeAction) on the open layer, else on the
    // rightmost page. An open popup keeps its Esc: Qt does not deliver a window
    // shortcut past it (KBS-F4).
    Shortcut {
        sequence: "Esc"
        onActivated: {
            let page = pageStack.layers.depth > 1 ? pageStack.layers.currentItem
                                                  : pageStack.lastItem
            if (page && page.escapeAction && page.escapeAction.enabled)
                page.escapeAction.trigger()
        }
    }

    // Sidebar (fork, D34): a fixed column with the collection menu on top, the
    // sections grouped and described, the running job and Settings at the foot
    function sidebarGo(name, page) {
        appManager1.setLastPage(name)
        root.showPage(page)
    }

    globalDrawer: Kirigami.GlobalDrawer {
        id: sidebar
        // Fixed beside the pages; on a narrow window it slides in over them
        modal: root.width < Kirigami.Units.gridUnit * 48
        drawerOpen: !modal
        width: 264
        actions: []

        background: Rectangle {
            color: KTheme.sidebar
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: KTheme.line }
        }

        // The collection: its name, and the menu to open, create or reopen one
        header: Controls.AbstractButton {
            id: collectionButton
            implicitHeight: collectionRow.implicitHeight + 24
            hoverEnabled: true
            padding: 12
            Accessible.name: appManager1.currentCollectionDisplayName
            onClicked: collectionMenu.popup(collectionButton, 12, collectionButton.height)
            background: Rectangle { color: collectionButton.hovered ? KTheme.hover : "transparent" }
            contentItem: RowLayout {
                id: collectionRow
                spacing: 10
                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    radius: 9
                    color: KTheme.accent
                    Controls.Label { anchors.centerIn: parent; text: "T"; color: KTheme.onAccent; font.weight: Font.Bold }
                }
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    // The app's name (D34, owner 2026-10-30): Teca, based on Katalog
                    Controls.Label { text: "Teca"; font.weight: Font.Bold; color: KTheme.text }
                    Controls.Label {
                        Layout.fillWidth: true
                        text: appManager1.currentCollectionDisplayName
                        elide: Text.ElideRight
                        font.pixelSize: KTheme.note
                        color: KTheme.text3
                    }
                }
                Kirigami.Icon { source: "arrow-down"; isMask: true; color: KTheme.text3; Layout.preferredWidth: 14; Layout.preferredHeight: 14 }
            }
            Controls.Menu {
                id: collectionMenu
                Controls.Menu {
                    title: qsTr("Open...")
                    Controls.MenuItem { icon.name: "network-server-database"; text: qsTr("SQLite Database..."); onTriggered: openFreshDialog(databaseFileDialogComponent, "") }
                    Controls.MenuItem { icon.name: "folder"; text: qsTr("Collection Folder..."); onTriggered: openFreshDialog(memoryFolderDialogComponent, "") }
                    Controls.MenuItem { icon.name: "network-workgroup"; text: qsTr("Hosted Database..."); onTriggered: root.showLayer(settingsPageComponent, { showHostedForm: true }) }
                }
                Controls.Menu {
                    title: qsTr("New...")
                    Controls.MenuItem {
                        icon.name: "network-server-database"
                        text: qsTr("SQLite Database...")
                        onTriggered: {
                            var p = appManager1.getNewCollectionDefaultPath()
                            newDatabaseFileDialog.currentFile = appManager1.pathToFileUrl(p)
                            newDatabaseFileDialog.open()
                        }
                    }
                }
                Controls.MenuSeparator { visible: appManager1.recentCollections.length > 0 }
                Controls.MenuItem { text: qsTr("Recent collections"); enabled: false; visible: appManager1.recentCollections.length > 0 }
                Instantiator {
                    model: appManager1.recentCollections
                    delegate: Controls.MenuItem {
                        required property var modelData
                        icon.name: modelData.iconName
                        text: modelData.displayName
                        onTriggered: root.openRecentCollection(modelData)
                    }
                    onObjectAdded: (index, object) => collectionMenu.insertItem(collectionMenu.count, object)
                    onObjectRemoved: (index, object) => collectionMenu.removeItem(object)
                }
            }
        }

        topContent: [
            ColumnLayout {
                // The drawer stretches its top area: the column fills it and a
                // spacer at its end keeps the entries at the top
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 2
                KSidebarItem {
                    iconName: "go-home"
                    title: qsTr("Home")
                    description: qsTr("Archive overview")
                    active: root.openFeaturePage === pageHome
                    onClicked: root.sidebarGo("Home", pageHome)
                }
                Controls.Label { text: qsTr("Archive").toUpperCase(); font.pixelSize: KTheme.groupLabel; font.weight: Font.DemiBold; font.letterSpacing: KTheme.groupSpacing; color: KTheme.text3; leftPadding: 10; topPadding: 14; bottomPadding: 4 }
                KSidebarItem {
                    iconName: "view-list-icons"
                    title: qsTr("My drives")
                    description: qsTr("Catalogued drives and folders")
                    active: root.openFeaturePage === null
                    onClicked: root.sidebarGo("Selection", pageSelection)
                }
                KSidebarItem {
                    iconName: "view-list-tree"
                    title: qsTr("Browse the archive")
                    description: qsTr("Folders, previews, details")
                    active: root.openFeaturePage === pageExplore
                    onClicked: root.sidebarGo("Explore", pageExplore)
                }
                KSidebarItem {
                    iconName: "edit-find"
                    title: qsTr("Search the drives")
                    description: qsTr("By name, content or meaning")
                    active: root.openFeaturePage === pageSearch || root.openFeaturePage === pageSearchResults
                    onClicked: root.sidebarGo("Search", pageSearch)
                }
                KSidebarItem {
                    iconName: "list-add"
                    title: qsTr("Add a drive")
                    description: qsTr("Catalogue a drive or a folder")
                    active: root.openFeaturePage === pageCreate
                    onClicked: root.sidebarGo("Create", pageCreate)
                }
                Controls.Label { text: qsTr("Intelligence").toUpperCase(); font.pixelSize: KTheme.groupLabel; font.weight: Font.DemiBold; font.letterSpacing: KTheme.groupSpacing; color: KTheme.text3; leftPadding: 10; topPadding: 14; bottomPadding: 4 }
                KSidebarItem {
                    iconName: "document-properties"
                    title: qsTr("Archive knowledge")
                    description: qsTr("Context, glossary, documents")
                    active: root.openFeaturePage === pageContext
                    onClicked: root.sidebarGo("Context", pageContext)
                }
                KSidebarItem {
                    iconName: "utilities-system-monitor"
                    title: qsTr("Work in progress")
                    description: qsTr("Analysis, resources, history")
                    active: root.openFeaturePage === pageActivity
                    onClicked: root.sidebarGo("Activity", pageActivity)
                }
                Controls.Label { text: qsTr("Other").toUpperCase(); font.pixelSize: KTheme.groupLabel; font.weight: Font.DemiBold; font.letterSpacing: KTheme.groupSpacing; color: KTheme.text3; leftPadding: 10; topPadding: 14; bottomPadding: 4 }
                KSidebarItem { iconName: "view-statistics"; title: qsTr("Archive in numbers"); active: root.openFeaturePage === pageStatistics; onClicked: root.sidebarGo("Statistics", pageStatistics) }
                KSidebarItem { iconName: "tag"; title: qsTr("Labels"); active: root.openFeaturePage === pageTags; onClicked: root.sidebarGo("Tags", pageTags) }
                KSidebarItem { iconName: "backup"; title: qsTr("Safety copies"); active: root.openFeaturePage === pageBackup; onClicked: root.sidebarGo("Backup", pageBackup) }
                KSidebarItem { iconName: "view-list-details"; title: qsTr("Manage drives"); active: root.openFeaturePage === pageDevices; onClicked: root.sidebarGo("Devices", pageDevices) }
                KSidebarItem { iconName: "help-about"; title: qsTr("About"); onClicked: root.showLayer(aboutPage) }
                Item { Layout.fillHeight: true }
            }
        ]

        footer: ColumnLayout {
            spacing: 6
            // The running job, compact; opens Work in progress (MDI-F61)
            Controls.AbstractButton {
                id: jobButton
                visible: appManager1.mdiIsRunning
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                padding: 12
                hoverEnabled: true
                Accessible.name: qsTr("Work in progress")
                onClicked: root.sidebarGo("Activity", pageActivity)
                background: Rectangle { radius: 12; color: jobButton.hovered ? KTheme.hover : KTheme.surface; border.color: KTheme.line }
                contentItem: ColumnLayout {
                    spacing: 6
                    RowLayout {
                        Controls.Label { text: qsTr("AI analysis"); font.pixelSize: KTheme.note; font.weight: Font.DemiBold; color: KTheme.text; Layout.fillWidth: true }
                        KInfoTag {
                            text: appManager1.mdiIsPaused ? qsTr("Paused")
                                  : appManager1.mdiJobInfo.waitingForSource ? qsTr("Waiting for the drive") : qsTr("In Progress")
                            tone: appManager1.mdiIsPaused || appManager1.mdiJobInfo.waitingForSource ? "warn" : "info"
                        }
                    }
                    KProgressBar {
                        Layout.fillWidth: true
                        indeterminate: !(appManager1.mdiJobInfo.fileCount > 0)
                        value: appManager1.mdiJobInfo.fileCount > 0 ? appManager1.mdiJobInfo.fileIndex / appManager1.mdiJobInfo.fileCount : 0
                    }
                    Controls.Label { Layout.fillWidth: true; text: appManager1.mdiJobInfo.catalog || ""; elide: Text.ElideRight; font.pixelSize: KTheme.note; color: KTheme.text3 }
                }
            }
            RowLayout {
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.bottomMargin: 10
                KSidebarItem {
                    iconName: "configure"
                    title: qsTr("Settings")
                    onClicked: { appManager1.setLastPage("Settings"); root.showLayer(settingsPageComponent) }
                }
                IconButton {
                    flat: true
                    // Show or hide the drives column beside the open page (K2 ShowHideFilters portage)
                    icon.name: "edit-select"
                    opacity: appManager1.showSelectionPage ? 1.0 : 0.5
                    enabled: root.featureOpen
                    onClicked: appManager1.showSelectionPage = !appManager1.showSelectionPage
                    Controls.ToolTip.text: appManager1.showSelectionPage ? qsTr("Selection shown") : qsTr("Selection hidden")
                    Controls.ToolTip.visible: hovered
                }
            }
        }
    }

    // Quit stays on its standard shortcut (Cmd+Q / Ctrl+Q) without a drawer entry
    Shortcut {
        sequences: [StandardKey.Quit]
        context: Qt.ApplicationShortcut
        onActivated: Qt.quit()
    }

    // Database status notification
        Connections {
            target: appManager1
            function onDatabaseConnectionChanged(success, message) {
                if (success) {
                    showPassiveNotification("✓ " + message, 5000)
                    root.showPage(pageHome)  // the new collection at a glance (D34)
                    pageSearchForm.restoreLastSearch()
                    if (appManager1.isFirstRun) {
                        appManager1.clearFirstRun()
                        if (root._firstRunCreateNew)
                            firstRunReadyDialog.open()
                    }
                } else {
                    showPassiveNotification("✗ " + message, 5000)
                }
            }
            // Collapse/restore Selection when the user toggles it, keeping the
            // open feature page in view.
            function onShowSelectionPageChanged() {
                root.syncSelectionVisibility()
                if (pageStack.depth > 0)
                    pageStack.currentIndex = pageStack.depth - 1
            }
        }

    //Pages ---------------------------------------------------------------------
    pageStack.initialPage: pageSelection

    Component.onCompleted: {
        //The position is asked for unconditionally: a platform that does not let
        //an application place its own window ignores it and places the window
        //itself. It is only asked for if it still lands on an existing screen.
        var savedGeometry = appManager1.getWindowGeometry()
        width  = savedGeometry.width
        height = savedGeometry.height
        if (savedGeometry.hasPosition) {
            x = savedGeometry.x
            y = savedGeometry.y
        }
        root._windowedX      = root.x
        root._windowedY      = root.y
        root._windowedWidth  = root.width
        root._windowedHeight = root.height
        if (savedGeometry.maximized)
            visibility = Window.Maximized
        root._geometryRestored = true

        // Clamped to the setting's range (TYP-F10): earlier builds allowed 0.7 to 1.3.
        root.cardScale = Math.min(1.2, Math.max(0.8, windowSettings.savedCardScale))

        if (appManager1.isFirstRun) {
            firstRunWelcomeDialog.open()
            return
        }

        if (appManager1.shouldShowAlphaWarning())
            alphaWarningDialog.open()

        pageSearchForm.restoreLastSearch()

        // Katalog always opens on Home, the archive at a glance (D34, owner 2026-10-29)
        root.showPage(pageHome)
        // Headers that already exist before the connections above could see them.
        for (let i = 0; i < pageStack.depth; ++i)
            root.styleHeader(pageStack.get(i).globalToolBarItem)
    }

    onXChanged:      root.trackWindowedGeometry()
    onYChanged:      root.trackWindowedGeometry()
    onWidthChanged:  root.trackWindowedGeometry()
    onHeightChanged: root.trackWindowedGeometry()
    onVisibilityChanged: {
        if (root._resetSizePending && root.visibility === Window.Windowed) {
            root._resetSizePending = false
            root.applyDefaultWindowSize()
        }
    }

    //Saved when the window goes away rather than on every resize step.
    onClosing: root.saveWindowGeometry()

    Controls.Dialog {
        id: alphaWarningDialog
        // Fusion paints the dialog, its title and its buttons from the
        // window palette: the design system surface and line (D34)
        palette.window: KTheme.surface
        palette.mid:    KTheme.line
        title: "Teca"
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, parent.width - Kirigami.Units.largeSpacing * 4)

        ColumnLayout {
            width: parent.width
            // One blank line between the message and the link. Set here rather
            // than with newlines in the text, so the spacing can be changed
            // without touching a string that is translated into 30 languages.
            spacing: Kirigami.Units.gridUnit

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Welcome to Teca. Teca is based on Katalog: news, questions and feedback are shared on the Katalog project discussions:")
            }

            // Where to find news and support. The visible text is a
            // shortened form of the address, so it is not prose and carries no
            // qsTr(): a URL is the same in every language. The link points at
            // the discussions root, not at one post or category: this address
            // is frozen into every released build and can never be corrected.
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                textFormat: Text.StyledText
                linkColor: root.titleColor
                text: "<a href=\"https://github.com/StephaneCouturier/Katalog/discussions\">Github: Katalog/discussions</a>"
                onLinkActivated: (link) => Qt.openUrlExternally(link)
                // And one blank line between the link and the buttons below.
                Layout.bottomMargin: Kirigami.Units.gridUnit

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }

        footer: Controls.DialogButtonBox {
            Controls.CheckBox {
                id: doNotShowAgain
                text: qsTr("Do not show again")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.ResetRole
            }
            Controls.Button {
                text: qsTr("OK")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
            }
            onAccepted: {
                if (doNotShowAgain.checked)
                    appManager1.setAlphaWarningShown()
                alphaWarningDialog.close()
            }
        }
    }

    // A newer release is published (SpecVersionCheck.md VCK-F2). K2's message, verbatim.
    Controls.Dialog {
        id: newVersionDialog
        // Fusion paints the dialog, its title and its buttons from the
        // window palette: the design system surface and line (D34)
        palette.window: KTheme.surface
        palette.mid:    KTheme.line
        title: "Teca"
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, parent.width - Kirigami.Units.largeSpacing * 4)
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.Cancel

        property string currentVersion: ""
        property string latestVersion: ""
        property string releaseNotesUrl: ""

        Controls.Label {
            width: parent.width
            wrapMode: Text.WordWrap
            textFormat: Text.RichText
            linkColor: root.titleColor
            text: qsTr("This is version: v%1 <br/><br/>A new version is available: <b>v%2</b> <br/> Find the list of new features in the <a href='%3'>Release Notes</a><br/><br/>Do you want to download it?")
                      .arg(newVersionDialog.currentVersion)
                      .arg(newVersionDialog.latestVersion)
                      .arg(newVersionDialog.releaseNotesUrl)
            onLinkActivated: (link) => Qt.openUrlExternally(link)

            HoverHandler {
                cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
            }
        }

        onAccepted: Qt.openUrlExternally("https://sourceforge.net/projects/katalogg/files/latest/download")

        Connections {
            target: appManager1
            function onNewVersionAvailable(currentVersion, latestVersion, releaseNotesUrl) {
                newVersionDialog.currentVersion  = currentVersion
                newVersionDialog.latestVersion   = latestVersion
                newVersionDialog.releaseNotesUrl = releaseNotesUrl
                newVersionDialog.open()
            }
        }
    }

    // First-run welcome dialog
    Controls.Dialog {
        id: firstRunWelcomeDialog
        // Fusion paints the dialog, its title and its buttons from the
        // window palette: the design system surface and line (D34)
        palette.window: KTheme.surface
        palette.mid:    KTheme.line
        title: "Teca"
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, parent.width - Kirigami.Units.largeSpacing * 4)
        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.RichText
                text: qsTr("<br/><b>Welcome to Teca!</b><br/><br/>It seems this is the first run.<br/><br/>The following Settings have been applied:<br/> - Language: <b>%1</b><br/> - Theme: <b>%2</b><br/><br/>You can change these on the %3 page.")
                          .arg(Qt.locale().name)
                          // The theme actually applied, by its Settings name
                          .arg(appManager1.themeId === 1 ? qsTr("Katalog Colors")
                               : appManager1.themeId === 2 ? qsTr("Desktop Theme (gray)")
                               : qsTr("Desktop Theme"))
                          .arg(qsTr("Settings"))
            }
        }
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("Open existing...")
                onClicked: {
                    root._firstRunCreateNew = false
                    firstRunWelcomeDialog.close()
                    openFreshDialog(databaseFileDialogComponent, "")
                }
            }
            Controls.Button {
                text: qsTr("Create new...")
                onClicked: {
                    root._firstRunCreateNew = true
                    firstRunWelcomeDialog.close()
                    var p = appManager1.getNewCollectionDefaultPath()
                    newDatabaseFileDialog.currentFile = appManager1.pathToFileUrl(p)
                    newDatabaseFileDialog.open()
                }
            }
        }
    }

    // First-run ready dialog (shown after the collection is created)
    Controls.Dialog {
        id: firstRunReadyDialog
        // Fusion paints the dialog, its title and its buttons from the
        // window palette: the design system surface and line (D34)
        palette.window: KTheme.surface
        palette.mid:    KTheme.line
        title: "Teca"
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, parent.width - Kirigami.Units.largeSpacing * 4)
        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.RichText
                text: qsTr("<br/><b>Ready to create a file catalog:</b><br/><br/>")
                      + qsTr("1- Select an entire drive or directory, <br/>2- select options, and <br/>3- click 'Create'<br/>")
            }
        }
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("OK")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
            }
            onAccepted: {
                firstRunReadyDialog.close()
                appManager1.setLastPage("Create")
                root.showPage(pageCreate)
            }
        }
    }

    // Edit device dialogs
    Controls.Dialog {
        id: editDeleteDeviceDialog
        // Fusion paints the dialog, its title and its buttons from the
        // window palette: the design system surface and line (D34)
        palette.window: KTheme.surface
        palette.mid:    KTheme.line
        property string deviceName: ""
        property string deviceType: ""
        property int    deviceId:   0
        title: "Teca"
        modal: true
        anchors.centerIn: parent
        width: Math.min(460, root.width - Kirigami.Units.largeSpacing * 4)
        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.RichText
                text: qsTr("Do you want to <b>delete</b> this %1 device?<br/><br/>Name: <b>%2</b>")
                      .arg(editDeleteDeviceDialog.deviceType)
                      .arg(editDeleteDeviceDialog.deviceName)
            }
        }
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("Yes")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
            }
            Controls.Button {
                text: qsTr("Cancel")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.RejectRole
            }
            onAccepted: {
                editDeleteDeviceDialog.close()
                var err = appManager1.deleteDevice(editDeleteDeviceDialog.deviceId)
                if (err !== "") {
                    editValidationDialog.message = err
                    editValidationDialog.open()
                } else if (pageDeviceEdit.fromDevicesPage) {
                    pageDeviceEdit.fromDevicesPage = false
                    root.showPage(pageDevices)
                } else {
                    root.closeFeaturePage(pageDeviceEdit)
                }
            }
            onRejected: editDeleteDeviceDialog.close()
        }
    }

    Controls.Dialog {
        id: editValidationDialog
        // Fusion paints the dialog, its title and its buttons from the
        // window palette: the design system surface and line (D34)
        palette.window: KTheme.surface
        palette.mid:    KTheme.line
        property alias message: editValidationLabel.text
        title: "Teca"
        modal: true
        anchors.centerIn: parent
        width: Math.min(460, root.width - Kirigami.Units.largeSpacing * 4)
        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                id: editValidationLabel
                wrapMode: Text.WordWrap
            }
        }
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("OK")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
            }
            onAccepted: editValidationDialog.close()
        }
    }

    Controls.Dialog {
        id: editCatalogConfirmDialog
        // Fusion paints the dialog, its title and its buttons from the
        // window palette: the design system surface and line (D34)
        palette.window: KTheme.surface
        palette.mid:    KTheme.line
        property bool rescanNeeded: false
        property bool pathChanged:  false
        title: "Teca"
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, root.width - Kirigami.Units.largeSpacing * 4)
        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                id: editCatalogConfirmLabel
                wrapMode: Text.WordWrap
            }
        }
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("Yes")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
            }
            Controls.Button {
                text: qsTr("Cancel")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.RejectRole
            }
            onAccepted: {
                editCatalogConfirmDialog.close()
                pageDeviceEdit_form.confirmCatalogSave(editCatalogConfirmDialog.rescanNeeded,
                                                       editCatalogConfirmDialog.pathChanged)
            }
            onRejected: editCatalogConfirmDialog.close()
        }
    }

    Controls.Dialog {
        id: editCatalogUpdateDialog
        // Fusion paints the dialog, its title and its buttons from the
        // window palette: the design system surface and line (D34)
        palette.window: KTheme.surface
        palette.mid:    KTheme.line
        title: "Teca"

        // Set when the user accepts; consumed once the dialog is off screen.
        property int pendingRescanDeviceId: 0
        onClosed: {
            if (pendingRescanDeviceId > 0) {
                var id = pendingRescanDeviceId
                pendingRescanDeviceId = 0
                appManager1.triggerDeviceRescan(id)
            }
        }
        modal: true
        anchors.centerIn: parent
        width: Math.min(460, root.width - Kirigami.Units.largeSpacing * 4)
        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Update the catalog content with the new criteria?")
            }
        }
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("Yes")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
            }
            Controls.Button {
                text: qsTr("No")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.RejectRole
            }
            onAccepted: {
                // Started from onClosed, not here. The scan blocks the GUI thread
                // for as long as it runs, so anything begun before the dialog has
                // finished closing leaves it frozen on screen. callLater was not
                // enough — one turn does not cover the close animation.
                editCatalogUpdateDialog.pendingRescanDeviceId = pageDeviceEdit_form.deviceId
                editCatalogUpdateDialog.close()
                pageDeviceEdit_form.finalizeSave()
            }
            onRejected: { editCatalogUpdateDialog.close(); pageDeviceEdit_form.finalizeSave() }
        }
    }

    Controls.Dialog {
        id: editStoragePathDialog
        // Fusion paints the dialog, its title and its buttons from the
        // window palette: the design system surface and line (D34)
        palette.window: KTheme.surface
        palette.mid:    KTheme.line
        property string previousPath: ""
        property string newPath: ""
        title: "Teca"
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, root.width - Kirigami.Units.largeSpacing * 4)
        contentItem: Column {
            spacing: Kirigami.Units.largeSpacing
            Controls.Label {
                width: editStoragePathDialog.availableWidth
                wrapMode: Text.WordWrap
                text: (pageDeviceEdit_form.deviceType === "Catalog"
                       ? qsTr("The catalog source path changed.")
                       : qsTr("The storage path changed."))
                      + "\n\n" + qsTr("Old path:") + " " + editStoragePathDialog.previousPath
                      + "\n" + qsTr("New path:") + " " + editStoragePathDialog.newPath
                      + "\n\n" + qsTr("How should the catalog indexes be updated?")
            }
        }
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("Replace path root")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
            }
            Controls.Button {
                id: editPathRescanBtn
                text: qsTr("Full re-index")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.ApplyRole
            }
            Controls.Button {
                text: qsTr("Skip")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.RejectRole
            }
            onAccepted: {
                editStoragePathDialog.close()
                appManager1.triggerStoragePathReplace(pageDeviceEdit_form.deviceId,
                                                      editStoragePathDialog.previousPath,
                                                      editStoragePathDialog.newPath)
                pageDeviceEdit_form.finalizeSave()
            }
            onRejected: { editStoragePathDialog.close(); pageDeviceEdit_form.finalizeSave() }
        }
        Connections {
            target: editStoragePathDialog.footer
            function onClicked(button) {
                if (button === editPathRescanBtn) {
                    editStoragePathDialog.close()
                    appManager1.triggerDeviceRescan(pageDeviceEdit_form.deviceId)
                    pageDeviceEdit_form.finalizeSave()
                }
            }
        }
    }

    // Device page snapshot dialog
    Controls.Dialog {
        id: devSnapshotDialog
        // Fusion paints the dialog, its title and its buttons from the
        // window palette: the design system surface and line (D34)
        palette.window: KTheme.surface
        palette.mid:    KTheme.line
        property var data: null
        title: "Teca"
        modal: true
        anchors.centerIn: parent
        width: Math.min(500, root.width - Kirigami.Units.largeSpacing * 4)
        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                textFormat: Text.RichText
                wrapMode: Text.WordWrap
                text: {
                    if (!devSnapshotDialog.data) return ""
                    var d = devSnapshotDialog.data
                    var fc    = (d.newCatalogFileCount   || 0)
                    var dfc   = (d.deltaCatalogFileCount || 0)
                    var fs    = appManager1.formatDataSize(d.newCatalogFileSize   || 0)
                    var dfs   = appManager1.formatDataSizeDelta(d.deltaCatalogFileSize  || 0)
                    var spc   = appManager1.formatDataSize(d.newStorageFreeSpace  || 0)
                    var dspc  = appManager1.formatDataSizeDelta(d.deltaStorageFree      || 0)
                    var stot  = appManager1.formatDataSize(d.newStorageTotalSpace || 0)
                    var dstot = appManager1.formatDataSizeDelta(d.deltaStorageTotal     || 0)
                    return "<br/>" + qsTr("A snapshot of this collection was recorded:") +
                        "<table>" +
                        "<tr><td><br/><b>" + qsTr("Catalogs") + "</b></td><td></td><td></td></tr>" +
                        "<tr><td>" + qsTr("Number of files:") + " </td><td style='text-align:right;'><b> " + Number(fc).toLocaleString(Qt.locale(), "f", 0) + " </b></td><td>  (" + qsTr("added:") + " <b> " + ((dfc >= 0) ? "+" : "") + Number(dfc).toLocaleString(Qt.locale(), "f", 0) + " </b>)</td></tr>" +
                        "<tr><td>" + qsTr("Total file size:") + " </td><td style='text-align:right;'><b> " + fs + " </b></td><td>  (" + qsTr("added:") + " <b> " + ((d.deltaCatalogFileSize >= 0) ? "+" : "") + dfs + " </b>)</td></tr>" +
                        "<tr><td><br/><b>" + qsTr("Storage") + "</b></td><td></td><td></td></tr>" +
                        "<tr><td>" + qsTr("Storage free space:") + " </td><td style='text-align:right;'><b> " + spc + " </b></td><td>  (" + qsTr("added:") + " <b> " + ((d.deltaStorageFree >= 0) ? "+" : "") + dspc + " </b>)</td></tr>" +
                        "<tr><td>" + qsTr("Storage total space:") + " </td><td style='text-align:right;'><b> " + stot + " </b></td><td>  (" + qsTr("added:") + " <b> " + ((d.deltaStorageTotal >= 0) ? "+" : "") + dstot + " </b>)</td></tr>" +
                        "</table>"
                }
            }
        }
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("OK")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
            }
            onAccepted: devSnapshotDialog.close()
        }
    }

    // Hosted connection to a private-network host — confirmation, No by default
    // (SpecHostedSecurity.md HST-F3). Used by Settings Connect and recent entries.
    Controls.Dialog {
        id: privateHostDialog
        // Fusion paints the dialog, its title and its buttons from the
        // window palette: the design system surface and line (D34)
        palette.window: KTheme.surface
        palette.mid:    KTheme.line
        property var entry: null
        title: "Teca"
        modal: true
        anchors.centerIn: parent
        width: Math.min(440, root.width - Kirigami.Units.largeSpacing * 4)
        onOpened: privateHostNoButton.forceActiveFocus()
        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: privateHostDialog.entry
                      ? qsTr("Private network connection") + ": " + privateHostDialog.entry.hostName + "\n\n"
                        + qsTr("Your data will be sent to this network database.") + "\n\n"
                        + qsTr("Continue?")
                      : ""
            }
        }
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("Yes")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.YesRole
                onClicked: {
                    var e = privateHostDialog.entry
                    privateHostDialog.close()
                    appManager1.openCollectionHosted(e.hostName, e.dbName, e.port, e.userName, e.password)
                }
            }
            Controls.Button {
                id: privateHostNoButton
                text: qsTr("No")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.NoRole
                onClicked: privateHostDialog.close()
            }
        }
    }

    // Hosted connection checks (SpecHostedSecurity.md HST-F2..F4). Connects a
    // localhost entry, asks for a private one; returns the check so the caller
    // can show the refusal of an invalid host on its form.
    function requestHostedConnect(entry) {
        var check = appManager1.hostedHostCheck(entry.hostName)
        if (check === "localhost") {
            appManager1.openCollectionHosted(entry.hostName, entry.dbName, entry.port,
                                             entry.userName, entry.password)
        } else if (check === "private") {
            privateHostDialog.entry = entry
            privateHostDialog.open()
        }
        return check
    }

    // Recent collections: a refused Hosted entry opens the hosted form, pre-filled,
    // with the refusal shown (HST-F5)
    function openRecentCollection(entry) {
        if (entry.mode === "Hosted") {
            if (requestHostedConnect(entry) === "invalid")
                root.showLayer(settingsPageComponent, { showHostedForm: true, refusedHostedEntry: entry })
            return
        }
        appManager1.openRecentCollection(entry)
    }

    // Update All Active — pre-confirmation dialog
    Controls.Dialog {
        id: devUpdateAllDialog
        // Fusion paints the dialog, its title and its buttons from the
        // window palette: the design system surface and line (D34)
        palette.window: KTheme.surface
        palette.mid:    KTheme.line
        title: "Teca"
        modal: true
        anchors.centerIn: parent
        width: Math.min(440, root.width - Kirigami.Units.largeSpacing * 4)
        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Do you want the summary of updates for each catalog?")
            }
        }
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("Yes")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.YesRole
                onClicked: { devUpdateAllDialog.close(); appManager1.updateAllActiveDevices(true) }
            }
            Controls.Button {
                text: qsTr("No")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.NoRole
                onClicked: { devUpdateAllDialog.close(); appManager1.updateAllActiveDevices(false) }
            }
            Controls.Button {
                text: qsTr("Cancel")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.RejectRole
                onClicked: devUpdateAllDialog.close()
            }
        }
    }

    // Device update result dialog — shown after single update or each catalog in "All active" with Yes
    Controls.Dialog {
        id: devUpdateReportDialog
        // Fusion paints the dialog, its title and its buttons from the
        // window palette: the design system surface and line (D34)
        palette.window: KTheme.surface
        palette.mid:    KTheme.line
        property var report: ({})
        title: "Teca"
        modal: true
        anchors.centerIn: parent
        width: Math.min(460, root.width - Kirigami.Units.largeSpacing * 4)
        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.RichText
                text: {
                    var r = devUpdateReportDialog.report
                    if (!r || !r.deviceType) return ""
                    var msg = ""

                    if (r.deviceType === "Catalog") {
                        // A creation report (OPQ-F16) has no "(added:" columns for the catalog, as K2
                        msg += "<table>"
                        msg += "<tr><td>" + (r.created ? qsTr("Catalog created: ") : qsTr("Catalog updated: "))
                             + "</td><td align='center'><b>" + r.deviceName + "</b></td></tr>"
                        msg += "<tr><td>" + qsTr("Path: ")            + "</td><td><b>" + r.devicePath + "</b></td></tr>"
                        msg += "</table><br/><table>"
                        msg += "<tr><td>" + qsTr("Number of files: ") + "</td><td align='right'><b>" + Number(r.fileCount || 0).toLocaleString(Qt.locale(), "f", 0) + "</b></td>"
                        if (!r.created)
                            msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + Number(r.filesAdded || 0).toLocaleString(Qt.locale(), "f", 0) + "</b>)</td>"
                        msg += "</tr>"
                        msg += "<tr><td>" + qsTr("Total file size: ") + "</td><td align='right'><b>" + appManager1.formatDataSize(r.totalSize || 0) + "</b></td>"
                        if (!r.created)
                            msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + appManager1.formatDataSizeDelta(r.sizeAdded || 0) + "</b>)</td>"
                        msg += "</tr>"
                        msg += "</table>"
                        if (r.storageUpdated) {
                            msg += "<br/><table>"
                            msg += "<tr><td>" + qsTr("Storage updated: ") + "</td><td align='center'><b>" + (r.storageName || "") + "</b></td></tr>"
                            msg += "<tr><td>" + qsTr("Path: ")            + "</td><td><b>" + (r.storagePath || "") + "</b></td></tr>"
                            msg += "</table><br/><table>"
                            msg += "<tr><td>" + qsTr("Used Space: ")  + "</td><td align='right'><b>" + appManager1.formatDataSize(r.storageUsed  || 0) + "</b></td>"
                            msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + appManager1.formatDataSizeDelta(r.storageUsedAdded  || 0) + "</b>)</td></tr>"
                            msg += "<tr><td>" + qsTr("Free Space: ")  + "</td><td align='right'><b>" + appManager1.formatDataSize(r.storageFree  || 0) + "</b></td>"
                            msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + appManager1.formatDataSizeDelta(r.storageFreeAdded  || 0) + "</b>)</td></tr>"
                            msg += "<tr><td>" + qsTr("Total Space: ") + "</td><td align='right'><b>" + appManager1.formatDataSize(r.storageTotal || 0) + "</b></td>"
                            msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + appManager1.formatDataSizeDelta(r.storageTotalAdded || 0) + "</b>)</td></tr>"
                            msg += "</table>"
                        }

                    } else if (r.deviceType === "Storage") {
                        msg += "<table>"
                        msg += "<tr><td>" + qsTr("Storage updated: ") + "</td><td align='center'><b>" + r.deviceName + "</b></td></tr>"
                        msg += "<tr><td>" + qsTr("Path: ")            + "</td><td><b>" + r.devicePath + "</b></td></tr>"
                        msg += "</table><br/><table>"
                        msg += "<tr><td>" + qsTr("Used Space: ")  + "</td><td align='right'><b>" + appManager1.formatDataSize(r.storageUsed  || 0) + "</b></td>"
                        msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + appManager1.formatDataSizeDelta(r.storageUsedAdded  || 0) + "</b>)</td></tr>"
                        msg += "<tr><td>" + qsTr("Free Space: ")  + "</td><td align='right'><b>" + appManager1.formatDataSize(r.storageFree  || 0) + "</b></td>"
                        msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + appManager1.formatDataSizeDelta(r.storageFreeAdded  || 0) + "</b>)</td></tr>"
                        msg += "<tr><td>" + qsTr("Total Space: ") + "</td><td align='right'><b>" + appManager1.formatDataSize(r.storageTotal || 0) + "</b></td>"
                        msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + appManager1.formatDataSizeDelta(r.storageTotalAdded || 0) + "</b>)</td></tr>"
                        msg += "</table>"

                    } else if (r.deviceType === "Virtual") {
                        msg += "<table>"
                        msg += "<tr><td>" + qsTr("Virtual device updated: ") + "</td><td align='center'><b>" + r.deviceName + "</b></td></tr>"
                        msg += "</table><br/><table>"
                        msg += "<tr><td>" + qsTr("Number of files: ") + "</td><td align='right'><b>" + Number(r.fileCount || 0).toLocaleString(Qt.locale(), "f", 0) + "</b></td>"
                        msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + Number(r.filesAdded || 0).toLocaleString(Qt.locale(), "f", 0) + "</b>)</td></tr>"
                        msg += "<tr><td>" + qsTr("Total file size: ") + "</td><td align='right'><b>" + appManager1.formatDataSize(r.totalSize || 0) + "</b></td>"
                        msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + appManager1.formatDataSizeDelta(r.sizeAdded || 0) + "</b>)</td></tr>"
                        msg += "</table><br/>"
                        msg += qsTr("Catalogs updated:") + " <b>" + (r.catalogsUpdated || 0) + "</b> (" + (r.catalogsSkipped || 0) + " " + qsTr("skipped") + ")<br/>"
                        if (r.storageUpdated) {
                            msg += "<br/><table>"
                            msg += "<tr><td>" + qsTr("Storage") + "</td></tr>"
                            msg += "<tr><td>" + qsTr("Used Space: ")  + "</td><td align='right'><b>" + appManager1.formatDataSize(r.storageUsed  || 0) + "</b></td>"
                            msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + appManager1.formatDataSizeDelta(r.storageUsedAdded  || 0) + "</b>)</td></tr>"
                            msg += "<tr><td>" + qsTr("Free Space: ")  + "</td><td align='right'><b>" + appManager1.formatDataSize(r.storageFree  || 0) + "</b></td>"
                            msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + appManager1.formatDataSizeDelta(r.storageFreeAdded  || 0) + "</b>)</td></tr>"
                            msg += "<tr><td>" + qsTr("Total Space: ") + "</td><td align='right'><b>" + appManager1.formatDataSize(r.storageTotal || 0) + "</b></td>"
                            msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + appManager1.formatDataSizeDelta(r.storageTotalAdded || 0) + "</b>)</td></tr>"
                            msg += "</table>"
                        }

                    } else if (r.deviceType === "list") {
                        msg += "<table><br/>"
                        msg += qsTr("Selected active catalogs are updated.") + "&nbsp;<br/>"
                        msg += "<tr><td>" + qsTr("Number of files: ") + "</td><td align='right'><b>" + Number(r.fileCount || 0).toLocaleString(Qt.locale(), "f", 0) + "</b></td>"
                        msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + Number(r.filesAdded || 0).toLocaleString(Qt.locale(), "f", 0) + "</b>)</td></tr>"
                        msg += "<tr><td>" + qsTr("Total file size: ") + "</td><td align='right'><b>" + appManager1.formatDataSize(r.totalSize || 0) + "</b></td>"
                        msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + appManager1.formatDataSizeDelta(r.sizeAdded || 0) + "</b>)</td></tr>"
                        msg += "</table><br/>"
                        msg += qsTr("Catalogs updated:") + " <b>" + (r.catalogsUpdated || 0) + "</b> (" + (r.catalogsSkipped || 0) + " " + qsTr("skipped") + ")<br/>"
                        if (r.storageUpdated) {
                            msg += "<br/><table>"
                            msg += "<tr><td>" + qsTr("Used Space: ")  + "</td><td align='right'><b>" + appManager1.formatDataSize(r.storageUsed  || 0) + "</b></td>"
                            msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + appManager1.formatDataSizeDelta(r.storageUsedAdded  || 0) + "</b>)</td></tr>"
                            msg += "<tr><td>" + qsTr("Free Space: ")  + "</td><td align='right'><b>" + appManager1.formatDataSize(r.storageFree  || 0) + "</b></td>"
                            msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + appManager1.formatDataSizeDelta(r.storageFreeAdded  || 0) + "</b>)</td></tr>"
                            msg += "<tr><td>" + qsTr("Total Space: ") + "</td><td align='right'><b>" + appManager1.formatDataSize(r.storageTotal || 0) + "</b></td>"
                            msg += "<td>&nbsp;&nbsp;" + qsTr("(added: ") + "</td><td align='right'><b>" + appManager1.formatDataSizeDelta(r.storageTotalAdded || 0) + "</b>)</td></tr>"
                            msg += "</table>"
                        }
                    }
                    return msg
                }
            }
        }
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("OK")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
            }
            onAccepted: {
                devUpdateReportDialog.close()
                appManager1.acknowledgeUpdateReport()
            }
        }
    }

    Connections {
        target: appManager1
        function onDeviceUpdateReportReady(report) {
            devUpdateReportDialog.report = report
            devUpdateReportDialog.open()
        }
    }

    // Enrichment of an existing catalog (fork, SpecMediaIntelligence.md MDI-F8, Q9):
    // the wizard's options, filled from the catalog's profile when it has one
    Kirigami.Dialog {
        id: enrichmentDialog
        property int    deviceId:   -1
        property string sourcePath: ""

        function openFor(id, path, name) {
            deviceId   = id
            sourcePath = path
            title      = qsTr("Analyze") + " — " + name
            enrichmentValidation.visible = false
            var profile = appManager1.getEnrichmentProfile(id)
            if (profile.settings)
                enrichmentOptions.loadProfile(profile)
            else
                enrichmentOptions.restoreSettings()
            open()
        }
        function showError(message) {
            enrichmentValidation.text    = message
            enrichmentValidation.visible = true
        }
        function start() {
            var destination = appManager1.normalizeSourcePath(enrichmentOptions.destinationPath)
            if (destination === "")
                return showError(qsTr("Provide a destination path for the previews and derived data."))
            if (!appManager1.isPathWritable(destination))
                return showError(qsTr("Select a destination path that can be written to."))
            if (appManager1.isPathInside(destination, sourcePath))
                return showError(qsTr("Select a destination path outside the source path."))
            if (!enrichmentOptions.anyFileType() && enrichmentOptions.anyPreview())
                return showError(qsTr("Select at least one file type, or turn off all previews."))
            enrichmentOptions.rememberSettings()
            appManager1.saveEnrichmentProfile(deviceId, enrichmentOptions.buildProfile())
            var error = appManager1.startEnrichment(deviceId)
            if (error !== "")
                return showError(error)
            close()
        }

        width: Math.min(root.width - Kirigami.Units.gridUnit * 4, Kirigami.Units.gridUnit * 38)
        preferredHeight: Kirigami.Units.gridUnit * 34
        // The design system surface, not Kirigami's View colours (D34)
        background: Rectangle {
            color: KTheme.surface
            radius: KTheme.radiusCard
            border.width: 1
            border.color: KTheme.line
        }
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Analyse with AI")
                icon.name: "tools-wizard"
                onTriggered: enrichmentDialog.start()
            }
        ]

        contentItem: Controls.ScrollView {
            contentWidth: availableWidth
            Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
            ColumnLayout {
                width: parent.width
                Kirigami.InlineMessage {
                    id: enrichmentValidation
                    Layout.fillWidth: true
                    type: Kirigami.MessageType.Warning
                    showCloseButton: true
                    visible: false
                }
                CreateWizard {
                    id: enrichmentOptions
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.largeSpacing
                    onChoiceEdited: enrichmentValidation.visible = false
                }
            }
        }
    }

    // Create empty-source confirmation (Yes/No decision → modal)
    Controls.Dialog {
        id: createEmptyDirDialog
        // Fusion paints the dialog, its title and its buttons from the
        // window palette: the design system surface and line (D34)
        palette.window: KTheme.surface
        palette.mid:    KTheme.line
        title: "Teca"
        modal: true
        anchors.centerIn: parent
        width: Math.min(460, root.width - Kirigami.Units.largeSpacing * 4)
        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("The source folder does not contain any file.\nThis could mean that the source is empty or the device is not mounted to this folder.\nDo you want to save it anyway (the catalog would be empty)?")
            }
        }
        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: qsTr("Yes")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
            }
            Controls.Button {
                text: qsTr("No")
                Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.RejectRole
            }
            onAccepted: { createEmptyDirDialog.close(); pageCreate_formLayout_Create.doCreate() }
            onRejected: createEmptyDirDialog.close()
        }
    }

    Component {
        id: aboutPage
        Kirigami.AboutPage {
            property Kirigami.Action escapeAction: aboutPageEscapeAction  // Esc (KBS-F1)
            aboutData: About
            actions: [
                // Filing a bug means quoting versions the user would otherwise
                // have to hunt for; one action puts them all on the clipboard.
                // The block itself is assembled in AppManager and deliberately
                // carries no paths, no host, and no credentials — see
                // SpecAbout.md.
                Kirigami.Action {
                    text: qsTr("Copy version and system information")
                    icon.name: "edit-copy"
                    onTriggered: {
                        appManager1.copyToClipboard(appManager1.systemInformation())
                        showPassiveNotification(
                            qsTr("Version and system information copied to clipboard"))
                    }
                },
                Kirigami.Action {
                    id: aboutPageEscapeAction
                    text: qsTr("Close")
                    icon.name: "view-close"
                    onTriggered: pageStack.layers.pop()
                }
            ]
        }
    }

    //Pages - Selection
    ScrollablePageFitted {
        id: pageSelection

        // The design system's ground, like every page (D34): the drive cards
        // carry their own surface, so they stand out without a tinted page
        background: Rectangle {
            color: KTheme.ground
        }
        title: qsTr("My drives")
        property string deviceType: "Storage"

        header: ColumnLayout {
            width: parent.width
            spacing: 10

            // What the page is for, like every page of the redesign (D34)
            Controls.Label {
                Layout.fillWidth: true
                Layout.leftMargin:  KTheme.gap
                Layout.rightMargin: KTheme.gap
                Layout.topMargin:   KTheme.gap
                wrapMode: Text.WordWrap
                color: KTheme.text2
                text: qsTr("The drive or folder chosen here is the one searched, browsed and counted.")
            }

            // A reminder of what is currently selected, with no label of its
            // own — the icon and type styling say what it is (SpecSelection.md
            // SEL-F5). Always present, including the All state, so the rows below
            // never shift as the selection changes. It paints its own surface
            // (THM-F10: Fusion's ItemDelegate background is opaque), from the
            // design system tokens (D34). Not a list row: no stripe and no
            // highlighted state, it mirrors the selection rather than being
            // selectable.
            Controls.ItemDelegate {
                id: selectionReminder
                Layout.fillWidth: true
                Layout.leftMargin:  KTheme.gap
                Layout.rightMargin: KTheme.gap
                Layout.preferredHeight: root.headerRowHeight + 12
                leftPadding: 12
                rightPadding: 12
                topPadding: 0
                bottomPadding: 0
                hoverEnabled: true

                background: Rectangle {
                    radius: KTheme.radiusControl
                    color: selectionReminder.hovered ? KTheme.hover : KTheme.accentSoft
                    border.width: 1
                    border.color: KTheme.line
                }

                // Scrolls the list to the selected card — the "where is it?" half
                // of the problem this reminder solves. It deliberately does NOT
                // change or clear the selection: it sits directly above the filter
                // field, and a misclick that widened the search scope would be
                // silent (SEL-F6, SEL-C7).
                onClicked: {
                    var row = appManager1.selectionRowForDevice(appManager1.selectedDeviceId)
                    if (row >= 0)
                        selectionListView1.positionViewAtIndex(row, ListView.Contain)
                }

                contentItem: DeviceIdentity {
                    deviceType: appManager1.selectedDeviceId > 0
                                ? appManager1.selectedDeviceType : "All"
                    deviceName: appManager1.selectedDeviceId > 0
                                ? appManager1.selectedDeviceName : qsTr("All")
                    deviceIsActive: appManager1.selectedDeviceIsActive
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin:   KTheme.gap
                Layout.rightMargin:  KTheme.gap
                Layout.bottomMargin: 4
                spacing: 10
                // Filters the device list, not files: a filter icon and
                // placeholder keep it from reading as a file search.
                // ActionTextField, not SearchField: SearchField draws its own
                // magnifier, which cannot be replaced. Both icons are plain
                // Icons, not actions, so they carry no button frame or tooltip:
                // the filter icon is styled like SearchField's magnifier, the
                // clear icon like the Search page's (SearchTermList.qml).
                Kirigami.ActionTextField {
                    id: deviceSearchField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Name") + "..."
                    focusSequence: StandardKey.Find
                    inputMethodHints: Qt.ImhNoPredictiveText
                    Accessible.searchEdit: true
                    leftPadding: filterIcon.width + Kirigami.Units.smallSpacing * 3
                    rightPadding: text.length > 0
                                  ? Kirigami.Units.iconSizes.small + Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing
                                  : undefined
                    onTextChanged: appManager1.setDeviceFilter(text)
                    color: KTheme.text
                    placeholderTextColor: KTheme.text3
                    topPadding: 9
                    bottomPadding: 9
                    background: Rectangle {
                        radius: KTheme.radiusControl
                        color: KTheme.surface
                        border.width: deviceSearchField.activeFocus ? 2 : 1
                        border.color: deviceSearchField.activeFocus ? KTheme.accent : KTheme.line
                    }

                    Kirigami.Icon {
                        id: filterIcon
                        anchors { left: parent.left; leftMargin: Kirigami.Units.smallSpacing * 2; verticalCenter: parent.verticalCenter }
                        implicitWidth:  Kirigami.Units.iconSizes.sizeForLabels
                        implicitHeight: Kirigami.Units.iconSizes.sizeForLabels
                        color: deviceSearchField.placeholderTextColor
                        source: "view-filter"
                    }

                    Kirigami.Icon {
                        anchors { right: parent.right; rightMargin: Kirigami.Units.smallSpacing * 2; verticalCenter: parent.verticalCenter }
                        source: parent.LayoutMirroring.enabled ? "edit-clear-locationbar-ltr" : "edit-clear-locationbar-rtl"
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: Kirigami.Units.iconSizes.small
                        visible: deviceSearchField.text.length > 0
                        opacity: deviceClearTap.pressed ? 0.5 : 1.0
                        Behavior on opacity { NumberAnimation { duration: Kirigami.Units.shortDuration } }
                        HoverHandler { id: deviceClearHover; cursorShape: Qt.ArrowCursor }
                        Controls.ToolTip.text: qsTr("Clear")
                        Controls.ToolTip.visible: deviceClearHover.hovered
                        TapHandler {
                            id: deviceClearTap
                            onTapped: {
                                deviceSearchField.clear()
                                deviceSearchField.forceActiveFocus()
                            }
                        }
                    }
                }
                // The device details on the cards, as a switch with its name in
                // view, like the criteria of the Search page (D34)
                Controls.CheckBox {
                    id: deviceInfoSwitch
                    text: qsTr("Show device info")
                    checked: appManager1.showDeviceInfo
                    onToggled: appManager1.showDeviceInfo = checked
                    hoverEnabled: true
                    spacing: 8
                    leftPadding: mirrored ? 0 : indicator.width + spacing
                    rightPadding: mirrored ? indicator.width + spacing : 0
                    indicator: Rectangle {
                        implicitWidth: 40
                        implicitHeight: 24
                        x: deviceInfoSwitch.mirrored ? deviceInfoSwitch.width - width : 0
                        y: (deviceInfoSwitch.height - height) / 2
                        radius: height / 2
                        color: deviceInfoSwitch.checked ? KTheme.accent : KTheme.neutralSoft
                        border.width: deviceInfoSwitch.visualFocus ? 2 : 1
                        border.color: deviceInfoSwitch.checked || deviceInfoSwitch.visualFocus ? KTheme.accent : KTheme.line
                        Rectangle {
                            width: 18
                            height: 18
                            radius: 9
                            y: 3
                            x: deviceInfoSwitch.checked ? parent.width - width - 3 : 3
                            color: deviceInfoSwitch.checked ? KTheme.onAccent : KTheme.surface
                            border.width: deviceInfoSwitch.checked ? 0 : 1
                            border.color: KTheme.line
                            Behavior on x { NumberAnimation { duration: Kirigami.Units.shortDuration } }
                        }
                    }
                    contentItem: Controls.Label {
                        text: deviceInfoSwitch.text
                        color: KTheme.text2
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Item {}
                }
                Controls.ToolButton {
                    icon.name: "go-up"
                    icon.width:  Kirigami.Units.iconSizes.small
                    icon.height: Kirigami.Units.iconSizes.small
                    implicitWidth:  root.selectionButton + 8
                    implicitHeight: root.selectionButton + 8
                    padding: 0
                    enabled: appManager1.canCollapseDevices
                    onClicked: appManager1.collapseDevices()
                    Controls.ToolTip.text: qsTr("Collapse one level")
                    Controls.ToolTip.visible: hovered
                    background: Rectangle {
                        radius: KTheme.radiusItem
                        color: parent.hovered ? KTheme.hover : KTheme.surface
                        border.width: 1
                        border.color: KTheme.line
                        opacity: parent.enabled ? 1 : 0.5
                    }
                }
                Controls.ToolButton {
                    icon.name: "go-down"
                    icon.width:  Kirigami.Units.iconSizes.small
                    icon.height: Kirigami.Units.iconSizes.small
                    implicitWidth:  root.selectionButton + 8
                    implicitHeight: root.selectionButton + 8
                    padding: 0
                    enabled: appManager1.canExpandDevices
                    onClicked: appManager1.expandDevices()
                    Controls.ToolTip.text: qsTr("Expand one level")
                    Controls.ToolTip.visible: hovered
                    background: Rectangle {
                        radius: KTheme.radiusItem
                        color: parent.hovered ? KTheme.hover : KTheme.surface
                        border.width: 1
                        border.color: KTheme.line
                        opacity: parent.enabled ? 1 : 0.5
                    }
                }
            }
        }

        Kirigami.CardsListView {
            id: selectionListView1
            model: appManager1.deviceFilterModel
            delegate: PageSelectionDelegate {}
            // Without this, delegates keep painting outside the viewport while
            // the list scrolls — which is how card icons ended up drawn over the
            // search history dialog.
            clip: true
            topMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing
        }
    }

    //Pages - Search
    ScrollablePageFitted {
        id: pageSearch
        background: Rectangle { color: KTheme.ground }   // design system ground (D34)
        // Hidden until pushed, like every other feature page: when the app opens
        // on another page, Search is never pushed and would otherwise stay drawn
        // on the window behind the page stack.
        visible: false
        property Kirigami.Action escapeAction: pageSearchEscapeAction  // Esc (KBS-F1)
        title: qsTr("Search the drives")

        // Run the search and show the results page. Shared by the Search action button
        // and the Enter key in the search text field so both behave identically.
        function runSearch() {
            if (appManager1.searchIsRunning)
                return
            // A catalog update or creation runs synchronously on this thread and
            // keeps the UI alive by pumping the event loop, so the Search action
            // stays clickable while one is in progress. Starting a search there
            // would run it nested inside that job's call stack, re-entrantly, on
            // the same database connection. Refuse instead.
            if (appManager1.deviceUpdateIsRunning || appManager1.catalogIsCreating)
                return
            root.searchTriggered()
            pageSearchForm.executeSearch()
            // If the results page is already the top of the stack, do NOT remove and
            // re-push it: removing and immediately re-pushing the same static page object
            // in one event loop tick corrupts the QML component context and crashes.
            // The onSearchTriggered Connections in PageSearchResultsForm resets the model.
            // Results already after Search (possibly with Explore after it, from
            // "Explore folder"): drop only what follows Results and reuse it in
            // place - never remove and re-push it in one pass (SRS-C3).
            let resultsIdx = indexOfPage(pageSearchResults)
            if (resultsIdx >= 0 && resultsIdx > indexOfPage(pageSearch)) {
                root.removePagesAfter(pageSearchResults)
                root.recomputeFeatureOpen()
                pageStack.currentIndex = resultsIdx
            } else {
                // Remove any stale pages beyond Search, then push Results fresh.
                let searchIdx = indexOfPage(pageSearch)
                while (pageStack.depth - 1 > searchIdx) {
                    let p = pageStack.get(pageStack.depth - 1)
                    pageStack.removePage(p)
                    p.visible = false
                }
                // Ensure currentIndex points to Search before pushing.
                // Kirigami's push() truncates pages forward of currentIndex, so if
                // the user back-navigated without removing pages, push() would drop Search.
                pageStack.currentIndex = searchIdx
                pageSearchResults.visible = true
                root.refreshTextIfStale(pageSearchResults)
                pageStack.push(pageSearchResults)
            }
            // The keyboard goes to the results straight away, no row selected
            // (SRS-F11). Once the page change has settled, so it is not taken back.
            Qt.callLater(pageSearchResultsForm.focusTable)
        }

        actions: [
            Kirigami.Action {
                text:        qsTr("Search")
                icon.name:   "edit-find"
                displayHint: Kirigami.DisplayHint.KeepVisible
                enabled:     !appManager1.searchIsRunning
                             && !appManager1.deviceUpdateIsRunning
                             && !appManager1.catalogIsCreating
                             && !appManager1.exploreLoadIsRunning   // OPQ-F20
                onTriggered: pageSearch.runSearch()
            },
            Kirigami.Action {
                text:        appManager1.searchIsPaused ? qsTr("Resume") : qsTr("Pause")
                icon.name:   appManager1.searchIsPaused ? "media-playback-start" : "media-playback-pause"
                displayHint: Kirigami.DisplayHint.KeepVisible
                enabled:     appManager1.searchIsRunning
                onTriggered: appManager1.searchIsPaused ? appManager1.resumeSearch() : appManager1.pauseSearch()
            },
            Kirigami.Action {
                text:        qsTr("Stop")
                icon.name:   "process-stop"
                displayHint: Kirigami.DisplayHint.KeepVisible
                enabled:     appManager1.searchIsRunning
                onTriggered: appManager1.stopSearch()
            },
            Kirigami.Action {
                text:        qsTr("Reset")
                icon.name:   "edit-clear-history"
                displayHint: Kirigami.DisplayHint.KeepVisible
                enabled:     !appManager1.searchIsRunning
                onTriggered: pageSearchForm.resetSearch()
            },
            Kirigami.Action {
                text:        qsTr("History")
                icon.name:   "view-history"
                displayHint: Kirigami.DisplayHint.KeepVisible
                enabled:     !appManager1.searchIsRunning
                onTriggered: pageSearchForm.openHistorySheet()
            },
            Kirigami.Action {
                id: pageSearchEscapeAction
                text:        qsTr("Close")
                icon.name:   "view-close"
                displayHint: Kirigami.DisplayHint.KeepVisible
                onTriggered: {
                    pageStack.removePage(pageSearch)
                    pageSearch.visible = false
                    // Results stays in the stack if present; navigate to it or Selection
                    root.recomputeFeatureOpen()
                    root.syncSelectionVisibility()
                    pageStack.currentIndex = Math.max(0, pageStack.depth - 1)
                }
            }
        ]


        PageSearchForm {
            id: pageSearchForm
            onSearchRequested: pageSearch.runSearch()
        }
    }

    //Pages - SearchResults
    Kirigami.Page {
        id: pageSearchResults
        background: Rectangle { color: KTheme.ground }   // design system ground (D34)
        property Kirigami.Action escapeAction: pageSearchResultsEscapeAction  // Esc (KBS-F1)
        visible: false
        title: {
            let n = Number(newSearch1.properties.filesFoundNumber ?? 0).toLocaleString(Qt.locale(), "f", 0)
            if (newSearch1.properties.searchOnDuplicates)
                return qsTr("Duplicates (%1)").arg(n)
            if (newSearch1.properties.searchOnDifferences)
                return qsTr("Differences (%1)").arg(n)
            return qsTr("Results")  //return qsTr("Results (%1)").arg(n)
        }
        padding: 0

        actions: [
            Kirigami.Action {
                // Offline Inspector beside the results (fork); also Space
                text: qsTr("File details")
                icon.name: "document-properties"
                checkable: true
                checked: root.inspectorVisible
                onTriggered: root.inspectorVisible = !root.inspectorVisible
            },
            Kirigami.Action {
                id: pageSearchResultsEscapeAction
                text: qsTr("Close")
                icon.name: "view-close"
                onTriggered: {
                    root.removePagesAfter(pageSearchResults)   // e.g. Explore (SRS-F8)
                    pageStack.removePage(pageSearchResults)
                    pageSearchResults.visible = false
                    // Go to Search if still in stack, otherwise Selection
                    root.recomputeFeatureOpen()
                    root.syncSelectionVisibility()
                    pageStack.currentIndex = Math.max(0, pageStack.depth - 1)
                }
            }
        ]


        PageSearchResultsForm {
            id: pageSearchResultsForm
            width:  pageSearchResults.availableWidth
            height: pageSearchResults.availableHeight
            isCurrentPage: pageStack.lastItem === pageSearchResults && pageStack.layers.depth <= 1
            onExploreFolderRequested: function(deviceId, folderPath) {
                // Same opening path as the Devices page Explore action (SRS-C3),
                // then the row's folder is selected and revealed (EXP-F8).
                appManager1.setLastPage("Explore")
                exploreFolders.openByDeviceId(deviceId, folderPath)
                root.showExploreAfterResults()
            }
            onCloseRequested: {
                root.removePagesAfter(pageSearchResults)   // e.g. Explore (SRS-F8)
                pageStack.removePage(pageSearchResults)
                pageSearchResults.visible = false
                root.recomputeFeatureOpen()
                root.syncSelectionVisibility()
                pageStack.currentIndex = Math.max(0, pageStack.depth - 1)
            }
        }
    }

    //Pages - Devices
    Kirigami.Page {
        id: pageDevices
        background: Rectangle { color: KTheme.ground }   // design system ground (D34)
        property Kirigami.Action escapeAction: pageDevicesEscapeAction  // Esc (KBS-F1)
        visible: false
        padding: 0
        title: qsTr("Manage drives")
        actions: [
            Kirigami.Action {
                text:        qsTr("All active")
                icon.name:   "media-playlist-repeat"
                displayHint: Kirigami.DisplayHint.KeepVisible
                // Always enabled: requests are queued while an operation runs
                // (SpecOperationQueue.md).
                onTriggered: devUpdateAllDialog.open()
            },
            Kirigami.Action {
                text:        qsTr("Stop")
                icon.name:   "process-stop"
                displayHint: Kirigami.DisplayHint.KeepVisible
                enabled:     appManager1.deviceUpdateIsRunning
                onTriggered: appManager1.stopDeviceUpdate()
            },
            Kirigami.Action {
                text: qsTr("Snapshot")
                icon.name: "camera-photo"
                enabled: !appManager1.deviceUpdateIsRunning && !appManager1.searchIsRunning
                onTriggered: {
                    var result = appManager1.recordDevicesSnapshot()
                    devSnapshotDialog.data = result
                    devSnapshotDialog.open()
                }
            },
            Kirigami.Action {
                text: qsTr("Insert Virtual Group")
                icon.name: "folder-new"
                enabled: !appManager1.deviceUpdateIsRunning && !appManager1.searchIsRunning
                onTriggered: {
                    var newId = appManager1.addDeviceVirtual(0)
                    if (newId > 0) {
                        pageDeviceEdit.fromDevicesPage = true
                        pageDeviceEdit_form.deviceId = newId
                        pageDeviceEdit_form.loadDevice()
                        root.showPage(pageDeviceEdit)
                    }
                }
            },
            Kirigami.Action {
                text: qsTr("Add Storage")
                icon.name: "drive-harddisk"
                enabled: !appManager1.deviceUpdateIsRunning && !appManager1.searchIsRunning
                onTriggered: {
                    var newId = appManager1.addDeviceStorage(0)
                    if (newId > 0) {
                        pageDeviceEdit.fromDevicesPage = true
                        pageDeviceEdit_form.deviceId = newId
                        pageDeviceEdit_form.loadDevice()
                        root.showPage(pageDeviceEdit)
                    }
                }
            },
            // Kirigami.Action {
            //     text:        qsTr("Gentle stop")
            //     icon.name:   "media-playback-stop"
            //     displayHint: Kirigami.DisplayHint.KeepVisible
            //     enabled:     appManager1.deviceUpdateIsRunning
            //     onTriggered: appManager1.gentleStopDeviceUpdate()
            // },
            Kirigami.Action {
                id: pageDevicesEscapeAction
                text: qsTr("Close")
                icon.name: "view-close"
                onTriggered: root.closeFeaturePage(pageDevices)
            }
        ]

        // Update progress is reported by the global activity panel, next to
        // the page area — see OperationQueueView.qml.

        PageDevicesView {
            id: pageDevicesView
            cardScale: root.cardScale
            onEditDeviceRequested: (deviceId) => {
                console.log("Main: onEditDeviceRequested deviceId=" + deviceId)
                pageDeviceEdit.fromDevicesPage = true
                pageDeviceEdit_form.deviceId = deviceId
                pageDeviceEdit_form.loadDevice()
                root.showPage(pageDeviceEdit)
                console.log("Main: showPage(pageDeviceEdit) done")
            }
            onExploreDeviceRequested: (deviceId) => {
                appManager1.setLastPage("Explore")
                exploreFolders.openByDeviceId(deviceId)
                root.showPage(pageExplore)
            }
        }
    }

    //Pages - Explore
    Kirigami.Page {
        id: pageExplore
        background: Rectangle { color: KTheme.ground }   // design system ground (D34)
        property Kirigami.Action escapeAction: pageExploreEscapeAction  // Esc (KBS-F1)
        visible: false
        title: qsTr("Browse the archive")
        padding: 0

        actions: [
            Kirigami.Action {
                // Finder-style column view instead of tree + list (fork)
                text: qsTr("Columns")
                icon.name: "view-list-details"
                checkable: true
                checked: root.exploreColumns
                onTriggered: root.exploreColumns = !root.exploreColumns
            },
            Kirigami.Action {
                // Offline Inspector beside the file list (fork); also Space
                text: qsTr("File details")
                icon.name: "document-properties"
                checkable: true
                checked: root.inspectorVisible
                onTriggered: root.inspectorVisible = !root.inspectorVisible
            },
            // Enrich the catalog shown (fork, SpecMediaIntelligence.md MDI-F8, Q9)
            Kirigami.Action {
                text: qsTr("Analyse with AI")
                icon.name: "tools-wizard"
                enabled: exploreFolders.openedDeviceId > 0
                onTriggered: enrichmentDialog.openFor(exploreFolders.openedDeviceId,
                                                      exploreFiles.catalogPath,
                                                      exploreFiles.catalogName)
            },
            Kirigami.Action {
                text: qsTr("Topic clusters")
                icon.name: "view-group"
                readonly property bool available:
                    exploreFiles.catalogId > 0 && appManager1.mdiRevision >= 0
                    && Object.keys(appManager1.mdiClusters(exploreFiles.catalogId)).length > 0
                visible: available
                onTriggered: exploreFiles.showClusters()
            },
            Kirigami.Action {
                id: pageExploreEscapeAction
                text: qsTr("Close")
                icon.name: "view-close"
                // Opened from Results by "Explore folder": back to the same
                // Results (SRS-F8, KBS-F1); otherwise back to Selection.
                onTriggered: root.exploreIsAfterResults() ? root.closeExploreBackToResults()
                                                          : root.closeFeaturePage(pageExplore)
            }
        ]

        // The tree's width is the user's, as K2's splitter (EXP-F34). Saved
        // when a drag ends; restored as the saved share of the width each
        // time Explore opens, within both minimums (EXP-C27).
        Controls.SplitView {
            id: exploreSplit
            anchors.fill: parent
            orientation: Qt.Horizontal

            // A 1 px line, as the separator it replaces. Wider, its transparent
            // sides showed the window background as a grey band; the grab
            // area is the strip laid over it below (exploreSplitGrab).
            // The panes are cards with their own edges: the handle is a gap
            handle: Item { implicitWidth: 6 }

            readonly property real treeMinimum:  Kirigami.Units.gridUnit * 10
            readonly property real filesMinimum: Kirigami.Units.gridUnit * 8

            // The saved share, held while Explore opens: the page is laid out in
            // steps after it shows (630 then 1187 px measured on the AppImage),
            // so the share is applied again at each width until the user drags.
            // -1: nothing to restore, or the user has taken over.
            property real restoreShare: -1
            function restoreDivision() {
                let saved = appManager1.getExploreSplitterWidths()
                restoreShare = saved.length === 2 ? saved[0] / (saved[0] + saved[1]) : -1
                applyRestoreShare()
            }
            function applyRestoreShare() {
                if (restoreShare < 0 || width <= 0)
                    return
                let room = width - filesMinimum
                exploreFolders.Controls.SplitView.preferredWidth =
                    Math.max(treeMinimum, Math.min(room, width * restoreShare))
            }
            function saveDivision() {
                appManager1.setExploreSplitterSizes(exploreFolders.width, exploreFolders.height,
                                                    exploreFiles.width,   exploreFiles.height)
            }
            onResizingChanged: {
                if (resizing)
                    restoreShare = -1
                else
                    saveDivision()
            }
            onWidthChanged: applyRestoreShare()
            Connections {
                target: pageExplore
                function onVisibleChanged() {
                    if (pageExplore.visible)
                        Qt.callLater(exploreSplit.restoreDivision)
                }
            }
            Component.onCompleted: if (pageExplore.visible) Qt.callLater(restoreDivision)

            PageExploreFolders {
                id: exploreFolders
                visible: !root.exploreColumns
                Controls.SplitView.preferredWidth: Kirigami.Units.gridUnit * 16
                Controls.SplitView.minimumWidth:   exploreSplit.treeMinimum

                onFolderClicked: function(folderPath) {
                    exploreFiles.currentFolderPath = folderPath
                }
                onCatalogOpenRequested: function(info) {
                    exploreFiles.catalogName  = info.name  ?? ""
                    exploreFiles.catalogPath  = info.path  ?? ""
                    exploreFiles.catalogId    = info.externalId ?? 0
                }
                onTagFolderRequested: function(folderPath) {
                    pageTagsForm.setFolderPath(folderPath)
                    root.showTagsAfterExplore()
                }
            }

            PageExploreFiles {
                id: exploreFiles
                visible: !root.exploreColumns
                Controls.SplitView.fillWidth:    true
                Controls.SplitView.minimumWidth: exploreSplit.filesMinimum

                onInspectorToggleRequested: root.inspectorVisible = !root.inspectorVisible
                onFolderNavigated: function(folderPath) {
                    exploreFolders.selectedFolderPath = folderPath
                    exploreFiles.currentFolderPath    = folderPath
                }
            }

            // Column view (fork): replaces the tree and the list when chosen
            ColumnBrowser {
                id: exploreColumnBrowser
                visible: root.exploreColumns
                Controls.SplitView.fillWidth: true
                rootPath:  exploreFiles.catalogPath
                catalogId: exploreFiles.catalogId
                onInspectorToggleRequested: root.inspectorVisible = !root.inspectorVisible
            }

            // Offline Inspector (fork): follows the selected file
            Inspector {
                visible: root.inspectorVisible
                Controls.SplitView.preferredWidth: Kirigami.Units.gridUnit * 18
                Controls.SplitView.minimumWidth:   Kirigami.Units.gridUnit * 12
                catalogId: exploreFiles.catalogId
                fullPath:  root.exploreColumns ? exploreColumnBrowser.selectedFullPath
                                               : exploreFiles.selectedFullPath
            }
        }

        // The styles' handle grabs only its own 1-2 px (measured) and SplitView
        // ignores a wider containmentMask, so this strip, laid over the line
        // and a few pixels each side of it, does the drag (EXP-F34).
        MouseArea {
            id: exploreSplitGrab
            x: exploreSplit.x + exploreFolders.width - Kirigami.Units.smallSpacing
            y: exploreSplit.y
            width:  Kirigami.Units.smallSpacing * 2 + 1
            height: exploreSplit.height
            cursorShape: Qt.SplitHCursor
            preventStealing: true
            // In window coordinates: the strip itself moves with the line.
            property real startSceneX: 0
            property real startWidth:  0
            onPressed: function(mouse) {
                exploreSplit.restoreShare = -1
                startSceneX = mapToItem(null, mouse.x, 0).x
                startWidth  = exploreFolders.width
            }
            onPositionChanged: function(mouse) {
                if (!pressed)
                    return
                let treeWidth = startWidth + mapToItem(null, mouse.x, 0).x - startSceneX
                exploreFolders.Controls.SplitView.preferredWidth = Math.max(exploreSplit.treeMinimum,
                    Math.min(exploreSplit.width - exploreSplit.filesMinimum - 1, treeWidth))
            }
            onReleased: exploreSplit.saveDivision()
        }
    }

    //Pages - Activity (fork, SpecMediaIntelligence.md MDI-F61..F75)
    ScrollablePageFitted {
        id: pageActivity
        background: Rectangle { color: KTheme.ground }   // design system ground (D34)
        property Kirigami.Action escapeAction: pageActivityEscapeAction  // Esc (KBS-F1)
        visible: false
        title: qsTr("Work in progress")
        // The monitor samples only while the page is shown (or a job runs)
        onVisibleChanged: pageActivity_form.setShown(visible)
        actions: [
            Kirigami.Action {
                id: pageActivityEscapeAction
                text: qsTr("Close")
                icon.name: "view-close"
                onTriggered: root.closeFeaturePage(pageActivity)
            }
        ]
        PageActivity {
            id: pageActivity_form
        }
    }

    //Pages - Home (fork, D34): the archive at a glance
    ScrollablePageFitted {
        id: pageHome
        property Kirigami.Action escapeAction: pageHomeEscapeAction  // Esc (KBS-F1)
        visible: false
        title: qsTr("Home")
        background: Rectangle { color: KTheme.ground }
        onVisibleChanged: pageHome_form.setShown(visible)
        actions: [
            Kirigami.Action {
                id: pageHomeEscapeAction
                text: qsTr("Close")
                icon.name: "view-close"
                visible: false   // Home is the start of the sidebar, nothing to close (Esc still works)
                onTriggered: root.closeFeaturePage(pageHome)
            }
        ]
        PageHome {
            id: pageHome_form
            onOpenPage: (name) => {
                var pages = { "Search": pageSearch, "Create": pageCreate, "Explore": pageExplore,
                              "Context": pageContext, "Activity": pageActivity, "Selection": pageSelection }
                if (pages[name])
                    root.sidebarGo(name, pages[name])
            }
        }
    }

    //Pages - Context (fork, SpecMediaIntelligence.md D33, MDI-F94..F107)
    ScrollablePageFitted {
        id: pageContext
        background: Rectangle { color: KTheme.ground }   // design system ground (D34)
        property Kirigami.Action escapeAction: pageContextEscapeAction  // Esc (KBS-F1)
        visible: false
        title: qsTr("Archive knowledge")
        onVisibleChanged: pageContext_form.setShown(visible)
        actions: [
            Kirigami.Action {
                icon.name: "document-save"
                text: qsTr("Save")
                enabled: pageContext_form.dirty
                onTriggered: pageContext_form.save()
            },
            Kirigami.Action {
                // Classification only, for files already analysed (MDI-F103)
                icon.name: "view-refresh"
                text: qsTr("Re-interpret with the context")
                enabled: pageContext_form.hasSavedContext && !pageContext_form.dirty
                         && pageContext_form.catalogDeviceId >= 0
                onTriggered: pageContext_form.openReinterpret()
            },
            Kirigami.Action {
                id: pageContextEscapeAction
                text: qsTr("Close")
                icon.name: "view-close"
                onTriggered: root.closeFeaturePage(pageContext)
            }
        ]
        PageContext {
            id: pageContext_form
        }
    }

    //Pages - Create
    ScrollablePageFitted {
        id: pageCreate
        background: Rectangle { color: KTheme.ground }   // design system ground (D34)
        property Kirigami.Action escapeAction: pageCreateEscapeAction  // Esc (KBS-F1)
        visible: false
        title: qsTr("Add a drive")
        Connections {
            target: appManager1
            function onCatalogCreationCompleted(success, report) {
                // A full success is reported by the creation report dialog, on
                // the Devices page, whichever page is open (OPQ-F16, OPQ-F18)
                if (success && report.length === 0) {
                    appManager1.refreshDeviceList()
                    root.showPage(pageDevices)
                    return
                }
                if (pageCreate.visible) {
                    // On success `report` is non-empty only when the catalog was
                    // kept with an unfinished metadata/checksum scan; it already
                    // explains what happened.
                    showPassiveNotification(
                        success ? report : qsTr("Catalog creation failed: ") + report, "long")
                    if (success)
                        appManager1.refreshDeviceList()
                }
            }
        }

        Connections {
            target: pageCreate_formLayout_Create
            function onEmptyDirConfirmNeeded() {
                createEmptyDirDialog.open()
            }
        }

        actions: [
            Kirigami.Action {
                text: qsTr("Catalogue the drive")
                icon.name: "document-save"
                // Always enabled: a creation requested while another operation
                // runs is queued rather than refused (SpecOperationQueue.md).
                onTriggered: pageCreate_formLayout_Create.triggerCreate()
            },
            Kirigami.Action {
                text: qsTr("Stop")
                icon.name: "process-stop"
                enabled: appManager1.catalogIsCreating
                onTriggered: pageCreate_formLayout_Create.triggerStop()
            },
            Kirigami.Action {
                id: pageCreateEscapeAction
                text: qsTr("Close")
                icon.name: "view-close"
                onTriggered: root.closeFeaturePage(pageCreate)
            }
        ]

        // Creation progress is reported by the global activity panel in the
        // window footer, alongside the queue — see OperationQueueView.qml.

        PageCreateForm {
            id: pageCreate_formLayout_Create
        }
    }

    //Pages - Device Edit
    ScrollablePageFitted {
        id: pageDeviceEdit
        background: Rectangle { color: KTheme.ground }   // design system ground (D34)
        property Kirigami.Action escapeAction: pageDeviceEditEscapeAction  // Esc (KBS-F1)
        visible: false
        title: qsTr("Edit Device")
        property bool fromDevicesPage: false
        Connections {
            target: pageDeviceEdit_form
            function onSaveError(message) {
                pageDeviceEdit_form.showSaveError(message)
            }
            function onCatalogConfirmNeeded(message, rescanNeeded, pathChanged) {
                editCatalogConfirmLabel.text = qsTr("Save changes to the catalog definition?\n\n%1\n\n(The catalog must be updated to reflect these changes)").arg(message)
                editCatalogConfirmDialog.rescanNeeded = rescanNeeded
                editCatalogConfirmDialog.pathChanged  = pathChanged
                editCatalogConfirmDialog.open()
            }
            function onCatalogUpdateContentNeeded() {
                editCatalogUpdateDialog.open()
            }
            function onStoragePathChangeNeeded(previousPath, newPath) {
                editStoragePathDialog.previousPath = previousPath
                editStoragePathDialog.newPath = newPath
                editStoragePathDialog.open()
            }
            function onSaveCompleted() {
                if (pageDeviceEdit.fromDevicesPage) {
                    pageDeviceEdit.fromDevicesPage = false
                    root.showPage(pageDevices)
                } else {
                    root.closeFeaturePage(pageDeviceEdit)
                }
            }
        }

        actions: [
            Kirigami.Action {
                text: qsTr("Save")
                icon.name: "document-save"
                onTriggered: pageDeviceEdit_form.triggerSave()
            },
            Kirigami.Action {
                text: qsTr("Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    var check = appManager1.checkDeviceDeleteAllowed(pageDeviceEdit_form.deviceId)
                    if (!check.allowed) {
                        editValidationDialog.message = check.errorMessage
                        editValidationDialog.open()
                    } else {
                        var d = appManager1.getDeviceDetails(pageDeviceEdit_form.deviceId)
                        editDeleteDeviceDialog.deviceId   = pageDeviceEdit_form.deviceId
                        editDeleteDeviceDialog.deviceType = pageDeviceEdit_form.deviceType
                        editDeleteDeviceDialog.deviceName = d.name
                        editDeleteDeviceDialog.open()
                    }
                }
            },
            Kirigami.Action {
                id: pageDeviceEditEscapeAction
                text: qsTr("Cancel")
                icon.name: "view-close"
                onTriggered: {
                    if (pageDeviceEdit.fromDevicesPage) {
                        pageDeviceEdit.fromDevicesPage = false
                        root.showPage(pageDevices)
                    } else {
                        root.closeFeaturePage(pageDeviceEdit)
                    }
                }
            }
        ]

        PageDeviceEditForm {
            id: pageDeviceEdit_form
        }
    }

    //Pages - Statistics
    Kirigami.Page {
        id: pageStatistics
        background: Rectangle { color: KTheme.ground }   // design system ground (D34)
        property Kirigami.Action escapeAction: pageStatisticsEscapeAction  // Esc (KBS-F1)
        visible: false
        title: qsTr("Archive in numbers")
        padding: 0

        actions: [
            Kirigami.Action {
                id: pageStatisticsEscapeAction
                text: qsTr("Close")
                icon.name: "view-close"
                onTriggered: root.closeFeaturePage(pageStatistics)
            }
        ]

        PageStatisticsForm {
            anchors.fill: parent
        }
    }

    //Pages - Tags
    ScrollablePageFitted {
        id: pageTags
        background: Rectangle { color: KTheme.ground }   // design system ground (D34)
        property Kirigami.Action escapeAction: pageTagsEscapeAction  // Esc (KBS-F1)
        visible: false
        title: qsTr("Labels")
        actions: [
            Kirigami.Action {
                id: pageTagsEscapeAction
                text: qsTr("Close")
                icon.name: "view-close"
                // Opened from Explore by "Tag this folder": back to the same
                // Explore (EXP-F23, KBS-F1); otherwise back to Selection.
                onTriggered: root.tagsIsAfterExplore() ? root.closeTagsBackToExplore()
                                                       : root.closeFeaturePage(pageTags)
            }
        ]

        PageTagsForm { id: pageTagsForm }
    }

    //Pages - Backup
    ScrollablePageFitted {
        id: pageBackup
        background: Rectangle { color: KTheme.ground }   // design system ground (D34)
        property Kirigami.Action escapeAction: pageBackupEscapeAction  // Esc (KBS-F1)
        visible: false
        title: qsTr("Safety copies")

        onVisibleChanged: {
            if (visible)
                Qt.callLater(function() { backupPageForm.refresh() })
        }

        actions: [
            Kirigami.Action {
                text:      qsTr("Add")
                icon.name: "list-add"
                onTriggered: root.showLayer(backupMappingFormComponent)
            },
            Kirigami.Action {
                text:      qsTr("LuckyBackup")
                tooltip: qsTr("Generate LuckyBackup profile")
                icon.name: "document-save"
                enabled:   backupPageForm.mappings.length > 0
                onTriggered: {
                    var ids = backupPageForm.mappings.map(function(m) { return m.mappingId })
                    appManager1.generateLuckyBackupProfile(ids)
                }
            },
            Kirigami.Action {
                id: pageBackupEscapeAction
                text:      qsTr("Close")
                icon.name: "view-close"
                onTriggered: root.closeFeaturePage(pageBackup)
            }
        ]

        // Preparation progress shown on the Backup page while "Update catalogs" runs and/or
        // while the preview compare runs (BKP-F14/BKP-F16) — same StatusBarMessageBuilder
        // messages as the other screens. The compare can be cancelled from here.
        footer: RowLayout {
            id: backupPrepFooter
            readonly property bool   prepRunning: appManager1.catalogUpdateForBackupRunning
                                                  || appManager1.backupPreviewRunning
            readonly property string prepStatus:  appManager1.backupPreviewRunning
                                                  ? appManager1.backupPreviewStatusText
                                                  : appManager1.catalogUpdateForBackupStatusText
            visible: prepRunning || prepStatus.length > 0
            spacing: Kirigami.Units.smallSpacing
            Controls.BusyIndicator {
                running: backupPrepFooter.prepRunning
                visible: backupPrepFooter.prepRunning
                implicitWidth:  Kirigami.Units.gridUnit * 1.5
                implicitHeight: Kirigami.Units.gridUnit * 1.5
                Layout.leftMargin: Kirigami.Units.smallSpacing
            }
            Controls.Label {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.smallSpacing
                text: backupPrepFooter.prepStatus
                textFormat: Text.StyledText
                elide: Text.ElideRight
            }
            Controls.Button {
                visible: appManager1.backupPreviewRunning
                text:      qsTr("Cancel")
                icon.name: "process-stop"
                Layout.rightMargin: Kirigami.Units.smallSpacing
                onClicked: appManager1.stopBackupPreview()
            }
        }

        PageBackupForm {
            id: backupPageForm
            cardScale: root.cardScale
            onRequestAddMapping:        root.showLayer(backupMappingFormComponent)
            // The whole preview flow (optional catalog update → cancellable compare) runs in
            // AppManager on the Backup page; the footer shows progress. The Preview page opens
            // only when the report is ready (BKP-F14/BKP-F16).
            onRequestPreviewMapping: (mappingId) => appManager1.startBackupPreview(mappingId)
            onRequestEditMapping: (mappingData) => root.showLayer(backupMappingFormComponent, { editMappingId: mappingData.mappingId, mappingData: mappingData })
            onRequestReport: root.showLayer(backupPreviewFormComponent, { reportMode: true })
        }

        // Preview compare finished — open the Preview page now that the report is ready.
        // Not fired for a cancelled or failed preview.
        Connections {
            target: appManager1
            function onBackupPreviewReady(mappingId, success, cancelled) {
                if (success)
                    root.showLayer(backupPreviewFormComponent, { mappingId: mappingId })
            }
        }
    }

    Component {
        id: backupMappingFormComponent
        PageBackupMappingForm {}
    }

    Component {
        id: backupPreviewFormComponent
        PageBackupPreviewForm {}
    }

    //Pages - Settings
    Component {
        id: settingsPageComponent
        PageSettings {
            textScale: root.cardScale
            onTextScaleEdited: function(value) {
                value = Math.round(value * 10) / 10   // keep exact steps (0.8 … 1.2)
                root.cardScale = value
                windowSettings.savedCardScale = value
            }
        }
    }

    // Dialogs - triggered from Open Collection menu
    Component {   // a fresh dialog per open (openFreshDialog)
        id: memoryFolderDialogComponent
        Dialogs.FolderDialog {
            id: memoryFolderDialog
            title: qsTr("Select Collection Folder")
            onAccepted: {
                var path = appManager1.pathFromFileUrl(selectedFolder.toString())
                appManager1.openCollectionMemory(path)
            }
        }
    }

    Component {   // a fresh dialog per open (openFreshDialog)
        id: databaseFileDialogComponent
        Dialogs.FileDialog {
            id: databaseFileDialog
            title: qsTr("Open SQLite Database")
            nameFilters: ["SQLite databases (*.db *.sqlite *.sqlite3)", "All files (*)"]
            onAccepted: {
                var path = appManager1.pathFromFileUrl(selectedFile.toString())
                appManager1.setDatabaseFilePath(path)
            }
        }
    }

    Dialogs.FileDialog {
        id: newDatabaseFileDialog
        title: qsTr("Select the database to create and open:")
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: ["SQLite databases (*.db)", "All files (*)"]
        onAccepted: {
            var path = appManager1.pathFromFileUrl(selectedFile.toString())
            appManager1.createNewSQLiteCollection(path)
        }
    }
}
