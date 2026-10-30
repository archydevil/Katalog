import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.impl
import QtQuick.Controls.Fusion as F

// A button of the design system: rounded, a quiet surface with a line, the
// accent when highlighted or checked
F.Button {
    id: control

    topPadding: 6
    bottomPadding: 6
    leftPadding: display === T.AbstractButton.IconOnly ? 8 : 12
    rightPadding: display === T.AbstractButton.IconOnly ? 8 : 12

    // Fusion's label, with two changes. On the accent fill the text takes the
    // colour the icon already takes there: the plain button text was 2.7:1 on
    // the accent (WCAG 1.4.3). Disabled, it is dimmed with the frame: the
    // window palette gives every colour group the same text colour, so an
    // unavailable action would otherwise read as available.
    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: control.checked || control.highlighted ? control.palette.brightText : control.palette.buttonText
        opacity: control.enabled ? 1 : 0.5
    }

    background: Rectangle {
        implicitWidth: 32
        implicitHeight: 30
        visible: !control.flat || control.down || control.checked || control.highlighted
                 || control.visualFocus || (control.enabled && control.hovered)
        radius: 10
        color: control.highlighted || control.checked
               ? (control.down ? Qt.darker(control.palette.highlight, 1.15) : control.palette.highlight)
               : control.down ? control.palette.mid
               : control.hovered ? control.palette.midlight
               : control.flat ? "transparent" : control.palette.button
        border.width: control.flat || control.highlighted || control.checked ? 0 : 1
        border.color: control.palette.mid
        opacity: control.enabled ? 1 : 0.5

        // Keyboard focus: a ring outside the frame, so it shows on the accent
        // fill of a highlighted or checked button too (WCAG 2.4.7)
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
}
