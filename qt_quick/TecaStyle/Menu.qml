import QtQuick
import QtQuick.Controls.Fusion as F

// A menu of the design system: a rounded surface with a line
F.Menu {
    id: control

    padding: 4
    delegate: MenuItem {}

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 28
        radius: 10
        color: control.palette.base
        border.width: 1
        border.color: control.palette.mid
    }
}
