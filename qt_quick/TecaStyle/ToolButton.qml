import QtQuick
import QtQuick.Controls.impl
import QtQuick.Controls.Fusion as F

// A tool button of the design system: flat, with a rounded tint on hover
F.ToolButton {
    id: control

    // Fusion's label, dimmed when disabled: the window palette gives every
    // colour group the same text colour, so an unavailable action would
    // otherwise read as available
    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: control.palette.buttonText
        opacity: control.enabled ? 1 : 0.5
    }

    background: Rectangle {
        implicitWidth: 24
        implicitHeight: 24
        visible: control.down || control.checked || control.highlighted || control.visualFocus
                 || (control.enabled && control.hovered)
        radius: 8
        color: control.down || control.checked || control.highlighted ? control.palette.mid
                                                                     : control.palette.midlight
        // Keyboard focus: the accent line, on a tint that is never the accent
        border.width: control.visualFocus ? 2 : 0
        border.color: control.palette.highlight
    }
}
