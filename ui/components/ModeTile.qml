import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Selectable tile for choosing between mutually exclusive modes.
Rectangle {
    id: root

    property string title
    property string description
    property string badge
    property string icon
    property bool selected: false
    property bool available: true
    signal clicked()

    Layout.fillWidth: true
    implicitHeight: content.implicitHeight + 28
    radius: Theme.radiusSmall
    color: selected ? Theme.accentMuted : (mouse.containsMouse && available ? Theme.surfaceHover : Theme.surfaceRaised)
    border.color: selected ? Theme.accent : Theme.border
    border.width: selected ? 2 : 1
    opacity: available ? 1.0 : 0.5
    Behavior on color { ColorAnimation { duration: 90 } }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 5

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                visible: root.icon !== ""
                text: root.icon
                font.family: Theme.iconFont
                font.pixelSize: 14
                color: root.selected ? Theme.accent : Theme.textSecondary
            }
            Text {
                text: root.title
                color: Theme.text
                font.pixelSize: Theme.fontBody + 1
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Rectangle {
                visible: root.badge !== ""
                radius: 5
                color: root.available ? Theme.mutedFor(Theme.success) : Theme.surfaceHover
                implicitWidth: badgeText.implicitWidth + 12
                implicitHeight: badgeText.implicitHeight + 4
                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: root.badge
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.4
                    color: root.available ? Theme.success : Theme.textSecondary
                }
            }
            Rectangle {
                visible: root.selected
                implicitWidth: 18
                implicitHeight: 18
                radius: 9
                color: Theme.accent
                Text {
                    anchors.centerIn: parent
                    text: "\uE73E"
                    font.family: Theme.iconFont
                    font.pixelSize: 10
                    color: Theme.textOnAccent
                }
            }
        }
        Text {
            visible: root.description !== ""
            text: root.description
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.available
        cursorShape: root.available ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }
}
