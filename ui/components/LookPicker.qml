import QtQuick
import QtQuick.Layouts
import VoiceStudio

// The voices to pick from: the built-in looks, then the voices the user saved. `big` shows the voice changer's large
// tiles (with delete on saved voices); otherwise compact pills (Home).
Item {
    id: root

    property int wideColumns: 4
    property bool wide: true
    property bool big: true
    // Segoe MDL2 glyphs per built-in voice; saved voices use a person icon.
    readonly property var icons: ({ natural: "", strongThick: "", deep: "", warm: "",
                                    radio: "", clear: "", lighter: "", veryDeep: "" })
    readonly property var chips: {
        const out = []
        for (const v of backend.voiceLooks)
            out.push({ key: v.key, label: v.label })
        return out
    }

    Layout.fillWidth: true
    implicitHeight: big ? grid.implicitHeight : chipRow.implicitHeight

    GridLayout {
        id: grid
        visible: root.big
        anchors.left: parent.left
        anchors.right: parent.right
        columns: root.wide ? root.wideColumns : 2
        columnSpacing: 10
        rowSpacing: 10
        uniformCellWidths: true

        Repeater {
            model: root.big ? backend.voiceLooks : []
            VoiceTile {
                required property var modelData
                Layout.fillWidth: true
                Layout.fillHeight: true
                icon: modelData.custom ? ""
                      : root.icons[modelData.key] !== undefined ? root.icons[modelData.key] : ""
                title: modelData.label
                description: modelData.description
                selected: backend.voiceLook === modelData.key
                deletable: modelData.custom === true
                onClicked: backend.applyVoiceLook(modelData.key)
                onDeleteClicked: backend.deleteVoiceLook(modelData.key)
            }
        }
    }

    ChipRow {
        id: chipRow
        visible: !root.big
        anchors.left: parent.left
        anchors.right: parent.right
        model: root.chips
        current: backend.voiceLook
        onPicked: (key) => backend.applyVoiceLook(key)
    }
}
