import QtQuick
import QtQuick.Layouts
import VoiceStudio

// A collapsible section inside a card ("Fine-tune", "Technical details"): a clickable header line
// that shows or hides its content. Collapsed by default so everyday screens stay simple.
ColumnLayout {
    id: root

    property string title: "Fine-tune"
    property string hint
    property bool expanded: false
    default property alias content: body.data

    Layout.fillWidth: true
    spacing: 12

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: Theme.border
    }

    Item {
        Layout.fillWidth: true
        implicitHeight: header.implicitHeight

        RowLayout {
            id: header
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 8
            Text {
                text: root.expanded ? "" : ""
                font.family: Theme.iconFont
                font.pixelSize: 11
                color: Theme.accent
            }
            Text {
                text: root.title
                color: headerMouse.containsMouse ? Theme.accentHover : Theme.accent
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                text: root.expanded ? "" : root.hint
                color: Theme.textSecondary
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        MouseArea {
            id: headerMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.expanded = !root.expanded
        }
    }

    ColumnLayout {
        id: body
        visible: root.expanded
        Layout.fillWidth: true
        spacing: 12
    }
}
