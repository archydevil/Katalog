import QtQuick
import QtQuick.Controls.Fusion as F

// A text area of the design system, framed like the text field
F.TextArea {
    id: control

    padding: 8
    leftPadding: 10
    rightPadding: 10

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 40
        radius: 10
        color: control.palette.base
        // Unfocused, the edge is the controls' line, 3:1 on every surface
        // (palette.dark, set from KTheme.controlLine; WCAG 1.4.11)
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? control.palette.highlight : control.palette.dark
        opacity: control.enabled ? 1 : 0.6
    }
}
