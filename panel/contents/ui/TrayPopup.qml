pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property var actions: []
    implicitWidth: 280
    implicitHeight: content.implicitHeight
    Layout.minimumWidth: implicitWidth
    Layout.preferredWidth: implicitWidth
    Layout.maximumWidth: implicitWidth
    Layout.minimumHeight: content.implicitHeight
    Layout.preferredHeight: content.implicitHeight
    Layout.maximumHeight: content.implicitHeight

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 4

        Repeater {
            model: root.actions
            delegate: Button {
                required property int index
                required property var modelData
                objectName: `tray-popup-action-${index}`
                Layout.fillWidth: true
                Layout.fillHeight: false
                Layout.preferredHeight: implicitHeight
                Layout.maximumHeight: implicitHeight
                text: modelData.text
                enabled: modelData.enabled
                Accessible.name: text
                onClicked: modelData.trigger()
            }
        }
    }
}
