import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls

// Section header of the design system (fork, D34): a title and the line that
// says what the section is for.
ColumnLayout {
    property string title
    property string description
    property int level: 2     // 1 = page title, 2 = section, 3 = card

    spacing: 2
    Layout.fillWidth: true
    Controls.Label {
        Layout.fillWidth: true
        text: parent.title
        wrapMode: Text.WordWrap
        color: KTheme.text
        font.weight: Font.Bold
        font.pixelSize: parent.level === 1 ? KTheme.titlePage : parent.level === 2 ? KTheme.titleSection : KTheme.titleCard + 1
        Accessible.role: Accessible.Heading
    }
    Controls.Label {
        Layout.fillWidth: true
        visible: parent.description !== ""
        text: parent.description
        wrapMode: Text.WordWrap
        color: KTheme.text2
    }
}
