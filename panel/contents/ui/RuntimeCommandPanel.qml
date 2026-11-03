pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var controller: null
    property var actions: []
    property var followChoices: []
    property color textColor: "#232629"
    property color secondaryTextColor: "#55595c"
    spacing: 8

    Label {
        text: qsTr("Target controls")
        font.weight: Font.DemiBold
        color: root.textColor
        Layout.fillWidth: true
    }

    ComboBox {
        id: targetSelector
        objectName: "runtimeTargetSelector"
        Layout.fillWidth: true
        Accessible.name: qsTr("Wallpaper target for playback and follow commands")
        model: root.controller ? [{id: "all", label: qsTr("All wallpaper screens")},
            ...root.controller.targets.map(target => ({id: target.instanceId,
                label: target.screenLabel || target.screenName || qsTr("Unknown screen")}))] : []
        textRole: "label"
        currentIndex: {
            if (!root.controller || root.controller.scopeMode === "all") return 0;
            const selectedIndex = root.controller.targets.findIndex(
                target => target.instanceId === root.controller.selectedTargetId);
            return selectedIndex < 0 ? -1 : selectedIndex + 1;
        }
        displayText: currentIndex < 0
            ? qsTr("Selected wallpaper unavailable — choose a target") : currentText
        onActivated: index => {
            if (!root.controller) return;
            if (index === 0) root.controller.scopeMode = "all";
            else {
                root.controller.scopeMode = "selected";
                root.controller.selectedTargetId = model[index].id;
            }
        }
    }

    Label {
        objectName: "runtimeGlobalScopeNote"
        Layout.fillWidth: true
        text: qsTr("Audio and sizing actions apply globally to all wallpaper screens.")
        color: root.secondaryTextColor
        wrapMode: Text.WordWrap
    }

    Label {
        text: qsTr("Follow source")
        font.weight: Font.DemiBold
        color: root.textColor
        Layout.fillWidth: true
    }

    ComboBox {
        id: followSelector
        objectName: "runtimeFollowSourceSelector"
        Layout.fillWidth: true
        enabled: model.length > 0
        Accessible.name: qsTr("Follow account and source")
        model: root.followChoices || []
        textRole: "label"
        currentIndex: {
            if (!root.controller || !root.controller.followBinding) return -1;
            return model.findIndex(choice => choice.accountKey === root.controller.followBinding.accountKey
                && choice.sourceKey === root.controller.followBinding.sourceKey
                && choice.mode === root.controller.followBinding.mode);
        }
        displayText: currentIndex < 0 ? qsTr("Choose a follow source") : currentText
        onActivated: index => {
            const choice = model[index];
            root.controller.followBinding = {mode: choice.mode || "",
                accountKey: choice.accountKey || "", sourceKey: choice.sourceKey || "",
                identityVerified: choice.identityVerified === true};
        }
        delegate: ItemDelegate {
            required property var modelData
            width: followSelector.width
            text: modelData.label || modelData.sourceKey || qsTr("Follow source")
            Accessible.name: text
        }
    }

    Repeater {
        model: root.actions
        delegate: Button {
            required property var modelData
            objectName: `runtime-action-${modelData.actionId}`
            Layout.fillWidth: true
            text: modelData.text
            enabled: modelData.enabled
            Accessible.name: text
            Accessible.description: modelData.stateReason || ""
            ToolTip.visible: hovered && !enabled && Boolean(modelData.stateReason)
            ToolTip.text: modelData.stateReason || ""
            onClicked: modelData.trigger()
        }
    }

    Label {
        objectName: "runtimeCommandStatus"
        visible: Boolean(root.controller && root.controller.lastCommandText)
        Layout.fillWidth: true
        text: root.controller ? root.controller.lastCommandText : ""
        Accessible.name: text
        color: root.controller && root.controller.lastError ? "#c0392b" : root.secondaryTextColor
        wrapMode: Text.WordWrap
    }

    Label {
        objectName: "runtimeCommandTargetResults"
        visible: Boolean(root.controller && root.controller.lastTargetResultText)
        Layout.fillWidth: true
        text: root.controller ? root.controller.lastTargetResultText : ""
        Accessible.name: text
        color: root.secondaryTextColor
        wrapMode: Text.WordWrap
    }
}
