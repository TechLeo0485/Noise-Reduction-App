import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Strict voice isolation: the on/off switch, its status and, under "Technical details", the measured
// cost. The voice it uses is recorded in VoiceRecorderCard, shown below it on the Only my voice page.
Card {
    id: root

    readonly property var iso: backend.isolation
    readonly property bool on: backend.params.isolationEnabled === true

    title: "Only send my voice"
    subtitle: "Removes other people's voices, even when they talk at the same time as you"
    icon: "\uE716"

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
        color: Theme.warningMuted
        border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.4)
        Text {
            id: silenceText
            anchors.fill: parent
            anchors.margins: 10
            wrapMode: Text.WordWrap
            color: Theme.warning
            font.pixelSize: 12
            text: root.iso.enrolled !== true && root.iso.state !== "loading"
                  ? "Your microphone is silenced: record your voice below to start isolation."
                  : root.iso.state === "loading" || root.iso.busy === true
                    ? "Your microphone is silenced while the isolation model gets ready..."
                    : root.iso.state === "error"
                      ? "Your microphone is silenced: voice isolation is unavailable. Turn it off to use your microphone normally."
                      : "Your microphone is briefly silenced while isolation starts."
        }
    }

    Expander {
        title: "Technical details"
        hint: root.on ? "Your voice reaches others about " + Theme.fmt(backend.stats.isolationLatencyMs > 0
              ? backend.stats.isolationLatencyMs : 80, 0, "ms") + " later" : ""
        StatRow { label: "Engine"; value: root.iso.backend !== undefined && root.iso.backend !== "" ? root.iso.backend : "-" }
        StatRow {
            label: "Mode"
            value: root.iso.lightFallback === true ? "Voice check only (this computer is too slow for the selected model)"
                   : root.iso.light === true ? "Voice check only"
                   : root.iso.model === "voicefilter" ? "Lightweight filter (VoiceFilter-Lite)"
                   : "Full isolation (VoxLock)"
        }
        StatRow {
            label: "Chunk size (calibrated)"
            value: root.iso.light === true ? "-"
                   : root.iso.framesPerRun > 0 ? root.iso.chunkMs + " ms, " + root.iso.threads + " threads" : "-"
            monospace: true
        }
        StatRow { label: "Isolation latency (measured)"; value: root.on ? Theme.fmt(backend.stats.isolationLatencyMs, 1, "ms") : "-"; monospace: true }
        StatRow {
            label: "Isolation latency limit (300 ms in total)"
            value: Theme.fmt(backend.stats.isolationMaxMs, 0, "ms")
            monospace: true
        }
        StatRow {
            label: "Backlog dropped to stay under the limit"
            value: backend.stats.isolationLatencyCaps !== undefined ? String(backend.stats.isolationLatencyCaps) : "n/a"
            monospace: true
        }
        StatRow {
            label: "Voice check (mutes other voices)"
            value: root.iso.voiceCheck === true
                   ? (root.on && backend.stats.isolationVoiceGateOpen === false ? "muting another voice" : "active")
                   : "off"
            monospace: true
        }
        StatRow {
            label: "Other voices muted"
            value: backend.stats.isolationVoiceCheckMutes !== undefined ? String(backend.stats.isolationVoiceCheckMutes) : "n/a"
            monospace: true
        }
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
