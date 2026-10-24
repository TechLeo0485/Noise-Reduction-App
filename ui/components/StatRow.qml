import QtQuick
import QtQuick.Layouts
import VoiceStudio

RowLayout {
    id: root

    property string label
    property string value
    property color valueColor: Theme.text
    property bool monospace: false

    Layout.fillWidth: true
    spacing: 12

    Text {
        text: root.label
        color: Theme.textSecondary
        font.pixelSize: 13
        Layout.fillWidth: true
        elide: Text.ElideRight
    }
    Text {
        text: root.value
        color: root.valueColor
        font.pixelSize: 13
        font.family: root.monospace ? "Consolas" : Theme.fontFamily
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideMiddle
        // Fixed cap (not derived from the row width, which would feed back into the layout).
        Layout.maximumWidth: 360
    }
}
