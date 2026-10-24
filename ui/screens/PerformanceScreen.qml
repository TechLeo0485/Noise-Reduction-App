import QtQuick
import QtQuick.Layouts
import VoiceStudio

ScreenPage {
    id: page

    readonly property var s: backend.stats
    readonly property var cpu: backend.hardware.cpu

    function count(v) { return v !== undefined ? String(v) : "n/a" }

    Card {
        title: "Performance mode"
        icon: ""
        GridLayout {
            Layout.fillWidth: true
            columns: page.wide ? 4 : 1
            columnSpacing: 12
            rowSpacing: 12
            Repeater {
                model: backend.performanceModes
                ModeTile {
                    required property var modelData
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    title: modelData.label
                    description: modelData.description
                    badge: modelData.key === "custom" ? "" : "~" + modelData.targetMs + " ms buffer"
                    selected: backend.performanceMode === modelData.key
                    onClicked: backend.setPerformanceMode(modelData.key)
                }
            }
        }
    }

    TwoColumns {
        wide: page.wide

        leftContent: [
            Card {
                title: "Hardware"
                subtitle: "Detected at startup"
                icon: ""
                StatRow { label: "CPU"; value: page.cpu.brand !== "" ? page.cpu.brand : page.cpu.vendor }
                StatRow { label: "Cores / threads"; value: page.cpu.physicalCores + " / " + page.cpu.logicalProcessors }
                StatRow { label: "SIMD"; value: page.cpu.simd !== "" ? page.cpu.simd : "none detected" }
                StatRow { label: "Memory (available / total)"; value: page.cpu.availableMemoryGb + " / " + page.cpu.totalMemoryGb + " GB" }
                Repeater {
                    model: backend.hardware.gpus
                    StatRow {
                        required property var modelData
                        label: "GPU " + modelData.index + (modelData.software ? " (software)" : "")
                        value: modelData.name + (modelData.dedicatedMemoryMb > 0 ? "  -  " + modelData.dedicatedMemoryMb + " MB" : "")
                    }
                }
                StatRow { label: "GPU utilization"; value: backend.hardware.gpuUtilization; valueColor: Theme.textSecondary }
            },

            Card {
                title: "Live measurements"
                subtitle: "Real measurements from this session (last 30 s window for timings)"
                icon: ""
                StatRow { label: "Process CPU (all cores)"; value: Theme.fmt(page.s.processCpuPercent, 1, "%"); monospace: true }
                StatRow { label: "Memory (working set / private)"; value: Theme.fmt(page.s.workingSetMb, 0, "") + " / " + Theme.fmt(page.s.privateMb, 0, "MB"); monospace: true }
                StatRow { label: "Chain time avg / P50"; value: Theme.fmt(page.s.frameAvgUs, 0, "") + " / " + Theme.fmt(page.s.frameP50Us, 0, "µs"); monospace: true }
                StatRow { label: "Chain time P95 / P99 / max"; value: Theme.fmt(page.s.frameP95Us, 0, "") + " / " + Theme.fmt(page.s.frameP99Us, 0, "") + " / " + Theme.fmt(page.s.frameMaxUs, 0, "µs"); monospace: true }
                StatRow { label: "Capture callback P99 / max"; value: Theme.fmt(page.s.callbackP99Us, 0, "") + " / " + Theme.fmt(page.s.callbackMaxUs, 0, "µs"); monospace: true }
                StatRow { label: "Frames processed"; value: page.count(page.s.framesProcessed); monospace: true }
                StatRow { label: "Deadline misses"; value: page.count(page.s.deadlineMisses); monospace: true; valueColor: page.s.deadlineMisses > 0 ? Theme.warning : Theme.text }
                StatRow { label: "Output underruns (dropouts)"; value: page.count(page.s.outputUnderruns); monospace: true; valueColor: page.s.outputUnderruns > 0 ? Theme.warning : Theme.text }
                StatRow { label: "Output overruns / latency resets"; value: page.count(page.s.outputOverruns) + " / " + page.count(page.s.latencyResets); monospace: true }
                StatRow { label: "Capture glitches (device reported)"; value: page.count(page.s.captureGlitches); monospace: true }
                StatRow { label: "Clock drift correction"; value: Theme.fmt(Math.abs(page.s.driftPpm), 0, "ppm"); monospace: true }
                VsButton { text: "Reset timing window"; onClicked: backend.resetStatistics() }
            }
        ]

        rightContent: [
            Card {
                title: "Processing hardware"
                subtitle: "Where AI inference runs. Only backends this build can actually use are selectable."
                icon: ""
                VsComboBox {
                    Layout.fillWidth: true
                    model: backend.hardware.backends
                    currentId: backend.selectedBackend
                    onPicked: (id) => backend.selectedBackend = id
                }
                Repeater {
                    model: backend.hardware.backends
                    ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 1
                        Text {
                            text: modelData.name
                            color: modelData.available ? Theme.text : Theme.textDisabled
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: modelData.detail
                            color: Theme.textSecondary
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }
            },

            Card {
                title: "Latency breakdown"
                subtitle: "Measured or derived from the negotiated streams. Driver, converter and virtual-cable latency are invisible to the app; measure them with the loopback test."
                icon: ""
                StatRow { label: "Capture device period"; value: Theme.fmt(page.s.latencyCapturePeriodMs, 2, "ms"); monospace: true }
                StatRow { label: "10 ms framing (worst case)"; value: Theme.fmt(page.s.latencyFramingMs, 2, "ms"); monospace: true }
                StatRow { label: "Algorithmic (suppression / pitch)"; value: Theme.fmt(page.s.latencyAlgorithmicMs, 2, "ms"); monospace: true }
                StatRow { label: "Processing execution (P50)"; value: Theme.fmt(page.s.latencyProcessingMs, 2, "ms"); monospace: true }
                StatRow { label: "Resampling filters"; value: Theme.fmt(page.s.latencyResamplerMs, 2, "ms"); monospace: true }
                StatRow { label: "Jitter buffer (measured fill)"; value: Theme.fmt(page.s.latencyJitterBufferMs, 2, "ms"); monospace: true }
                StatRow { label: "Render queue (measured)"; value: Theme.fmt(page.s.latencyRenderQueueMs, 2, "ms"); monospace: true }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                StatRow {
                    label: "Estimated total (excl. drivers / cable)"
                    value: Theme.fmt(page.s.latencyEstimatedTotalMs, 1, "ms")
                    valueColor: Theme.accent
                    monospace: true
                }
            }
        ]
    }
}
