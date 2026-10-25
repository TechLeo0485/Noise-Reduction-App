import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Everyday sound controls in plain language: noise removal, voice pitch, tone and style.
// Every control here maps to a real engine parameter; advanced screens expose the rest.
ScreenPage {
    id: page

    readonly property var p: backend.params
    readonly property real pitch: p.pitchSemitones !== undefined ? p.pitchSemitones : 0
    readonly property bool pitchOn: p.pitchEnabled === true && Math.abs(pitch) >= 0.01

    function setPitch(semitones) {
        backend.setParam("pitchSemitones", semitones)
        // The shifter is only switched on when there is something to shift (it adds ~12 ms of delay).
        backend.setParam("pitchEnabled", Math.abs(semitones) >= 0.01)
    }

    function num(key, fallback) {
        return p[key] !== undefined ? p[key] : fallback
    }

    TwoColumns {
        wide: page.wide

        leftContent: [
            Card {
                title: "Noise removal"
                subtitle: "Cleans up background noise around you: fans, keyboards, traffic, hum"
                icon: ""
                NoiseLevelPicker {}

                Text {
                    text: "Fine-tune"
                    color: Theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    Layout.topMargin: 6
                }
                VsSlider {
                    label: "Noise removal strength"
                    unit: "%"
                    from: 0
                    to: 100
                    stepSize: 1
                    decimals: 0
                    enabled: page.p.suppressionEnabled === true
                    value: page.num("suppressionStrength", 100)
                    onMoved: (v) => backend.setParam("suppressionStrength", v)
                }
                VsSlider {
                    label: "Silence threshold (sounds quieter than this are turned down)"
                    unit: "dB"
                    from: -80
                    to: -10
                    stepSize: 1
                    decimals: 0
                    enabled: page.p.gateEnabled === true
                    value: page.num("gateThresholdDb", -50)
                    onMoved: (v) => backend.setParam("gateThresholdDb", v)
                }
                VsSlider {
                    label: "How much to turn down silence"
                    unit: "dB"
                    from: 0
                    to: 80
                    stepSize: 1
                    decimals: 0
                    enabled: page.p.gateEnabled === true
                    value: page.num("gateRangeDb", 30)
                    onMoved: (v) => backend.setParam("gateRangeDb", v)
                }
                VsSwitch {
                    label: "Protect quiet words"
                    description: "Keeps the sound on whenever the AI hears speech, even soft speech"
                    enabled: page.p.gateEnabled === true
                    checked: page.p.gateUseVad === true
                    onToggled: (value) => backend.setParam("gateUseVad", value)
                }
                VsSwitch {
                    label: "Remove low rumble"
                    description: "Cuts desk bumps, traffic rumble and low hum"
                    checked: page.p.highPassEnabled === true
                    onToggled: (value) => backend.setParam("highPassEnabled", value)
                }
                VsSlider {
                    label: "Rumble cut-off"
                    unit: "Hz"
                    from: 20
                    to: 300
                    stepSize: 1
                    decimals: 0
                    enabled: page.p.highPassEnabled === true
                    value: page.num("highPassHz", 80)
                    onMoved: (v) => backend.setParam("highPassHz", v)
                }
            },

            VoiceIsolationCard {},

            Card {
                title: "Voice style"
                subtitle: "Ready-made starting points. You can still adjust everything afterwards."
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

        rightContent: [
            Card {
                title: "Voice pitch"
                subtitle: "Make your voice sound deeper or higher"
                icon: ""

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: page.pitchOn ? (page.pitch > 0 ? "+" : "") + page.pitch.toFixed(1) + " semitones" : "Normal (off)"
                        color: page.pitchOn ? Theme.accent : Theme.text
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                    }
                }

                VsSlider {
                    label: "Lower  <->  Higher"
                    unit: "st"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: page.pitch
                    onMoved: (v) => page.setPitch(v)
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: [
                            { label: "Much deeper", value: -6 },
                            { label: "Deeper", value: -3 },
                            { label: "Normal", value: 0 },
                            { label: "Higher", value: 3 },
                            { label: "Much higher", value: 6 }
                        ]
                        VsButton {
                            required property var modelData
                            text: modelData.label
                            primary: Math.abs(page.pitch - modelData.value) < 0.01 && (modelData.value !== 0 || !page.pitchOn)
                            onClicked: page.setPitch(modelData.value)
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    text: "Pitch change adds about 12 ms of delay and can sound slightly robotic on big shifts. "
                          + "Small changes (up to 3 semitones) sound most natural. Set to Normal to switch it off."
                }
            },

            Card {
                title: "Tone"
                subtitle: "Shape how your voice sounds. Small changes go a long way; 0 means unchanged."
                icon: ""
                VsSlider {
                    label: "Voice volume"
                    unit: "dB"
                    from: -24
                    to: 12
                    stepSize: 0.5
                    value: page.num("voiceVolumeDb", 0)
                    onMoved: (v) => backend.setParam("voiceVolumeDb", v)
                }
                VsSlider {
                    label: "Bass - deeper, fuller voice"
                    unit: "dB"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: page.num("bassDb", 0)
                    onMoved: (v) => backend.setParam("bassDb", v)
                }
                VsSlider {
                    label: "Warmth - richer, less thin"
                    unit: "dB"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: page.num("warmthDb", 0)
                    onMoved: (v) => backend.setParam("warmthDb", v)
                }
                VsSlider {
                    label: "Mid - more body (lower if nasal or boxy)"
                    unit: "dB"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: page.num("midDb", 0)
                    onMoved: (v) => backend.setParam("midDb", v)
                }
                VsSlider {
                    label: "Clarity - clearer, easier to understand"
                    unit: "dB"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: page.num("clarityDb", 0)
                    onMoved: (v) => backend.setParam("clarityDb", v)
                }
                VsSlider {
                    label: "Brightness - airy top end (lower if harsh or hissy)"
                    unit: "dB"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: page.num("trebleDb", 0)
                    onMoved: (v) => backend.setParam("trebleDb", v)
                }
                VsSwitch {
                    label: "Even out loud and quiet speech"
                    description: "Keeps your volume steady so you are never too quiet or too loud"
                    checked: page.p.compressorEnabled === true
                    onToggled: (value) => backend.setParam("compressorEnabled", value)
                }
                VsSlider {
                    label: "Evening strength"
                    unit: ":1"
                    from: 1
                    to: 8
                    stepSize: 0.1
                    enabled: page.p.compressorEnabled === true
                    value: page.num("compRatio", 2.5)
                    onMoved: (v) => backend.setParam("compRatio", v)
                }
                VsButton {
                    text: "Reset tone"
                    onClicked: {
                        for (const key of ["voiceVolumeDb", "bassDb", "warmthDb", "midDb", "clarityDb", "trebleDb"])
                            backend.setParam(key, 0)
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
        text: "Need even more (attack/release timing, limiter, effect order, engine choice)? Turn on Advanced controls in Settings."
    }
}
