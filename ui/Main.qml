import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

ApplicationWindow {
    id: window

    width: 1240
    height: 820
    minimumWidth: 980
    minimumHeight: 660
    visible: false // shown from C++ (stays hidden when started with Windows into the tray)
    title: "VoiceStudio"
    color: Theme.background
    font.family: Theme.fontFamily

    // Pages are addressed by key; `index` is the position in the StackLayout below. Four pages in the sidebar:
    // Home holds the everyday choices, the others the details. Help is opened from the sidebar footer.
    readonly property var allPages: ({
        home:    { title: "Home", icon: "\uE80F", subtitle: "Your voice at a glance and the choices you change most", index: 0 },
        voice:   { title: "Voice", icon: "\uE9E9", subtitle: "Shape how you sound: look, depth, body, presence and tone", index: 1 },
        cleanup: { title: "Clean-up", icon: "\uE9D9", subtitle: "Remove background noise and other people's voices", index: 2 },
        setup:   { title: "Setup", icon: "\uE713", subtitle: "Microphone, virtual microphone, headphones and delay", index: 3 },
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

    readonly property bool engineOn: backend.engineEnabled
    readonly property bool muted: backend.params.masterMute === true
    readonly property string engState: backend.engineState
    readonly property real delayMs: backend.stats.latencyWithVirtualMicMs !== undefined ? backend.stats.latencyWithVirtualMicMs : -1

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Sidebar ------------------------------------------------------------------
        Rectangle {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            color: Theme.sidebar

            Rectangle { // hairline to the content
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 4

                // Brand
                RowLayout {
                    spacing: 12
                    Layout.topMargin: 8
                    Layout.leftMargin: 6
                    Layout.bottomMargin: 22
                    // The logo (ui/icons/voicestudio.png, drawn by scripts/draw-logo.ps1; its rounded corners are transparent).
                    Image {
                        Layout.preferredWidth: 42
                        Layout.preferredHeight: 42
                        source: "qrc:/icons/voicestudio.png"
                        sourceSize: Qt.size(256, 256) // decoded well above display size: sharp on high-DPI screens
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                    }
                    ColumnLayout {
                        spacing: 0
                        Text { text: "VoiceStudio"; color: Theme.text; font.pixelSize: 17; font.weight: Font.Bold }
                        Text { text: "Clear voice for every call"; color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                    }
                }

                Text {
                    text: "MENU"
                    color: Theme.textDisabled
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.2
                    Layout.leftMargin: 14
                    Layout.bottomMargin: 6
                }

                Repeater {
                    model: window.navKeys
                    NavButton {
                        required property string modelData
                        text: window.allPages[modelData].title
                        icon: window.allPages[modelData].icon
                        active: window.currentInfo === window.allPages[modelData]
                        badge: modelData === "setup" && backend.setup.allOk !== true && window.engineOn ? "!" : ""
                        onClicked: window.currentKey = modelData
                    }
                }

                Item { Layout.fillHeight: true }

                NavButton {
                    text: "Help and guide"
                    icon: window.allPages.help.icon
                    active: window.currentKey === "help"
                    onClicked: window.currentKey = "help"
                }

                // On / off: the one switch for the whole app
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    implicitHeight: powerColumn.implicitHeight + 28
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: window.engineOn ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45) : Theme.border

                    ColumnLayout {
                        id: powerColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 14
                        spacing: 12

                        VsSwitch {
                            label: window.engineOn ? "VoiceStudio is on" : "VoiceStudio is off"
                            description: !window.engineOn ? "Turn on to send your voice"
                                         : window.engState === "Running" ? "Processing your voice"
                                         : window.engState
                            checked: window.engineOn
                            onToggled: (value) => backend.engineEnabled = value
                        }

                        // Master mute
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 38
                            radius: Theme.radiusSmall
                            color: window.muted ? Theme.danger : (muteMouse.containsMouse ? Theme.surfaceHover : Theme.surfaceRaised)
                            border.color: window.muted ? Theme.danger : Theme.borderStrong
                            Behavior on color { ColorAnimation { duration: 90 } }

                            Row {
                                anchors.centerIn: parent
                                spacing: 9
                                Text {
                                    text: window.muted ? "\uEC54" : "\uE720"
                                    font.family: Theme.iconFont
                                    font.pixelSize: 14
                                    color: window.muted ? Theme.textOnAccent : Theme.text
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: window.muted ? "Muted - click to talk" : "Mute microphone"
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.DemiBold
                                    color: window.muted ? Theme.textOnAccent : Theme.text
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            MouseArea {
                                id: muteMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: backend.setParam("masterMute", !window.muted)
                            }
                        }
                    }
                }
            }
        }

        // ---- Content -------------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Header: page title, and live status on the right
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 32
                Layout.rightMargin: 32
                Layout.topMargin: 26
                Layout.bottomMargin: 4
                spacing: 12

                ColumnLayout {
                    spacing: 3
                    Layout.fillWidth: true
                    Text {
                        text: window.currentInfo.title
                        color: Theme.text
                        font.pixelSize: Theme.fontTitle + 2
                        font.weight: Font.Bold
                    }
                    Text {
                        text: window.currentInfo.subtitle
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }

                StatusPill {
                    visible: window.engineOn && window.delayMs > 0
                    icon: "\uE916"
                    text: "Delay " + Math.round(window.delayMs) + " ms"
                    tone: window.delayMs < 150 ? Theme.success : window.delayMs < Theme.latencyBudgetMs * 0.85 ? Theme.accent : Theme.warning
                    clickable: true
                    onClicked: window.currentKey = "setup"
                }
                StatusPill {
                    text: !window.engineOn ? "Off"
                          : window.muted ? "Muted"
                          : window.engState === "Running" ? "Live"
                          : window.engState
                    tone: !window.engineOn ? Theme.textSecondary
                          : window.muted ? Theme.danger
                          : Theme.stateColor(window.engState)
                    pulse: window.engineOn && !window.muted && window.engState === "Running"
                }
            }

            // Actionable routing / engine warnings
            Rectangle {
                visible: warningText.text !== ""
                Layout.fillWidth: true
                Layout.leftMargin: 32
                Layout.rightMargin: 32
                Layout.topMargin: 14
                implicitHeight: warningRow.implicitHeight + 22
                radius: Theme.radiusSmall
                color: Theme.warningMuted
                border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.4)
                RowLayout {
                    id: warningRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 14
                    spacing: 12
                    Text {
                        text: "\uE7BA"
                        font.family: Theme.iconFont
                        font.pixelSize: 14
                        color: Theme.warning
                        Layout.alignment: Qt.AlignTop
                    }
                    Text {
                        id: warningText
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
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
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 6
                currentIndex: window.currentInfo.index

                HomeScreen { onOpenPage: (key) => window.showPage(key) }  // 0
                VoiceScreen {}                                            // 1
                CleanupScreen { onOpenPage: (key) => window.showPage(key) } // 2
                SetupScreen { onOpenPage: (key) => window.showPage(key) } // 3
                HelpScreen {}                                             // 4
            }
        }
    }

    UpdateDialogs {}
}
