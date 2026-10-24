import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Two independent stacks of cards ("masonry"). Unlike a grid, a tall card in one column does
// not leave a gap in the other. Collapses to a single column when the window is narrow.
GridLayout {
    id: root

    property bool wide: true
    property alias leftContent: leftColumn.data
    property alias rightContent: rightColumn.data
    Layout.fillWidth: true
    columns: wide ? 2 : 1
    columnSpacing: Theme.spacing
    rowSpacing: Theme.spacing

    ColumnLayout {
        id: leftColumn
        Layout.preferredWidth: 1 // equal tiny preferred widths + fillWidth = equal columns, no width feedback loop
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        spacing: Theme.spacing
    }

    ColumnLayout {
        id: rightColumn
        Layout.preferredWidth: 1 // equal tiny preferred widths + fillWidth = equal columns, no width feedback loop
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        spacing: Theme.spacing
    }
}
