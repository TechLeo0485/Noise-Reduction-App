import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

// Microphone, virtual microphone (VB-CABLE or your own driver), headphones and app settings.
// Measurements, buffers, logging and reset are in collapsed sections at the bottom.
ScreenPage {
    id: page

    signal openPage(string key)

    property bool showVirtualMics: false
    property bool changeOutput: false

    readonly property var su: backend.setup
    readonly property var s: backend.stats
    readonly property var cpu: backend.hardware.cpu
    readonly property var bufferModes: [
        { id: "0", name: "Low latency (smallest device buffers)" },
        { id: "1", name: "Default (Windows engine period, usually 10 ms)" },
        { id: "2", name: "Safe (larger buffer for Bluetooth / unstable drivers)" }
    ]
    readonly property var logLevels: [
        { id: "0", name: "Debug" }, { id: "1", name: "Info" }, { id: "2", name: "Warnings" },
        { id: "3", name: "Errors only" }, { id: "4", name: "Off" }
    ]

    function formatOf(list, id) {
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === id) return list[i].format
        return ""
    }
    function defaultName(list) {
        for (let i = 0; i < list.length; ++i)
            if (list[i].isDefault) return list[i].name
        return ""
    }
    function defaultIsVirtual(list) {
        for (let i = 0; i < list.length; ++i)
            if (list[i].isDefault) return list[i].isVirtualCable
        return false
    }
    function countVirtual(list) {
        let n = 0
        for (let i = 0; i < list.length; ++i)
            if (list[i].isVirtualCable) ++n
        return n
    }
    function count(v) { return v !== undefined ? String(v) : "n/a" }

    CardGrid {
        wide: page.wide

            Card {
                Layout.fillHeight: true
                title: "1. Your microphone"
                subtitle: "Click the microphone you speak into. The bar under it should move when you talk."
                actions: VsButton { text: "Refresh"; iconText: "\uE72C"; implicitHeight: 32; onClicked: backend.refreshDevices() }
                icon: "\uE720"

                DeviceTile {
                    title: "Windows default microphone"
                    // Common trap: after making the virtual microphone the Windows default for call apps,
                    // VoiceStudio would listen to its own output.
                    subtitle: page.defaultIsVirtual(backend.inputDevices)
                              ? "Your Windows default is \"" + page.defaultName(backend.inputDevices)
                                + "\", a virtual device - so VoiceStudio listens to \"" + page.su.micName
                                + "\" instead. Better: pick your real microphone below."
                              : page.defaultName(backend.inputDevices) !== ""
                                ? "Currently: " + page.defaultName(backend.inputDevices)
                                  + ". Follows Windows when you change it there."
                                : "Follows the default microphone set in Windows."
                    warn: page.defaultIsVirtual(backend.inputDevices)
                    selected: backend.inputFollowDefault
                    showLevel: selected && backend.engineRunning
                    levelDb: backend.inputLevelDb
                    onClicked: backend.selectInputDevice("")
                }

                Repeater {
                    model: backend.inputDevices
                    DeviceTile {
                        required property var modelData
                        visible: !modelData.isVirtualCable || page.showVirtualMics
                        title: modelData.name
                        subtitle: modelData.isVirtualCable ? "Virtual device - not a real microphone; nothing you say reaches it."
                                  : !modelData.formatSupported ? "This device reports an unsupported format." : ""
                        warn: modelData.isVirtualCable
                        tag: modelData.isVirtualCable ? "virtual" : (modelData.isDefault ? "Windows default" : "")
                        selected: !backend.inputFollowDefault && backend.inputDeviceId === modelData.id
                        showLevel: selected && backend.engineRunning
                        levelDb: backend.inputLevelDb
                        onClicked: backend.selectInputDevice(modelData.id)
                    }
                }

                LinkText {
                    visible: page.countVirtual(backend.inputDevices) > 0
                    text: page.showVirtualMics ? "Hide virtual devices"
                                               : "Show virtual devices (" + page.countVirtual(backend.inputDevices) + ")"
                    onClicked: page.showVirtualMics = !page.showVirtualMics
                }
                Text {
                    visible: backend.inputDevices.length === 0
                    text: "No microphone found. Plug one in, then press Refresh devices."
                    color: Theme.warning
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            Card {
                Layout.fillHeight: true
                title: "2. Send your voice to apps"
                subtitle: "VoiceStudio plays your voice into a virtual microphone. Call apps pick it up from there."
                icon: "\uE8F1"

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: statusColumn.implicitHeight + 24
                    radius: Theme.radiusSmall
                    color: page.su.outputOk ? Theme.successMuted : Theme.warningMuted
                    border.color: page.su.outputOk ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.4)
                                                   : Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.4)
                    ColumnLayout {
                        id: statusColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 12
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: page.su.outputOk ? "✓  Sending your voice to \"" + page.su.outputName + "\""
                                  : !page.su.cableInstalled ? "No virtual microphone found"
                                  : page.su.outputName !== "" ? "Your voice is going to \"" + page.su.outputName + "\" (a speaker)"
                                  : "Your voice is not being sent anywhere yet"
                            color: page.su.outputOk ? Theme.success : Theme.warning
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            textFormat: Text.StyledText
                            text: page.su.outputOk
                                  ? "In Zoom, Teams, Discord or Meet, choose <b>" + page.su.appMicName + "</b> as the microphone."
                                  : !page.su.cableInstalled
                                    ? "Install VB-CABLE (free) and restart Windows, or mark your own virtual microphone below."
                                    : "Other apps cannot hear you. Click <b>Use virtual microphone</b>."
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // VoiceStudio's own driver installed but not running: say why and what to do.
                Rectangle {
                    readonly property string vm: page.su.virtualMic !== undefined ? page.su.virtualMic : "notInstalled"
                    visible: vm !== "notInstalled" && vm !== "running"
                    Layout.fillWidth: true
                    implicitHeight: vmText.implicitHeight + 24
                    radius: Theme.radiusSmall
                    color: Theme.warningMuted
                    border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.4)
                    Text {
                        id: vmText
                        anchors.fill: parent
                        anchors.margins: 12
                        wrapMode: Text.WordWrap
                        textFormat: Text.StyledText
                        color: Theme.text
                        font.pixelSize: 12
                        text: {
                            const st = parent.vm
                            const head = "<b>VoiceStudio Virtual Microphone is installed, but not working yet.</b><br>"
                            if (st === "notStarted")
                                return head + "Restart Windows to finish installing it."
                            if (st === "problem")
                                return head + "Windows reports problem code " + page.su.virtualMicProblem
                                       + ". Restart Windows; if it stays, run VoiceStudio setup again."
                            // blocked: the test-signed driver is not allowed to load
                            if (page.su.secureBoot === true)
                                return head + "It is a free test-signed driver, which Windows only runs in <b>Test Mode</b> - "
                                       + "and Test Mode needs <b>Secure Boot turned off</b>, which is on on this PC. Turn Secure "
                                       + "Boot off in your PC's UEFI/BIOS settings, then run VoiceStudio setup again and allow "
                                       + "Test Mode. Until then, use VB-CABLE."
                            if (page.su.testMode !== true)
                                return head + "It is a free test-signed driver, which Windows only runs in <b>Test Mode</b>, and "
                                       + "Test Mode is off. Run VoiceStudio setup again and allow Test Mode, then restart."
                            return head + "Test Mode is on, but Windows does not trust the driver's certificate. Run "
                                   + "VoiceStudio setup again (it installs the certificate), then restart."
                        }
                    }
                }

                RowLayout {
                    spacing: 10
                    VsButton {
                        visible: !page.su.outputOk && page.su.canFixOutput
                        primary: true
                        text: "Use virtual microphone"
                        onClicked: backend.fixOutput()
                    }
                    VsButton {
                        visible: !page.su.cableInstalled
                        primary: true
                        text: "Get VB-CABLE"
                        onClicked: Qt.openUrlExternally("https://vb-audio.com/Cable/")
                    }
                    VsButton {
                        text: page.changeOutput ? "Done" : "Choose another device"
                        onClicked: page.changeOutput = !page.changeOutput
                    }
                }

                VsComboBox {
                    visible: page.changeOutput
                    Layout.fillWidth: true
                    model: backend.outputDevices
                    textRole: "label"
                    currentId: backend.outputDeviceId
                    placeholder: "Select where your voice goes (normally a virtual microphone)"
                    onPicked: (id) => backend.selectOutputDevice(id)
                }
                VsButton {
                    text: backend.cableTest.running ? "Testing..." : "Test virtual microphone"
                    enabled: !backend.cableTest.running && page.su.cableInstalled
                    onClicked: backend.runVirtualCableTest()
                }
                Text {
                    visible: backend.cableTest.hasResult === true || backend.cableTest.running === true
                    text: backend.cableTest.message !== undefined ? backend.cableTest.message : ""
                    color: backend.cableTest.running ? Theme.textSecondary : (backend.cableTest.ok ? Theme.success : Theme.warning)
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Expander {
                    title: "Use my own virtual microphone"
                    hint: "Any virtual audio driver, not only VB-CABLE"
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textSecondary
                        font.pixelSize: 12
                        text: "Devices with \"cable\" or \"virtual\" in the name are found automatically. If yours is not, "
                              + "switch it on here: its playback side becomes where VoiceStudio sends your voice, and its "
                              + "recording side is hidden from the microphone list."
                    }
                    Text { text: "Playback side (VoiceStudio sends your voice here)"; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                    Repeater {
                        model: backend.outputDevices
                        VsSwitch {
                            required property var modelData
                            label: modelData.name
                            description: modelData.isVirtualCable && !modelData.userVirtual ? "Detected as virtual automatically" : ""
                            enabled: !modelData.isVirtualCable || modelData.userVirtual
                            checked: modelData.isVirtualCable
                            onToggled: (value) => backend.setDeviceVirtual(modelData.id, value)
                        }
                    }
                    Text { text: "Recording side (call apps use this as the microphone)"; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                    Repeater {
                        model: backend.inputDevices
                        VsSwitch {
                            required property var modelData
                            label: modelData.name
                            description: modelData.isVirtualCable && !modelData.userVirtual ? "Detected as virtual automatically" : ""
                            enabled: !modelData.isVirtualCable || modelData.userVirtual
                            checked: modelData.isVirtualCable
                            onToggled: (value) => backend.setDeviceVirtual(modelData.id, value)
                        }
                    }
                }
            }

            Card {
                Layout.fillHeight: true
                title: "3. Headphones (optional)"
                subtitle: "Where you hear yourself (Listen live on Home) and voice checks. Use headphones, not speakers."
                icon: "\uE7F6"
                VsComboBox {
                    Layout.fillWidth: true
                    model: backend.outputDevices
                    textRole: "label"
                    currentId: backend.monitorDeviceId
                    placeholder: "Windows default output"
                    onPicked: (id) => backend.selectMonitorDevice(id)
                }
                ParamSlider { paramKey: "monitorGainDb" }
                Text {
                    visible: backend.streamInfo.monitorError !== undefined && backend.streamInfo.monitorError !== ""
                    text: backend.streamInfo.monitorError !== undefined ? backend.streamInfo.monitorError : ""
                    color: Theme.warning
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            Card {
                Layout.fillHeight: true
                title: "General"
                icon: "\uE713"
                VsSwitch {
                    label: "Keep running when I close the window"
                    description: "VoiceStudio stays in the notification area (next to the clock). Right-click its icon to quit."
                    enabled: backend.trayAvailable
                    checked: backend.closeToTray
                    onToggled: (value) => backend.closeToTray = value
                }
                VsSwitch {
                    label: "Start with Windows"
                    description: "Starts in the notification area when you sign in, so your voice is ready in every call"
                    checked: backend.startWithWindows
                    onToggled: (value) => backend.startWithWindows = value
                }
                VsSwitch {
                    label: "Reconnect automatically"
                    description: "Restart audio by itself after unplugging a device, Bluetooth drop-outs or sleep"
                    checked: backend.autoRecover
                    onToggled: (value) => backend.autoRecover = value
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    text: "Privacy: everything runs on this computer. Nothing is uploaded, there is no account or tracking. "
                          + "Only the voice you record on the Clean-up page is saved, in its own folder."
                }
            }

            Card {
                Layout.fillHeight: true
                title: "Keyboard shortcuts"
                subtitle: "Work everywhere, even while you are in a game or call and VoiceStudio is hidden. Click the keys to change them."
                icon: ""
                Repeater {
                    model: hotkeys.items
                    HotkeyRow {}
                }
            }

            Card {
                Layout.fillHeight: true
                title: "Delay"
                subtitle: "From your microphone to the app that records the virtual microphone"
                icon: "\uE916"
                LatencyMeter {}
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    text: "Every part is limited so the total stays under " + Theme.latencyBudgetMs + " ms. \"Only my voice\" "
                          + "uses most of it; it runs on " + (backend.isolation.processorName !== undefined
                          && backend.isolation.processorName !== "" ? backend.isolation.processorName : "the CPU")
                          + (backend.selectedBackend === "auto" ? " (chosen automatically for the lowest delay)." : ".")
                }
            }

            Card {
                Layout.fillHeight: true
                title: "Help and troubleshooting"
                icon: "\uE897"
                RowLayout {
                    spacing: 8
                    VsButton { text: "Open user guide"; primary: true; onClicked: page.openPage("help") }
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
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    text: "Logs and diagnostics contain settings and device names only, never audio."
                }
            }

    }

    // ---- Technical: everything most people never need ----------------------------------------------
    Card {
        title: "Advanced"
        subtitle: "Processor, speed, buffers, measurements and logging"
        iconColor: Theme.textSecondary
        icon: "\uE9F5"

        // Where voice isolation runs: Automatic measures the CPU and every usable graphics card on this PC.
        Expander {
            id: processorSection
            readonly property var iso: backend.isolation
            readonly property var trials: iso.trials !== undefined ? iso.trials : []
            title: "Processor for voice isolation"
            divider: false
            hint: iso.processorName !== undefined && iso.processorName !== ""
                  ? "Runs on " + iso.processorName + (backend.selectedBackend === "auto" ? " (automatic)" : "")
                  : (backend.selectedBackend === "auto" ? "Automatic" : "")
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 12
                text: "Only \"Only send my voice\" uses it; noise removal and effects always run on the CPU. A graphics "
                      + "card is not always faster for live voice - Automatic measures and picks the lowest delay. "
                      + "If you choose a graphics card that turns out slower than the CPU, the CPU is used so the delay "
                      + "stays low. A change is measured right away (about 10 seconds)."
            }
            GridLayout {
                Layout.fillWidth: true
                columns: page.wide ? 2 : 1
                columnSpacing: 12
                rowSpacing: 12
                Repeater {
                    model: backend.hardware.backends
                    ModeTile {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        title: modelData.name
                        description: modelData.detail
                        badge: modelData.kind === "auto" ? "RECOMMENDED" : ""
                        available: modelData.available
                        selected: backend.selectedBackend === modelData.id
                        onClicked: backend.selectedBackend = modelData.id
                    }
                }
            }
            Text {
                visible: processorSection.trials.length > 0
                text: "Measured on this PC"
                color: Theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Repeater {
                model: processorSection.trials
                StatRow {
                    required property var modelData
                    label: modelData.name
                    value: modelData.error !== "" ? "Cannot run: " + modelData.error
                           : modelData.chunkMs + " ms steps, " + Theme.fmt(modelData.p50Ms, 1, "ms") + " each"
                             + (modelData.realTime ? " - keeps up" : " - too slow")
                    valueColor: modelData.error !== "" || !modelData.realTime ? Theme.warning : Theme.text
                }
            }
        }

        Expander {
            title: "Speed vs. stability"
            hint: {
                const list = backend.performanceModes
                for (let i = 0; i < list.length; ++i)
                    if (list[i].key === backend.performanceMode) return list[i].label
                return ""
            }
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

        Expander {
            title: "Audio buffers and recovery"
            hint: "Changes restart the streams (about 100 ms of silence)"
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
            StatRow {
                label: "Effective target"
                value: backend.streamInfo.outputTargetMs > 0 ? backend.streamInfo.outputTargetMs + " ms" : "-"
                monospace: true
            }
            StatRow { label: "Engine starts this session"; value: page.count(page.s.restartCount) }
            StatRow { label: "Last error"; value: backend.engineError !== "" ? backend.engineError : "None"; valueColor: backend.engineError !== "" ? Theme.warning : Theme.text }
            VsButton { text: "Restart audio engine"; onClicked: backend.restartEngine() }
        }

        Expander {
            title: "Streams and latency"
            hint: "Estimated total " + Theme.fmt(page.s.latencyEstimatedTotalMs, 1, "ms")
            StatRow {
                label: "Microphone"
                value: backend.streamInfo.capture && backend.streamInfo.capture.running
                       ? backend.streamInfo.capture.format + ", " + backend.streamInfo.capture.periodMs + " / "
                         + backend.streamInfo.capture.bufferMs + " ms" : "-"
                monospace: true
            }
            StatRow {
                label: "Output"
                value: backend.streamInfo.output && backend.streamInfo.output.running
                       ? backend.streamInfo.output.format + ", " + backend.streamInfo.output.periodMs + " / "
                         + backend.streamInfo.output.bufferMs + " ms" : "-"
                monospace: true
            }
            StatRow { label: "Resampled to 48 kHz"; value: backend.streamInfo.captureResampling ? "Yes" : "No (native 48 kHz)" }
            StatRow { label: "Capture device period"; value: Theme.fmt(page.s.latencyCapturePeriodMs, 2, "ms"); monospace: true }
            StatRow { label: "10 ms framing (worst case)"; value: Theme.fmt(page.s.latencyFramingMs, 2, "ms"); monospace: true }
            StatRow { label: "Algorithmic (noise removal / voice character / isolation)"; value: Theme.fmt(page.s.latencyAlgorithmicMs, 2, "ms"); monospace: true }
            StatRow { label: "Processing execution (P50)"; value: Theme.fmt(page.s.latencyProcessingMs, 2, "ms"); monospace: true }
            StatRow { label: "Resampling filters"; value: Theme.fmt(page.s.latencyResamplerMs, 2, "ms"); monospace: true }
            StatRow { label: "Jitter buffer (measured fill)"; value: Theme.fmt(page.s.latencyJitterBufferMs, 2, "ms"); monospace: true }
            StatRow { label: "Render queue (measured)"; value: Theme.fmt(page.s.latencyRenderQueueMs, 2, "ms"); monospace: true }
            StatRow { label: "Virtual microphone (estimate)"; value: "25 ms"; monospace: true }
            StatRow {
                label: "Limit for \"Only my voice\" (what the 300 ms leave)"
                value: Theme.fmt(page.s.isolationMaxMs, 0, "ms")
                monospace: true
            }
            StatRow {
                label: "Total incl. virtual microphone (limit 300 ms)"
                value: Theme.fmt(page.s.latencyWithVirtualMicMs, 1, "ms")
                valueColor: Theme.accent
                monospace: true
            }
            StatRow {
                label: "Estimated total (excl. drivers / cable)"
                value: Theme.fmt(page.s.latencyEstimatedTotalMs, 1, "ms")
                valueColor: Theme.accent
                monospace: true
            }
        }

        Expander {
            title: "Performance and hardware"
            hint: "Process CPU " + Theme.fmt(page.s.processCpuPercent, 1, "%")
            StatRow { label: "CPU"; value: page.cpu.brand !== "" ? page.cpu.brand : page.cpu.vendor }
            StatRow { label: "Cores / threads"; value: page.cpu.physicalCores + " / " + page.cpu.logicalProcessors }
            StatRow { label: "Memory (available / total)"; value: page.cpu.availableMemoryGb + " / " + page.cpu.totalMemoryGb + " GB" }
            Repeater {
                model: backend.hardware.gpus
                StatRow {
                    required property var modelData
                    label: modelData.kind === "npu" ? "NPU" : modelData.kind === "integrated" ? "Integrated graphics" : "Graphics card"
                    value: modelData.name + (modelData.dedicatedMemoryMb >= 256
                                             ? " (" + Math.round(modelData.dedicatedMemoryMb / 1024) + " GB)" : "")
                }
            }
            StatRow {
                visible: backend.hardware.ignoredAdapters !== undefined && backend.hardware.ignoredAdapters.length > 0
                label: "Not used"
                value: backend.hardware.ignoredAdapters !== undefined ? backend.hardware.ignoredAdapters.join("; ") : ""
            }
            StatRow { label: "Process CPU (all cores)"; value: Theme.fmt(page.s.processCpuPercent, 1, "%"); monospace: true }
            StatRow { label: "Memory (working set / private)"; value: Theme.fmt(page.s.workingSetMb, 0, "") + " / " + Theme.fmt(page.s.privateMb, 0, "MB"); monospace: true }
            StatRow { label: "Chain time P50 / P99 / max"; value: Theme.fmt(page.s.frameP50Us, 0, "") + " / " + Theme.fmt(page.s.frameP99Us, 0, "") + " / " + Theme.fmt(page.s.frameMaxUs, 0, "µs"); monospace: true }
            StatRow { label: "Share of 10 ms real-time budget"; value: Theme.fmt(page.s.cpuLoadOfFrameBudget, 1, "%"); monospace: true }
            StatRow { label: "Deadline misses"; value: page.count(page.s.deadlineMisses); monospace: true; valueColor: page.s.deadlineMisses > 0 ? Theme.warning : Theme.text }
            StatRow { label: "Output underruns (dropouts)"; value: page.count(page.s.outputUnderruns); monospace: true; valueColor: page.s.outputUnderruns > 0 ? Theme.warning : Theme.text }
            StatRow { label: "Capture glitches (device reported)"; value: page.count(page.s.captureGlitches); monospace: true }
            VsButton { text: "Reset timing window"; onClicked: backend.resetStatistics() }
        }

        Expander {
            title: "Logging"
            hint: "Log level and file"
            VsComboBox {
                Layout.fillWidth: true
                model: page.logLevels
                currentId: String(backend.logLevel)
                onPicked: (id) => backend.logLevel = parseInt(id)
            }
            StatRow { label: "Log file"; value: backend.logFilePath; monospace: true }
        }

        Expander {
            title: "Start over"
            hint: "Reset every setting"
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 12
                text: "Restores every setting to its default. Your saved situations, your recorded voice and the devices "
                      + "you marked as virtual are kept."
            }
            VsButton { text: "Reset all settings"; danger: true; onClicked: confirmReset.open() }
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
