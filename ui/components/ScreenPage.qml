import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

// Scrollable page container. Content is laid out in a column that tracks the page width.
ScrollView {
    id: page

    default property alias content: column.data
    readonly property bool wide: availableWidth > 860

    leftPadding: 32
    rightPadding: 32
    topPadding: 18
    bottomPadding: 32
    clip: true
    contentWidth: availableWidth

    ScrollBar.vertical: ScrollBar {
        id: vbar
        parent: page
        x: page.width - width - 4
        y: page.topPadding
        height: page.availableHeight
        active: page.ScrollBar.horizontal.active
        policy: ScrollBar.AsNeeded
        contentItem: Rectangle {
            implicitWidth: 6
            radius: 3
            color: Theme.borderStrong
            opacity: vbar.pressed ? 1.0 : (vbar.active ? 0.9 : 0.0)
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }
    }

    ColumnLayout {
        id: column
        width: page.availableWidth
        spacing: Theme.spacing
    }
}
