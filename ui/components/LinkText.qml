import QtQuick
import VoiceStudio

// A small clickable text link ("More voice settings", "Open Setup").
Text {
    id: root

    signal clicked()

    color: linkMouse.containsMouse ? Theme.accentHover : Theme.accent
    font.pixelSize: 12
    font.underline: linkMouse.containsMouse

    MouseArea {
        id: linkMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
