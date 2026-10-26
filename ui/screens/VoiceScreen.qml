import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Shaping how you sound: the ready-made looks, voice character (formant-preserving depth, body, presence),
// tone (EQ, volume, evening out) and the voice stabilizer. Expert dynamics sit in "Advanced".
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

    Card {
        title: "Voice look"
        subtitle: "Start from a look, then fine-tune below. Your choice stays when you change the noise settings."
        icon: "\uE9E9"
        LookPicker { wide: page.wide; wideColumns: 5 }
    }

    // Two balanced columns: character and stabilizer on the left, tone and evening out on the right.
    GridLayout {
        Layout.fillWidth: true
        columns: page.wide ? 2 : 1
        columnSpacing: Theme.spacing
        rowSpacing: Theme.spacing
        uniformCellWidths: true

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Theme.spacing

            Card {
                title: "Voice character"
                subtitle: "Deeper, stronger, thicker - still you, not a robot"
                icon: "\uE720"
                actions: StatusPill {
                    text: page.characterOn ? "On  +35 ms" : "Off"
                    tone: page.characterOn ? Theme.accent : Theme.textSecondary
                }

                VsSlider {
                    label: "Depth"
                    hint: "Lower or higher pitch"
                    unit: "st"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: page.num("pitchSemitones", 0)
                    onMoved: (v) => page.setCharacter("pitchSemitones", v)
                }
                VsSlider {
                    label: "Body"
                    hint: "Right: thicker, bigger voice. Left: lighter"
                    unit: "st"
                    from: -6
                    to: 6
                    stepSize: 0.5
                    value: page.num("pitchBodySt", 0)
                    onMoved: (v) => page.setCharacter("pitchBodySt", v)
                }
                VsSlider {
                    label: "Presence"
                    hint: "Clear and forward"
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
                    font.pixelSize: Theme.fontSmall
                    text: page.characterOn
                          ? "For strong and thick, go 1 to 2 steps deeper and add body. Bass is held back automatically, "
                            + "so a deeper voice does not get boomy. Double-click a slider to reset it."
                          : "Off: your voice is unchanged and no delay is added."
                }
            }

            Card {
                title: "Steady voice at any distance"
                subtitle: "Same level and tone whether you lean in or sit back"
                icon: "\uE81D"
                VsSwitch {
                    label: "Voice stabilizer"
                    description: "Brings your speech to a steady level and trims the extra bass you get close to the "
                                 + "microphone (or restores it further away), before the voice effects - so the changed "
                                 + "voice sounds the same at any distance. Adapts only while you talk; adds no delay."
                    checked: page.p.stabilizerEnabled === true
                    onToggled: (value) => backend.setParam("stabilizerEnabled", value)
                }
                StabilizerReadout { visible: page.p.stabilizerEnabled === true && backend.engineRunning }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Theme.spacing

            Card {
                title: "Tone"
                subtitle: "Small changes go a long way; 0 means unchanged"
                icon: "\uE767"
                actions: LinkText {
                    text: "Reset"
                    onClicked: {
                        for (const key of ["voiceVolumeDb", "bassDb", "warmthDb", "midDb", "clarityDb", "trebleDb"])
                            backend.setParam(key, 0)
                    }
                }
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
                    label: "Bass"
                    hint: "Low end - lower if boomy"
                    unit: "dB"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: page.num("bassDb", 0)
                    onMoved: (v) => backend.setParam("bassDb", v)
                }
                VsSlider {
                    label: "Warmth"
                    hint: "Richer, less thin"
                    unit: "dB"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: page.num("warmthDb", 0)
                    onMoved: (v) => backend.setParam("warmthDb", v)
                }
                VsSlider {
                    label: "Mid"
                    hint: "More body - lower if nasal or boxy"
                    unit: "dB"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: page.num("midDb", 0)
                    onMoved: (v) => backend.setParam("midDb", v)
                }
                VsSlider {
                    label: "Clarity"
                    hint: "Easier to understand"
                    unit: "dB"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: page.num("clarityDb", 0)
                    onMoved: (v) => backend.setParam("clarityDb", v)
                }
                VsSlider {
                    label: "Brightness"
                    hint: "Airy top end - lower if harsh"
                    unit: "dB"
                    from: -12
                    to: 12
                    stepSize: 0.5
                    value: page.num("trebleDb", 0)
                    onMoved: (v) => backend.setParam("trebleDb", v)
                }
            }

            Card {
                title: "Even out loud and quiet speech"
                subtitle: "Keeps your volume steady so you are never too quiet or too loud"
                icon: "\uE9D2"
                VsSwitch {
                    label: "Compressor"
                    description: "Turns down loud words and lifts quiet ones."
                    checked: page.p.compressorEnabled === true
                    onToggled: (value) => backend.setParam("compressorEnabled", value)
                }
                RowLayout {
                    visible: page.p.compressorEnabled === true && backend.engineRunning
                    Layout.fillWidth: true
                    Text { text: "Turning down now"; color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; Layout.fillWidth: true }
                    Text {
                        text: Theme.signed(backend.compressorReductionDb, 1, "dB")
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.monoFamily
                    }
                }
            }
        }
    }

    // ---- Advanced ------------------------------------------------------------------------------------
    Card {
        title: "Advanced"
        subtitle: "Compressor timing, limiter and the order of the effects"
        icon: "\uE713"
        iconColor: Theme.textSecondary

        Expander {
            title: "Compressor"
            hint: "Threshold, ratio, timing and makeup gain"
            divider: false
            ParamSlider { paramKey: "compThresholdDb"; enabled: page.p.compressorEnabled === true }
            ParamSlider { paramKey: "compRatio"; enabled: page.p.compressorEnabled === true }
            ParamSlider { paramKey: "compAttackMs"; enabled: page.p.compressorEnabled === true }
            ParamSlider { paramKey: "compReleaseMs"; enabled: page.p.compressorEnabled === true }
            ParamSlider { paramKey: "compMakeupDb"; enabled: page.p.compressorEnabled === true }
        }

        Expander {
            title: "Limiter"
            hint: "Catches peaks before they distort"
            ParamSwitch { paramKey: "limiterEnabled"; description: "A hard clip at 0 dBFS always stays active." }
            ParamSlider { paramKey: "limiterCeilingDb"; enabled: page.p.limiterEnabled === true }
        }

        Expander {
            title: "Effect order"
            hint: Array.from(backend.effectOrder).map((k) => page.effectNames[k].split(" (")[0]).join("  →  ")
            Repeater {
                model: backend.effectOrder
                RowLayout {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 8
                    Rectangle {
                        implicitWidth: 24
                        implicitHeight: 24
                        radius: 12
                        color: Theme.surfaceRaised
                        Text {
                            anchors.centerIn: parent
                            text: index + 1
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                    Text {
                        text: page.effectNames[modelData]
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        Layout.fillWidth: true
                    }
                    VsButton { iconText: "\uE70E"; implicitWidth: 40; enabled: index > 0; onClicked: backend.moveEffect(index, -1) }
                    VsButton { iconText: "\uE70D"; implicitWidth: 40; enabled: index < backend.effectOrder.length - 1; onClicked: backend.moveEffect(index, 1) }
                }
            }
        }
    }
}
