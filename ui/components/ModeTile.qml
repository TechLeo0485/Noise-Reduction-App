import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Selectable tile for choosing between mutually exclusive modes.
Rectangle {
    id: root

    property string title
    property string description
    property string badge
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

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 14
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: root.title
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Rectangle {
                visible: root.badge !== ""
                radius: 4
                color: root.available ? Theme.accent : Theme.surfaceHover
                implicitWidth: badgeText.implicitWidth + 12
                implicitHeight: badgeText.implicitHeight + 4
                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: root.badge
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    color: root.available ? Theme.background : Theme.textSecondary
                }
            }
        }
        Text {
            text: root.description
            color: Theme.textSecondary
            font.pixelSize: 12
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
