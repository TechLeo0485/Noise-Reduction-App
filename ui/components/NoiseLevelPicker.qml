import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Four plain-language noise-removal levels instead of individual suppression/gate controls.
ColumnLayout {
    id: root

    readonly property var levels: [
        { title: "Off", hint: "Your microphone exactly as it is." },
        { title: "Light", hint: "Removes steady hum and fan noise. Most natural sound." },
        { title: "Balanced", hint: "Removes fans, keyboards and clicks. Recommended for calls." },
        { title: "Strong", hint: "Maximum clean-up for noisy rooms. Quiet words may be cut slightly." }
    ]
    readonly property int current: backend.noiseLevel

    Layout.fillWidth: true
    spacing: 10

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 40
        radius: Theme.radiusSmall
        color: Theme.background
        border.color: Theme.border

        RowLayout {
            anchors.fill: parent
            anchors.margins: 3
            spacing: 3
            Repeater {
                model: root.levels
                Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool selected: root.current === index
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    radius: 8
                    color: selected ? Theme.accent : (mouse.containsMouse ? Theme.surfaceRaised : "transparent")
                    Behavior on color { ColorAnimation { duration: 90 } }
                    Text {
                        anchors.centerIn: parent
                        text: modelData.title
                        color: parent.selected ? Theme.textOnAccent : Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                        font.weight: parent.selected ? Font.DemiBold : Font.Normal
                    }
                    MouseArea {
                        id: mouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.setNoiseLevel(index)
                    }
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.fontSmall
        color: Theme.textSecondary
        text: root.current >= 0 ? root.levels[root.current].hint
                                : "Custom settings (fine-tuned or set by a situation). Pick a level to go back to a standard setting."
    }
}
