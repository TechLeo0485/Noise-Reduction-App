import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Choosing the voice-isolation model: one tile per model with a plain name, how it works and when to use it.
Card {
    id: root

    property bool wide: true
    readonly property var iso: backend.isolation

    title: "Isolation model"
    subtitle: "How your voice is separated from other people. All models use the same recording of your voice - "
              + "switch any time."
    icon: ""

    GridLayout {
        Layout.fillWidth: true
        columns: root.wide ? 3 : 1
        columnSpacing: 10
        rowSpacing: 10
        uniformCellWidths: true

        Repeater {
            model: backend.isolationModels
            ModelTile {
                required property var modelData
                Layout.fillHeight: true
                model: modelData
                selected: backend.isolationModel === modelData.id
                onClicked: backend.isolationModel = modelData.id
            }
        }
    }

    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: root.iso.lightFallback === true ? Theme.warning : Theme.textSecondary
        font.pixelSize: Theme.fontSmall
        text: root.iso.lightFallback === true
              ? "This computer is too slow for the selected model right now, so \"Voice check only\" is used instead."
              : root.iso.busy === true
                ? "Switching... your microphone is silenced meanwhile (other voices are never passed through)."
                : "Switching takes a few seconds; your microphone is silenced meanwhile, never passed through unfiltered."
    }
}
