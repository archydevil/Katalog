import QtQuick
import QtQuick.Layouts

// Card of the design system (fork, D34): a rounded surface with a hairline
// border; its children are laid out in a column.
Rectangle {
    id: card
    default property alias content: column.data
    property int padding: 18
    property alias spacing: column.spacing

    color: KTheme.surface
    border.color: KTheme.line
    border.width: 1
    radius: KTheme.radiusCard
    implicitWidth: column.implicitWidth + 2 * padding
    implicitHeight: column.implicitHeight + 2 * padding

    ColumnLayout {
        id: column
        x: card.padding
        y: card.padding
        width: card.width - 2 * card.padding
        spacing: 12
    }
}
