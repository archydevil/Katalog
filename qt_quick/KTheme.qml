pragma Singleton
import QtQuick

// Katalog design system, "Control Center" (fork, D34): the tokens every
// component reads — cards on a soft ground, one accent, tag tones for state,
// type and AI. Light and dark follow the system colour scheme.
QtObject {
    id: theme
    readonly property bool dark: Qt.styleHints.colorScheme === Qt.ColorScheme.Dark

    // Surfaces and text
    readonly property color ground:      dark ? "#0B0E14" : "#F2F4F7"
    readonly property color sidebar:     dark ? "#10141C" : "#FBFCFD"
    readonly property color surface:     dark ? "#151A23" : "#FFFFFF"
    readonly property color surface2:    dark ? "#1B212C" : "#F9FAFB"
    readonly property color line:        dark ? "#2A3140" : "#E4E7EC"
    // The edge of a control one acts on (field, check box, radio, switch):
    // 3:1 or more on ground, surface, hover and the accent's soft tone (WCAG
    // 1.4.11). `line` stays for card hairlines, which are decoration.
    readonly property color controlLine: dark ? "#6B7590" : "#7D8799"
    readonly property color hover:       dark ? "#1E2430" : "#EEF1F5"
    readonly property color text:        dark ? "#F2F4F7" : "#101828"
    readonly property color text2:       dark ? "#B2BAC8" : "#475467"
    readonly property color text3:       dark ? "#9AA3B3" : "#5D6679"

    // Accent and tones (each soft background has a text colour passing 4.5:1)
    readonly property color accent:      dark ? "#6E8EFF" : "#1D4ED8"
    // Text and marks on the accent. Declared as `onAccent: <value>`, QML read
    // the initialiser as a handler of the `accent` property and left the
    // colour unset (black): black on the light accent is 2.8:1 (WCAG 1.4.3).
    // The value lives under another name and onAccent stays as an alias.
    readonly property color textOnAccent: dark ? "#0B0E14" : "#FFFFFF"
    readonly property alias onAccent:    theme.textOnAccent
    readonly property color accentSoft:  dark ? "#1E2A4D" : "#E8EEFF"
    readonly property color accentText:  dark ? "#9DB2FF" : "#1D4ED8"
    readonly property color ok:          dark ? "#5FD39A" : "#067647"
    readonly property color okSoft:      dark ? "#16302A" : "#E7F6EC"
    readonly property color warn:        dark ? "#F5B963" : "#93370D"
    readonly property color warnSoft:    dark ? "#33281A" : "#FEF3E2"
    readonly property color bad:         dark ? "#FF8A80" : "#B42318"
    readonly property color badSoft:     dark ? "#3A1F1F" : "#FEECEB"
    readonly property color ai:          dark ? "#B9A4FF" : "#5B3CC4"
    readonly property color aiSoft:      dark ? "#2A2347" : "#EFEBFD"
    readonly property color neutralSoft: dark ? "#222936" : "#EEF0F4"

    // File types (always shown with their written label, never colour alone)
    readonly property color typeImage:   "#2E90FA"
    readonly property color typeVideo:   "#7A5AF8"
    readonly property color typeAudio:   "#12B76A"
    readonly property color typeText:    "#F79009"

    // Shape and rhythm
    readonly property int radiusCard:    16
    readonly property int radiusControl: 12
    readonly property int radiusItem:    10
    readonly property int gap:           14
    readonly property int pagePadding:   32
    // Keyboard focus: a ring in the accent, drawn this far outside the control
    // so it shows on an accent fill too (WCAG 2.4.7)
    readonly property int focusWidth:    2
    readonly property int focusOffset:   3

    // Type scale (pixels at 100 %). Multiplied by the text size the user chose,
    // which Main.qml binds to textScale, so the design system's own sizes grow
    // with the rest of the text (WCAG 1.4.4).
    property real textScale: 1.0
    readonly property int titlePage:     Math.round(28 * textScale)
    readonly property int titleSection:  Math.round(18 * textScale)
    readonly property int titleCard:     Math.round(15 * textScale)
    readonly property int body:          Math.round(14 * textScale)
    readonly property int note:          Math.round(12 * textScale)
    readonly property int metric:        Math.round(26 * textScale)    // key numbers
    readonly property int titleSub:      Math.round(16 * textScale)    // card titles in a section header
    readonly property int groupLabel:    Math.round(11 * textScale)    // sidebar group titles, upper case
    readonly property real groupSpacing: 0.6   // their letter spacing
    readonly property string mono:       Qt.platform.os === "osx" ? "Menlo"
                                       : Qt.platform.os === "windows" ? "Consolas" : "monospace"

    function toneText(tone) {
        return tone === "ok" ? ok : tone === "warn" ? warn : tone === "bad" ? bad
             : tone === "ai" ? ai : tone === "info" ? accentText : text2
    }
    function toneSoft(tone) {
        return tone === "ok" ? okSoft : tone === "warn" ? warnSoft : tone === "bad" ? badSoft
             : tone === "ai" ? aiSoft : tone === "info" ? accentSoft : neutralSoft
    }
}
