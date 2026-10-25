import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

// Everything that removes sound you do not want: background noise (one level picker, details in
// "Fine-tune"), other people's voices (strict isolation) and ready-made settings per situation.
ScreenPage {
    id: page

    signal openPage(string key)

    readonly property var p: backend.params
    readonly property int mode: p.suppressionMode !== undefined ? p.suppressionMode : 1
    readonly property bool suppressionOn: p.suppressionEnabled === true
    readonly property bool gateOn: p.gateEnabled === true
    readonly property var presetChips: {
        const list = backend.presets
        const chips = []
        for (let i = 0; i < list.length; ++i)
            chips.push({ key: list[i].name, label: list[i].name + (list[i].builtIn ? "" : "  *") })
        return chips
    }

    TwoColumns {
        wide: page.wide

        leftContent: [
            Card {
                title: "Background noise"
                subtitle: "Fans, keyboards, traffic, hum"
                icon: ""
                NoiseLevelPicker {}

                Expander {
                    title: "Fine-tune"
                    hint: "Engine, strength, rumble filter and noise gate"

                    Text { text: "Engine"; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 10
                        ModeTile {
                            Layout.fillHeight: true
                            Layout.preferredWidth: 1
                            title: "Simple"
                            badge: "LOWEST CPU"
                            description: "Removes steady noise between words. No added delay."
                            selected: page.mode === 0
                            onClicked: backend.setParam("suppressionMode", 0)
                        }
                        ModeTile {
                            Layout.fillHeight: true
                            Layout.preferredWidth: 1
                            title: "AI (RNNoise)"
                            badge: "RECOMMENDED"
                            available: backend.rnnoiseAvailable
                            description: "Also removes noise while you speak. Adds 10 ms of delay."
                            selected: page.mode === 1
                            onClicked: backend.setParam("suppressionMode", 1)
                        }
                    }
                    ParamSwitch {
                        paramKey: "suppressionEnabled"
                        description: "Off passes your voice through unprocessed (the gate and voice settings still apply)"
                    }
                    ParamSlider { paramKey: "suppressionStrength"; enabled: page.suppressionOn }
                    ParamSwitch { paramKey: "highPassEnabled"; description: "Removes rumble, desk bumps and low hum below the cutoff" }
                    ParamSlider { paramKey: "highPassHz"; enabled: page.p.highPassEnabled === true }

                    Text { text: "Noise gate"; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                    ParamSwitch { paramKey: "gateEnabled"; description: "Turns down the sound between words; opens instantly when you speak" }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Gate attenuation now"; color: Theme.textSecondary; font.pixelSize: 12; Layout.fillWidth: true }
                        Text {
                            text: backend.gateReductionDb.toFixed(1) + " dB"
                            color: backend.gateReductionDb < -3 ? Theme.warning : Theme.success
                            font.pixelSize: 12
                            font.family: "Consolas"
                        }
                    }
                    ParamSlider { paramKey: "gateThresholdDb"; enabled: page.gateOn }
                    ParamSlider { paramKey: "gateRangeDb"; enabled: page.gateOn }
                    ParamSlider { paramKey: "gateRatio"; enabled: page.gateOn }
                    ParamSlider { paramKey: "gateAttackMs"; enabled: page.gateOn }
                    ParamSlider { paramKey: "gateHoldMs"; enabled: page.gateOn }
                    ParamSlider { paramKey: "gateReleaseMs"; enabled: page.gateOn }
                    ParamSwitch {
                        paramKey: "gateUseVad"
                        description: "Keeps the gate open whenever the AI hears speech, even soft speech"
                        enabled: page.gateOn
                    }
                }
            }
        ]

        rightContent: [
            VoiceIsolationCard {
                onRecordRequested: page.openPage("myvoice")
            },

            Card {
                title: "Situation"
                subtitle: "Ready-made noise and tone settings. Your voice character is kept."
                icon: ""
                ChipRow {
                    model: page.presetChips
                    current: backend.presetModified ? "" : backend.activePreset
                    onPicked: (key) => backend.applyPreset(key)
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    text: {
                        const list = backend.presets
                        for (let i = 0; i < list.length; ++i)
                            if (list[i].name === backend.activePreset)
                                return (backend.presetModified ? "Based on " + list[i].name + " (adjusted). " : "") + list[i].description
                        return ""
                    }
                }

                Expander {
                    title: "Save your own"
                    hint: "Keep the current settings under a name"
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        TextField {
                            id: presetName
                            Layout.fillWidth: true
                            implicitHeight: 36
                            placeholderText: "Name, e.g. Office"
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
                            text: "Save"
                            primary: true
                            enabled: presetName.text.trim().length > 0
                            onClicked: {
                                backend.saveCustomPreset(presetName.text)
                                presetName.text = ""
                            }
                        }
                    }
                    VsButton {
                        text: "Delete \"" + backend.activePreset + "\""
                        danger: true
                        visible: {
                            const list = backend.presets
                            for (let i = 0; i < list.length; ++i)
                                if (list[i].name === backend.activePreset) return !list[i].builtIn
                            return false
                        }
                        onClicked: backend.deleteCustomPreset(backend.activePreset)
                    }
                }
            }
        ]
    }
}
