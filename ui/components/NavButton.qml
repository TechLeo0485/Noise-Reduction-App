import QtQuick
import QtQuick.Layouts
import VoiceStudio

Rectangle {
    id: root

    property string icon
    property string text
    property string badge          // small text on the right, e.g. a warning dot label
    property color badgeColor: Theme.warning
    property bool active: false
    signal clicked()

    Layout.fillWidth: true
    implicitHeight: 40
    radius: Theme.radiusSmall
    color: active ? Theme.surfaceRaised : (mouse.containsMouse ? Qt.rgba(1, 1, 1, 0.03) : "transparent")
    Behavior on color { ColorAnimation { duration: 90 } }

    Rectangle {
        visible: root.active
        width: 3
        height: 18
        radius: 2
        color: Theme.accent
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 12
        spacing: 12
        Text {
            text: root.icon
            font.family: Theme.iconFont
            font.pixelSize: 15
            color: root.active ? Theme.accent : Theme.textSecondary
            Layout.preferredWidth: 18
        }
        Text {
            text: root.text
            font.pixelSize: 14
            font.weight: root.active ? Font.DemiBold : Font.Normal
            color: root.active ? Theme.text : Theme.textSecondary
            Layout.fillWidth: true
        }
        Rectangle {
            visible: root.badge !== ""
            implicitWidth: 8
            implicitHeight: 8
            radius: 4
            color: root.badgeColor
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
