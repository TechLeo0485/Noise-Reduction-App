import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Record a few seconds and play back exactly what others hear, or listen live (on Home and the Voice page).
Card {
    id: root

    readonly property string checkState: backend.voiceCheck.state !== undefined ? backend.voiceCheck.state : "idle"
    readonly property bool checkBusy: checkState === "recording" || checkState === "playing"

    title: "Hear yourself"
    subtitle: "Use headphones, not speakers"
    icon: ""

    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        VsButton {
            primary: !root.checkBusy
            iconText: root.checkBusy ? "" : ""
            text: root.checkState === "recording" ? "Stop recording"
                  : root.checkState === "playing" ? "Stop playback"
                  : "Record 5 s and play back"
            enabled: backend.engineRunning || root.checkBusy
            onClicked: root.checkBusy ? backend.stopVoiceCheck() : backend.startVoiceCheck(5)
        }
        Text {
            Layout.fillWidth: true
            text: backend.voiceCheck.message !== undefined && backend.voiceCheck.message !== ""
                  ? backend.voiceCheck.message : "Speak for 5 seconds, then listen."
            color: root.checkState === "error" ? Theme.warning
                   : root.checkState === "done" ? Theme.success : Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
    }
    ProgressBarThin {
        visible: root.checkState === "recording"
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
