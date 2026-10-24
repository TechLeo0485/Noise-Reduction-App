import QtQuick
import QtQuick.Layouts
import VoiceStudio

ScreenPage {
    id: page

    readonly property int mode: backend.params.suppressionMode !== undefined ? backend.params.suppressionMode : 1
    readonly property bool suppressionOn: backend.params.suppressionEnabled === true
    readonly property bool gateOn: backend.params.gateEnabled === true

    Card {
        title: "Suppression engine"
        subtitle: "All engines run locally on your computer. Switching is crossfaded over 10 ms, so it is safe to change while talking."
        icon: ""

        GridLayout {
            Layout.fillWidth: true
            columns: page.wide ? 3 : 1
            columnSpacing: 12
            rowSpacing: 12

            ModeTile {
                Layout.fillHeight: true
                title: "DSP only"
                badge: "LOWEST CPU"
                description: "Adaptive expander that follows the background noise floor. Removes steady noise "
                             + "between words but not noise that overlaps speech. No added latency."
                selected: page.mode === 0
                onClicked: backend.setParam("suppressionMode", 0)
            }
            ModeTile {
                Layout.fillHeight: true
                title: "RNNoise"
                badge: "RECOMMENDED"
                available: backend.rnnoiseAvailable
                description: "Neural noise suppression (Xiph RNNoise) on 10 ms frames. Removes fans, keyboards, "
                             + "clicks and hum while you speak. Adds 10 ms of algorithmic delay."
                selected: page.mode === 1
                onClicked: backend.setParam("suppressionMode", 1)
            }
            ModeTile {
                Layout.fillHeight: true
                title: "Advanced AI"
                badge: backend.advancedAiAvailable ? "GPU / NPU" : "NOT IN THIS BUILD"
                available: backend.advancedAiAvailable
                description: "Larger ONNX model for higher-quality suppression. Planned for Phase 3, after model "
                             + "licensing and real-time benchmarks. Requests fall back to RNNoise."
                selected: page.mode === 2
                onClicked: backend.setParam("suppressionMode", 2)
            }
        }

        Text {
            visible: backend.stats.requestedModeUnavailable === true
            text: "The selected engine is unavailable, so a fallback engine (" + backend.activeSuppressor + ") is active."
            color: Theme.warning
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    TwoColumns {
        wide: page.wide

        leftContent: [
            Card {
                title: "Noise suppression"
                icon: ""
                ParamSwitch {
                    paramKey: "suppressionEnabled"
                    description: "Off passes your voice through unprocessed (gate and effects still apply)"
                }
                ParamSlider { paramKey: "suppressionStrength"; enabled: page.suppressionOn }
                Text {
                    text: "Lower strength blends in some of the original signal. Stronger is not always better: "
                          + "100 % removes the most noise but can thin out quiet or breathy speech."
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                ParamSwitch { paramKey: "highPassEnabled"; description: "Removes rumble, desk bumps and low hum below the cutoff" }
                ParamSlider { paramKey: "highPassHz"; enabled: backend.params.highPassEnabled === true }
            },

            Card {
                title: "Processing cost"
                subtitle: "Measured on this machine over the last 30 seconds"
                icon: ""
                StatRow { label: "Active engine"; value: backend.engineRunning ? backend.activeSuppressor : "-" }
                StatRow { label: "Algorithmic latency"; value: Theme.fmt(backend.stats.latencyAlgorithmicMs, 1, "ms"); monospace: true }
                StatRow { label: "Chain time per frame (P50)"; value: Theme.fmt(backend.stats.frameP50Us, 0, "µs"); monospace: true }
                StatRow { label: "Chain time per frame (P99)"; value: Theme.fmt(backend.stats.frameP99Us, 0, "µs"); monospace: true }
                StatRow { label: "Share of 10 ms real-time budget"; value: Theme.fmt(backend.stats.cpuLoadOfFrameBudget, 1, "%"); monospace: true }
                StatRow {
                    label: "Deadline misses"
                    value: backend.stats.deadlineMisses !== undefined ? String(backend.stats.deadlineMisses) : "n/a"
                    valueColor: backend.stats.deadlineMisses > 0 ? Theme.warning : Theme.text
                    monospace: true
                }
            },

            Card {
                title: "Background voice isolation"
                subtitle: "Experimental - not available in this build"
                icon: ""
                VsSwitch {
                    label: "Reduce other people's voices"
                    description: "Requires a speaker-conditioned model and a local voice enrollment"
                    checked: false
                    enabled: false
                }
                Text {
                    text: "Noise suppression, voice-activity detection and separating your voice from another person's "
                          + "are different problems. RNNoise and the gate reduce distant background chatter somewhat, "
                          + "but cannot reliably remove a nearby person talking at the same time as you. A target-speaker "
                          + "model with on-device enrollment is planned as an opt-in experimental feature; your voice "
                          + "sample would stay on this computer and could be deleted at any time."
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        ]

        rightContent: [
            Card {
                title: "Noise gate"
                subtitle: "Reduces the level between words. Opens instantly when you speak."
                icon: ""
                ParamSwitch { paramKey: "gateEnabled" }
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
                ProgressBarThin {
                    // Full bar = gate fully open; shrinks as the gate attenuates (0..-60 dB).
                    value: 1.0 + Math.max(-60, backend.gateReductionDb) / 60
                    barColor: backend.gateReductionDb < -3 ? Theme.warning : Theme.success
                }
                ParamSlider { paramKey: "gateThresholdDb"; enabled: page.gateOn }
                ParamSlider { paramKey: "gateRangeDb"; enabled: page.gateOn }
                ParamSlider { paramKey: "gateRatio"; enabled: page.gateOn }
                ParamSlider { paramKey: "gateAttackMs"; enabled: page.gateOn }
                ParamSlider { paramKey: "gateHoldMs"; enabled: page.gateOn }
                ParamSlider { paramKey: "gateReleaseMs"; enabled: page.gateOn }
                ParamSwitch {
                    paramKey: "gateUseVad"
                    description: "Uses RNNoise voice detection to keep the gate open for quiet speech"
                    enabled: page.gateOn
                }
            }
        ]
    }
}
