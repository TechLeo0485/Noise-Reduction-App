import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

// The voice changer: pick a voice, adjust it with three sliders, save it under your own name, hear the result.
// Tone, stabilizer and dynamics are below, collapsed under "More controls".
ScreenPage {
    id: page

    readonly property var p: backend.params
    readonly property bool characterOn: p.pitchEnabled === true
    readonly property var effectNames: ({ "eq": "Tone (equalizer)", "compressor": "Evening out (compressor)", "pitch": "Voice character" })
    readonly property string currentVoiceLabel: {
        for (const v of backend.voiceLooks)
            if (v.key === backend.voiceLook) return v.label
        return "Custom"
    }
    readonly property bool currentIsSaved: backend.voiceLook.startsWith("custom:")

    function num(key, fallback) {
        return p[key] !== undefined ? p[key] : fallback
    }
    // Moving a voice slider switches the character on; all three at zero switches it off (no delay added).
    function setCharacter(key, value) {
        backend.setParam(key, value)
        const depth = key === "pitchSemitones" ? value : num("pitchSemitones", 0)
        const body = key === "pitchBodySt" ? value : num("pitchBodySt", 0)
        const presence = key === "pitchPresenceDb" ? value : num("pitchPresenceDb", 0)
        backend.setParam("pitchEnabled", Math.abs(depth) >= 0.01 || Math.abs(body) >= 0.01 || presence >= 0.05)
    }
    function hotkeyText(action) {
        for (const item of hotkeys.items)
            if (item.action === action) return item.text
        return ""
    }

    // ---- Voice changer -----------------------------------------------------------------------------
    Card {
        title: "Voice changer"
        subtitle: "Click a voice to use it right away. Still you - natural-sounding, not robotic."
        icon: ""
        actions: RowLayout {
            spacing: 10
            StatusPill {
                text: page.characterOn ? page.currentVoiceLabel + "  +35 ms" : "Natural"
                tone: page.characterOn ? Theme.accent : Theme.textSecondary
            }
            VsSwitch {
                label: "Voice effects"
                checked: page.characterOn
                onToggled: backend.toggleVoiceEffects()
            }
        }

        LookPicker { wide: page.wide; wideColumns: 4; big: true }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            textFormat: Text.StyledText
            readonly property string next: page.hotkeyText("nextVoice")
            readonly property string onOff: page.hotkeyText("voiceOnOff")
            visible: next !== "" || onOff !== ""
            text: "Shortcuts, also during a call: "
                  + (next !== "" ? "<b>" + next + "</b> next voice" : "")
                  + (next !== "" && onOff !== "" ? "  ·  " : "")
                  + (onOff !== "" ? "<b>" + onOff + "</b> voice effects off / on" : "")
                  + ". Change them in Setup."
        }
    }

    // ---- Adjust + hear ------------------------------------------------------------------------------
    GridLayout {
        Layout.fillWidth: true
        columns: page.wide ? 2 : 1
        columnSpacing: Theme.spacing
        rowSpacing: Theme.spacing
        uniformCellWidths: true

        Card {
            Layout.fillHeight: true
            title: "Adjust this voice"
            subtitle: "Fine-tune, then save it as your own"
            icon: ""
            actions: LinkText {
                text: "Reset to natural"
                visible: page.characterOn
                onClicked: backend.applyVoiceLook("natural")
            }

            VsSlider {
                label: "Pitch"
                hint: "Lower or higher - your own tone is kept"
                unit: "st"
                from: -12
                to: 12
                stepSize: 0.5
                value: page.num("pitchSemitones", 0)
                onMoved: (v) => page.setCharacter("pitchSemitones", v)
            }
            VsSlider {
                label: "Thickness"
                hint: "Right: fuller, bigger voice. Left: lighter"
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

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

            Text {
                text: "Save as my voice"
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: voiceName
                    Layout.fillWidth: true
                    implicitHeight: 36
                    placeholderText: page.characterOn ? "Name, e.g. Interview voice" : "Change the voice first"
                    enabled: page.characterOn
                    color: Theme.text
                    placeholderTextColor: Theme.textDisabled
                    font.pixelSize: Theme.fontBody
                    maximumLength: 32
                    leftPadding: 12
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.background
                        border.color: voiceName.activeFocus ? Theme.accent : Theme.borderStrong
                    }
                    onAccepted: saveButton.clicked()
                }
                VsButton {
                    id: saveButton
                    text: "Save"
                    primary: true
                    enabled: page.characterOn && voiceName.text.trim().length > 0
                    onClicked: {
                        if (backend.saveVoiceLook(voiceName.text))
                            voiceName.text = ""
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                text: page.currentIsSaved
                      ? "This is your saved voice \"" + page.currentVoiceLabel + "\". Saving under the same name updates it; "
                        + "hover its tile to delete it."
                      : "Saved voices appear after the built-in ones and work with the next-voice shortcut."
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Theme.spacing

            HearYourselfCard {}

            Card {
                title: "Steady voice at any distance"
                subtitle: "Same level and tone whether you lean in or sit back"
                icon: ""
                VsSwitch {
                    label: "Voice stabilizer"
                    description: "Evens out your level and the extra bass of a close microphone before the voice "
                                 + "effects, so the changed voice sounds the same at any distance. Adds no delay."
                    checked: page.p.stabilizerEnabled === true
                    onToggled: (value) => backend.setParam("stabilizerEnabled", value)
                }
                StabilizerReadout { visible: page.p.stabilizerEnabled === true && backend.engineRunning }
            }
        }
    }

    // ---- More controls ------------------------------------------------------------------------------
    Card {
        title: "More controls"
        subtitle: "Tone, evening out loud and quiet speech, limiter and the order of the effects"
        icon: ""
        iconColor: Theme.textSecondary

        Expander {
            title: "Tone"
            hint: "Volume, bass, warmth, mid, clarity and brightness"
            divider: false
            LinkText {
                text: "Reset tone"
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

        Expander {
            title: "Even out loud and quiet speech"
            hint: page.p.compressorEnabled === true
                  ? (backend.engineRunning ? "On, turning down " + Theme.signed(backend.compressorReductionDb, 1, "dB") + " now" : "On")
                  : "Off"
            ParamSwitch { paramKey: "compressorEnabled"; description: "Turns down loud words and lifts quiet ones." }
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
                    VsButton { iconText: ""; implicitWidth: 40; enabled: index > 0; onClicked: backend.moveEffect(index, -1) }
                    VsButton { iconText: ""; implicitWidth: 40; enabled: index < backend.effectOrder.length - 1; onClicked: backend.moveEffect(index, 1) }
                }
            }
        }
    }
}
