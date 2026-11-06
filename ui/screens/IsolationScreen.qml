import QtQuick
import QtQuick.Layouts
import VoiceStudio

// Removing other people's voices: the switch and its status, the recording of your voice that every model uses,
// and the model to isolate with.
ScreenPage {
    id: page

    signal openPage(string key)

    CardGrid {
        wide: page.wide

        VoiceIsolationCard { Layout.fillHeight: true }

        VoiceRecorderCard { Layout.fillHeight: true }
    }

    IsolationModelPicker { wide: page.wide }
}
