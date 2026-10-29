import QtQuick
import QtQuick.Controls as Controls

// Info tag of the design system (fork, D34): a pill naming a state, a type or
// an AI result. Tones: neutral, info, ok, warn, bad, ai. The text always says
// what the colour means.
Rectangle {
    id: tag
    property string text
    property string tone: "neutral"

    color: KTheme.toneSoft(tone)
    radius: height / 2
    implicitWidth: label.implicitWidth + 18
    implicitHeight: label.implicitHeight + 6

    Controls.Label {
        id: label
        anchors.centerIn: parent
        text: tag.text
        color: KTheme.toneText(tag.tone)
        font.pixelSize: KTheme.note
        font.weight: Font.Medium
    }
}
