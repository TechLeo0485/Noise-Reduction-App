import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VoiceStudio

// The two update dialogs, driven by `updater` (src/app/Updater.h):
// "VoiceStudio X is available" (update now / later / skip) and, after an update, "What's new".
Item {
    id: root
    anchors.fill: parent

    component NotesView: ScrollView {
        id: notesView
        property string markdown
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(notesText.implicitHeight + 8, 320)
        clip: true
        contentWidth: availableWidth
        Text {
            id: notesText
            width: notesView.availableWidth
            textFormat: Text.MarkdownText
            text: notesView.markdown !== "" ? notesView.markdown : "No details were published for this version."
            color: notesView.markdown !== "" ? Theme.text : Theme.textSecondary
            linkColor: Theme.accent
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            lineHeight: 1.2
            onLinkActivated: (link) => Qt.openUrlExternally(link)
        }
    }

    // Opened and closed from `wanted` (a plain visible binding would break once Esc closes the dialog).
    component DialogFrame: Dialog {
        id: frame
        property bool wanted: false
        onWantedChanged: wanted ? open() : close()
        Component.onCompleted: if (wanted) open()
        anchors.centerIn: Overlay.overlay
        width: Math.min(560, root.width - 80)
        modal: true
        closePolicy: Popup.CloseOnEscape
        padding: 22
        topPadding: 6
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.border }
        header: Text {
            text: frame.title
            color: Theme.text
            font.pixelSize: 17
            font.weight: Font.DemiBold
            padding: 22
            bottomPadding: 8
        }
    }

    DialogFrame {
        id: updateDialog
        readonly property bool busy: updater.state === "downloading" || updater.state === "installing"
        title: "VoiceStudio " + updater.latestVersion + " is available"
        wanted: updater.promptVisible
        closePolicy: busy ? Popup.NoAutoClose : Popup.CloseOnEscape
        onRejected: updater.later()

        contentItem: ColumnLayout {
            spacing: 14
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 12
                text: "You have " + updater.currentVersion + ". What's new:"
            }
            NotesView { markdown: updater.notes }

            ColumnLayout {
                visible: updateDialog.busy
                Layout.fillWidth: true
                spacing: 6
                ProgressBarThin { value: updater.state === "installing" ? 1 : updater.progress }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    text: updater.state === "installing"
                          ? "Installing. Windows asks for permission; VoiceStudio closes and starts again by itself."
                          : "Downloading... " + Math.round(updater.progress * 100) + " %"
                }
            }
            Text {
                visible: updater.error !== "" && !updateDialog.busy
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.warning
                font.pixelSize: 12
                text: updater.error
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 10
                LinkText {
                    visible: !updateDialog.busy
                    text: "Skip this version"
                    onClicked: updater.skipVersion()
                }
                Item { Layout.fillWidth: true }
                VsButton {
                    visible: updater.state === "downloading"
                    text: "Cancel"
                    onClicked: updater.cancelDownload()
                }
                VsButton {
                    visible: !updateDialog.busy
                    text: "Later"
                    onClicked: updater.later()
                }
                VsButton {
                    visible: !updateDialog.busy
                    primary: true
                    text: "Update now"
                    onClicked: updater.install()
                }
            }
        }
    }

    DialogFrame {
        id: whatsNewDialog
        title: "VoiceStudio is updated to " + updater.currentVersion
        // After the update dialog of a still newer version, if both are due.
        wanted: updater.whatsNewVisible && !updater.promptVisible
        onRejected: updater.closeWhatsNew()

        contentItem: ColumnLayout {
            spacing: 14
            Text {
                Layout.fillWidth: true
                color: Theme.textSecondary
                font.pixelSize: 12
                text: "What's new in this version:"
            }
            NotesView { markdown: updater.whatsNewNotes }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                LinkText {
                    text: "Open the release page"
                    onClicked: Qt.openUrlExternally(updater.whatsNewUrl)
                }
                Item { Layout.fillWidth: true }
                VsButton {
                    primary: true
                    text: "Got it"
                    onClicked: updater.closeWhatsNew()
                }
            }
        }
    }
}
