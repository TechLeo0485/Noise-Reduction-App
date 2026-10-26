import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

// A switch with a label and optional description; the whole row is clickable.
RowLayout {
    id: root

    property string label
    property string description
    property bool checked: false
    signal toggled(bool checked)

    Layout.fillWidth: true
    spacing: 14
    opacity: enabled ? 1.0 : 0.45

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Text {
            text: root.label
            color: Theme.text
            font.pixelSize: Theme.fontBody
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Text {
            visible: root.description !== ""
            text: root.description
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        TapHandler {
            enabled: root.enabled
            onTapped: root.toggled(!root.checked)
        }
        HoverHandler { cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    }

    Switch {
        id: control
        checked: root.checked
        onToggled: {
            root.toggled(checked)
            // Clicking breaks the binding; restore it so the switch always shows the backend's
            // real state, even when the backend declines or adjusts the change.
            checked = Qt.binding(() => root.checked)
        }
        padding: 0
        // Basic-style Switch excludes the indicator from its implicit size, which made layouts
        // give it zero width and clip it at the card edge. Reserve the indicator's space.
        implicitWidth: 40
        implicitHeight: 22
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: 40

        indicator: Rectangle {
            implicitWidth: 40
            implicitHeight: 22
            x: control.leftPadding
            y: control.height / 2 - height / 2
            radius: 11
            color: control.checked ? Theme.accent : Theme.surfaceHover
            border.color: control.checked ? Theme.accent : Theme.borderStrong
            Behavior on color { ColorAnimation { duration: 120 } }

            Rectangle {
                x: control.checked ? parent.width - width - 3 : 3
                y: 3
                width: 16
                height: 16
                radius: 8
                color: control.checked ? Theme.textOnAccent : Theme.textSecondary
                Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            }
        }
        contentItem: Item {}
    }
}
