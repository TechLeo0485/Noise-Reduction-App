import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Cards side by side in rows of equal height (two per row when the window is wide, one when narrow).
// Children should set Layout.fillHeight: true so a row's cards line up top and bottom.
GridLayout {
    property bool wide: true
    property int wideColumns: 2

    Layout.fillWidth: true
    columns: wide ? wideColumns : 1
    columnSpacing: Theme.spacing
    rowSpacing: Theme.spacing
    uniformCellWidths: true
}
