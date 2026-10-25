import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Recording your voice: as long as you like, guided through normal, loud, quiet, high, low and
// expressive speech. VoiceStudio picks the clearest normal speech for voice isolation and keeps the
// whole recording in the voice folder.
ScreenPage {
    id: page

    readonly property var iso: backend.isolation
    readonly property string phase: iso.phase !== undefined ? iso.phase : "idle"
    readonly property bool recording: phase === "recording"
    readonly property bool processing: iso.busy === true
    readonly property real seconds: iso.recordSeconds !== undefined ? iso.recordSeconds : 0
    readonly property real minSeconds: iso.recordMinSeconds !== undefined ? iso.recordMinSeconds : 5
    readonly property bool canRecord: (iso.state === "ready" || iso.state === "needsEnrollment")
                                      && backend.engineRunning && phase === "idle" && !processing

    // One guide step every 20 seconds; after the last one, free talk continues as long as you like.
    readonly property int stepSeconds: 20
    readonly property var steps: [
        { title: "Normal", how: "Talk as you do in a call.",
          say: "Hello, this is my voice. I'm recording it so VoiceStudio knows exactly how I sound." },
        { title: "Louder", how: "Like calling to someone across the room - strong, but don't shout into the mic.",
          say: "Can you hear me over there? I said the meeting starts at ten!" },
        { title: "Quiet", how: "Soft and close, like late at night. Keep your voice on, not a whisper.",
          say: "It's late, so I'll keep my voice down. Let's talk about it tomorrow." },
        { title: "Higher", how: "Lift your pitch a little, bright and friendly.",
          say: "Oh, that's great news! Really? Thank you so much!" },
        { title: "Lower", how: "Deep, slow and calm, from your chest.",
          say: "Let me explain this slowly. First, we check the numbers. Then, we decide." },
        { title: "With feeling", how: "Excited, then serious, then ask a question.",
          say: "We won! ... Okay, now listen carefully. ... Wait, what did you just say?" },
        { title: "Free talk", how: "Talk about anything: your day, a story, a plan. Keep going as long as you like.",
          say: "Tell it in your own words." }
    ]
    readonly property int step: Math.min(Math.floor(seconds / stepSeconds), steps.length - 1)

    function clock(s) {
        const total = Math.floor(s)
        const m = Math.floor(total / 60)
        const r = total % 60
        return m + ":" + (r < 10 ? "0" : "") + r
    }

    TwoColumns {
        wide: page.wide

        leftContent: [
            Card {
                title: page.recording ? "Recording" : "Record your voice"
                subtitle: page.recording ? "Follow the guide. Press Done whenever you like."
                                         : "Talk for as long as you like - 1 to 3 minutes works great"
                icon: ""

                // ---- Idle: what will happen -------------------------------------------------------
                ColumnLayout {
                    visible: !page.recording && !page.processing
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textSecondary
                        font.pixelSize: 12
                        lineHeight: 1.3
                        text: "A short guide takes you through your normal voice, then louder, quieter, higher, lower and with "
                              + "feeling. VoiceStudio picks your clearest everyday speech to recognise you, and saves the "
                              + "whole recording on this computer."
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textSecondary
                        font.pixelSize: 12
                        text: "Record alone, in a quiet room, with the microphone you use in calls."
                    }
                    Text {
                        visible: !page.canRecord && !page.processing
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.warning
                        font.pixelSize: 12
                        text: !backend.engineRunning ? "Turn on the audio engine to record."
                              : page.iso.state === "loading" ? "Getting ready..."
                              : page.iso.available !== true ? "Voice recording is not available: " + (page.iso.message || "")
                              : ""
                    }
                    VsButton {
                        primary: true
                        enabled: page.canRecord
                        text: page.iso.enrolled === true ? "Record again" : "Start recording"
                        implicitHeight: 42
                        onClicked: backend.startVoiceEnrollment()
                    }
                }

                // ---- Recording: timer, level and the guide -------------------------------------
                ColumnLayout {
                    visible: page.recording
                    Layout.fillWidth: true
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Rectangle {
                            implicitWidth: 14
                            implicitHeight: 14
                            radius: 7
                            color: Theme.danger
                            SequentialAnimation on opacity {
                                running: page.recording
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.3; duration: 600 }
                                NumberAnimation { to: 1.0; duration: 600 }
                            }
                        }
                        Text {
                            text: page.clock(page.seconds)
                            color: Theme.text
                            font.pixelSize: 30
                            font.weight: Font.Bold
                            font.family: "Consolas"
                        }
                        Item { Layout.fillWidth: true }
                        VsButton {
                            primary: true
                            enabled: page.seconds >= page.minSeconds
                            text: page.seconds >= page.minSeconds ? "Done" : "Done (in " + Math.ceil(page.minSeconds - page.seconds) + " s)"
                            implicitHeight: 42
                            onClicked: backend.finishVoiceEnrollment()
                        }
                        VsButton {
                            text: "Cancel"
                            implicitHeight: 42
                            onClicked: backend.cancelVoiceEnrollment()
                        }
                    }

                    LevelMeter {
                        label: page.iso.recordLevelDb < -40 ? "Too quiet - move closer or speak up"
                               : page.iso.recordLevelDb > -3 ? "Very loud - this part may distort"
                               : "Level"
                        levelDb: page.iso.recordLevelDb !== undefined ? page.iso.recordLevelDb : -120
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: guide.implicitHeight + 28
                        radius: Theme.radiusSmall
                        color: Theme.accentMuted
                        border.color: Theme.accent
                        ColumnLayout {
                            id: guide
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 6
                            Text {
                                text: "Now: " + page.steps[page.step].title
                                color: Theme.accent
                                font.pixelSize: 18
                                font.weight: Font.Bold
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: page.steps[page.step].how
                                color: Theme.text
                                font.pixelSize: 13
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "“" + page.steps[page.step].say + "”"
                                color: Theme.text
                                font.pixelSize: 14
                                font.italic: true
                            }
                            ProgressBarThin {
                                visible: page.step < page.steps.length - 1
                                value: (page.seconds % page.stepSeconds) / page.stepSeconds
                            }
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: page.steps
                            Rectangle {
                                required property var modelData
                                required property int index
                                readonly property bool done: index < page.step
                                readonly property bool current: index === page.step
                                width: stepLabel.implicitWidth + 20
                                height: 26
                                radius: 13
                                color: current ? Theme.accent : (done ? Theme.accentMuted : Theme.surfaceRaised)
                                border.color: current || done ? Theme.accent : Theme.border
                                Text {
                                    id: stepLabel
                                    anchors.centerIn: parent
                                    text: (parent.done ? "✓ " : "") + modelData.title
                                    color: parent.current ? Theme.background : (parent.done ? Theme.accent : Theme.textSecondary)
                                    font.pixelSize: 11
                                    font.weight: parent.current ? Font.DemiBold : Font.Normal
                                }
                            }
                        }
                    }
                }

                // ---- Processing ---------------------------------------------------------------
                Text {
                    visible: page.processing
                    text: "Preparing your voice and measuring this computer's speed..."
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                Text {
                    visible: text !== "" && !page.recording
                    text: page.iso.message !== undefined ? page.iso.message : ""
                    color: page.iso.state === "error" ? Theme.danger : Theme.textSecondary
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        ]

        rightContent: [
            Card {
                title: "Your saved voice"
                icon: ""

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text {
                        text: page.iso.enrolled === true ? "" : ""
                        font.family: Theme.iconFont
                        font.pixelSize: 14
                        color: page.iso.enrolled === true ? Theme.success : Theme.warning
                    }
                    Text {
                        Layout.fillWidth: true
                        text: page.iso.enrolled === true ? "Saved on this computer" : "No voice recorded yet"
                        color: Theme.text
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                }
                StatRow {
                    visible: page.iso.enrolled === true && page.iso.enrolledMicrophone !== ""
                    label: "Recorded with"
                    value: page.iso.enrolledMicrophone !== undefined ? page.iso.enrolledMicrophone : ""
                }
                Text {
                    visible: page.iso.microphoneMismatch === true
                    text: "You are now using \"" + backend.inputName + "\". Record again with this microphone for the best results."
                    color: Theme.warning
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    text: "Used by \"Only send my voice\" on the Clean-up page. The folder holds your full recording "
                          + "(my_voice_recording.wav) and the 3 seconds chosen for recognition. Nothing is uploaded."
                }
                RowLayout {
                    spacing: 10
                    VsButton { text: "Open folder"; onClicked: backend.openVoiceFolder() }
                    VsButton {
                        visible: page.iso.enrolled === true
                        danger: true
                        enabled: !page.processing && !page.recording
                        text: "Delete my voice"
                        onClicked: backend.deleteVoiceEnrollment()
                    }
                }
            }
        ]
    }
}
