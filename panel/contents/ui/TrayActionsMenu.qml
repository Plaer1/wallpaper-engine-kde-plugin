pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Menu {
    id: root

    property var actions: []

    popupType: Popup.Window
    implicitWidth: 280

    Repeater {
        model: root.actions
        delegate: MenuItem {
            required property int index
            required property var modelData
            objectName: `tray-menu-action-${index}`
            text: modelData.text
            enabled: modelData.enabled
            ToolTip.visible: hovered && !enabled && Boolean(modelData.stateReason)
            ToolTip.text: modelData.stateReason || ""
            onTriggered: modelData.trigger()
        }
    }
}
