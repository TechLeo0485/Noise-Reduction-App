import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Strict voice isolation: the on/off switch, its status and, under "Technical details", the measured
// cost. Recording the voice it uses lives on the My Voice page (`recordRequested`).
Card {
    id: root

    signal recordRequested()

    readonly property var iso: backend.isolation
    readonly property bool on: backend.params.isolationEnabled === true

    title: "Only send my voice"
    subtitle: "Removes other people's voices, even when they talk at the same time as you"
    icon: ""

    VsSwitch {
        label: "Strict voice isolation"
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
                  ? "Your microphone is silenced: record your voice on the My Voice page to start isolation."
                  : root.iso.state === "loading" || root.iso.busy === true
                    ? "Your microphone is silenced while the isolation model gets ready..."
                    : root.iso.state === "error"
                      ? "Your microphone is silenced: voice isolation is unavailable. Turn it off to use your microphone normally."
                      : "Your microphone is briefly silenced while isolation starts."
        }
    }

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
            text: root.iso.enrolled === true ? "Uses the voice you recorded on this computer"
                                             : "Record your voice first"
            color: Theme.text
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        LinkText {
            text: root.iso.enrolled === true ? "Re-record" : "Record my voice"
            onClicked: root.recordRequested()
        }
    }

    Text {
        visible: root.iso.microphoneMismatch === true
        text: "Your voice was recorded with \"" + root.iso.enrolledMicrophone + "\", but you are now using \""
              + backend.inputName + "\". Re-record it with this microphone for the best isolation."
        color: Theme.warning
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    Expander {
        title: "Technical details"
        hint: root.on ? "Your voice reaches others about " + Theme.fmt(backend.stats.isolationLatencyMs > 0
              ? backend.stats.isolationLatencyMs : 80, 0, "ms") + " later" : ""
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
