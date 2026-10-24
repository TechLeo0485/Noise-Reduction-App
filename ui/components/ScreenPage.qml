import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

// Scrollable page container. Content is laid out in a column that tracks the page width.
ScrollView {
    id: page

    default property alias content: column.data
    readonly property bool wide: availableWidth > 900

    padding: 28
    clip: true
    contentWidth: availableWidth

    ColumnLayout {
        id: column
        width: page.availableWidth
        spacing: Theme.spacing
    }
}
