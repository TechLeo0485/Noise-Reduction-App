import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Recording your voice for "Only send my voice": as long as you like, guided through normal, loud, quiet,
// high, low and expressive speech. "Add more" continues the guide where the saved recording left off and
// adds to it, so the voice can be built up over several sessions. VoiceStudio then tests which parts of the
// whole recording recognise you best (see VoiceIsolator::enroll).
Card {
    id: root

    readonly property var iso: backend.isolation
    readonly property string phase: iso.phase !== undefined ? iso.phase : "idle"
    readonly property bool recording: phase === "recording"
    readonly property bool processing: iso.busy === true
    readonly property bool enrolled: iso.enrolled === true
    readonly property real seconds: iso.recordSeconds !== undefined ? iso.recordSeconds : 0
    readonly property real minSeconds: iso.recordMinSeconds !== undefined ? iso.recordMinSeconds : 5
    readonly property real savedSeconds: iso.savedSeconds !== undefined ? iso.savedSeconds : 0
    readonly property bool appending: iso.appending === true
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
    readonly property int lastStep: steps.length - 1
    // Styles the saved recording already covers (the guide runs in order, one step per 20 s).
    readonly property int savedSteps: Math.min(Math.floor(savedSeconds / stepSeconds), steps.length)
    // Adding more continues the guide after the covered styles.
    readonly property int stepOffset: appending ? Math.min(savedSteps, lastStep) : 0
    readonly property int step: Math.min(stepOffset + Math.floor(seconds / stepSeconds), lastStep)

    function clock(s) {
        const total = Math.floor(s)
        const m = Math.floor(total / 60)
        const r = total % 60
        return m + ":" + (r < 10 ? "0" : "") + r
    }

    title: recording ? "Recording" : "My voice"
    subtitle: recording ? "Follow the guide. Press Done whenever you like."
              : enrolled ? "Saved on this computer - " + clock(savedSeconds) + " recorded"
              : "Record once so VoiceStudio knows it is you"
    icon: ""

    // ---- Idle: what is saved, and what to do next ----------------------------------------------
    ColumnLayout {
        visible: !root.recording && !root.processing
        Layout.fillWidth: true
        spacing: 10

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: 12
            lineHeight: 1.3
            text: root.enrolled
                  ? "The more of your voice VoiceStudio hears - loud, quiet, high, low - the better it keeps all of it. "
                    + "Add more any time; it builds on what you already recorded."
                  : "A short guide takes you through your normal voice, then louder, quieter, higher, lower and with "
                    + "feeling. 1 to 3 minutes works great. Record alone, in a quiet room, with the microphone you use in calls."
        }

        // Styles covered so far
        Flow {
            visible: root.enrolled
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: root.steps
                Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool done: index < root.savedSteps
                    width: coveredLabel.implicitWidth + 20
                    height: 26
                    radius: 13
                    color: done ? Theme.accentMuted : Theme.surfaceRaised
                    border.color: done ? Theme.accent : Theme.border
                    Text {
                        id: coveredLabel
                        anchors.centerIn: parent
                        text: (parent.done ? "✓ " : "") + modelData.title
                        color: parent.done ? Theme.accent : Theme.textSecondary
                        font.pixelSize: 11
                    }
                }
            }
        }

        Text {
            visible: !root.canRecord
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.warning
            font.pixelSize: 12
            text: !backend.engineRunning ? "Turn VoiceStudio on to record."
                  : root.iso.state === "loading" ? "Getting ready..."
                  : root.iso.available !== true ? "Voice recording is not available: " + (root.iso.message || "")
                  : ""
        }
        Text {
            visible: root.iso.microphoneMismatch === true
            text: "Your voice was recorded with \"" + root.iso.enrolledMicrophone + "\", but you are now using \""
                  + backend.inputName + "\". Start over with this microphone for the best results."
            color: Theme.warning
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: 10
            VsButton {
                primary: true
                enabled: root.canRecord
                text: root.enrolled ? "Add more" : "Start recording"
                implicitHeight: 40
                onClicked: backend.startVoiceEnrollment(root.enrolled)
            }
            VsButton {
                visible: root.enrolled
                enabled: root.canRecord
                text: "Start over"
                implicitHeight: 40
                onClicked: backend.startVoiceEnrollment(false)
            }
            Item { Layout.fillWidth: true }
            LinkText { text: "Open folder"; onClicked: backend.openVoiceFolder() }
            LinkText {
                visible: root.enrolled
                text: "Delete"
                color: Theme.danger
                onClicked: backend.deleteVoiceEnrollment()
            }
        }
    }

    // ---- Recording: timer, level and the guide -------------------------------------------------
    ColumnLayout {
        visible: root.recording
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
                    running: root.recording
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 600 }
                    NumberAnimation { to: 1.0; duration: 600 }
                }
            }
            Text {
                text: root.clock(root.seconds)
                color: Theme.text
                font.pixelSize: 30
                font.weight: Font.Bold
                font.family: "Consolas"
            }
            Text {
                visible: root.appending
                text: "+ " + root.clock(root.savedSeconds) + " saved"
                color: Theme.textSecondary
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            VsButton {
                primary: true
                enabled: root.seconds >= root.minSeconds
                text: root.seconds >= root.minSeconds ? "Done" : "Done (in " + Math.ceil(root.minSeconds - root.seconds) + " s)"
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
            label: root.iso.recordLevelDb < -40 ? "Too quiet - move closer or speak up"
                   : root.iso.recordLevelDb > -3 ? "Very loud - this part may distort"
                   : "Level"
            levelDb: root.iso.recordLevelDb !== undefined ? root.iso.recordLevelDb : -120
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
                    text: "Now: " + root.steps[root.step].title
                    color: Theme.accent
                    font.pixelSize: 18
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.steps[root.step].how
                    color: Theme.text
                    font.pixelSize: 13
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "“" + root.steps[root.step].say + "”"
                    color: Theme.text
                    font.pixelSize: 14
                    font.italic: true
                }
                ProgressBarThin {
                    visible: root.step < root.lastStep
                    value: (root.seconds % root.stepSeconds) / root.stepSeconds
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: root.steps
                Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool done: index < root.step || (root.appending && index < root.savedSteps)
                    readonly property bool current: index === root.step
                    width: stepLabel.implicitWidth + 20
                    height: 26
                    radius: 13
                    color: current ? Theme.accent : (done ? Theme.accentMuted : Theme.surfaceRaised)
                    border.color: current || done ? Theme.accent : Theme.border
                    Text {
                        id: stepLabel
                        anchors.centerIn: parent
                        text: (parent.done && !parent.current ? "✓ " : "") + modelData.title
                        color: parent.current ? Theme.background : (parent.done ? Theme.accent : Theme.textSecondary)
                        font.pixelSize: 11
                        font.weight: parent.current ? Font.DemiBold : Font.Normal
                    }
                }
            }
        }
    }

    // ---- Processing ----------------------------------------------------------------------------
    ColumnLayout {
        visible: root.processing
        Layout.fillWidth: true
        spacing: 8
        Text {
            text: root.iso.message !== undefined && root.iso.message !== "" ? root.iso.message
                  : "Preparing your voice..."
            color: Theme.text
            font.pixelSize: 13
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        ProgressBarThin {
            value: root.iso.prepareProgress !== undefined ? root.iso.prepareProgress : 0
        }
    }

    Text {
        visible: text !== "" && !root.recording && !root.processing
        text: root.iso.message !== undefined ? root.iso.message : ""
        color: root.iso.state === "error" ? Theme.danger : Theme.textSecondary
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
}
