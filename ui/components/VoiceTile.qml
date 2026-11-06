import QtQuick
import QtQuick.Layouts
import VoiceStudio

// A large, clickable voice in the voice changer: round icon, name, one line. Saved voices can be deleted.
Rectangle {
    id: root

    property string title
    property string description
    property string icon: ""
    property bool selected: false
    property bool deletable: false
    signal clicked()
    signal deleteClicked()

    Layout.fillWidth: true
    implicitHeight: content.implicitHeight + 28
    radius: Theme.radius
    color: selected ? Theme.accentMuted : (mouse.containsMouse ? Theme.surfaceHover : Theme.surfaceRaised)
    border.color: selected ? Theme.accent : (mouse.containsMouse ? Theme.borderStrong : Theme.border)
    border.width: selected ? 2 : 1
    Behavior on color { ColorAnimation { duration: 90 } }

    Accessible.role: Accessible.RadioButton
    Accessible.name: title
    Accessible.checked: selected

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 8

        Rectangle {
            implicitWidth: 44
            implicitHeight: 44
            radius: 22
            color: root.selected ? Theme.accent : Theme.mutedFor(Theme.accent)
            Text {
                anchors.centerIn: parent
                text: root.icon
                font.family: Theme.iconFont
                font.pixelSize: 19
                color: root.selected ? Theme.textOnAccent : Theme.accent
            }
        }
        Text {
            Layout.fillWidth: true
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontHeading
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: root.description
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    // Delete (saved voices only), above the tile's own click area.
    Rectangle {
        visible: root.deletable && (mouse.containsMouse || deleteMouse.containsMouse)
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8
        width: 26
        height: 26
        radius: 13
        color: deleteMouse.containsMouse ? Theme.dangerMuted : Theme.surface
        border.color: deleteMouse.containsMouse ? Theme.danger : Theme.border
        Text {
            anchors.centerIn: parent
            text: ""
            font.family: Theme.iconFont
            font.pixelSize: 11
            color: deleteMouse.containsMouse ? Theme.danger : Theme.textSecondary
        }
        MouseArea {
            id: deleteMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.deleteClicked()
        }
    }

    // Selected mark.
    Rectangle {
        visible: root.selected && !(root.deletable && (mouse.containsMouse || deleteMouse.containsMouse))
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 10
        width: 20
        height: 20
        radius: 10
        color: Theme.accent
        Text {
            anchors.centerIn: parent
            text: ""
            font.family: Theme.iconFont
            font.pixelSize: 10
            color: Theme.textOnAccent
        }
    }
}
