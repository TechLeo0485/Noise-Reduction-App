import QtQuick
import QtQuick.Controls.Basic
import VoiceStudio

// Combo box over a JS array of objects. Selection is driven by `currentId` (matched against
// `valueRole`) so it always reflects the backend state. Items with `available === false`
// are shown but cannot be picked.
ComboBox {
    id: control

    property string currentId
    property string placeholder: "Select..."
    signal picked(string id)

    textRole: "name"
    valueRole: "id"
    implicitHeight: 38
    font.pixelSize: 13

    currentIndex: {
        const m = model
        if (!m || m.length === undefined) return -1
        for (let i = 0; i < m.length; ++i) {
            if (m[i][valueRole] === currentId) return i
        }
        return -1
    }
    displayText: currentIndex < 0 ? placeholder : currentText
    onActivated: (index) => control.picked(model[index][valueRole])

    delegate: ItemDelegate {
        id: item
        required property var modelData
        required property int index
        width: control.width
        enabled: modelData.available === undefined || modelData.available === true
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: item.modelData[control.textRole] + (item.enabled ? "" : "  (unavailable)")
            color: item.enabled ? Theme.text : Theme.textDisabled
            font: control.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 6
            color: item.highlighted ? Theme.surfaceHover : "transparent"
        }
    }

    contentItem: Text {
        leftPadding: 12
        rightPadding: control.indicator.width + 12
        text: control.displayText
        font: control.font
        color: control.currentIndex < 0 ? Theme.textSecondary : Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - 12
        y: (control.height - height) / 2
        text: "\uE70D"
        font.family: Theme.iconFont
        font.pixelSize: 10
        color: Theme.textSecondary
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surfaceRaised
        border.color: control.activeFocus || control.hovered ? Theme.accent : Theme.border
        opacity: control.enabled ? 1.0 : 0.5
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        padding: 4

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            color: Theme.surfaceRaised
            radius: Theme.radiusSmall
            border.color: Theme.border
        }
    }
}
