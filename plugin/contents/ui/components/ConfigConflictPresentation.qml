pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../js/config-draft-reconciler.mjs" as Draft

ColumnLayout {
    id: root

    // R6 state is read-only here. The host decides how to synchronously update
    // its adapter after a user choice; this component never edits cfg_ exports.
    property var reconcilerState: null
    property var fieldLabels: ({})

    readonly property var conflictKeys: reconcilerState
        ? Draft.conflictKeys(reconcilerState) : []

    signal choiceRequested(string key, string choice)

    visible: conflictKeys.length > 0
    spacing: 8

    function displayValue(value) {
        if (value === undefined) return qsTr("Not set");
        if (value === null) return qsTr("None");
        if (typeof value === "object") {
            try { return JSON.stringify(value); } catch (e) { return String(value); }
        }
        return String(value);
    }

    Label {
        objectName: "conflictHeading"
        Layout.fillWidth: true
        text: qsTr("Settings changed elsewhere")
        font.weight: Font.DemiBold
        wrapMode: Text.WordWrap
    }

    Label {
        objectName: "conflictPolicyExplanation"
        Layout.fillWidth: true
        text: qsTr("The newer live value is kept for Apply unless you explicitly choose your edit.")
        wrapMode: Text.WordWrap
        Accessible.description: text
    }

    Repeater {
        model: root.conflictKeys

        delegate: ColumnLayout {
            id: row
            required property string modelData
            Layout.fillWidth: true
            spacing: 4

            readonly property var details: Draft.conflict(root.reconcilerState, modelData)
            readonly property string fieldLabel: root.fieldLabels[modelData] || modelData

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: rowContent.implicitHeight + 20
                radius: 6
                color: "transparent"
                border.color: palette.mid

                ColumnLayout {
                    id: rowContent
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    Label {
                        objectName: `conflict-field-${row.modelData}`
                        Layout.fillWidth: true
                        text: qsTr("Conflicting setting: %1").arg(row.fieldLabel)
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        objectName: `conflict-live-${row.modelData}`
                        Layout.fillWidth: true
                        text: qsTr("Newer live value: %1").arg(root.displayValue(row.details.live))
                        wrapMode: Text.WrapAnywhere
                    }
                    Label {
                        objectName: `conflict-draft-${row.modelData}`
                        Layout.fillWidth: true
                        text: qsTr("Your edit: %1").arg(root.displayValue(row.details.draft))
                        wrapMode: Text.WrapAnywhere
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Button {
                            objectName: `keep-live-${row.modelData}`
                            text: qsTr("Keep live value")
                            Accessible.name: qsTr("Keep the newer live value for %1").arg(row.fieldLabel)
                            Accessible.description: qsTr("Discard your staged edit and keep %1").arg(root.displayValue(row.details.live))
                            onClicked: root.choiceRequested(row.modelData, "keepLive")
                        }
                        Button {
                            objectName: `use-my-edit-${row.modelData}`
                            text: qsTr("Use my edit")
                            Accessible.name: qsTr("Use your edit for %1").arg(row.fieldLabel)
                            Accessible.description: qsTr("Restage %1 against the latest live value").arg(root.displayValue(row.details.live))
                            onClicked: root.choiceRequested(row.modelData, "useMyEdit")
                        }
                    }
                }
            }
        }
    }
}
