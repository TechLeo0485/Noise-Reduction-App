import QtQuick
import QtQuick.Layouts
import VoiceStudio

// One voice-isolation model: plain name, the technology behind it, how it works, when to use it and its costs.
// `model` is an entry of backend.isolationModels.
Rectangle {
    id: root

    property var model: ({})
    property bool selected: false
    readonly property bool installed: model.installed === true
    signal clicked()

    Layout.fillWidth: true
    implicitHeight: content.implicitHeight + 32
    radius: Theme.radius
    color: selected ? Theme.accentMuted : (mouse.containsMouse && installed ? Theme.surfaceHover : Theme.surfaceRaised)
    border.color: selected ? Theme.accent : Theme.border
    border.width: selected ? 2 : 1
    Behavior on color { ColorAnimation { duration: 90 } }

    Accessible.role: Accessible.RadioButton
    Accessible.name: (model.name || "") + ", " + (model.engine || "")
    Accessible.checked: selected

    component Fact: Rectangle {
        property string label
        property string value
        implicitWidth: factText.implicitWidth + 16
        implicitHeight: factText.implicitHeight + 8
        radius: 6
        color: Theme.surface
        border.color: Theme.border
        Text {
            id: factText
            anchors.centerIn: parent
            textFormat: Text.StyledText
            text: "<font color='" + Theme.textSecondary + "'>" + parent.label + "</font>  " + parent.value
            color: Theme.text
            font.pixelSize: Theme.fontCaption
        }
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            // Radio mark
            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 2
                implicitWidth: 18
                implicitHeight: 18
                radius: 9
                color: "transparent"
                border.width: 2
                border.color: root.selected ? Theme.accent : Theme.borderStrong
                Rectangle {
                    anchors.centerIn: parent
                    visible: root.selected
                    width: 8
                    height: 8
                    radius: 4
                    color: Theme.accent
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    Layout.fillWidth: true
                    text: root.model.name || ""
                    color: Theme.text
                    font.pixelSize: Theme.fontHeading
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: root.model.engine || ""
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.Wrap
                }
            }
            Rectangle {
                Layout.alignment: Qt.AlignTop
                radius: 5
                color: root.installed ? Theme.mutedFor(Theme.success) : Theme.warningMuted
                implicitWidth: badgeText.implicitWidth + 12
                implicitHeight: badgeText.implicitHeight + 4
                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: root.installed ? (root.model.badge || "") : "NEEDS MODEL FILE"
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.4
                    color: root.installed ? Theme.success : Theme.warning
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.model.howItWorks || ""
            color: Theme.text
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            textFormat: Text.StyledText
            text: "<b>Use it for:</b> " + (root.model.useWhen || "")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Fact { label: "Delay"; value: root.model.delay || "" }
            Fact { label: "CPU"; value: root.model.cpu || "" }
            Fact { label: "Talking at the same time"; value: root.model.overlapping || "" }
        }
        Rectangle {
            visible: !root.installed
            Layout.fillWidth: true
            implicitHeight: hint.implicitHeight + 16
            radius: Theme.radiusSmall
            color: Theme.warningMuted
            Text {
                id: hint
                anchors.fill: parent
                anchors.margins: 8
                text: root.model.installHint || ""
                color: Theme.warning
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.installed
        cursorShape: root.installed ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }
}
