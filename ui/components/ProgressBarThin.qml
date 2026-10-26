import QtQuick
import QtQuick.Layouts
import VoiceStudio

Rectangle {
    id: root

    property real value: 0      // 0..1
    property color barColor: Theme.accent

    Layout.fillWidth: true
    implicitHeight: 6
    radius: 3
    color: Theme.surfaceHover

    Rectangle {
        width: parent.width * Math.max(0, Math.min(1, root.value))
        height: parent.height
        radius: 3
        color: root.barColor
        Behavior on width { NumberAnimation { duration: 60 } }
    }
}
