import QtQuick
import QtQuick.Layouts
import VoiceStudio

Rectangle {
    id: root

    property string icon
    property string text
    property bool active: false
    signal clicked()

    Layout.fillWidth: true
    implicitHeight: 42
    radius: 10
    color: active ? Theme.accentMuted : (mouse.containsMouse ? Theme.surfaceHover : "transparent")

    Rectangle {
        visible: root.active
        width: 3
        height: 18
        radius: 2
        color: Theme.accent
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
    }

    Row {
        anchors.verticalCenter: parent.verticalCenter
        x: 16
        spacing: 12
        Text {
            text: root.icon
            font.family: Theme.iconFont
            font.pixelSize: 16
            color: root.active ? Theme.accent : Theme.textSecondary
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.text
            font.pixelSize: 14
            font.weight: root.active ? Font.DemiBold : Font.Normal
            color: root.active ? Theme.text : Theme.textSecondary
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
