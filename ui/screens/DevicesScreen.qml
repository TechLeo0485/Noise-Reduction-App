import QtQuick
import QtQuick.Layouts
import VoiceStudio

ScreenPage {
    id: page

    property bool showVirtualMics: false
    property bool changeOutput: false

    readonly property var su: backend.setup

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

    RowLayout {
        Layout.fillWidth: true
        Text {
            text: "Pick your microphone, check that VoiceStudio hears you, and make sure your voice goes to the virtual cable."
            color: Theme.textSecondary
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        VsButton { text: "Refresh devices"; onClicked: backend.refreshDevices() }
    }

    TwoColumns {
        wide: page.wide

        leftContent: [
            Card {
                title: "1. Your microphone"
                subtitle: "Click the microphone you speak into. The bar under it should move when you talk."
                icon: ""

                DeviceTile {
                    title: "Windows default microphone"
                    // Common trap: after setting "CABLE Output" as the default microphone for call apps, the
                    // Windows default is the virtual cable - VoiceStudio would then listen to its own output.
                    subtitle: page.defaultIsVirtual(backend.inputDevices)
                              ? "Not recommended: your Windows default is \"" + page.defaultName(backend.inputDevices)
                                + "\", a virtual device. Pick your real microphone below."
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

                Text {
                    visible: page.countVirtual(backend.inputDevices) > 0
                    text: page.showVirtualMics ? "Hide virtual devices"
                                               : "Show virtual devices (" + page.countVirtual(backend.inputDevices) + ")"
                    color: Theme.accent
                    font.pixelSize: 12
                    font.underline: true
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.showVirtualMics = !page.showVirtualMics
                    }
                }
                Text {
                    visible: backend.inputDevices.length === 0
                    text: "No microphone found. Plug one in, then press Refresh devices."
                    color: Theme.warning
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                StatRow {
                    visible: backend.advancedUi
                    label: "Device format"
                    value: backend.streamInfo.capture && backend.streamInfo.capture.running
                           ? backend.streamInfo.capture.format : page.formatOf(backend.inputDevices, backend.inputDeviceId)
                    monospace: true
                }
                StatRow {
                    label: "Resampled to 48 kHz"
                    value: backend.streamInfo.captureResampling ? "Yes" : "No (native 48 kHz)"
                    visible: backend.advancedUi && backend.streamInfo.capture !== undefined && backend.streamInfo.capture.running
                }
            },

            Card {
                title: "3. Headphones (optional)"
                subtitle: "Hear your cleaned voice. Use headphones: through speakers your microphone picks it up again and echoes."
                icon: ""
                VsSwitch {
                    label: "Listen live"
                    checked: backend.monitorEnabled
                    onToggled: (value) => backend.monitorEnabled = value
                }
                Text { text: "Headphones (also used for voice check playback)"; color: Theme.textSecondary; font.pixelSize: 12 }
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
        ]

        rightContent: [
            Card {
                title: "2. Send your voice to apps"
                subtitle: "VoiceStudio plays your cleaned voice into the virtual cable. Call apps pick it up from there."
                icon: ""

                // Status: the one thing that matters here is "is it going to CABLE Input?"
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: statusColumn.implicitHeight + 24
                    radius: Theme.radiusSmall
                    color: page.su.outputOk ? "#0F2A27" : "#2A2210"
                    border.color: page.su.outputOk ? Theme.accent : Theme.warning
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
                                  : !page.su.cableInstalled ? "The virtual cable (VB-CABLE) is not installed"
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
                                  ? "In Zoom, Teams, Discord or Google Meet choose <b>CABLE Output</b> as the microphone."
                                  : !page.su.cableInstalled
                                    ? "Run the VoiceStudio setup again and keep <b>Install VB-CABLE</b> ticked, then restart Windows."
                                    : "Other apps cannot hear you. Click <b>Use CABLE Input</b>."
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                RowLayout {
                    spacing: 10
                    VsButton {
                        visible: !page.su.outputOk && page.su.canFixOutput
                        primary: true
                        text: "Use CABLE Input"
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
                    visible: page.changeOutput || backend.advancedUi
                    Layout.fillWidth: true
                    model: backend.outputDevices
                    textRole: "label"
                    currentId: backend.outputDeviceId
                    placeholder: "Select output (normally CABLE Input)"
                    onPicked: (id) => backend.selectOutputDevice(id)
                }
                StatRow {
                    visible: backend.advancedUi
                    label: "Device format"
                    value: backend.streamInfo.output && backend.streamInfo.output.running
                           ? backend.streamInfo.output.format : page.formatOf(backend.outputDevices, backend.outputDeviceId)
                    monospace: true
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
            },

            Card {
                visible: backend.advancedUi
                title: "Active streams"
                subtitle: "What Windows actually negotiated for each endpoint"
                icon: ""
                StatRow { label: "Input"; value: backend.streamInfo.inputName || "-" }
                StatRow {
                    label: "Input period / buffer"
                    value: backend.streamInfo.capture && backend.streamInfo.capture.running
                           ? backend.streamInfo.capture.periodMs + " / " + backend.streamInfo.capture.bufferMs + " ms"
                             + (backend.streamInfo.capture.audioClient3 ? " (low latency)" : "")
                           : "-"
                    monospace: true
                }
                StatRow { label: "Output"; value: backend.streamInfo.outputName || "-" }
                StatRow {
                    label: "Output period / buffer"
                    value: backend.streamInfo.output && backend.streamInfo.output.running
                           ? backend.streamInfo.output.periodMs + " / " + backend.streamInfo.output.bufferMs + " ms"
                             + (backend.streamInfo.output.audioClient3 ? " (low latency)" : "")
                           : "-"
                    monospace: true
                }
                StatRow {
                    label: "Jitter buffer target"
                    value: backend.streamInfo.outputTargetMs > 0 ? backend.streamInfo.outputTargetMs + " ms" : "-"
                    monospace: true
                }
            }
        ]
    }

    Card {
        visible: !backend.cableDetected || backend.advancedUi
        title: "Virtual cable (VB-CABLE)"
        icon: ""
        Text {
            text: backend.cableDetected ? "VB-CABLE is installed."
                                        : "VB-CABLE is not installed, so other apps cannot receive your cleaned voice."
            color: backend.cableDetected ? Theme.success : Theme.warning
            font.pixelSize: 13
        }
        Text {
            textFormat: Text.StyledText
            text: "The VoiceStudio setup installs VB-CABLE for you (keep <b>Install VB-CABLE</b> ticked). To install it by hand:<br>"
                  + "1. Download it from <b>vb-audio.com/Cable</b> (free donationware by VB-Audio Software).<br>"
                  + "2. Extract it, right-click <b>VBCABLE_Setup_x64.exe</b>, choose <i>Run as administrator</i>, then <i>Install Driver</i>.<br>"
                  + "3. Restart Windows, then press <b>Refresh devices</b> above."
            color: Theme.textSecondary
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            lineHeight: 1.3
            Layout.fillWidth: true
        }
    }
}
