import QtQuick
import QtQuick.Layouts
import VoiceStudio

// A content card: optional icon tile, title, subtitle and header actions (e.g. a switch or a link), then the
// content. Content stays at the top when a row stretches the card to match its neighbour.
Rectangle {
    id: root

    property string title
    property string subtitle
    property string icon
    property color iconColor: Theme.accent
    property int padding: 22
    default property alias content: body.data
    property alias actions: actionRow.data

    Layout.fillWidth: true
    Layout.alignment: Qt.AlignTop
    implicitHeight: column.implicitHeight + 2 * padding
    color: Theme.surface
    radius: Theme.radius
    border.color: Theme.border
    border.width: 1

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: root.padding
        spacing: 16

        RowLayout {
            visible: root.title !== ""
            spacing: 12
            Layout.fillWidth: true

            Rectangle {
                visible: root.icon !== ""
                Layout.alignment: Qt.AlignTop
                implicitWidth: 34
                implicitHeight: 34
                radius: 10
                color: Theme.mutedFor(root.iconColor)
                Text {
                    anchors.centerIn: parent
                    text: root.icon
                    font.family: Theme.iconFont
                    font.pixelSize: 15
                    color: root.iconColor
                }
            }
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                Text {
                    text: root.title
                    color: Theme.text
                    font.pixelSize: Theme.fontHeading
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Text {
                    visible: root.subtitle !== ""
                    text: root.subtitle
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
            RowLayout {
                id: actionRow
                Layout.alignment: Qt.AlignTop
                spacing: 8
            }
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: 14
        }
    }
}
