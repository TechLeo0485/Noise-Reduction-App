import QtQuick
import VoiceStudio

// Slider bound to an engine parameter by key. Range, label and unit come from the native
// parameter registry, so the UI cannot drift out of sync with what the engine accepts.
VsSlider {
    id: root

    property string paramKey
    readonly property var spec: backend.paramSpecs[paramKey]

    label: spec ? spec.label : paramKey
    unit: spec ? spec.unit : ""
    from: spec ? spec.min : 0
    to: spec ? spec.max : 1
    stepSize: spec ? spec.step : 0.1
    decimals: stepSize < 1 ? 1 : 0
    value: backend.params[paramKey] !== undefined ? backend.params[paramKey] : 0
    onMoved: (v) => backend.setParam(paramKey, v)
}
