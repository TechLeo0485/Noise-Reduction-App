import QtQuick
import QtQuick.Layouts
import VoiceStudio

// The user guide (docs/user-guide.md, compiled into the app) rendered as Markdown.
ScreenPage {
    id: page

    // The long Markdown text re-lays itself out after the page appears; always start at the top.
    function toTop() { page.contentItem.contentY = 0 }
    onVisibleChanged: if (visible) Qt.callLater(toTop)

    Card {
        Text {
            Layout.fillWidth: true
            textFormat: Text.MarkdownText
            text: backend.userGuide()
            color: Theme.text
            linkColor: Theme.accent
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            lineHeight: 1.25
            onLinkActivated: (link) => Qt.openUrlExternally(link)
            HoverHandler { cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor }
        }
    }
}
