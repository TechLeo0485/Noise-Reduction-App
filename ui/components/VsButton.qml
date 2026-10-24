import QtQuick
import QtQuick.Controls.Basic
import VoiceStudio

Button {
    id: control

    property bool primary: false
    property bool danger: false

    font.pixelSize: 13
    implicitHeight: 36
    leftPadding: 16
    rightPadding: 16

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.primary ? Theme.background : (control.danger ? Theme.danger : Theme.text)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        opacity: control.enabled ? 1.0 : 0.4
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.primary
               ? (control.down ? Theme.accentMuted : (control.hovered ? Theme.accentHover : Theme.accent))
               : (control.down ? Theme.surface : (control.hovered ? Theme.surfaceHover : Theme.surfaceRaised))
        border.color: control.primary ? "transparent" : (control.danger ? Theme.danger : Theme.border)
    }
}
