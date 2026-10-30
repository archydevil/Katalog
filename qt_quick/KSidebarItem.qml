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

    // Reached with Tab, and with the arrow keys once Main.qml links the
    // entries (KeyNavigation); a click does not take the focus from a field
    focusPolicy: Qt.TabFocus
    // Return and Enter open the section, as Space does (WCAG 2.1.1)
    Keys.onReturnPressed: item.clicked()
    Keys.onEnterPressed: item.clicked()
    // Up and Down walk the sidebar, as in a list (D37)
    Keys.onDownPressed: nextItemInFocusChain(true).forceActiveFocus(Qt.TabFocusReason)
    Keys.onUpPressed:   nextItemInFocusChain(false).forceActiveFocus(Qt.BacktabFocusReason)

    // Read as a tab of the sidebar that says which section is open
    Accessible.role: Accessible.PageTab
    Accessible.name: title
    Accessible.description: description
    Accessible.selectable: true
    Accessible.selected: active
    // A page tab is not pressed by the assistive technologies the way a
    // button is: VoiceOver's press (AXPress) did nothing until this
    Accessible.onPressAction: item.clicked()

    background: Rectangle {
        radius: KTheme.radiusItem
        color: item.active ? KTheme.accentSoft : item.hovered ? KTheme.hover : "transparent"

        // The open section is told by a bar and a heavier name as well as by
        // the tint, never by colour alone (WCAG 1.4.1)
        Rectangle {
            visible: item.active
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: 20
            radius: 1.5
            color: KTheme.accent
        }

        // Keyboard focus: a ring outside the entry (WCAG 2.4.7)
        Rectangle {
            anchors.fill: parent
            anchors.margins: -KTheme.focusOffset
            radius: parent.radius + KTheme.focusOffset
            color: "transparent"
            border.width: KTheme.focusWidth
            border.color: KTheme.accent
            visible: item.visualFocus
        }
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
            Accessible.ignored: true   // the title names the section
        }
        // Name and description wrap rather than being cut, at any text size
        // (WCAG 1.4.4, CDT-C1)
        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true
            Controls.Label {
                Layout.fillWidth: true
                text: item.title
                wrapMode: Text.Wrap
                font.weight: item.active ? (item.description !== "" ? Font.Bold : Font.DemiBold)
                                         : (item.description !== "" ? Font.DemiBold : Font.Normal)
                color: item.active ? KTheme.accentText : item.description !== "" ? KTheme.text : KTheme.text2
                Accessible.ignored: true   // read as the entry's own name
            }
            Controls.Label {
                Layout.fillWidth: true
                visible: item.description !== ""
                text: item.description
                wrapMode: Text.Wrap
                font.pixelSize: KTheme.note
                color: KTheme.text3
                Accessible.ignored: true   // read as the entry's description
            }
        }
    }
}
