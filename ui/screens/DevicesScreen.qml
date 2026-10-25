import QtQuick
import QtQuick.Layouts
import VoiceStudio

ScreenPage {
    id: page

    function formatOf(list, id) {
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === id) return list[i].format
        return ""
    }

    RowLayout {
        Layout.fillWidth: true
        Text {
            text: backend.inputDevices.length + " microphones, " + backend.outputDevices.length + " output devices"
            color: Theme.textSecondary
            font.pixelSize: 12
            Layout.fillWidth: true
        }
        VsButton { text: "Refresh devices"; onClicked: backend.refreshDevices() }
    }

    TwoColumns {
        wide: page.wide

        leftContent: [
            Card {
                title: "Microphone"
                subtitle: "The microphone you speak into (USB, headset, built-in or Bluetooth). Not a virtual device."
                icon: ""
                VsSwitch {
                    label: "Follow Windows default microphone"
                    description: "Switches automatically when you change the default device in Windows"
                    checked: backend.inputFollowDefault
                    onToggled: (value) => backend.inputFollowDefault = value
                }
                VsComboBox {
                    Layout.fillWidth: true
                    enabled: !backend.inputFollowDefault
                    model: backend.inputDevices
                    textRole: "label"
                    currentId: backend.inputDeviceId
                    placeholder: backend.inputFollowDefault ? "Windows default" : "Select a microphone"
                    onPicked: (id) => backend.selectInputDevice(id)
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
                title: "Headphones (hear yourself)"
                subtitle: "Hear your cleaned voice. Use headphones: through speakers your microphone picks it up again and echoes."
                icon: ""
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
                title: "Send my voice to apps"
                subtitle: "Choose \"CABLE Input\" here. Then, in Discord, Zoom, Teams or your game, choose \"CABLE Output\" as the microphone."
                icon: ""
                VsComboBox {
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
                    primary: true
                    enabled: !backend.cableTest.running
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
                icon: ""
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
        title: "Virtual cable setup (VB-CABLE)"
        icon: ""
        Text {
            text: backend.cableDetected
                  ? "A virtual audio cable was detected."
                  : "No virtual audio cable detected. VoiceStudio does not install a driver itself."
            color: backend.cableDetected ? Theme.success : Theme.warning
            font.pixelSize: 13
        }
        Text {
            textFormat: Text.StyledText
            text: "1. Download VB-CABLE from <b>vb-audio.com/Cable</b> (free; donationware, from VB-Audio).<br>"
                  + "2. Extract it, right-click <b>VBCABLE_Setup_x64.exe</b> and choose <i>Run as administrator</i>, then <i>Install Driver</i>.<br>"
                  + "3. Reboot, then press <b>Refresh devices</b> above.<br>"
                  + "4. Set <b>Processed output</b> to <b>CABLE Input</b> (a playback device).<br>"
                  + "5. In Discord / Zoom / Teams / your game, select <b>CABLE Output</b> (a recording device) as the microphone.<br>"
                  + "6. Press <b>Test virtual microphone</b> to confirm audio passes through the cable."
            color: Theme.textSecondary
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            lineHeight: 1.3
            Layout.fillWidth: true
        }
    }
}
