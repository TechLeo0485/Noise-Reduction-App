import QtQuick
import QtQuick.Layouts
import VoiceStudio

// One selectable device (radio-style). The selected tile can show a live level bar so the user
// sees immediately that this device hears them.
Rectangle {
    id: root

    property string title
    property string subtitle
    property string tag            // small badge, e.g. "Windows default", "virtual"
    property bool selected: false
    property bool warn: false      // e.g. a virtual device chosen as microphone
    property bool showLevel: false
    property real levelDb: -120
    signal clicked()

    Layout.fillWidth: true
    implicitHeight: content.implicitHeight + 26
    radius: Theme.radiusSmall
    color: selected ? Theme.accentMuted : (mouse.containsMouse ? Theme.surfaceHover : Theme.surfaceRaised)
    border.color: selected ? (warn ? Theme.warning : Theme.accent) : Theme.border
    border.width: selected ? 2 : 1
    Behavior on color { ColorAnimation { duration: 90 } }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: 14
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            // Radio indicator
            Rectangle {
                implicitWidth: 18
                implicitHeight: 18
                radius: 9
                color: "transparent"
                border.color: root.selected ? Theme.accent : Theme.borderStrong
                border.width: 2
                Rectangle {
                    visible: root.selected
                    anchors.centerIn: parent
                    width: 8
                    height: 8
                    radius: 4
                    color: Theme.accent
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: root.title
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                    font.weight: root.selected ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                }
                Text {
                    visible: root.subtitle !== ""
                    Layout.fillWidth: true
                    text: root.subtitle
                    color: root.warn ? Theme.warning : Theme.textSecondary
                    font.pixelSize: Theme.fontCaption + 1
                    wrapMode: Text.WordWrap
                }
            }
            Rectangle {
                visible: root.tag !== ""
                radius: 6
                color: Theme.surface
                border.color: Theme.border
                implicitWidth: tagText.implicitWidth + 14
                implicitHeight: 20
                Text {
                    id: tagText
                    anchors.centerIn: parent
                    text: root.tag
                    color: Theme.textSecondary
                    font.pixelSize: 10
                }
            }
        }

        // Live level of the active microphone
        RowLayout {
            visible: root.showLevel
            Layout.fillWidth: true
            Layout.leftMargin: 30
            spacing: 10
            ProgressBarThin {
                Layout.fillWidth: true
                value: Theme.dbToFraction(root.levelDb)
                barColor: root.levelDb > -45 ? Theme.success : Theme.textSecondary
            }
            Text {
                text: root.levelDb > -45 ? "Hearing you" : "Speak to test"
                color: root.levelDb > -45 ? Theme.success : Theme.textSecondary
                font.pixelSize: Theme.fontCaption
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
