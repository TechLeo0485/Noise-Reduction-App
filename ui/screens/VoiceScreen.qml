import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Fine-tuning how you sound: voice character (formant-preserving depth, body, presence) and tone (EQ,
// volume, evening out). The ready-made looks are picked on Home. Expert dynamics sit in "Fine-tune".
ScreenPage {
    id: page

    readonly property var p: backend.params
    readonly property bool characterOn: p.pitchEnabled === true
    readonly property var effectNames: ({ "eq": "Tone (equalizer)", "compressor": "Evening out (compressor)", "pitch": "Voice character" })

    function num(key, fallback) {
        return p[key] !== undefined ? p[key] : fallback
    }
    // Moving a character slider switches the character on; all three at zero switches it off (no delay added).
    function setCharacter(key, value) {
        backend.setParam(key, value)
        const depth = key === "pitchSemitones" ? value : num("pitchSemitones", 0)
        const body = key === "pitchBodySt" ? value : num("pitchBodySt", 0)
        const presence = key === "pitchPresenceDb" ? value : num("pitchPresenceDb", 0)
        backend.setParam("pitchEnabled", Math.abs(depth) >= 0.01 || Math.abs(body) >= 0.01 || presence >= 0.05)
    }
    function signed(v, unit) {
        return (v > 0 ? "+" : "") + v.toFixed(1) + " " + unit
    }
    readonly property string lookName: {
        const list = backend.voiceLooks
        for (let i = 0; i < list.length; ++i)
            if (list[i].key === backend.voiceLook) return list[i].label
        return "Custom"
    }

    TwoColumns {
        wide: page.wide

        leftContent: [
            Card {
                title: "Voice character"
                subtitle: "Deeper, stronger, thicker - still you, not a robot"
                icon: ""

                StatRow { label: "Look (choose on Home)"; value: page.lookName }
                VsSlider {
                    label: "Depth - lower or higher pitch"
                    unit: "st"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: page.num("pitchSemitones", 0)
                    onMoved: (v) => page.setCharacter("pitchSemitones", v)
                }
                VsSlider {
                    label: "Body - thicker, bigger voice (left: lighter)"
                    unit: "st"
                    from: -6
                    to: 6
                    stepSize: 0.5
                    value: page.num("pitchBodySt", 0)
                    onMoved: (v) => page.setCharacter("pitchBodySt", v)
                }
                VsSlider {
                    label: "Presence - clear and forward"
                    unit: "dB"
                    from: 0
                    to: 9
                    stepSize: 0.5
                    value: page.num("pitchPresenceDb", 0)
                    onMoved: (v) => page.setCharacter("pitchPresenceDb", v)
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    text: page.characterOn
                          ? "On: depth " + page.signed(page.num("pitchSemitones", 0), "st") + ", body "
                            + page.signed(page.num("pitchBodySt", 0), "st") + ". Adds about 35 ms of delay. "
                            + "For strong and thick, go 1 to 2 steps deeper and add body. Bass is held back "
                            + "automatically, so a deeper voice does not get boomy."
                          : "Off: your voice is unchanged and no delay is added."
                }
            }
        ]

        rightContent: [
            Card {
                title: "Tone"
                subtitle: "Small changes go a long way; 0 means unchanged"
                icon: ""
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
                    label: "Bass - low end (lower if boomy)"
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
                    label: "Clarity - easier to understand"
                    unit: "dB"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: page.num("clarityDb", 0)
                    onMoved: (v) => backend.setParam("clarityDb", v)
                }
                VsSlider {
                    label: "Brightness - airy top end (lower if harsh)"
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
                VsButton {
                    text: "Reset tone"
                    onClicked: {
                        for (const key of ["voiceVolumeDb", "bassDb", "warmthDb", "midDb", "clarityDb", "trebleDb"])
                            backend.setParam(key, 0)
                    }
                }

                Expander {
                    title: "Fine-tune"
                    hint: "Compressor timing, limiter and effect order"

                    ParamSlider { paramKey: "compThresholdDb"; enabled: page.p.compressorEnabled === true }
                    ParamSlider { paramKey: "compRatio"; enabled: page.p.compressorEnabled === true }
                    ParamSlider { paramKey: "compAttackMs"; enabled: page.p.compressorEnabled === true }
                    ParamSlider { paramKey: "compReleaseMs"; enabled: page.p.compressorEnabled === true }
                    ParamSlider { paramKey: "compMakeupDb"; enabled: page.p.compressorEnabled === true }
                    StatRow { label: "Compressor gain reduction now"; value: backend.compressorReductionDb.toFixed(1) + " dB"; monospace: true }
                    ParamSwitch { paramKey: "limiterEnabled"; description: "Catches peaks before they distort. A hard clip at 0 dBFS always stays active." }
                    ParamSlider { paramKey: "limiterCeilingDb"; enabled: page.p.limiterEnabled === true }

                    Text { text: "Effect order"; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                    Repeater {
                        model: backend.effectOrder
                        RowLayout {
                            required property string modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: (index + 1) + ".  " + page.effectNames[modelData]
                                color: Theme.text
                                font.pixelSize: 13
                                Layout.fillWidth: true
                            }
                            VsButton { text: "↑"; enabled: index > 0; onClicked: backend.moveEffect(index, -1) }
                            VsButton { text: "↓"; enabled: index < backend.effectOrder.length - 1; onClicked: backend.moveEffect(index, 1) }
                        }
                    }
                }
            }
        ]
    }
}
