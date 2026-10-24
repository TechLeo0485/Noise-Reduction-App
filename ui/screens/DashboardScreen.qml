import QtQuick
import QtQuick.Layouts
import VoiceStudio

ScreenPage {
    id: page

    readonly property string checkState: backend.voiceCheck.state !== undefined ? backend.voiceCheck.state : "idle"
    readonly property bool checkBusy: checkState === "recording" || checkState === "playing"

    TwoColumns {
        wide: page.wide

        leftContent: [
            Card {
                title: "Microphone"
                subtitle: backend.inputName !== "" ? backend.inputName : "No microphone active"
                icon: ""
                LevelMeter { label: "Input level"; levelDb: backend.inputLevelDb }
                ParamSlider { paramKey: "inputGainDb" }
            },

            Card {
                title: "Noise suppression"
                subtitle: backend.engineRunning ? "Active engine: " + backend.activeSuppressor : "Engine not running"
                icon: ""
                ParamSwitch {
                    paramKey: "suppressionEnabled"
                    description: "Removes fans, keyboards, hum and other background noise"
                }
                ParamSlider { paramKey: "suppressionStrength"; enabled: backend.params.suppressionEnabled === true }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: backend.voiceProbability >= 0
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Voice detected"; color: Theme.textSecondary; font.pixelSize: 12; Layout.fillWidth: true }
                        Text {
                            text: Math.round(Math.max(0, backend.voiceProbability) * 100) + " %"
                            color: Theme.text
                            font.pixelSize: 12
                        }
                    }
                    ProgressBarThin { value: backend.voiceProbability; barColor: Theme.success }
                }
            },

            Card {
                title: "Preset"
                subtitle: backend.presetModified ? backend.activePreset + " (modified)" : backend.activePreset
                icon: ""
                VsComboBox {
                    Layout.fillWidth: true
                    model: backend.presets
                    valueRole: "name"
                    currentId: backend.activePreset
                    onPicked: (name) => backend.applyPreset(name)
                }
                Text { text: "Performance mode"; color: Theme.textSecondary; font.pixelSize: 12 }
                VsComboBox {
                    Layout.fillWidth: true
                    model: backend.performanceModes
                    textRole: "label"
                    valueRole: "key"
                    currentId: backend.performanceMode
                    onPicked: (key) => backend.setPerformanceMode(key)
                }
            }
        ]

        rightContent: [
            Card {
                title: "Processed output"
                subtitle: backend.outputName !== "" ? backend.outputName
                                                   : "No output - select CABLE Input in Audio Devices"
                icon: ""
                LevelMeter { label: "Output level (what others receive)"; levelDb: backend.outputLevelDb }
                ParamSlider { paramKey: "voiceVolumeDb" }
            },

            Card {
                title: "Hear yourself"
                subtitle: "Listen to your processed voice exactly as others hear it. Use headphones."
                icon: ""

                // Voice check: record, then play back
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    VsButton {
                        primary: !page.checkBusy
                        text: page.checkState === "recording" ? "Stop recording"
                              : page.checkState === "playing" ? "Stop playback"
                              : "Record 5 s and play back"
                        enabled: backend.engineRunning || page.checkBusy
                        onClicked: page.checkBusy ? backend.stopVoiceCheck() : backend.startVoiceCheck(5)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: backend.voiceCheck.message !== undefined && backend.voiceCheck.message !== ""
                              ? backend.voiceCheck.message
                              : "Speak for 5 seconds, then listen to the result."
                        color: page.checkState === "error" ? Theme.warning
                               : page.checkState === "done" ? Theme.success : Theme.textSecondary
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
                ProgressBarThin {
                    visible: page.checkState === "recording"
                    value: backend.voiceCheck.progress !== undefined ? backend.voiceCheck.progress : 0
                    barColor: Theme.danger
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

                // Live monitoring
                VsSwitch {
                    label: "Listen live"
                    description: "Plays your processed voice to your headphones while you talk (with a small delay)"
                    checked: backend.monitorEnabled
                    onToggled: (value) => backend.monitorEnabled = value
                }
                Text { text: "Headphones"; color: Theme.textSecondary; font.pixelSize: 12 }
                VsComboBox {
                    Layout.fillWidth: true
                    model: backend.outputDevices
                    currentId: backend.monitorDeviceId
                    placeholder: "Windows default output"
                    onPicked: (id) => backend.selectMonitorDevice(id)
                }
                ParamSlider { paramKey: "monitorGainDb" }
            }
        ]
    }

    Card {
        title: "Status"
        icon: ""
        GridLayout {
            Layout.fillWidth: true
            columns: page.wide ? 2 : 1
            columnSpacing: 32
            rowSpacing: 8
            StatRow { label: "Audio engine"; value: backend.engineState; valueColor: Theme.stateColor(backend.engineState) }
            StatRow {
                label: "Processing hardware"
                value: backend.selectedBackend === "auto" ? "Auto (CPU)" : backend.selectedBackend.toUpperCase()
            }
            StatRow {
                label: "Estimated latency (excl. drivers / cable)"
                value: Theme.fmt(backend.stats.latencyEstimatedTotalMs, 1, "ms")
            }
            StatRow { label: "Processing per 10 ms frame (P95)"; value: Theme.fmt(backend.stats.frameP95Us, 0, "µs") }
            StatRow { label: "Process CPU"; value: Theme.fmt(backend.stats.processCpuPercent, 1, "%") }
            StatRow {
                label: "Dropouts (underruns)"
                value: backend.stats.outputUnderruns !== undefined ? String(backend.stats.outputUnderruns) : "n/a"
                valueColor: backend.stats.outputUnderruns > 0 ? Theme.warning : Theme.text
            }
        }
        Text {
            text: backend.engineMessage
            color: Theme.textSecondary
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
