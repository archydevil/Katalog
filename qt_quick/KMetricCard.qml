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
    // Both wrap inside the card rather than run past it when the text is
    // enlarged or the value is long (WCAG 1.4.4, 1.4.10)
    Controls.Label {
        Layout.fillWidth: true
        text: metric.label
        wrapMode: Text.Wrap
        color: KTheme.text3
        font.pixelSize: KTheme.note
        font.weight: Font.Medium
    }
    Controls.Label {
        Layout.fillWidth: true
        text: metric.value
        wrapMode: Text.Wrap
        color: KTheme.text
        font.pixelSize: KTheme.metric
        font.weight: Font.Bold
    }
    KInfoTag {
        visible: metric.tagText !== ""
        text: metric.tagText
        tone: metric.tagTone
    }
}
