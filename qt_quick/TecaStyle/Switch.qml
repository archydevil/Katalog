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
        // Off: a quiet track whose edge, and the knob's, is the controls'
        // line, 3:1 on every surface (palette.dark, set from
        // KTheme.controlLine; WCAG 1.4.11)
        color: control.checked ? control.palette.highlight : control.palette.alternateBase
        border.width: 1
        border.color: control.checked ? control.palette.highlight : control.palette.dark
        opacity: control.enabled ? 1 : 0.5

        Rectangle {
            width: 14
            height: 14
            radius: 7
            y: 3
            x: control.visualPosition * (parent.width - width - 6) + 3
            color: control.checked ? control.palette.highlightedText : control.palette.base
            border.width: control.checked ? 0 : 1
            border.color: control.palette.dark
            Behavior on x { enabled: !control.down; NumberAnimation { duration: 120 } }
        }

        // Keyboard focus: a ring outside the track, so it shows on the accent
        // fill of a switch that is on (WCAG 2.4.7)
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
        color: control.palette.text
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        opacity: control.enabled ? 1 : 0.5
    }
}
