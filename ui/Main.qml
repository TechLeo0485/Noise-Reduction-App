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

    // Pages are addressed by key; `index` is the position in the StackLayout below. Four pages in the sidebar:
    // Home holds the everyday choices, the others the details. Help is opened from the sidebar footer.
    readonly property var allPages: ({
        home:    { title: "Home", icon: "\uE80F", subtitle: "Is everything working, and the choices you change most", index: 0 },
        voice:   { title: "Voice", icon: "\uE9E9", subtitle: "Fine-tune how you sound: depth, body, presence and tone", index: 1 },
        cleanup: { title: "Clean-up", icon: "\uE9D9", subtitle: "Remove background noise and other people's voices", index: 2 },
        setup:   { title: "Setup", icon: "\uE713", subtitle: "Microphone, virtual microphone, headphones and settings", index: 3 },
        help:    { title: "Help", icon: "\uE897", subtitle: "How to set up VoiceStudio and use it in your calls", index: 4 }
    })
    // Opens a page by key (used by C++, e.g. the "User Guide" shortcut opens "help").
    function showPage(key) {
        if (allPages[key] !== undefined)
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

    readonly property var navKeys: ["home", "voice", "cleanup", "setup"]
    property string currentKey: "home"
    readonly property var currentInfo: allPages[currentKey] !== undefined ? allPages[currentKey] : allPages.home

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

                LinkText {
                    Layout.leftMargin: 8
                    Layout.bottomMargin: 6
                    text: "Help and user guide"
                    onClicked: window.currentKey = "help"
                }

                // On / off: the one switch for the whole app
                Rectangle {
                    id: powerButton
                    readonly property bool on: backend.engineEnabled
                    Layout.fillWidth: true
                    implicitHeight: powerRow.implicitHeight + 22
                    radius: Theme.radiusSmall
                    color: on ? Theme.accentMuted : (powerMouse.containsMouse ? Theme.surfaceHover : Theme.surfaceRaised)
                    border.color: on ? Theme.accent : Theme.border

                    RowLayout {
                        id: powerRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 12
                        spacing: 10
                        Rectangle {
                            implicitWidth: 30
                            implicitHeight: 30
                            radius: 15
                            color: powerButton.on ? Theme.accent : Theme.surfaceHover
                            Text {
                                anchors.centerIn: parent
                                text: "\uE7E8"
                                font.family: Theme.iconFont
                                font.pixelSize: 14
                                color: powerButton.on ? Theme.background : Theme.textSecondary
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: powerButton.on ? "VoiceStudio is on" : "VoiceStudio is off"
                                color: Theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            RowLayout {
                                spacing: 6
                                Rectangle {
                                    visible: powerButton.on
                                    implicitWidth: 7
                                    implicitHeight: 7
                                    radius: 4
                                    color: Theme.stateColor(backend.engineState)
                                }
                                Text {
                                    text: powerButton.on ? backend.engineState : "Click to turn on"
                                    color: Theme.textSecondary
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                    MouseArea {
                        id: powerMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.engineEnabled = !powerButton.on
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

                HomeScreen { onOpenPage: (key) => window.showPage(key) }  // 0
                VoiceScreen {}                                            // 1
                CleanupScreen {}                                          // 2
                SetupScreen { onOpenPage: (key) => window.showPage(key) } // 3
                HelpScreen {}                                             // 4
            }
        }
    }
}
