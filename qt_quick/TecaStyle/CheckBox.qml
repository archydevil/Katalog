import QtQuick
import QtQuick.Controls.impl
import QtQuick.Controls.Fusion as F

// A check box of the design system: a rounded box filled with the accent
// when checked
F.CheckBox {
    id: control

    spacing: 8

    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 5
        readonly property bool on: control.checkState !== Qt.Unchecked
        color: on ? control.palette.highlight : control.palette.base
        border.width: control.visualFocus ? 2 : 1
        border.color: on || control.visualFocus ? control.palette.highlight : control.palette.mid
        opacity: control.enabled ? 1 : 0.5

        ColorImage {
            anchors.centerIn: parent
            color: control.palette.highlightedText
            source: "qrc:/qt-project.org/imports/QtQuick/Controls/Fusion/images/checkmark.png"
            visible: control.checkState === Qt.Checked
        }
        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 2
            radius: 1
            color: control.palette.highlightedText
            visible: control.checkState === Qt.PartiallyChecked
        }
    }
}
