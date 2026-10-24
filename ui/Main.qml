import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

ApplicationWindow {
    id: window

    width: 1240
    height: 800
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    title: "VoiceStudio"
    color: Theme.background
    font.family: Theme.fontFamily

    property int currentPage: 0
    readonly property var pages: [
        { title: "Dashboard", icon: "", subtitle: "Live overview of your processed microphone" },
        { title: "Noise Reduction", icon: "", subtitle: "Suppression engine, strength and noise gate" },
        { title: "Voice Effects", icon: "", subtitle: "Equalizer, dynamics, pitch and presets" },
        { title: "Audio Devices", icon: "", subtitle: "Microphone, virtual output and monitoring" },
        { title: "Performance", icon: "", subtitle: "Hardware, processing backend and live measurements" },
        { title: "Advanced", icon: "", subtitle: "Buffers, recovery, logging and diagnostics" }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Sidebar ------------------------------------------------------------------
        Rectangle {
            Layout.preferredWidth: 236
            Layout.fillHeight: true
            color: Theme.sidebar

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 6

                RowLayout {
                    spacing: 10
                    Layout.bottomMargin: 18
                    Layout.topMargin: 6
                    Layout.leftMargin: 6
                    Rectangle {
                        width: 34
                        height: 34
                        radius: 10
                        color: Theme.accent
                        Text {
                            anchors.centerIn: parent
                            text: ""
                            font.family: Theme.iconFont
                            font.pixelSize: 17
                            color: Theme.background
                        }
                    }
                    ColumnLayout {
                        spacing: 0
                        Text { text: "VoiceStudio"; color: Theme.text; font.pixelSize: 17; font.weight: Font.Bold }
                        Text { text: "Local AI voice processing"; color: Theme.textSecondary; font.pixelSize: 11 }
                    }
                }

                Repeater {
                    model: window.pages
                    NavButton {
                        required property var modelData
                        required property int index
                        text: modelData.title
                        icon: modelData.icon
                        active: window.currentPage === index
                        onClicked: window.currentPage = index
                    }
                }

                Item { Layout.fillHeight: true }

                // Engine status
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: statusColumn.implicitHeight + 24
                    radius: Theme.radiusSmall
                    color: Theme.surface
                    border.color: Theme.border

                    ColumnLayout {
                        id: statusColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4
                        RowLayout {
                            spacing: 8
                            Rectangle {
                                width: 8
                                height: 8
                                radius: 4
                                color: Theme.stateColor(backend.engineState)
                            }
                            Text {
                                text: backend.engineState
                                color: Theme.text
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                        Text {
                            text: backend.activeSuppressor !== "" && backend.engineRunning
                                  ? "Suppression: " + backend.activeSuppressor : "Suppression idle"
                            color: Theme.textSecondary
                            font.pixelSize: 11
                        }
                    }
                }

                // Master mute
                Rectangle {
                    id: muteButton
                    readonly property bool muted: backend.params.masterMute === true
                    Layout.fillWidth: true
                    implicitHeight: 44
                    radius: Theme.radiusSmall
                    color: muted ? Theme.danger : (muteMouse.containsMouse ? Theme.surfaceHover : Theme.surfaceRaised)
                    border.color: muted ? Theme.danger : Theme.border

                    Row {
                        anchors.centerIn: parent
                        spacing: 10
                        Text {
                            text: muteButton.muted ? "" : ""
                            font.family: Theme.iconFont
                            font.pixelSize: 15
                            color: muteButton.muted ? Theme.background : Theme.text
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: muteButton.muted ? "Microphone muted" : "Mute microphone"
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: muteButton.muted ? Theme.background : Theme.text
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    MouseArea {
                        id: muteMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.setParam("masterMute", !muteButton.muted)
                    }
                }
            }
        }

        // ---- Content -------------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                Layout.topMargin: 22
                spacing: 16

                ColumnLayout {
                    spacing: 2
                    Layout.fillWidth: true
                    Text {
                        text: window.pages[window.currentPage].title
                        color: Theme.text
                        font.pixelSize: 24
                        font.weight: Font.Bold
                    }
                    Text {
                        text: window.pages[window.currentPage].subtitle
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                }

                VsSwitch {
                    Layout.fillWidth: false
                    Layout.preferredWidth: 190
                    label: "Audio engine"
                    description: backend.engineEnabled ? "Processing enabled" : "Stopped"
                    checked: backend.engineEnabled
                    onToggled: (value) => backend.engineEnabled = value
                }
            }

            // Actionable routing / engine warnings
            Rectangle {
                visible: warningText.text !== ""
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                Layout.topMargin: 14
                implicitHeight: warningText.implicitHeight + 20
                radius: Theme.radiusSmall
                color: "#2A2210"
                border.color: Theme.warning
                Text {
                    id: warningText
                    anchors.fill: parent
                    anchors.margins: 10
                    wrapMode: Text.WordWrap
                    color: Theme.warning
                    font.pixelSize: 12
                    text: {
                        if (backend.engineState === "Error" || backend.engineState === "Recovering" ||
                                backend.engineState === "Running (no output)")
                            return backend.engineMessage
                        return backend.routingWarning
                    }
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.currentPage

                DashboardScreen {}
                NoiseReductionScreen {}
                VoiceEffectsScreen {}
                DevicesScreen {}
                PerformanceScreen {}
                AdvancedScreen {}
            }
        }
    }
}
