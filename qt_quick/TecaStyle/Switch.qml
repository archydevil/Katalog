import QtQuick
import QtQuick.Controls.Fusion as F

// A switch of the design system: a pill that fills with the accent when on
F.Switch {
    id: control

    spacing: 8

    indicator: Rectangle {
        implicitWidth: 36
        implicitHeight: 20
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? control.palette.highlight : control.palette.midlight
        border.width: control.visualFocus ? 2 : 1
        border.color: control.checked || control.visualFocus ? control.palette.highlight : control.palette.mid
        opacity: control.enabled ? 1 : 0.5

        Rectangle {
            width: 14
            height: 14
            radius: 7
            y: 3
            x: control.visualPosition * (parent.width - width - 6) + 3
            color: control.checked ? control.palette.highlightedText : control.palette.base
            border.width: control.checked ? 0 : 1
            border.color: control.palette.mid
            Behavior on x { enabled: !control.down; NumberAnimation { duration: 120 } }
        }
    }
}
