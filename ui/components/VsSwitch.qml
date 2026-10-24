import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

RowLayout {
    id: root

    property string label
    property string description
    property bool checked: false
    signal toggled(bool checked)

    Layout.fillWidth: true
    spacing: 12
    opacity: enabled ? 1.0 : 0.45

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Text {
            text: root.label
            color: Theme.text
            font.pixelSize: 13
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Text {
            visible: root.description !== ""
            text: root.description
            color: Theme.textSecondary
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    Switch {
        id: control
        checked: root.checked
        onToggled: root.toggled(checked)
        padding: 0
        // Basic-style Switch excludes the indicator from its implicit size, which made layouts
        // give it zero width and clip it at the card edge. Reserve the indicator's space.
        implicitWidth: 42
        implicitHeight: 24
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: 42

        indicator: Rectangle {
            implicitWidth: 42
            implicitHeight: 22
            x: control.leftPadding
            y: control.height / 2 - height / 2
            radius: 11
            color: control.checked ? Theme.accent : Theme.surfaceRaised
            border.color: control.checked ? Theme.accent : Theme.border

            Rectangle {
                x: control.checked ? parent.width - width - 3 : 3
                y: 3
                width: 16
                height: 16
                radius: 8
                color: control.checked ? Theme.background : Theme.textSecondary
                Behavior on x { NumberAnimation { duration: 120 } }
            }
        }
        contentItem: Item {}
    }
}
