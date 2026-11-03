import QtQuick
import QtQuick.Controls

ToolButton {
    id: root

    padding: 0
    implicitWidth: 32
    implicitHeight: 32

    property string statusText: ""
    property string targetText: ""
    property url iconSource
    signal activated()

    icon.source: iconSource
    icon.width: Math.max(1, Math.min(width, height) - 4)
    icon.height: Math.max(1, Math.min(width, height) - 4)
    display: AbstractButton.IconOnly
    Accessible.name: qsTr("Wallpaper Engine status: %1. Open status popup").arg(statusText)
    Accessible.description: targetText
    onClicked: activated()
    Keys.onReturnPressed: event => {
        root.activated()
        event.accepted = true
    }
    Keys.onEnterPressed: event => {
        root.activated()
        event.accepted = true
    }
}
