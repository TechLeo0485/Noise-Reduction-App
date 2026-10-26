import QtQuick
import QtQuick.Layouts
import VoiceStudio

// The ready-made voice looks as selectable tiles (name + one-line description).
GridLayout {
    id: root

    property int wideColumns: 3
    property bool wide: true
    // Segoe MDL2 glyphs per look key.
    readonly property var icons: ({ natural: "\uE77B", strongThick: "\uE945", deep: "\uE9E9", warm: "\uE706", radio: "\uE7F5" })

    Layout.fillWidth: true
    columns: wide ? wideColumns : 2
    columnSpacing: 10
    rowSpacing: 10
    uniformCellWidths: true

    Repeater {
        model: backend.voiceLooks
        ModeTile {
            required property var modelData
            Layout.fillWidth: true
            Layout.fillHeight: true
            icon: root.icons[modelData.key] !== undefined ? root.icons[modelData.key] : "\uE720"
            title: modelData.label
            description: modelData.description
            selected: backend.voiceLook === modelData.key
            onClicked: backend.applyVoiceLook(modelData.key)
        }
    }
}
