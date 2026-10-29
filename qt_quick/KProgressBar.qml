import QtQuick

// Progress bar of the design system (fork, D34): a rounded track and its fill;
// value between 0 and 1, or indeterminate.
Rectangle {
    id: bar
    property real value: 0
    property bool indeterminate: false

    implicitHeight: 6
    radius: height / 2
    color: KTheme.neutralSoft
    clip: true

    Rectangle {
        id: fill
        height: parent.height
        radius: parent.radius
        color: KTheme.accent
        width: bar.indeterminate ? parent.width * 0.3 : parent.width * Math.max(0, Math.min(1, bar.value))
        NumberAnimation on x {
            running: bar.indeterminate && bar.visible
            from: -bar.width * 0.3
            to: bar.width
            duration: 1400
            loops: Animation.Infinite
        }
    }
}
