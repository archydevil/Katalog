import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Sidebar entry of the design system (fork, D34): icon, a name saying what the
// section does and, when given, one line describing it.
Controls.AbstractButton {
    id: item
    property string title
    property string description
    property string iconName
    property bool active: false

    Layout.fillWidth: true
    hoverEnabled: true
    leftPadding: 10
    rightPadding: 10
    topPadding: 8
    bottomPadding: 8
    Accessible.name: title
    Accessible.description: description

    background: Rectangle {
        radius: KTheme.radiusItem
        color: item.active ? KTheme.accentSoft : item.hovered ? KTheme.hover : "transparent"
    }
    contentItem: RowLayout {
        spacing: 10
        Kirigami.Icon {
            source: item.iconName
            isMask: true
            color: item.active ? KTheme.accentText : item.description !== "" ? KTheme.text : KTheme.text2
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            Layout.alignment: item.description !== "" ? Qt.AlignTop : Qt.AlignVCenter
            Layout.topMargin: item.description !== "" ? 1 : 0
        }
        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true
            Controls.Label {
                Layout.fillWidth: true
                text: item.title
                elide: Text.ElideRight
                font.weight: item.description !== "" ? Font.DemiBold : Font.Normal
                color: item.active ? KTheme.accentText : item.description !== "" ? KTheme.text : KTheme.text2
            }
            Controls.Label {
                Layout.fillWidth: true
                visible: item.description !== ""
                text: item.description
                elide: Text.ElideRight
                font.pixelSize: KTheme.note
                color: KTheme.text3
            }
        }
    }
}
