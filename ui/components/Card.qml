import QtQuick
import QtQuick.Layouts
import VoiceStudio

Rectangle {
    id: root

    property string title
    property string subtitle
    property string icon
    default property alias content: body.data

    Layout.fillWidth: true
    Layout.alignment: Qt.AlignTop
    implicitHeight: column.implicitHeight + 40
    color: Theme.surface
    radius: Theme.radius
    border.color: Theme.border
    border.width: 1

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        RowLayout {
            visible: root.title !== ""
            spacing: 10
            Layout.fillWidth: true

            Text {
                visible: root.icon !== ""
                text: root.icon
                font.family: Theme.iconFont
                font.pixelSize: 16
                color: Theme.accent
            }
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                Text {
                    text: root.title
                    color: Theme.text
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: root.subtitle !== ""
                    text: root.subtitle
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: 12
        }
    }
}
