import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.Fusion as F

// A drop-down list of the design system: framed like the text field, its
// list on a rounded surface with the menu's entries
F.ComboBox {
    id: control

    padding: 6
    leftPadding: 10 + (!control.mirrored || !indicator || !indicator.visible ? 0 : indicator.width + spacing)
    rightPadding: 6 + (control.mirrored || !indicator || !indicator.visible ? 0 : indicator.width + spacing)

    delegate: MenuItem {
        required property var model
        required property int index
        width: ListView.view.width
        text: model[control.textRole]
        font.weight: control.currentIndex === index ? Font.DemiBold : Font.Normal
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 32
        visible: !control.flat || control.down || control.hovered
        radius: 10
        color: control.editable ? control.palette.base
             : control.down ? control.palette.mid
             : control.hovered ? control.palette.midlight : control.palette.button
        border.width: control.visualFocus || control.activeFocus ? 2 : 1
        border.color: control.visualFocus || control.activeFocus ? control.palette.highlight : control.palette.mid
        opacity: control.enabled ? 1 : 0.6
    }

    popup: T.Popup {
        width: control.width
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                         control.Window.height - topMargin - bottomMargin)
        topMargin: 6
        bottomMargin: 6
        padding: 4
        font: control.font
        palette: control.palette

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightRangeMode: ListView.ApplyRange
            highlightMoveDuration: 0
            T.ScrollBar.vertical: F.ScrollBar { }
        }

        background: Rectangle {
            radius: 10
            color: control.palette.base
            border.width: 1
            border.color: control.palette.mid
        }
    }
}
