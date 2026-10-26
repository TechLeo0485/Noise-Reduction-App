pragma Singleton
import QtQuick

QtObject {
    // ---- Colours: graphite surfaces, one indigo accent, tinted status colours ----
    readonly property color background: "#0E1016"
    readonly property color sidebar: "#0A0C11"
    readonly property color surface: "#151821"
    readonly property color surfaceRaised: "#1C202B"
    readonly property color surfaceHover: "#242936"
    readonly property color border: "#232836"
    readonly property color borderStrong: "#30364A"
    readonly property color accent: "#6E7CFF"
    readonly property color accentHover: "#8C97FF"
    readonly property color accentMuted: "#1D2147"
    readonly property color textOnAccent: "#FFFFFF"     // text and icons on accent / status fills
    readonly property color text: "#ECEEF4"
    readonly property color textSecondary: "#9299AD"
    readonly property color textDisabled: "#565D71"
    readonly property color success: "#3DCB8C"
    readonly property color successMuted: "#112A20"
    readonly property color warning: "#F2B544"
    readonly property color warningMuted: "#2A2313"
    readonly property color danger: "#F2575F"
    readonly property color dangerMuted: "#2E1619"

    // ---- Shape and type ----
    readonly property int radius: 14
    readonly property int radiusSmall: 10
    readonly property int spacing: 16
    readonly property string fontFamily: "Segoe UI"
    readonly property string monoFamily: "Consolas"
    readonly property string iconFont: "Segoe MDL2 Assets"
    readonly property int fontTitle: 22
    readonly property int fontHeading: 15
    readonly property int fontBody: 13
    readonly property int fontSmall: 12
    readonly property int fontCaption: 11

    // The whole delay from the microphone to the app recording the virtual microphone stays below this.
    readonly property real latencyBudgetMs: 300

    // Meter scale: -60 dBFS .. 0 dBFS
    function dbToFraction(db) {
        return Math.max(0, Math.min(1, (db + 60) / 60))
    }

    // Plain-language reading of a speech peak level.
    function levelWord(db) {
        if (db <= -119) return "Silent"
        if (db < -45) return "Quiet"
        if (db < -12) return "Good"
        if (db < -3) return "Loud"
        return "Too loud"
    }
    function levelColor(db) {
        if (db < -45) return textSecondary
        if (db < -12) return success
        if (db < -3) return warning
        return danger
    }

    function stateColor(state) {
        if (state === "Running") return success
        if (state === "Running (no output)" || state === "Recovering" || state === "Starting") return warning
        if (state === "Error") return danger
        return textDisabled
    }
    function mutedFor(c) {
        if (c === success) return successMuted
        if (c === warning) return warningMuted
        if (c === danger) return dangerMuted
        if (c === accent) return accentMuted
        return surfaceRaised
    }

    function fmt(value, decimals, unit) {
        if (value === undefined || value === null || value < 0) return "n/a"
        return Number(value).toFixed(decimals) + (unit ? " " + unit : "")
    }
    function signed(value, decimals, unit) {
        const v = Number(value)
        const s = (v > 0.0001 ? "+" : v < -0.0001 ? "−" : "") + Math.abs(v).toFixed(decimals)
        return s + (unit ? " " + unit : "")
    }
}
