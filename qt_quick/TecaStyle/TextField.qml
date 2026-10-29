import QtQuick
import QtQuick.Controls.Fusion as F

// A text field of the design system: rounded, with a line that turns to the
// accent while the field has focus
F.TextField {
    id: control

    topPadding: 7
    bottomPadding: 7
    leftPadding: 10
    rightPadding: 10

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 32
        radius: 10
        color: control.palette.base
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? control.palette.highlight : control.palette.mid
        opacity: control.enabled ? 1 : 0.6
    }
}
