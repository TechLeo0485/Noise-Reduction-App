import QtQuick
import QtQuick.Layouts
import VoiceStudio

// One global shortcut: what it does, its keys (click to change) and Off / Default links.
// `modelData` is an entry of hotkeys.items.
ColumnLayout {
    id: root

    required property var modelData
    readonly property bool capturing: hotkeys.capturing === modelData.action

    Layout.fillWidth: true
    spacing: 4

    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        Text {
            text: root.modelData.label
            color: Theme.text
            font.pixelSize: Theme.fontBody
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        VsButton {
            text: root.capturing ? "Press keys..." : (root.modelData.text !== "" ? root.modelData.text : "Off")
            primary: root.capturing
            font.family: root.capturing || root.modelData.text === "" ? Theme.fontFamily : Theme.monoFamily
            Layout.preferredWidth: 170
            onClicked: root.capturing ? hotkeys.cancelCapture() : hotkeys.capture(root.modelData.action)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 14
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: root.capturing ? Theme.textSecondary : Theme.warning
            text: root.capturing ? hotkeys.message : root.modelData.error
            visible: text !== ""
        }
        Item { Layout.fillWidth: true; visible: !root.capturing && root.modelData.error === "" }
        LinkText {
            text: "Off"
            visible: !root.capturing && root.modelData.text !== ""
            onClicked: hotkeys.clear(root.modelData.action)
        }
        LinkText {
            text: "Default (" + root.modelData.defaultText + ")"
            visible: !root.capturing && root.modelData.text !== root.modelData.defaultText
            onClicked: hotkeys.restoreDefault(root.modelData.action)
        }
    }
}
