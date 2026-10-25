import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Strict voice isolation: on/off switch, voice enrollment (record / re-record / delete) and, with
// `detailed`, the measured cost. Used on the Sound screen (simple mode) and the Noise Reduction screen.
Card {
    id: root

    property bool detailed: false

    readonly property var iso: backend.isolation
    readonly property bool on: backend.params.isolationEnabled === true
    readonly property string phase: iso.phase !== undefined ? iso.phase : "idle"
    readonly property bool recording: phase === "recording"
    readonly property bool processing: iso.busy === true
    readonly property bool canRecord: (iso.state === "ready" || iso.state === "needsEnrollment")
                                      && backend.engineRunning && phase === "idle" && !processing

    title: "Strict voice isolation"
    subtitle: "Removes other people's voices, even when they talk at the same time as you"
    icon: ""

    VsSwitch {
        label: "Only send my voice"
        description: "Uses your recorded voice to keep you and remove everyone else. If isolation cannot run, "
                     + "your microphone is silenced instead - other voices are never passed through."
        enabled: root.iso.available === true || root.on
        checked: root.on
        onToggled: (value) => backend.setParam("isolationEnabled", value)
    }

    Rectangle {
        visible: root.iso.silencing === true
        Layout.fillWidth: true
        implicitHeight: silenceText.implicitHeight + 20
        radius: Theme.radiusSmall
        color: "#2A2210"
        border.color: Theme.warning
        Text {
            id: silenceText
            anchors.fill: parent
            anchors.margins: 10
            wrapMode: Text.WordWrap
            color: Theme.warning
            font.pixelSize: 12
            text: root.iso.enrolled !== true && root.iso.state !== "loading"
                  ? "Your microphone is silenced: record your voice below to start isolation."
                  : root.iso.state === "loading" || root.processing
                    ? "Your microphone is silenced while the isolation model gets ready..."
                    : root.iso.state === "error"
                      ? "Your microphone is silenced: voice isolation is unavailable. Turn it off to use your microphone normally."
                      : "Your microphone is briefly silenced while isolation starts."
        }
    }

    Text {
        visible: text !== ""
        text: root.iso.message !== undefined ? root.iso.message : ""
        color: root.iso.state === "error" ? Theme.danger : Theme.textSecondary
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    Text {
        visible: root.iso.microphoneMismatch === true
        text: "Your voice was recorded with \"" + root.iso.enrolledMicrophone + "\", but you are now using \""
              + backend.inputName + "\". Re-record your voice with this microphone for the best isolation."
        color: Theme.warning
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    // ---- Voice enrollment -------------------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        Text {
            text: root.iso.enrolled === true ? "" : ""
            font.family: Theme.iconFont
            font.pixelSize: 14
            color: root.iso.enrolled === true ? Theme.success : Theme.warning
        }
        Text {
            Layout.fillWidth: true
            text: root.iso.enrolled === true ? "Your voice is saved on this computer"
                                             : "No voice recorded yet"
            color: Theme.text
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
    }

    ColumnLayout {
        visible: root.recording
        Layout.fillWidth: true
        spacing: 8
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.text
            font.pixelSize: 13
            font.italic: true
            text: "\"Hello, this is my voice. I am recording a short sample so that only I am heard during my calls. "
                  + "I will keep speaking at my normal pace and volume, just like in a real conversation, "
                  + "until the bar below is full.\""
        }
        ProgressBarThin { value: root.iso.recordProgress !== undefined ? root.iso.recordProgress : 0 }
        Text {
            text: "Level " + (root.iso.recordLevelDb !== undefined ? root.iso.recordLevelDb.toFixed(0) : "-") + " dBFS"
                  + (root.iso.recordLevelDb < -40 ? "  - speak louder or move closer" : "")
            color: root.iso.recordLevelDb < -40 ? Theme.warning : Theme.textSecondary
            font.pixelSize: 11
            font.family: "Consolas"
        }
    }

    Text {
        visible: root.processing
        text: "Preparing your voice and measuring this computer's speed..."
        color: Theme.textSecondary
        font.pixelSize: 12
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }

    RowLayout {
        spacing: 10
        VsButton {
            visible: !root.recording
            primary: true
            enabled: root.canRecord
            text: root.iso.enrolled === true ? "Re-record my voice" : "Record my voice (10 s)"
            onClicked: backend.startVoiceEnrollment()
        }
        VsButton {
            visible: root.recording
            text: "Cancel"
            onClicked: backend.cancelVoiceEnrollment()
        }
        VsButton {
            visible: root.iso.enrolled === true && !root.recording
            danger: true
            enabled: !root.processing
            text: "Delete my voice"
            onClicked: backend.deleteVoiceEnrollment()
        }
    }

    Text {
        text: "Record alone, in a quiet room, with the microphone you will use in the call. The sample never leaves "
              + "this computer. Your voice reaches others about " + Theme.fmt(backend.stats.isolationLatencyMs > 0
              ? backend.stats.isolationLatencyMs : 80, 0, "ms") + " later while isolation is on."
        color: Theme.textSecondary
        font.pixelSize: 11
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    // ---- Technical details --------------------------------------------------------------------------
    ColumnLayout {
        visible: root.detailed
        Layout.fillWidth: true
        spacing: 6
        StatRow { label: "Engine"; value: root.iso.backend !== undefined && root.iso.backend !== "" ? root.iso.backend : "-" }
        StatRow {
            label: "Chunk size (calibrated)"
            value: root.iso.framesPerRun > 0 ? root.iso.chunkMs + " ms, " + root.iso.threads + " threads" : "-"
            monospace: true
        }
        StatRow { label: "Isolation latency (measured)"; value: root.on ? Theme.fmt(backend.stats.isolationLatencyMs, 1, "ms") : "-"; monospace: true }
        StatRow { label: "Inference per chunk (P50)"; value: Theme.fmt(backend.stats.isolationInferenceP50Ms, 2, "ms"); monospace: true }
        StatRow { label: "Inference per chunk (P99)"; value: Theme.fmt(backend.stats.isolationInferenceP99Ms, 2, "ms"); monospace: true }
        StatRow {
            label: "Late chunks (silence inserted)"
            value: backend.stats.isolationUnderruns !== undefined ? String(backend.stats.isolationUnderruns) : "n/a"
            valueColor: backend.stats.isolationUnderruns > 0 ? Theme.warning : Theme.text
            monospace: true
        }
        StatRow {
            label: "Inference errors"
            value: backend.stats.isolationModelFailures !== undefined ? String(backend.stats.isolationModelFailures) : "n/a"
            valueColor: backend.stats.isolationModelFailures > 0 ? Theme.danger : Theme.text
            monospace: true
        }
    }
}
