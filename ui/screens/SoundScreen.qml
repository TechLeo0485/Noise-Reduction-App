import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Simple sound controls in plain language. Detailed controls live on the advanced screens.
ScreenPage {
    id: page

    TwoColumns {
        wide: page.wide

        leftContent: [
            Card {
                title: "Noise removal"
                subtitle: "Cleans up background noise around you: fans, keyboards, traffic, hum"
                icon: ""
                NoiseLevelPicker {}
            },

            Card {
                title: "Tone"
                subtitle: "Small changes go a long way. Leave at 0 if unsure."
                icon: ""
                VsSlider {
                    label: "Voice volume"
                    unit: "dB"
                    from: -24
                    to: 12
                    stepSize: 0.5
                    value: backend.params.voiceVolumeDb !== undefined ? backend.params.voiceVolumeDb : 0
                    onMoved: (v) => backend.setParam("voiceVolumeDb", v)
                }
                VsSlider {
                    label: "Deeper voice (bass)"
                    unit: "dB"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: backend.params.bassDb !== undefined ? backend.params.bassDb : 0
                    onMoved: (v) => backend.setParam("bassDb", v)
                }
                VsSlider {
                    label: "Clearer speech (presence)"
                    unit: "dB"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: backend.params.clarityDb !== undefined ? backend.params.clarityDb : 0
                    onMoved: (v) => backend.setParam("clarityDb", v)
                }
                VsSwitch {
                    label: "Even out loud and quiet speech"
                    description: "Keeps your volume steady so you are never too quiet or too loud"
                    checked: backend.params.compressorEnabled === true
                    onToggled: (value) => backend.setParam("compressorEnabled", value)
                }
                VsButton {
                    text: "Reset tone"
                    onClicked: {
                        backend.setParam("voiceVolumeDb", 0)
                        backend.setParam("bassDb", 0)
                        backend.setParam("clarityDb", 0)
                    }
                }
            }
        ]

        rightContent: [
            Card {
                title: "Voice style"
                subtitle: "Pick the situation you are in. You can still adjust everything afterwards."
                icon: ""
                Repeater {
                    model: backend.presets
                    ModeTile {
                        required property var modelData
                        title: modelData.name
                        description: modelData.description
                        badge: modelData.builtIn ? "" : "CUSTOM"
                        selected: modelData.name === backend.activePreset && !backend.presetModified
                        onClicked: backend.applyPreset(modelData.name)
                    }
                }
            }
        ]
    }

    Text {
        visible: !backend.advancedUi
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.textSecondary
        font.pixelSize: 12
        text: "Want full control (equalizer bands, gate timing, pitch, compressor details)? Turn on Advanced controls in Settings."
    }
}
