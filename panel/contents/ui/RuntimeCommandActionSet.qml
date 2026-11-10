pragma ComponentBehavior: Bound

import QtQuick
import org.kde.plasma.core as PlasmaCore

Item {
    id: root
    visible: false
    width: 0
    height: 0
    property var controller: null
    property var appletConfigureAction: null
    property var desktopSettingsInvoker: null
    // Tests may intercept the URI; production delegates to the desktop's
    // registered steam:// handler only after the user activates the action.
    property var externalUrlOpener: null
    readonly property string wallpaperEngineUri: "steam://rungameid/431960"
    readonly property var actions: [pauseAction, resumeAction, playDespiteWindowsAction,
        muteAction, followAction, nextAction, reloadAction, sizingOffAction,
        sizingFillAction, sizingStretchAction, configureAction]
    readonly property var playbackState: resolvePlaybackAction()
    readonly property string playbackActionId: playbackState ? playbackState.actionId : "pause"
    // Preserve the full action set for the expanded runtime controls and its
    // compatibility tests. Plasma appends the applet's built-in Configure
    // action itself; these are the three custom right-click actions.
    readonly property var contextualActions: [launchWallpaperEngineAction,
        playbackMenuAction, desktopSettingsAction]
    readonly property var popupActions: [launchWallpaperEngineAction, playbackMenuAction,
        appletConfigureAction, desktopSettingsAction]
        .filter(action => action !== null)

    function runAllPlaybackAction() {
        if (!controller || !playbackState || !playbackState.enabled) return "";
        const previousScope = controller.scopeMode;
        controller.scopeMode = "all";
        const requestId = controller.runAction(playbackActionId);
        controller.scopeMode = previousScope;
        return requestId;
    }

    function hasPauseReason(target, reason) {
        const reasons = target && target.pauseReasons;
        if (!reasons || typeof reasons.length !== "number") return false;
        for (let i = 0; i < reasons.length; ++i)
            if (reasons[i] === reason) return true;
        return false;
    }

    function hardPauseReason(targets) {
        const hardReasons = ["battery", "gameMode", "sleep", "inactiveSession"];
        for (let reasonIndex = 0; reasonIndex < hardReasons.length; ++reasonIndex) {
            const reason = hardReasons[reasonIndex];
            if (targets.some(target => hasPauseReason(target, reason))) return reason;
        }
        return "";
    }

    function hardPauseMessage(reason) {
        switch (reason) {
        case "battery": return qsTr("Resume is unavailable while the battery pause is active");
        case "gameMode": return qsTr("Resume is unavailable while game mode is active");
        case "sleep": return qsTr("Resume is unavailable while the session is sleeping");
        case "inactiveSession": return qsTr("Resume is unavailable while this session is inactive");
        default: return qsTr("Resume is unavailable while an automatic pause is active");
        }
    }

    function resolvePlaybackAction() {
        if (!controller) return {actionId: "pause", enabled: false, reason: qsTr("Runtime unavailable")};
        const targets = controller.targets || [];
        if (!targets.length)
            return Object.assign({actionId: "pause"}, controller.actionInfo("pause"));

        const allPaused = targets.every(target => target &&
            (target.effectivePlayback === "paused" || target.desiredPlayback === "paused"));
        if (!allPaused) {
            const pause = controller.actionInfo("pause");
            return Object.assign({actionId: "pause"}, pause);
        }

        const hardReason = hardPauseReason(targets);
        if (hardReason) {
            return {actionId: "resume", enabled: false, reason: hardPauseMessage(hardReason),
                text: qsTr("Resume")};
        }

        const hasBypassablePause = targets.some(target => hasPauseReason(target, "window")
            || hasPauseReason(target, "fullscreen"));
        const actionId = hasBypassablePause ? "playDespiteWindows" : "resume";
        return Object.assign({actionId}, controller.actionInfo(actionId));
    }

    PlasmaCore.Action {
        id: pauseAction
        property string actionId: "pause"
        readonly property var state: root.controller ? root.controller.actionInfo(actionId) : null
        text: state ? (state.enabled ? state.text : state.text + " — " + state.reason) : qsTr("Pause")
        enabled: state ? state.enabled : false
        property string stateReason: state ? state.reason : qsTr("Runtime unavailable")
        toolTip: stateReason
        statusTip: stateReason
        onTriggered: if (root.controller) root.controller.runAction(actionId)
    }
    PlasmaCore.Action {
        id: resumeAction
        property string actionId: "resume"
        readonly property var state: root.controller ? root.controller.actionInfo(actionId) : null
        text: state ? (state.enabled ? state.text : state.text + " — " + state.reason) : qsTr("Resume automatically")
        enabled: state ? state.enabled : false
        property string stateReason: state ? state.reason : qsTr("Runtime unavailable")
        toolTip: stateReason
        statusTip: stateReason
        onTriggered: if (root.controller) root.controller.runAction(actionId)
    }
    PlasmaCore.Action {
        id: playDespiteWindowsAction
        property string actionId: "playDespiteWindows"
        readonly property var state: root.controller ? root.controller.actionInfo(actionId) : null
        text: state ? (state.enabled ? state.text : state.text + " — " + state.reason) : qsTr("Play despite window coverage")
        enabled: state ? state.enabled : false
        property string stateReason: state ? state.reason : qsTr("Runtime unavailable")
        toolTip: stateReason
        statusTip: stateReason
        onTriggered: if (root.controller) root.controller.runAction(actionId)
    }
    PlasmaCore.Action {
        id: muteAction
        property string actionId: "muteAll"
        readonly property var state: root.controller ? root.controller.actionInfo(actionId) : null
        text: state ? (state.enabled ? state.text : state.text + " — " + state.reason) : qsTr("Mute all wallpaper audio")
        enabled: state ? state.enabled : false
        property string stateReason: state ? state.reason : qsTr("Runtime unavailable")
        toolTip: stateReason
        statusTip: stateReason
        onTriggered: if (root.controller) root.controller.runAction(actionId)
    }
    PlasmaCore.Action {
        id: followAction
        property string actionId: "follow"
        readonly property var state: root.controller ? root.controller.actionInfo(actionId) : null
        text: state ? (state.enabled ? state.text : state.text + " — " + state.reason) : qsTr("Follow selected source")
        enabled: state ? state.enabled : false
        property string stateReason: state ? state.reason : qsTr("Runtime unavailable")
        toolTip: stateReason
        statusTip: stateReason
        onTriggered: if (root.controller) root.controller.runAction(actionId)
    }
    PlasmaCore.Action {
        id: nextAction
        property string actionId: "next"
        readonly property var state: root.controller ? root.controller.actionInfo(actionId) : null
        text: state ? (state.enabled ? state.text : state.text + " — " + state.reason) : qsTr("Next wallpaper")
        enabled: state ? state.enabled : false
        property string stateReason: state ? state.reason : qsTr("Runtime unavailable")
        toolTip: stateReason
        statusTip: stateReason
        onTriggered: if (root.controller) root.controller.runAction(actionId)
    }
    PlasmaCore.Action {
        id: reloadAction
        property string actionId: "reload"
        readonly property var state: root.controller ? root.controller.actionInfo(actionId) : null
        text: state ? (state.enabled ? state.text : state.text + " — " + state.reason) : qsTr("Reload current wallpaper")
        enabled: state ? state.enabled : false
        property string stateReason: state ? state.reason : qsTr("Runtime unavailable")
        toolTip: stateReason
        statusTip: stateReason
        onTriggered: if (root.controller) root.controller.runAction(actionId)
    }
    PlasmaCore.Action {
        id: sizingOffAction
        property string actionId: "sizingOff"
        readonly property var state: root.controller ? root.controller.actionInfo(actionId) : null
        text: state ? (state.enabled ? state.text : state.text + " — " + state.reason) : qsTr("Global sizing: Off")
        enabled: state ? state.enabled : false
        property string stateReason: state ? state.reason : qsTr("Runtime unavailable")
        toolTip: stateReason
        statusTip: stateReason
        onTriggered: if (root.controller) root.controller.runAction(actionId)
    }
    PlasmaCore.Action {
        id: sizingFillAction
        property string actionId: "sizingFill"
        readonly property var state: root.controller ? root.controller.actionInfo(actionId) : null
        text: state ? (state.enabled ? state.text : state.text + " — " + state.reason) : qsTr("Global sizing: Fill")
        enabled: state ? state.enabled : false
        property string stateReason: state ? state.reason : qsTr("Runtime unavailable")
        toolTip: stateReason
        statusTip: stateReason
        onTriggered: if (root.controller) root.controller.runAction(actionId)
    }
    PlasmaCore.Action {
        id: sizingStretchAction
        property string actionId: "sizingStretch"
        readonly property var state: root.controller ? root.controller.actionInfo(actionId) : null
        text: state ? (state.enabled ? state.text : state.text + " — " + state.reason) : qsTr("Global sizing: Stretch")
        enabled: state ? state.enabled : false
        property string stateReason: state ? state.reason : qsTr("Runtime unavailable")
        toolTip: stateReason
        statusTip: stateReason
        onTriggered: if (root.controller) root.controller.runAction(actionId)
    }
    PlasmaCore.Action {
        id: configureAction
        property string actionId: "configure"
        readonly property var state: root.controller ? root.controller.actionInfo(actionId) : null
        text: state ? (state.enabled ? state.text : state.text + " — " + state.reason) : qsTr("Configure wallpaper")
        enabled: state ? state.enabled : false
        property string stateReason: state ? state.reason : qsTr("Runtime unavailable")
        toolTip: stateReason
        statusTip: stateReason
        onTriggered: if (root.controller) root.controller.runAction(actionId)
    }

    PlasmaCore.Action {
        id: playbackMenuAction
        text: root.playbackActionId === "pause" ? qsTr("Pause") : qsTr("Resume")
        enabled: Boolean(root.playbackState && root.playbackState.enabled)
        property string stateReason: root.playbackState ? root.playbackState.reason || "" : ""
        toolTip: stateReason
        statusTip: stateReason
        onTriggered: root.runAllPlaybackAction()
    }

    PlasmaCore.Action {
        id: desktopSettingsAction
        text: qsTr("Desktop and Wallpaper Settings")
        enabled: true
        onTriggered: {
            if (root.desktopSettingsInvoker)
                root.desktopSettingsInvoker.open();
        }
    }

    PlasmaCore.Action {
        id: launchWallpaperEngineAction
        text: qsTr("Launch Wallpaper Engine")
        enabled: true
        onTriggered: {
            if (root.externalUrlOpener)
                root.externalUrlOpener(root.wallpaperEngineUri);
            else
                Qt.openUrlExternally(root.wallpaperEngineUri);
        }
    }
}
