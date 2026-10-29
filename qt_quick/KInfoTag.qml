import QtQuick
import QtQuick.Controls as Controls

// Info tag of the design system (fork, D34): a pill naming a state, a type or
// an AI result. Tones: neutral, info, ok, warn, bad, ai. The text always says
// what the colour means; a long text wraps inside the space it has (CDT-C1).
Rectangle {
    id: tag
    property string text
    property string tone: "neutral"
    // At most the width of the row it sits in
    property real maximumWidth: parent ? parent.width : 100000

    color: KTheme.toneSoft(tone)
    radius: Math.min(height / 2, 12)
    implicitWidth: Math.min(label.implicitWidth + 18, maximumWidth)
    implicitHeight: label.implicitHeight + 6
    width: implicitWidth
    height: implicitHeight

    Controls.Label {
        id: label
        x: 9
        y: 3
        width: tag.width - 18
        text: tag.text
        color: KTheme.toneText(tag.tone)
        font.pixelSize: KTheme.note
        font.weight: Font.Medium
        wrapMode: Text.Wrap
    }
}
