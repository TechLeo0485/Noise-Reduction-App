import QtQuick
import QtQuick.Layouts
import VoiceStudio

// What the voice stabilizer is correcting right now: the level it adds or removes and the low end it trims
// (close to the microphone) or restores (further away).
RowLayout {
    id: root

    readonly property real gainDb: backend.stabilizerGainDb
    readonly property real toneDb: backend.stabilizerToneDb

    Layout.fillWidth: true
    spacing: 10

    Repeater {
        model: [
            { label: "Level", value: root.gainDb, hint: root.gainDb > 1 ? "you are quieter than usual"
                                                     : root.gainDb < -1 ? "you are louder than usual" : "as usual" },
            { label: "Low end", value: root.toneDb, hint: root.toneDb < -1 ? "close to the mic"
                                                     : root.toneDb > 1 ? "further from the mic" : "as usual" }
        ]
        Rectangle {
            required property var modelData
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            implicitHeight: col.implicitHeight + 18
            radius: Theme.radiusSmall
            color: Theme.surfaceRaised
            ColumnLayout {
                id: col
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: 12
                spacing: 1
                Text {
                    text: modelData.label
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                }
                Text {
                    text: Theme.signed(modelData.value, 1, "dB")
                    color: Theme.text
                    font.pixelSize: Theme.fontHeading
                    font.weight: Font.DemiBold
                    font.family: Theme.monoFamily
                }
                Text {
                    text: modelData.hint
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
    }
}
