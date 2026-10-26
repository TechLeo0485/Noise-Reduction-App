import QtQuick
import QtQuick.Layouts
import VoiceStudio

// A collapsible section ("Fine-tune", "Technical details"): a clickable header line that shows or hides
// its content. Collapsed by default so everyday screens stay simple.
ColumnLayout {
    id: root

    property string title: "Fine-tune"
    property string hint
    property bool expanded: false
    property bool divider: true
    default property alias content: body.data

    Layout.fillWidth: true
    spacing: 12

    Rectangle {
        visible: root.divider
        Layout.fillWidth: true
        implicitHeight: 1
        color: Theme.border
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: header.implicitHeight + 14
        radius: 8
        color: headerMouse.containsMouse ? Theme.surfaceRaised : "transparent"
        Layout.leftMargin: -8
        Layout.rightMargin: -8

        RowLayout {
            id: header
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 10
            Text {
                text: root.title
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
            }
            Text {
                text: root.hint
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                text: "\uE70D" // ChevronDown
                rotation: root.expanded ? 180 : 0
                font.family: Theme.iconFont
                font.pixelSize: 11
                color: Theme.textSecondary
                Behavior on rotation { NumberAnimation { duration: 120 } }
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
        spacing: 14
    }
}
