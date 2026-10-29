import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls

// Key number of the design system (fork, D34): what is counted, the value, and
// an optional tag saying what it covers.
KCard {
    id: metric
    property string label
    property string value
    property string tagText
    property string tagTone: "neutral"

    padding: 16
    spacing: 6
    Controls.Label {
        text: metric.label
        color: KTheme.text3
        font.pixelSize: KTheme.note
        font.weight: Font.Medium
    }
    Controls.Label {
        text: metric.value
        color: KTheme.text
        font.pixelSize: 26
        font.weight: Font.Bold
    }
    KInfoTag {
        visible: metric.tagText !== ""
        text: metric.tagText
        tone: metric.tagTone
    }
}
