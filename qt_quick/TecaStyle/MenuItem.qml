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
        // Disabled entries are dimmed: the window palette gives every colour
        // group the same text colour
        opacity: control.enabled ? 1 : 0.5
    }

    arrow: ColorImage {
        x: control.mirrored ? control.padding : control.width - width - control.padding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 20
        visible: control.subMenu
        rotation: control.mirrored ? 90 : -90
        color: control.palette.text
        opacity: control.enabled ? 1 : 0.5
        source: "qrc:/qt-project.org/imports/QtQuick/Controls/Fusion/images/arrow.png"
        fillMode: Image.Pad
    }

    // The entry in reach — by the pointer or the arrow keys — takes a tint of
    // the accent and a bar of it at its start edge. The tint alone was too
    // faint to follow from the keyboard; the bar is 4:1 or more on it (WCAG
    // 1.4.11, 2.4.7).
    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 28
        radius: 6
        color: Qt.rgba(control.palette.highlight.r, control.palette.highlight.g,
                       control.palette.highlight.b, control.down ? 0.32 : 0.22)
        visible: control.down || control.highlighted

        Rectangle {
            x: control.mirrored ? parent.width - width : 0
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 4
            anchors.bottomMargin: 4
            width: 3
            radius: 1.5
            color: control.palette.highlight
        }
    }
}
