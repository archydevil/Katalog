import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Button of the design system (fork, D34). Variants: "primary" (one per
// screen), "secondary", "tonal", "link".
Controls.Button {
    id: button
    property string variant: "secondary"

    implicitHeight: 40
    leftPadding: 14
    rightPadding: 14
    hoverEnabled: true
    font.weight: Font.DemiBold

    readonly property color fg: variant === "primary" ? KTheme.onAccent
                              : variant === "tonal" || variant === "link" ? KTheme.accentText : KTheme.text

    background: Rectangle {
        radius: KTheme.radiusItem
        color: button.variant === "primary" ? KTheme.accent
             : button.variant === "tonal" ? KTheme.accentSoft
             : button.variant === "secondary" ? (button.hovered ? KTheme.hover : KTheme.surface)
             : (button.hovered ? KTheme.hover : "transparent")
        border.width: button.variant === "secondary" ? 1 : 0
        border.color: KTheme.line
        opacity: button.enabled ? (button.down ? 0.85 : 1) : 0.5
    }
    contentItem: Row {
        spacing: 6
        Kirigami.Icon {
            visible: button.icon.name !== ""
            source: button.icon.name
            isMask: true
            color: button.fg
            width: 16
            height: 16
            anchors.verticalCenter: parent.verticalCenter
        }
        Controls.Label {
            text: button.text
            color: button.fg
            font: button.font
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
