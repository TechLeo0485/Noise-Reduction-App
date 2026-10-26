import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

// Primary (filled accent), secondary (outlined) or danger button, with an optional icon glyph.
Button {
    id: control

    property bool primary: false
    property bool danger: false
    property string iconText

    font.pixelSize: Theme.fontBody
    font.weight: Font.DemiBold
    implicitHeight: 36
    leftPadding: 16
    rightPadding: 16
    hoverEnabled: true

    contentItem: RowLayout {
        spacing: 8
        opacity: control.enabled ? 1.0 : 0.4
        Text {
            visible: control.iconText !== ""
            text: control.iconText
            font.family: Theme.iconFont
            font.pixelSize: 13
            color: label.color
            Layout.alignment: Qt.AlignVCenter
        }
        Text {
            id: label
            text: control.text
            font: control.font
            color: control.primary ? Theme.textOnAccent : (control.danger ? Theme.danger : Theme.text)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.primary
               ? (control.down ? Qt.darker(Theme.accent, 1.15) : (control.hovered ? Theme.accentHover : Theme.accent))
               : (control.down ? Theme.surface : (control.hovered ? Theme.surfaceHover : Theme.surfaceRaised))
        border.color: control.primary ? "transparent" : (control.danger ? Qt.darker(Theme.danger, 1.4) : Theme.borderStrong)
        opacity: control.enabled ? 1.0 : 0.6
        Behavior on color { ColorAnimation { duration: 90 } }
    }
}
