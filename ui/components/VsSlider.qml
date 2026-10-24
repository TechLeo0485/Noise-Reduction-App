import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

ColumnLayout {
    id: root

    property string label
    property string unit
    property real from: 0
    property real to: 1
    property real stepSize: 0.1
    property real value: 0
    property int decimals: 1
    signal moved(real value)

    spacing: 4
    Layout.fillWidth: true
    opacity: enabled ? 1.0 : 0.45

    RowLayout {
        Layout.fillWidth: true
        Text {
            text: root.label
            color: Theme.textSecondary
            font.pixelSize: 13
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Text {
            text: slider.value.toFixed(root.decimals) + (root.unit !== "" ? " " + root.unit : "")
            color: Theme.text
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
    }

    Slider {
        id: slider
        Layout.fillWidth: true
        from: root.from
        to: root.to
        stepSize: root.stepSize
        snapMode: Slider.SnapAlways
        value: root.value
        onMoved: root.moved(value)

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 4
            radius: 2
            color: Theme.surfaceRaised

            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: Theme.accent
            }
        }

        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 16
            height: 16
            radius: 8
            color: slider.pressed ? Theme.accentHover : Theme.text
            border.color: Theme.accent
            border.width: 2
        }
    }
}
