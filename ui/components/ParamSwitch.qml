import QtQuick
import VoiceStudio

VsSwitch {
    property string paramKey
    readonly property var spec: backend.paramSpecs[paramKey]

    label: spec ? spec.label : paramKey
    checked: backend.params[paramKey] === true
    onToggled: (value) => backend.setParam(paramKey, value)
}
