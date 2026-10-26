import QtQuick
import QtQuick.Layouts
import VoiceStudio

// How long your voice takes to reach the other apps, against the 300 ms limit, split by where it comes from.
ColumnLayout {
    id: root

    property bool showLegend: true
    readonly property var s: backend.stats
    readonly property real total: s.latencyWithVirtualMicMs !== undefined ? s.latencyWithVirtualMicMs : -1
    readonly property bool known: total > 0
    function n(v) { return v !== undefined && v > 0 ? v : 0 }
    readonly property real isolationMs: n(s.isolationLatencyMs)
    readonly property var parts: [
        { label: "Microphone", ms: n(s.latencyCapturePeriodMs) + n(s.latencyFramingMs) + n(s.latencyProcessingMs), color: "#8C97FF" },
        { label: "Noise removal and voice", ms: Math.max(0, n(s.latencyAlgorithmicMs) - isolationMs), color: "#6E7CFF" },
        { label: "Only my voice", ms: isolationMs, color: "#A77BFF" },
        { label: "Buffers", ms: n(s.latencyResamplerMs) + n(s.latencyJitterBufferMs) + n(s.latencyRenderQueueMs), color: "#4FA3F7" },
        { label: "Virtual microphone", ms: known ? 25 : 0, color: "#3DCB8C" }
    ]
    readonly property color tone: !known ? Theme.textSecondary : total < 150 ? Theme.success
                                  : total < Theme.latencyBudgetMs * 0.85 ? Theme.accent : Theme.warning

    Layout.fillWidth: true
    spacing: 10

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text {
            text: root.known ? Math.round(root.total) + " ms" : "–"
            color: Theme.text
            font.pixelSize: 24
            font.weight: Font.DemiBold
        }
        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true
            Text {
                text: !root.known ? "Measured while VoiceStudio sends your voice"
                      : root.total < 150 ? "Low delay - conversations feel natural"
                      : root.total < Theme.latencyBudgetMs * 0.85 ? "Fine for calls"
                      : "Close to the limit"
                color: root.tone
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
            }
            Text {
                text: "Limit " + Theme.latencyBudgetMs + " ms - VoiceStudio keeps the delay below it"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontCaption
            }
        }
    }

    // Stacked bar against the budget
    Rectangle {
        id: track
        Layout.fillWidth: true
        implicitHeight: 10
        radius: 5
        color: Theme.surfaceHover
        clip: true
        Row {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            Repeater {
                model: root.parts
                Rectangle {
                    required property var modelData
                    width: track.width * Math.min(1, modelData.ms / Theme.latencyBudgetMs)
                    height: track.height
                    color: modelData.color
                    Behavior on width { NumberAnimation { duration: 250 } }
                }
            }
        }
    }

    Flow {
        visible: root.showLegend && root.known
        Layout.fillWidth: true
        spacing: 14
        Repeater {
            model: root.parts
            Row {
                required property var modelData
                visible: modelData.ms >= 0.5
                spacing: 6
                Rectangle {
                    width: 8
                    height: 8
                    radius: 2
                    color: modelData.color
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: modelData.label + "  " + Math.round(modelData.ms) + " ms"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                }
            }
        }
    }
}
