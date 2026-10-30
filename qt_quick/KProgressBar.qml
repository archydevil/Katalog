import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Progress bar of the design system (fork, D34): a rounded track and its fill;
// value between 0 and 1, or indeterminate. Built on the control's progress
// bar for what it tells assistive technology — the progress bar role, its
// value and range — with the design system's look (WCAG 4.1.2).
Controls.ProgressBar {
    id: bar

    from: 0
    to: 1
    padding: 0
    implicitHeight: 6

    // The indeterminate sweep runs five seconds and then rests, centred, and
    // never runs when the desktop turns animations off (WCAG 2.2.2). It runs
    // again when the bar is shown again or turns indeterminate again.
    property bool _sweepDone: false
    readonly property bool _sweeping: indeterminate && visible && !_sweepDone
                                      && Kirigami.Units.longDuration > 0
    property real _sweepX: 0
    Timer {
        interval: 5000
        running: bar.indeterminate && bar.visible
        onRunningChanged: if (running) bar._sweepDone = false
        onTriggered: bar._sweepDone = true
    }

    background: Rectangle {
        implicitHeight: 6
        radius: height / 2
        color: KTheme.neutralSoft
    }

    contentItem: Item {
        clip: true
        Rectangle {
            id: fill
            height: parent.height
            radius: height / 2
            color: KTheme.accent
            width: bar.indeterminate ? parent.width * 0.3 : parent.width * bar.position
            x: !bar.indeterminate ? 0 : sweep.running ? bar._sweepX : (parent.width - width) / 2
        }
        NumberAnimation {
            id: sweep
            target: bar
            property: "_sweepX"
            running: bar._sweeping
            from: -bar.width * 0.3
            to: bar.width
            duration: 1400
            loops: Animation.Infinite
        }
    }
}
