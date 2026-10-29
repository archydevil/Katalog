import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// A device's icon and name, styled by type.
//
// The single definition of those rules (SpecSelection.md SEL-C1). Both the
// Selection cards and the selected-device reminder above them use this, so the
// reminder can never end up looking like a device card of a different vintage:
// a change here reaches both at once. Duplicating the rules into a second file
// is what this component exists to prevent.
//
// Redesign (fork, D34, phases R6/R7): drawn with the design system's tokens
// (KTheme), at the fixed sizes of the prototype (MDI-F115). A caller may ask for
// the Home drive-card icon tile (iconTile) instead of a bare icon.
RowLayout {
    id: identity

    // "Virtual" | "Storage" | "Catalog" | "All"
    required property string deviceType
    required property string deviceName
    property bool deviceIsActive: false

    // Overridden when the name sits on a filled highlight, where the ordinary
    // text colour would have too little contrast.
    property color nameColor: KTheme.text

    // Font rules ported from K2's device tree (qt_widgets/devicetreeview.cpp,
    // FontRole :133-150 and ForegroundRole :199-218): Virtual is bold italic and
    // the most dimmed, Storage is bold and dimmed a little, a Catalog is the
    // lightest so it reads as the leaf. Dimming stays above 4.5:1 on KTheme.
    readonly property int  nameWeight:  (deviceType === "Virtual" || deviceType === "Storage")
                                        ? Font.Bold : Font.Medium
    readonly property bool nameItalic:  deviceType === "Virtual"
    readonly property real nameOpacity: deviceType === "Virtual" ? 0.70
                                      : deviceType === "Storage" ? 0.85
                                      : 1.0

    // The Devices card draws its own, larger icon in a separate column, so it
    // takes the name rules from here without a second icon beside them
    // (DVP-C14). Default true, so every other caller is unaffected.
    property bool showIcon: true

    // A card never hides text, it wraps it (SpecCardsAndTables.md CDT-F1). The
    // caller decides, because this component also serves the selected-device
    // reminder above the Selection list, which is a single line that must not
    // grow and push the list down (CDT-C4 / DVP-C16).
    property bool nameWraps: false

    // The icon's size, so a caller can follow the "Use bigger icon size"
    // setting (THM-F7).
    property real iconSize: Kirigami.Units.iconSizes.small

    // Home drive-card language (D34): the icon sits on a rounded tile, accent
    // when the device is reachable, neutral and faded when it is not.
    property bool iconTile: false
    readonly property real tileSize: iconTile ? iconSize + 18 : iconSize

    // The name's size: a card title by default, smaller for one-line rows.
    property int namePixelSize: KTheme.titleCard

    // Only a Storage or a Catalog has a source to reach; a group never fades.
    readonly property bool isOffline: (deviceType === "Storage" || deviceType === "Catalog")
                                      && !deviceIsActive

    // "All" has no device of its own, so it borrows the generic folder icon
    // K3 already uses for the all-devices entry elsewhere (SEL-F5).
    readonly property string iconSource: deviceType === "All"     ? "folder"
                                       : deviceType === "Virtual" ? "drive-multidisk"
                                       : deviceType === "Storage" ? "drive-harddisk"
                                       : deviceIsActive ? "media-optical-blu-ray" : "media-optical"

    spacing: iconTile ? 12 : 8

    Rectangle {
        visible: identity.showIcon
        Layout.preferredWidth:  identity.tileSize
        Layout.preferredHeight: identity.tileSize
        Layout.alignment: identity.nameWraps ? Qt.AlignTop : Qt.AlignVCenter
        radius: identity.iconTile ? KTheme.radiusControl : 0
        color: !identity.iconTile ? "transparent"
             : (identity.deviceType === "Storage" || identity.deviceType === "Catalog")
               && identity.deviceIsActive ? KTheme.accentSoft : KTheme.neutralSoft

        Kirigami.Icon {
            anchors.centerIn: parent
            // A coloured device icon: drawn as it is, faded when unreachable
            source: identity.iconSource
            implicitWidth:  identity.iconSize
            implicitHeight: identity.iconSize
            opacity: identity.isOffline && identity.iconTile ? 0.5 : 1.0
        }
    }

    Controls.Label {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        text: identity.deviceName
        wrapMode:  identity.nameWraps ? Text.Wrap : Text.NoWrap
        elide:     identity.nameWraps ? Text.ElideNone : Text.ElideRight
        maximumLineCount: identity.nameWraps ? Number.MAX_VALUE : 1
        font.pixelSize: identity.namePixelSize
        font.weight: identity.nameWeight
        font.italic: identity.nameItalic
        opacity:     identity.nameOpacity
        color:       identity.nameColor
        Accessible.role: Accessible.Heading
    }
}
