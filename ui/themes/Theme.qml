pragma Singleton
import QtQuick

QtObject {
    readonly property color background: "#111317"
    readonly property color sidebar: "#0B0D10"
    readonly property color surface: "#171A1F"
    readonly property color surfaceRaised: "#1F232A"
    readonly property color surfaceHover: "#272C34"
    readonly property color border: "#2A2F37"
    readonly property color accent: "#1ED6C8"
    readonly property color accentHover: "#4CE8DC"
    readonly property color accentMuted: "#103E3B"
    readonly property color text: "#E9ECEF"
    readonly property color textSecondary: "#9AA4AF"
    readonly property color textDisabled: "#59616B"
    readonly property color success: "#3DDC97"
    readonly property color warning: "#FFB547"
    readonly property color danger: "#FF5C6C"

    readonly property int radius: 12
    readonly property int radiusSmall: 8
    readonly property int spacing: 16
    readonly property string fontFamily: "Segoe UI"
    readonly property string iconFont: "Segoe MDL2 Assets"

    // Meter scale: -60 dBFS .. 0 dBFS
    function dbToFraction(db) {
        return Math.max(0, Math.min(1, (db + 60) / 60))
    }

    function stateColor(state) {
        if (state === "Running") return success
        if (state === "Running (no output)" || state === "Recovering" || state === "Starting") return warning
        if (state === "Error") return danger
        return textDisabled
    }

    function fmt(value, decimals, unit) {
        if (value === undefined || value === null || value < 0) return "n/a"
        return Number(value).toFixed(decimals) + (unit ? " " + unit : "")
    }
}
