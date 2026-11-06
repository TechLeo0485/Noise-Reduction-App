import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

// Removing background noise: level and engine, ready-made settings per situation and, collapsed, the filter and
// gate details. Other people's voices have their own page ("Only my voice", IsolationScreen).
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

    CardGrid {
        wide: page.wide

        Card {
            Layout.fillHeight: true
            title: "Background noise"
            subtitle: "Fans, keyboards, traffic, hum"
            icon: "\uE9D9"

            NoiseLevelPicker {}

            Text { text: "How it is removed"; color: Theme.text; font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 10
                rowSpacing: 10
                uniformCellWidths: true
                ModeTile {
                    Layout.fillHeight: true
                    title: "Simple"
                    badge: "LOWEST CPU"
                    description: "Removes steady noise between words. No added delay."
                    selected: page.mode === 0
                    onClicked: backend.setParam("suppressionMode", 0)
                }
                ModeTile {
                    Layout.fillHeight: true
                    title: "AI (RNNoise)"
                    badge: "RECOMMENDED"
                    available: backend.rnnoiseAvailable
                    description: "Also removes noise while you speak. Adds 10 ms of delay."
                    selected: page.mode === 1
                    onClicked: backend.setParam("suppressionMode", 1)
                }
            }
        }

        Card {
            Layout.fillHeight: true
            title: "Situation"
            subtitle: "Ready-made noise and tone settings. Your voice look is kept."
            icon: "\uE80F"
            ChipRow {
                model: page.presetChips
                current: backend.presetModified ? "" : backend.activePreset
                onPicked: (key) => backend.applyPreset(key)
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
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
                        font.pixelSize: Theme.fontBody
                        maximumLength: 64
                        leftPadding: 12
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: Theme.background
                            border.color: presetName.activeFocus ? Theme.accent : Theme.borderStrong
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

    }

    // ---- Details -------------------------------------------------------------------------------------
    Card {
        title: "Advanced noise settings"
        subtitle: "Most people never need these - the level above sets them for you"
        icon: "\uE713"
        iconColor: Theme.textSecondary

        Expander {
            title: "Noise removal"
            hint: page.suppressionOn ? "On, strength " + Math.round(page.p.suppressionStrength) + " %" : "Off"
            divider: false
            ParamSwitch {
                paramKey: "suppressionEnabled"
                description: "Off passes your voice through unprocessed (the gate and voice settings still apply)"
            }
            ParamSlider { paramKey: "suppressionStrength"; enabled: page.suppressionOn }
        }

        Expander {
            title: "Clicks and taps (experimental)"
            hint: page.p.clickRemovalEnabled === true ? "On, adds 8 ms of delay" : "Off"
            ParamSwitch {
                paramKey: "clickRemovalEnabled"
                description: "Turns down mouse clicks, keys and taps between your words. Experimental: it catches about "
                             + "half of them and can slightly soften some consonants, so it is off by default."
            }
        }

        Expander {
            title: "Rumble filter"
            hint: page.p.highPassEnabled === true ? "Below " + Math.round(page.p.highPassHz) + " Hz" : "Off"
            ParamSwitch { paramKey: "highPassEnabled"; description: "Removes rumble, desk bumps and low hum below the cutoff" }
            ParamSlider { paramKey: "highPassHz"; enabled: page.p.highPassEnabled === true }
        }

        Expander {
            title: "Noise gate"
            hint: page.gateOn ? "Turning down between words by " + Theme.signed(backend.gateReductionDb, 0, "dB") + " now" : "Off"
            ParamSwitch { paramKey: "gateEnabled"; description: "Turns down the sound between words; opens instantly when you speak" }
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
}
