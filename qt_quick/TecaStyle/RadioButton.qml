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
        // The empty ring's edge is the controls' line, 3:1 on every surface
        // (palette.dark, set from KTheme.controlLine; WCAG 1.4.11)
        border.width: control.checked ? 2 : 1
        border.color: control.checked ? control.palette.highlight : control.palette.dark
        opacity: control.enabled ? 1 : 0.5

        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 8
            radius: 4
            color: control.palette.highlight
            visible: control.checked
        }

        // Keyboard focus: a ring outside the button, apart from the accent
        // ring of the chosen one (WCAG 2.4.7)
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
