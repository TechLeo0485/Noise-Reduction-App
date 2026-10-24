import QtQuick
import QtQuick.Layouts
import VoiceStudio

ColumnLayout {
    id: root

    property string label
    property real levelDb: -120
    readonly property real fraction: Theme.dbToFraction(levelDb)

    Layout.fillWidth: true
    spacing: 6

    RowLayout {
        Layout.fillWidth: true
        Text {
            text: root.label
            color: Theme.textSecondary
            font.pixelSize: 12
            Layout.fillWidth: true
        }
        Text {
            text: root.levelDb <= -119 ? "-inf dBFS" : root.levelDb.toFixed(1) + " dBFS"
            color: root.levelDb > -1 ? Theme.danger : Theme.text
            font.pixelSize: 12
            font.family: "Consolas"
        }
    }

    Rectangle {
        id: track
        Layout.fillWidth: true
        implicitHeight: 10
        radius: 5
        color: Theme.surfaceRaised

        Item {
            width: track.width * root.fraction
            height: parent.height
            clip: true
            Behavior on width { NumberAnimation { duration: 50 } }

            Rectangle {
                width: track.width
                height: track.height
                radius: 5
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Theme.accent }
                    GradientStop { position: 0.75; color: Theme.accent }   // -15 dBFS
                    GradientStop { position: 0.9; color: Theme.warning }   // -6 dBFS
                    GradientStop { position: 1.0; color: Theme.danger }
                }
            }
        }

        // Tick marks at -40, -20, -6 dBFS
        Repeater {
            model: [-40, -20, -6]
            Rectangle {
                required property var modelData
                x: track.width * Theme.dbToFraction(modelData)
                width: 1
                height: track.height
                color: Theme.background
                opacity: 0.6
            }
        }
    }
}
