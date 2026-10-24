import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

ScreenPage {
    id: page

    readonly property var bufferModes: [
        { id: "0", name: "Low latency (IAudioClient3 minimum period where supported)" },
        { id: "1", name: "Default (Windows engine period, usually 10 ms)" },
        { id: "2", name: "Safe (larger device buffer for Bluetooth / unstable drivers)" }
    ]
    readonly property var logLevels: [
        { id: "0", name: "Debug" }, { id: "1", name: "Info" }, { id: "2", name: "Warnings" },
        { id: "3", name: "Errors only" }, { id: "4", name: "Off" }
    ]

    TwoColumns {
        wide: page.wide

        leftContent: [
            Card {
                title: "Audio buffers"
                subtitle: "Smaller buffers reduce delay but are more sensitive to CPU spikes. Changes restart the streams (about 100 ms of silence)."
                icon: ""
                Text { text: "Device buffer mode"; color: Theme.textSecondary; font.pixelSize: 12 }
                VsComboBox {
                    Layout.fillWidth: true
                    model: page.bufferModes
                    currentId: String(backend.bufferMode)
                    onPicked: (id) => backend.bufferMode = parseInt(id)
                }
                VsSlider {
                    label: "Jitter buffer target"
                    unit: "ms"
                    from: 2
                    to: 60
                    stepSize: 1
                    decimals: 0
                    value: backend.outputTargetMs
                    onMoved: (v) => backend.outputTargetMs = v
                }
                Text {
                    text: "Effective target: " + (backend.streamInfo.outputTargetMs > 0 ? backend.streamInfo.outputTargetMs + " ms" : "-")
                          + ". It is raised automatically when the requested value would cause dropouts with the "
                          + "negotiated device period."
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            },

            Card {
                title: "Recovery"
                icon: ""
                VsSwitch {
                    label: "Automatic recovery"
                    description: "Reopen devices automatically after unplugging, Bluetooth drop-outs, driver resets or sleep"
                    checked: backend.autoRecover
                    onToggled: (value) => backend.autoRecover = value
                }
                StatRow { label: "Engine starts this session"; value: backend.stats.restartCount !== undefined ? String(backend.stats.restartCount) : "0" }
                StatRow { label: "Last error"; value: backend.engineError !== "" ? backend.engineError : "None"; valueColor: backend.engineError !== "" ? Theme.warning : Theme.text }
                VsButton { text: "Restart audio engine"; onClicked: backend.restartEngine() }
            }
        ]

        rightContent: [
            Card {
                title: "Sample rate"
                icon: ""
                StatRow { label: "Internal processing format"; value: "48 kHz mono, 32-bit float (fixed)" }
                StatRow {
                    label: "Microphone rate"
                    value: backend.streamInfo.capture && backend.streamInfo.capture.running ? backend.streamInfo.capture.sampleRate + " Hz" : "-"
                }
                StatRow {
                    label: "Output rate"
                    value: backend.streamInfo.output && backend.streamInfo.output.running ? backend.streamInfo.output.sampleRate + " Hz" : "-"
                }
                Text {
                    text: "48 kHz is RNNoise's native rate, so it is used internally. In shared mode, device rates follow the "
                          + "format set in Windows Sound settings (device Properties > Advanced). Setting your microphone and "
                          + "CABLE Input to 48000 Hz avoids resampling and gives the lowest latency."
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            },

            Card {
                title: "Logging and diagnostics"
                subtitle: "Logs contain events and device names only - never audio."
                icon: ""
                Text { text: "Log level"; color: Theme.textSecondary; font.pixelSize: 12 }
                VsComboBox {
                    Layout.fillWidth: true
                    model: page.logLevels
                    currentId: String(backend.logLevel)
                    onPicked: (id) => backend.logLevel = parseInt(id)
                }
                StatRow { label: "Log file"; value: backend.logFilePath; monospace: true }
                RowLayout {
                    spacing: 8
                    VsButton { text: "Open log folder"; onClicked: backend.openLogFolder() }
                    VsButton {
                        text: copied.running ? "Copied" : "Copy diagnostics"
                        onClicked: {
                            backend.copyToClipboard(backend.diagnosticsReport())
                            copied.restart()
                        }
                        Timer { id: copied; interval: 1500 }
                    }
                }
            }
        ]
    }

    Card {
        title: "Reset"
        icon: ""
        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Text {
                text: "Restores all processing settings, device selections and buffer options to their defaults. Custom presets are kept."
                color: Theme.textSecondary
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            VsButton { text: "Reset to defaults"; danger: true; onClicked: confirmReset.open() }
        }
    }

    Dialog {
        id: confirmReset
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Reset all settings?"
        standardButtons: Dialog.Yes | Dialog.Cancel
        onAccepted: backend.resetToDefaults()
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.border }
        header: Text {
            text: confirmReset.title
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
            padding: 20
        }
        contentItem: Text {
            text: "This cannot be undone."
            color: Theme.textSecondary
            font.pixelSize: 13
        }
    }
}
