import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

ScreenPage {
    id: page

    readonly property var effectNames: ({ "eq": "Equalizer", "compressor": "Compressor", "pitch": "Pitch shift" })
    readonly property bool compOn: backend.params.compressorEnabled === true

    TwoColumns {
        wide: page.wide

        leftContent: [
            Card {
                title: "Equalizer"
                subtitle: "Shape the tone of your voice. Bands at 0 dB cost no CPU."
                icon: ""
                ParamSlider { paramKey: "bassDb" }
                ParamSlider { paramKey: "warmthDb" }
                ParamSlider { paramKey: "midDb" }
                ParamSlider { paramKey: "clarityDb" }
                ParamSlider { paramKey: "trebleDb" }
                VsButton {
                    text: "Flatten EQ"
                    onClicked: {
                        for (const key of ["bassDb", "warmthDb", "midDb", "clarityDb", "trebleDb"])
                            backend.setParam(key, 0)
                    }
                }
            },

            Card {
                title: "Pitch"
                subtitle: "Optional effect. Adds about 12 ms of delay and some warble on large shifts, so it is off by default."
                icon: ""
                ParamSwitch { paramKey: "pitchEnabled" }
                ParamSlider { paramKey: "pitchSemitones"; enabled: backend.params.pitchEnabled === true }
            },

            Card {
                title: "Effect order"
                subtitle: "Order of the effects after noise suppression and the gate"
                icon: ""
                Repeater {
                    model: backend.effectOrder
                    RowLayout {
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: 8
                        Rectangle {
                            width: 24
                            height: 24
                            radius: 12
                            color: Theme.accentMuted
                            Text {
                                anchors.centerIn: parent
                                text: index + 1
                                color: Theme.accent
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                        Text {
                            text: page.effectNames[modelData]
                            color: Theme.text
                            font.pixelSize: 13
                            Layout.fillWidth: true
                        }
                        VsButton { text: "↑"; enabled: index > 0; onClicked: backend.moveEffect(index, -1) }
                        VsButton { text: "↓"; enabled: index < backend.effectOrder.length - 1; onClicked: backend.moveEffect(index, 1) }
                    }
                }
            }
        ]

        rightContent: [
            Card {
                title: "Volume and dynamics"
                icon: ""
                ParamSlider { paramKey: "voiceVolumeDb" }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                ParamSwitch { paramKey: "compressorEnabled"; description: "Evens out loud and quiet speech" }
                ParamSlider { paramKey: "compThresholdDb"; enabled: page.compOn }
                ParamSlider { paramKey: "compRatio"; enabled: page.compOn }
                ParamSlider { paramKey: "compAttackMs"; enabled: page.compOn }
                ParamSlider { paramKey: "compReleaseMs"; enabled: page.compOn }
                ParamSlider { paramKey: "compMakeupDb"; enabled: page.compOn }
                StatRow { label: "Compressor gain reduction"; value: backend.compressorReductionDb.toFixed(1) + " dB"; monospace: true }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                ParamSwitch { paramKey: "limiterEnabled"; description: "Zero-latency peak limiter. A hard clip at 0 dBFS always stays active." }
                ParamSlider { paramKey: "limiterCeilingDb"; enabled: backend.params.limiterEnabled === true }
            }
        ]
    }

    Card {
        title: "Presets"
        subtitle: backend.presetModified ? "Current: " + backend.activePreset + " (modified)" : "Current: " + backend.activePreset
        icon: ""

        Flow {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: backend.presets
                Rectangle {
                    required property var modelData
                    readonly property bool active: modelData.name === backend.activePreset
                    width: presetLabel.implicitWidth + 28
                    height: 34
                    radius: 17
                    color: active ? Theme.accentMuted : (presetMouse.containsMouse ? Theme.surfaceHover : Theme.surfaceRaised)
                    border.color: active ? Theme.accent : Theme.border
                    Text {
                        id: presetLabel
                        anchors.centerIn: parent
                        text: modelData.name + (modelData.builtIn ? "" : "  *")
                        color: Theme.text
                        font.pixelSize: 13
                    }
                    MouseArea {
                        id: presetMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.applyPreset(modelData.name)
                    }
                    ToolTip.visible: presetMouse.containsMouse
                    ToolTip.text: modelData.description
                    ToolTip.delay: 500
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: presetName
                Layout.fillWidth: true
                implicitHeight: 36
                placeholderText: "Name for a custom preset"
                color: Theme.text
                placeholderTextColor: Theme.textDisabled
                font.pixelSize: 13
                maximumLength: 64
                leftPadding: 12
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surfaceRaised
                    border.color: presetName.activeFocus ? Theme.accent : Theme.border
                }
            }
            VsButton {
                text: "Save preset"
                primary: true
                enabled: presetName.text.trim().length > 0
                onClicked: {
                    backend.saveCustomPreset(presetName.text)
                    presetName.text = ""
                }
            }
            VsButton {
                text: "Delete current"
                danger: true
                enabled: {
                    const list = backend.presets
                    for (let i = 0; i < list.length; ++i)
                        if (list[i].name === backend.activePreset) return !list[i].builtIn
                    return false
                }
                onClicked: backend.deleteCustomPreset(backend.activePreset)
            }
        }
    }
}
