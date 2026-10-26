import QtQuick
import QtQuick.Layouts
import VoiceStudio

// A small rounded status label with a coloured dot (or icon), e.g. "Live", "Delay 170 ms".
Rectangle {
    id: root

    property string text
    property string icon
    property color tone: Theme.textSecondary
    property bool pulse: false
    signal clicked()
    property bool clickable: false

    implicitHeight: 28
    implicitWidth: row.implicitWidth + 22
    radius: 14
    color: Theme.mutedFor(tone)
    border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.35)

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 7
        Rectangle {
            visible: root.icon === ""
            implicitWidth: 7
            implicitHeight: 7
            radius: 4
            color: root.tone
            SequentialAnimation on opacity {
                running: root.pulse
                loops: Animation.Infinite
                NumberAnimation { to: 0.35; duration: 900; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutSine }
            }
        }
        Text {
            visible: root.icon !== ""
            text: root.icon
            font.family: Theme.iconFont
            font.pixelSize: 12
            color: root.tone
        }
        Text {
            text: root.text
            color: Theme.text
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.clickable
        cursorShape: root.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }
}
