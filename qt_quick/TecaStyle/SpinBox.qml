import QtQuick
import QtQuick.Controls.impl
import QtQuick.Controls.Fusion as F

// A number field of the design system: framed like the text field, with the
// step arrows on a quiet strip
F.SpinBox {
    id: control

    padding: 6

    up.indicator: Rectangle {
        x: control.mirrored ? 1 : control.width - width - 1
        y: 1
        width: 20
        height: control.height / 2 - 1
        implicitWidth: 20
        implicitHeight: 12
        radius: 8
        color: control.up.pressed ? control.palette.mid
             : control.up.hovered ? control.palette.midlight : "transparent"
        ColorImage {
            anchors.centerIn: parent
            scale: -1
            opacity: control.up.indicator.enabled ? 1.0 : 0.5
            color: control.palette.buttonText
            source: "qrc:/qt-project.org/imports/QtQuick/Controls/Fusion/images/arrow.png"
        }
    }
    down.indicator: Rectangle {
        x: control.mirrored ? 1 : control.width - width - 1
        y: control.height - height - 1
        width: 20
        height: control.height / 2 - 1
        implicitWidth: 20
        implicitHeight: 12
        radius: 8
        color: control.down.pressed ? control.palette.mid
             : control.down.hovered ? control.palette.midlight : "transparent"
        ColorImage {
            anchors.centerIn: parent
            opacity: control.down.indicator.enabled ? 1.0 : 0.5
            color: control.palette.buttonText
            source: "qrc:/qt-project.org/imports/QtQuick/Controls/Fusion/images/arrow.png"
        }
    }

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
