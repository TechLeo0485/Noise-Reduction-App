import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

ScreenPage {
    id: page

    TwoColumns {
        wide: page.wide

        leftContent: [
            Card {
                title: "General"
                icon: ""
                VsSwitch {
                    label: "Audio engine"
                    description: "Turn off to stop using the microphone completely"
                    checked: backend.engineEnabled
                    onToggled: (value) => backend.engineEnabled = value
                }
                VsSwitch {
                    label: "Reconnect automatically"
                    description: "Restart audio by itself after unplugging a device, Bluetooth drop-outs or sleep"
                    checked: backend.autoRecover
                    onToggled: (value) => backend.autoRecover = value
                }
                VsSwitch {
                    label: "Advanced controls"
                    description: "Show detailed screens: noise gate timing, equalizer bands, buffers, performance measurements"
                    checked: backend.advancedUi
                    onToggled: (value) => backend.advancedUi = value
                }
            },

            Card {
                title: "Speed vs. stability"
                subtitle: "Lower delay feels more natural; more stability prevents crackling on busy PCs"
                icon: ""
                VsComboBox {
                    Layout.fillWidth: true
                    model: backend.performanceModes
                    textRole: "label"
                    valueRole: "key"
                    currentId: backend.performanceMode
                    onPicked: (key) => backend.setPerformanceMode(key)
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    text: {
                        const list = backend.performanceModes
                        for (let i = 0; i < list.length; ++i)
                            if (list[i].key === backend.performanceMode) return list[i].description
                        return ""
                    }
                }
            }
        ]

        rightContent: [
            Card {
                title: "Privacy"
                icon: ""
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    lineHeight: 1.3
                    text: "Everything runs on this computer. Your voice is never recorded to disk, never uploaded "
                          + "and there is no account or tracking. The \"Record 5 s\" check keeps audio in memory only "
                          + "and discards it after playback."
                }
            },

            Card {
                title: "Help and troubleshooting"
                icon: ""
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
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    text: "Diagnostics contain your settings and device names only, never audio."
                }
            },

            Card {
                title: "Start over"
                icon: ""
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    text: "Restores every setting to its default. Your own saved voice styles are kept."
                }
                VsButton { text: "Reset all settings"; danger: true; onClicked: confirmReset.open() }
            }
        ]
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
