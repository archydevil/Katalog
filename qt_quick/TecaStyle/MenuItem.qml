import QtQuick
import QtQuick.Controls.impl
import QtQuick.Controls.Fusion as F

// A menu entry of the design system: a rounded tint under the entry in
// reach, the text keeps its colour
F.MenuItem {
    id: control

    padding: 8
    leftInset: 4
    rightInset: 4
    leftPadding: 12
    rightPadding: 12

    contentItem: MenuItemIconLabel {
        readonly property real arrowPadding: control.subMenu && control.arrow ? control.arrow.width + control.spacing : 0
        readonly property real indicatorPadding: control.checkable && control.indicator ? control.indicator.width + control.spacing : 0
        leftPadding: !control.mirrored ? indicatorPadding : arrowPadding
        rightPadding: control.mirrored ? indicatorPadding : arrowPadding
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        alignment: Qt.AlignLeft
        icon: control.icon
        text: control.text
        font: control.font
        color: control.palette.text
        menuItem: control
    }

    arrow: ColorImage {
        x: control.mirrored ? control.padding : control.width - width - control.padding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 20
        visible: control.subMenu
        rotation: control.mirrored ? 90 : -90
        color: control.palette.text
        source: "qrc:/qt-project.org/imports/QtQuick/Controls/Fusion/images/arrow.png"
        fillMode: Image.Pad
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 28
        radius: 6
        color: control.down ? control.palette.mid : control.palette.midlight
        visible: control.down || control.highlighted
    }
}
