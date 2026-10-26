import QtQuick
import QtQuick.Layouts
import VoiceStudio

// One line of the setup checklist: a status icon, a plain-language explanation and an
// optional one-click fix.
RowLayout {
    id: root

    property string status: "pending"   // "ok" | "problem" | "pending"
    property string title
    property string detail
    property string actionText
    signal actionClicked()

    readonly property color statusColor: status === "ok" ? Theme.success : status === "problem" ? Theme.warning : Theme.textSecondary

    Layout.fillWidth: true
    spacing: 14

    Rectangle {
        Layout.alignment: Qt.AlignTop
        Layout.topMargin: 1
        implicitWidth: 26
        implicitHeight: 26
        radius: 13
        color: Theme.mutedFor(root.statusColor)
        border.color: root.status === "pending" ? Theme.borderStrong : "transparent"
        Text {
            anchors.centerIn: parent
            // Segoe MDL2: CheckMark / Important / More
            text: root.status === "ok" ? "\uE73E" : root.status === "problem" ? "\uE7BA" : "\uE712"
            font.family: Theme.iconFont
            font.pixelSize: 12
            color: root.statusColor
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Text {
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontBody + 1
            font.weight: Font.DemiBold
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Text {
            visible: root.detail !== ""
            text: root.detail
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
    }

    VsButton {
        visible: root.actionText !== ""
        Layout.alignment: Qt.AlignVCenter
        text: root.actionText
        primary: true
        onClicked: root.actionClicked()
    }
}
