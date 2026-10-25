import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Everyday page: is everything working, how do I sound, the two choices people change most
// (voice character and noise removal) and a quick listen. Details live on the other pages.
ScreenPage {
    id: page

    signal openPage(string key)

    readonly property var su: backend.setup
    readonly property bool allOk: su.allOk === true
    readonly property bool muted: backend.params.masterMute === true
    readonly property string checkState: backend.voiceCheck.state !== undefined ? backend.voiceCheck.state : "idle"
    readonly property bool checkBusy: checkState === "recording" || checkState === "playing"

    // ---- Status banner ------------------------------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: heroRow.implicitHeight + 40
        radius: Theme.radius
        color: page.allOk ? "#0F2A27" : "#2A2210"
        border.color: page.allOk ? Theme.accent : Theme.warning

        RowLayout {
            id: heroRow
            anchors.fill: parent
            anchors.margins: 20
            spacing: 18

            Rectangle {
                width: 52
                height: 52
                radius: 26
                color: page.allOk ? Theme.accent : Theme.warning
                Text {
                    anchors.centerIn: parent
                    text: page.allOk ? "" : ""
                    font.family: Theme.iconFont
                    font.pixelSize: 22
                    color: Theme.background
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
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
                    font.pixelSize: 13
                    textFormat: Text.StyledText
                    text: !backend.engineEnabled ? "Turn on <b>Audio engine</b> at the top right to start."
                          : page.allOk ? "Your voice is sent to <b>" + page.su.outputName + "</b>. In Discord, Zoom, Teams or "
                                         + "your game, pick the virtual microphone as your microphone (for VB-CABLE: <b>CABLE Output</b>)."
                          : "Follow the steps below. It takes less than a minute."
                }
            }
            VsButton {
                text: page.muted ? "Unmute" : "Mute"
                danger: !page.muted
                primary: page.muted
                implicitHeight: 42
                onClicked: backend.setParam("masterMute", !page.muted)
            }
        }
    }

    // ---- Setup checklist (only while something is missing) ------------------------------------
    Card {
        visible: !page.allOk && backend.engineEnabled
        title: "Setup checklist"
        icon: ""

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

    // ---- Everyday controls -------------------------------------------------------------------
    TwoColumns {
        wide: page.wide

        leftContent: [
            Card {
                title: "Your voice"
                icon: ""
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
                Text {
                    text: "Aim for the microphone meter to reach the middle when you talk normally."
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            },

            Card {
                title: "Voice character"
                subtitle: "How your voice sounds to others"
                icon: ""
                ChipRow {
                    model: backend.voiceLooks
                    current: backend.voiceLook
                    onPicked: (key) => backend.applyVoiceLook(key)
                }
                LinkText {
                    text: backend.voiceLook === "" ? "Custom character - adjust it on the Voice page"
                                                   : "Fine-tune depth, body and tone on the Voice page"
                    onClicked: page.openPage("voice")
                }
            }
        ]

        rightContent: [
            Card {
                title: "Hear yourself"
                subtitle: "Check how you sound to others. Use headphones."
                icon: ""
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    VsButton {
                        primary: !page.checkBusy
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
                        font.pixelSize: 12
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
                    description: "Hear yourself in your headphones while you talk"
                    checked: backend.monitorEnabled
                    onToggled: (value) => backend.monitorEnabled = value
                }
            },

            Card {
                title: "Noise removal"
                subtitle: "Cleans up background noise around you"
                icon: ""
                NoiseLevelPicker {}
                Text {
                    visible: backend.params.isolationEnabled === true
                    text: "Only your voice is sent: strict voice isolation is on."
                    color: Theme.success
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                LinkText {
                    text: "More clean-up options"
                    onClicked: page.openPage("cleanup")
                }
            }
        ]
    }
}
