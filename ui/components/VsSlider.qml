import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

// A labelled slider. The value shows in a small pill (signed for +/- ranges); double-click the slider to
// return to `defaultValue`.
ColumnLayout {
    id: root

    property string label
    property string hint                 // optional one-line explanation under the label
    property string unit
    property real from: 0
    property real to: 1
    property real stepSize: 0.1
    property real value: 0
    property int decimals: 1
    property real defaultValue: from < 0 && to > 0 ? 0 : from
    signal moved(real value)

    readonly property bool bipolar: from < 0 && to > 0

    spacing: 6
    Layout.fillWidth: true
    opacity: enabled ? 1.0 : 0.45

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                text: root.label
                color: Theme.text
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                visible: root.hint !== ""
                text: root.hint
                color: Theme.textSecondary
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        Rectangle {
            Layout.alignment: Qt.AlignTop
            implicitWidth: Math.max(56, valueText.implicitWidth + 16)
            implicitHeight: 22
            radius: 6
            color: Theme.surfaceRaised
            Text {
                id: valueText
                anchors.centerIn: parent
                text: root.bipolar ? Theme.signed(slider.value, root.decimals, root.unit)
                                   : slider.value.toFixed(root.decimals) + (root.unit !== "" ? " " + root.unit : "")
                color: Math.abs(slider.value - root.defaultValue) < 1e-6 ? Theme.textSecondary : Theme.text
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
            }
        }
    }

    Slider {
        id: slider
        Layout.fillWidth: true
        implicitHeight: 22
        from: root.from
        to: root.to
        stepSize: root.stepSize
        snapMode: Slider.SnapAlways
        value: root.value
        onMoved: root.moved(value)

        readonly property real zeroPosition: root.bipolar ? (0 - root.from) / (root.to - root.from) : 0

        TapHandler {
            acceptedButtons: Qt.LeftButton
            onDoubleTapped: {
                slider.value = root.defaultValue
                root.moved(root.defaultValue)
            }
        }

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 4
            radius: 2
            color: Theme.surfaceHover

            // Filled from the centre for +/- ranges, from the left otherwise.
            Rectangle {
                x: Math.min(slider.zeroPosition, slider.visualPosition) * parent.width
                width: Math.abs(slider.visualPosition - slider.zeroPosition) * parent.width
                height: parent.height
                radius: 2
                color: Theme.accent
            }
            Rectangle {
                visible: root.bipolar
                x: slider.zeroPosition * parent.width - 1
                y: -3
                width: 2
                height: 10
                radius: 1
                color: Theme.borderStrong
            }
        }

        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 18
            height: 18
            radius: 9
            color: "#FFFFFF"
            border.color: slider.pressed || slider.hovered ? Theme.accentHover : Theme.accent
            border.width: slider.pressed ? 5 : 4
            Behavior on border.width { NumberAnimation { duration: 80 } }
        }
    }
}
