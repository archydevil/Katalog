import QtQuick
import org.kde.kirigami as Kirigami

// The "My drives" list (fork, D34, phase R7): the drive cards of
// PageSelectionDelegate.qml, set as the list in Main.qml sets them.
Kirigami.CardsListView {
    id: selectionListView1
    model: appManager1.deviceListModel
    delegate: PageSelectionDelegate {}
    clip: true
    topMargin: Kirigami.Units.smallSpacing
    spacing: Kirigami.Units.largeSpacing
}
