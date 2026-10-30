import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Button of the design system (fork, D34). Variants: "primary" (one per
// screen), "secondary", "tonal", "link".
Controls.Button {
    id: button
    property string variant: "secondary"

    // 40 px, or taller when the text size setting enlarges the label (WCAG 1.4.4)
    implicitHeight: Math.max(40, implicitContentHeight + topPadding + bottomPadding)
    leftPadding: 14
    rightPadding: 14
    hoverEnabled: true
    font.weight: Font.DemiBold
    // Disabled, the whole button is dimmed — label and icon with the fill —
    // so an unavailable action never reads as available
    opacity: enabled ? 1 : 0.5

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
        opacity: button.down ? 0.85 : 1

        // Keyboard focus: a ring outside the button, so it shows on the
        // primary's accent fill too (WCAG 2.4.7)
        Rectangle {
            anchors.fill: parent
            anchors.margins: -KTheme.focusOffset
            radius: parent.radius + KTheme.focusOffset
            color: "transparent"
            border.width: KTheme.focusWidth
            border.color: KTheme.accent
            visible: button.visualFocus
        }
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
            Accessible.ignored: true   // the text names the action
        }
        Controls.Label {
            text: button.text
            color: button.fg
            font: button.font
            anchors.verticalCenter: parent.verticalCenter
            Accessible.ignored: true   // read as the button's own name
        }
    }
}
