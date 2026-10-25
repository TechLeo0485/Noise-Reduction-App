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
    visible: false // shown from C++ (stays hidden when started with Windows into the tray)
    title: "VoiceStudio"
    color: Theme.background
    font.family: Theme.fontFamily

    // Pages are addressed by key; `index` is the position in the StackLayout below. Every control lives on
    // exactly one page; expert settings sit in collapsed "Fine-tune" / "Technical" sections on that page.
    readonly property var allPages: ({
        home:    { title: "Home", icon: "\uE80F", subtitle: "Check that everything works and control your voice", index: 0 },
        voice:   { title: "Voice", icon: "\uE9E9", subtitle: "How you sound: character (deep, strong, warm...) and tone", index: 1 },
        cleanup: { title: "Clean-up", icon: "\uE9D9", subtitle: "Remove background noise and other people's voices", index: 2 },
        myvoice: { title: "My Voice", icon: "\uE720", subtitle: "Record your voice so VoiceStudio knows it is you", index: 3 },
        setup:   { title: "Setup", icon: "\uE713", subtitle: "Microphone, virtual microphone, headphones and settings", index: 4 },
        help:    { title: "Help", icon: "\uE897", subtitle: "How to set up VoiceStudio and use it in your calls", index: 5 }
    })
    // Opens a page by key (used by C++, e.g. the "User Guide" shortcut opens "help").
    function showPage(key) {
        if (navKeys.indexOf(key) >= 0)
            currentKey = key
    }

    // Closing the window keeps VoiceStudio running in the tray (unless turned off in Settings).
    onClosing: (close) => {
        if (backend.trayAvailable && backend.closeToTray) {
            close.accepted = false
            window.hide()
            backend.notifyHiddenToTray()
        }
    }

    readonly property var navKeys: ["home", "voice", "cleanup", "myvoice", "setup", "help"]
    property string currentKey: "home"
    readonly property var currentInfo: allPages[navKeys.indexOf(currentKey) >= 0 ? currentKey : "home"]

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
                        Text { text: "Clean, strong voice for every call"; color: Theme.textSecondary; font.pixelSize: 11 }
                    }
                }

                Repeater {
                    model: window.navKeys
                    NavButton {
                        required property string modelData
                        text: window.allPages[modelData].title
                        icon: window.allPages[modelData].icon
                        active: window.currentInfo === window.allPages[modelData]
                        onClicked: window.currentKey = modelData
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
                        text: window.currentInfo.title
                        color: Theme.text
                        font.pixelSize: 24
                        font.weight: Font.Bold
                    }
                    Text {
                        text: window.currentInfo.subtitle
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
                        if (backend.engineState === "Error" || backend.engineState === "Recovering")
                            return backend.engineMessage
                        // Home and Setup explain routing problems themselves.
                        if (window.currentInfo === window.allPages.home || window.currentInfo === window.allPages.setup)
                            return ""
                        if (backend.engineState === "Running (no output)")
                            return backend.engineMessage
                        return backend.routingWarning
                    }
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.currentInfo.index

                HomeScreen { onOpenPage: (key) => window.showPage(key) }    // 0
                VoiceScreen {}                                              // 1
                CleanupScreen { onOpenPage: (key) => window.showPage(key) } // 2
                MyVoiceScreen {}                                            // 3
                SetupScreen {}                                              // 4
                HelpScreen {}                                               // 5
            }
        }
    }
}
