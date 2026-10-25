import QtQuick
import QtQuick.Layouts
import VoiceStudio

// A row of pill buttons for choosing one item (voice looks, situation presets).
// `model` items need `key` and `label`; `current` is the selected key ("" = none, e.g. custom).
Flow {
    id: root

    property var model: []
    property string current
    signal picked(string key)

    Layout.fillWidth: true
    spacing: 8

    Repeater {
        model: root.model
        Rectangle {
            required property var modelData
            readonly property bool active: modelData.key === root.current
            width: chipLabel.implicitWidth + 28
            height: 34
            radius: 17
            color: active ? Theme.accent : (chipMouse.containsMouse ? Theme.surfaceHover : Theme.surfaceRaised)
            border.color: active ? Theme.accent : Theme.border
            Text {
                id: chipLabel
                anchors.centerIn: parent
                text: modelData.label
                color: parent.active ? Theme.background : Theme.text
                font.pixelSize: 13
                font.weight: parent.active ? Font.DemiBold : Font.Normal
            }
            MouseArea {
                id: chipMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.picked(modelData.key)
            }
        }
    }
}
