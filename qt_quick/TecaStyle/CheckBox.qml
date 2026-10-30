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
        // The empty box's edge is the controls' line, 3:1 on every surface
        // (palette.dark, set from KTheme.controlLine; WCAG 1.4.11)
        border.width: 1
        border.color: on ? control.palette.highlight : control.palette.dark
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

        // Keyboard focus: a ring outside the box, so it shows on the accent
        // fill of a checked box too (WCAG 2.4.7)
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: 2
            border.color: control.palette.highlight
            visible: control.visualFocus
        }
    }

    // Fusion's label, dimmed when disabled: the window palette gives every
    // colour group the same text colour
    contentItem: Text {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
        text: control.text
        font: control.font
        color: control.palette.windowText
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        opacity: control.enabled ? 1 : 0.5
    }
}
