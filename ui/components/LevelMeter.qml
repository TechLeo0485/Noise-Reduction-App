import QtQuick
import QtQuick.Layouts
import VoiceStudio

// A segmented level meter (-60..0 dBFS) with a plain-language reading ("Good", "Too loud") and the level.
ColumnLayout {
    id: root

    property string label
    property real levelDb: -120
    property bool showWord: true
    readonly property real fraction: Theme.dbToFraction(levelDb)
    readonly property int segments: 32

    Layout.fillWidth: true
    spacing: 7

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text {
            text: root.label
            color: Theme.text
            font.pixelSize: Theme.fontBody
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Text {
            visible: root.showWord
            text: Theme.levelWord(root.levelDb)
            color: Theme.levelColor(root.levelDb)
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
        }
        Text {
            text: root.levelDb <= -119 ? "−∞ dB" : Math.round(root.levelDb) + " dB"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontCaption
            font.family: Theme.monoFamily
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: 46
        }
    }

    Row {
        id: bar
        Layout.fillWidth: true
        Layout.preferredHeight: 10
        spacing: 3
        readonly property real segmentWidth: (width - (root.segments - 1) * spacing) / root.segments

        Repeater {
            model: root.segments
            Rectangle {
                required property int index
                readonly property real position: (index + 0.5) / root.segments
                readonly property bool lit: position <= root.fraction
                width: bar.segmentWidth
                height: bar.height
                radius: 2
                // Green up to -12 dBFS, amber to -3 dBFS, red above.
                color: !lit ? Theme.surfaceHover
                       : position > 0.95 ? Theme.danger
                       : position > 0.8 ? Theme.warning
                       : Theme.success
                opacity: lit ? 1.0 : 0.9
            }
        }
    }
}
