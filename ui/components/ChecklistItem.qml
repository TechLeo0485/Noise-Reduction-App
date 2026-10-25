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

    Layout.fillWidth: true
    spacing: 14

    Rectangle {
        Layout.alignment: Qt.AlignTop
        Layout.topMargin: 2
        width: 26
        height: 26
        radius: 13
        color: root.status === "ok" ? Theme.success : root.status === "problem" ? Theme.warning : Theme.surfaceHover
        Text {
            anchors.centerIn: parent
            // Segoe MDL2: CheckMark / Important / More
            text: root.status === "ok" ? "" : root.status === "problem" ? "" : ""
            font.family: Theme.iconFont
            font.pixelSize: 12
            color: root.status === "pending" ? Theme.textSecondary : Theme.background
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Text {
            text: root.title
            color: Theme.text
            font.pixelSize: 14
            font.weight: Font.DemiBold
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Text {
            visible: root.detail !== ""
            text: root.detail
            color: Theme.textSecondary
            font.pixelSize: 12
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
