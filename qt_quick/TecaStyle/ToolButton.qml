import QtQuick
import QtQuick.Controls.Fusion as F

// A tool button of the design system: flat, with a rounded tint on hover
F.ToolButton {
    id: control

    background: Rectangle {
        implicitWidth: 24
        implicitHeight: 24
        visible: control.down || control.checked || control.highlighted || control.visualFocus
                 || (control.enabled && control.hovered)
        radius: 8
        color: control.down || control.checked || control.highlighted ? control.palette.mid
                                                                     : control.palette.midlight
        border.width: control.visualFocus ? 2 : 0
        border.color: control.palette.highlight
    }
}
