import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.Fusion as F

// A button of the design system: rounded, a quiet surface with a line, the
// accent when highlighted or checked
F.Button {
    id: control

    topPadding: 6
    bottomPadding: 6
    leftPadding: display === T.AbstractButton.IconOnly ? 8 : 12
    rightPadding: display === T.AbstractButton.IconOnly ? 8 : 12

    background: Rectangle {
        implicitWidth: 32
        implicitHeight: 30
        visible: !control.flat || control.down || control.checked || control.highlighted
                 || control.visualFocus || (control.enabled && control.hovered)
        radius: 10
        color: control.highlighted || control.checked
               ? (control.down ? Qt.darker(control.palette.highlight, 1.15) : control.palette.highlight)
               : control.down ? control.palette.mid
               : control.hovered ? control.palette.midlight
               : control.flat ? "transparent" : control.palette.button
        border.width: control.visualFocus ? 2 : (control.flat || control.highlighted || control.checked ? 0 : 1)
        border.color: control.visualFocus ? control.palette.highlight : control.palette.mid
        opacity: control.enabled ? 1 : 0.5
    }
}
