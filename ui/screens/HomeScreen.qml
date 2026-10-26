import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Everyday page: is everything working, how do I sound, and the choices people change most (how you sound,
// background noise, only my voice, steady voice). The other pages hold the details.
ScreenPage {
    id: page

    signal openPage(string key)

    readonly property var su: backend.setup
    readonly property bool allOk: su.allOk === true
    readonly property bool muted: backend.params.masterMute === true
    readonly property var iso: backend.isolation
    readonly property bool isolationOn: backend.params.isolationEnabled === true
    readonly property bool stabilizerOn: backend.params.stabilizerEnabled === true
    readonly property string checkState: backend.voiceCheck.state !== undefined ? backend.voiceCheck.state : "idle"
    readonly property bool checkBusy: checkState === "recording" || checkState === "playing"
    readonly property color heroTone: !backend.engineEnabled ? Theme.textSecondary
                                      : page.muted ? Theme.danger
                                      : page.allOk ? Theme.success : Theme.warning

    // ---- Status ------------------------------------------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: heroRow.implicitHeight + 44
        radius: Theme.radius
        color: Theme.surface
        border.color: Qt.rgba(page.heroTone.r, page.heroTone.g, page.heroTone.b, 0.35)

        Rectangle { // tinted wash on the left
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(page.heroTone.r, page.heroTone.g, page.heroTone.b, 0.12) }
                GradientStop { position: 0.6; color: "transparent" }
            }
        }

        RowLayout {
            id: heroRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: 24
            spacing: 20

            Rectangle {
                implicitWidth: 56
                implicitHeight: 56
                radius: 28
                color: Theme.mutedFor(page.heroTone)
                border.color: Qt.rgba(page.heroTone.r, page.heroTone.g, page.heroTone.b, 0.5)
                Text {
                    anchors.centerIn: parent
                    text: !backend.engineEnabled ? "\uE7E8" : page.muted ? "\uEC54" : page.allOk ? "\uE73E" : "\uE7BA"
                    font.family: Theme.iconFont
                    font.pixelSize: 22
                    color: page.heroTone
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text {
                    text: !backend.engineEnabled ? "VoiceStudio is turned off"
                          : page.muted ? "Your microphone is muted"
                          : page.allOk ? "You're all set" : "Let's finish setting up"
                    color: Theme.text
                    font.pixelSize: 20
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                    textFormat: Text.StyledText
                    text: !backend.engineEnabled ? "Nothing is sent to your calls until you turn it on."
                          : page.muted ? "Nobody hears you. Unmute in the sidebar to talk again."
                          : page.allOk ? "In Discord, Zoom, Teams, Meet or your game, choose <b><font color='#ECEEF4'>"
                                         + page.su.appMicName + "</font></b> as your microphone."
                          : "Follow the steps below - it takes less than a minute."
                }
            }
            VsButton {
                visible: !backend.engineEnabled
                text: "Turn on"
                iconText: "\uE7E8"
                primary: true
                implicitHeight: 42
                onClicked: backend.engineEnabled = true
            }
            VsButton {
                visible: backend.engineEnabled && page.muted
                text: "Unmute"
                primary: true
                implicitHeight: 42
                onClicked: backend.setParam("masterMute", false)
            }
        }
    }

    // ---- Setup checklist (only while something is missing) ------------------------------------
    Card {
        visible: !page.allOk && backend.engineEnabled
        title: "Setup checklist"
        subtitle: "Each step fixes itself with one click"
        icon: "\uE9D5"
        iconColor: Theme.warning

        ChecklistItem {
            status: page.su.micOk ? "ok" : (page.su.micActive || backend.engineEnabled ? "problem" : "pending")
            title: page.su.micOk ? "Microphone: " + page.su.micName
                   : page.su.micIsVirtual ? "A virtual device is selected instead of your microphone"
                   : "No microphone is active"
            detail: page.su.micOk ? "This is the microphone VoiceStudio listens to."
                    : page.su.micIsVirtual ? "\"" + page.su.micName + "\" is not a real microphone, so nothing you say reaches VoiceStudio."
                    : page.su.hasPhysicalMic ? "Choose your microphone to continue."
                    : "Connect a microphone (USB, headset or built-in)."
            actionText: !page.su.micOk && page.su.hasPhysicalMic && backend.engineEnabled
                        ? "Use " + page.su.suggestedMicName : ""
            onActionClicked: backend.fixMicrophone()
        }

        ChecklistItem {
            status: page.su.cableInstalled ? "ok" : "problem"
            title: page.su.cableInstalled ? "Virtual microphone found" : "No virtual microphone found"
            detail: page.su.cableInstalled ? "This is how other apps receive your voice."
                    : page.su.virtualMic !== undefined && page.su.virtualMic !== "notInstalled"
                      ? "The VoiceStudio Virtual Microphone is installed but not working yet - Setup explains why."
                    : "Install VB-CABLE (free) and restart your PC, or mark your own virtual microphone in Setup."
            actionText: page.su.cableInstalled ? "" : "Open Setup"
            onActionClicked: page.openPage("setup")
        }

        ChecklistItem {
            status: !page.su.cableInstalled ? "pending" : page.su.outputOk ? "ok" : "problem"
            title: page.su.outputOk ? "Sending your voice to the virtual microphone"
                   : page.su.outputName !== "" ? "Your voice is going to \"" + page.su.outputName + "\""
                   : "Your voice is not being sent anywhere"
            detail: page.su.outputOk ? "In your call app, pick the virtual microphone as your microphone."
                    : page.su.outputName !== "" ? "That is a speaker, so other apps cannot hear you. Send it to the virtual microphone instead."
                    : "Other apps cannot hear you yet."
            actionText: !page.su.outputOk && page.su.canFixOutput ? "Fix it" : ""
            onActionClicked: backend.fixOutput()
        }

        ChecklistItem {
            status: page.su.heard ? "ok" : "pending"
            title: page.su.heard ? "We can hear you" : "Say something to test your microphone"
            detail: page.su.heard ? "Your microphone level is good."
                    : "The \"Your microphone\" meter below should move when you speak."
        }
    }

    // ---- Levels and how you sound --------------------------------------------------------------
    CardGrid {
        wide: page.wide

        Card {
            Layout.fillHeight: true
            title: "Live levels"
            subtitle: "Talk normally - the microphone should read \"Good\""
            icon: "\uE720"

            LevelMeter { label: "Your microphone"; levelDb: backend.inputLevelDb }
            LevelMeter { label: "What others hear"; levelDb: backend.outputLevelDb }
            VsSlider {
                label: "Microphone volume"
                unit: "dB"
                from: -24
                to: 24
                stepSize: 0.5
                value: backend.params.inputGainDb !== undefined ? backend.params.inputGainDb : 0
                onMoved: (v) => backend.setParam("inputGainDb", v)
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
            LatencyMeter { showLegend: false }
        }

        Card {
            Layout.fillHeight: true
            title: "Sound like"
            subtitle: "Still you - just the way you want to come across"
            icon: "\uE9E9"
            actions: LinkText { text: "Fine-tune"; onClicked: page.openPage("voice") }

            LookPicker { wide: true; wideColumns: 2 }
        }
    }

    // ---- Quick settings ---------------------------------------------------------------------------
    CardGrid {
        wide: page.wide

        Card {
            Layout.fillHeight: true
            title: "Background noise"
            subtitle: "Fans, keyboards, traffic, hum"
            icon: "\uE9D9"
            actions: LinkText { text: "More"; onClicked: page.openPage("cleanup") }
            NoiseLevelPicker {}
        }

        Card {
            Layout.fillHeight: true
            title: "Only my voice"
            subtitle: "Removes other people talking near you"
            icon: "\uE716"
            actions: LinkText {
                text: page.iso.enrolled === true ? "My voice" : "Record my voice"
                onClicked: page.openPage("cleanup")
            }
            VsSwitch {
                label: page.isolationOn ? "On - only your voice is sent" : "Off"
                description: page.iso.enrolled === true ? "Uses the voice you recorded on this computer."
                             : "Record your voice once on the Clean-up page first."
                enabled: page.iso.available === true || page.isolationOn
                checked: page.isolationOn
                onToggled: (value) => backend.setParam("isolationEnabled", value)
            }
            Text {
                visible: page.iso.silencing === true
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.warning
                font.pixelSize: Theme.fontSmall
                text: page.iso.enrolled !== true && page.iso.state !== "loading"
                      ? "Your microphone is silenced until you record your voice."
                      : "Your microphone is silenced while isolation gets ready..."
            }
        }

        Card {
            Layout.fillHeight: true
            title: "Steady voice at any distance"
            subtitle: "Sounds the same whether you lean in or sit back"
            icon: "\uE81D"
            VsSwitch {
                label: page.stabilizerOn ? "On" : "Off"
                description: page.stabilizerOn
                             ? "Evens out your level and the extra bass of a close microphone, before the voice effects."
                             : "Your level and tone change with your distance to the microphone."
                checked: page.stabilizerOn
                onToggled: (value) => backend.setParam("stabilizerEnabled", value)
            }
            StabilizerReadout { visible: page.stabilizerOn && backend.engineRunning }
        }

        Card {
            Layout.fillHeight: true
            title: "Hear yourself"
            subtitle: "Use headphones, not speakers"
            icon: "\uE7F6"
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                VsButton {
                    primary: !page.checkBusy
                    iconText: page.checkBusy ? "\uE71A" : "\uE7C8"
                    text: page.checkState === "recording" ? "Stop recording"
                          : page.checkState === "playing" ? "Stop playback"
                          : "Record 5 s and play back"
                    enabled: backend.engineRunning || page.checkBusy
                    onClicked: page.checkBusy ? backend.stopVoiceCheck() : backend.startVoiceCheck(5)
                }
                Text {
                    Layout.fillWidth: true
                    text: backend.voiceCheck.message !== undefined && backend.voiceCheck.message !== ""
                          ? backend.voiceCheck.message : "Speak for 5 seconds, then listen."
                    color: page.checkState === "error" ? Theme.warning
                           : page.checkState === "done" ? Theme.success : Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }
            ProgressBarThin {
                visible: page.checkState === "recording"
                value: backend.voiceCheck.progress !== undefined ? backend.voiceCheck.progress : 0
                barColor: Theme.danger
            }
            VsSwitch {
                label: "Listen live"
                description: "Hear yourself while you talk. Headphones are chosen in Setup."
                checked: backend.monitorEnabled
                onToggled: (value) => backend.monitorEnabled = value
            }
        }
    }
}
