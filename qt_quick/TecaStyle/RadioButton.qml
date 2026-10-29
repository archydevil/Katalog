import QtQuick
import QtQuick.Controls.Fusion as F

// A radio button of the design system: a ring with the accent dot when chosen
F.RadioButton {
    id: control

    spacing: 8

    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: width / 2
        color: control.palette.base
        border.width: control.checked || control.visualFocus ? 2 : 1
        border.color: control.checked || control.visualFocus ? control.palette.highlight : control.palette.mid
        opacity: control.enabled ? 1 : 0.5

        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 8
            radius: 4
            color: control.palette.highlight
            visible: control.checked
        }
    }
}
