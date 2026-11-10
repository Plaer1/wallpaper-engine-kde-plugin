import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import "../../panel/contents/ui" as Panel

TestCase {
    id: testCase
    name: "PanelRuntimeCommands"
    when: windowShown
    visible: true
    width: 480
    height: 900

    function target(id, followEnabled, caps) {
        return {
            instanceId: id,
            containmentId: id,
            screenName: id,
            screenLabel: id,
            desiredPlayback: "automatic",
            effectivePlayback: "playing",
            pauseReasons: [],
            wallpaper: {source: "workshop", title: id, workshopId: "42", backend: "scene"},
            follow: {enabled: followEnabled, accountKey: "steam:76561190000000001", status: "ready", provenance: "native",
                choices: [{mode: "manualMapping", accountKey: "steam:76561190000000001",
                    sourceKey: "config:Alice", label: "Alice — saved selection"}]},
            capabilities: caps === undefined ? allCaps() : caps
        };
    }
    function allCaps() {
        return ["SetPlayback", "SetGlobalAudioMuted", "SetGlobalSizing",
            "SetFollow", "NextWallpaper", "ReloadWallpaper", "OpenConfiguration"];
    }
    function snapshot(targets) {
        return {schemaVersion: 1, epoch: "epoch-A", revision: 1, targets,
            global: {muted: false, sizingEnabled: true, sizingMode: "crop"}};
    }

    Component {
        id: clientComponent
        QtObject {
            id: fakeClient
            signal commandFinished(var result)
            signal ownerChanged(bool available)
            signal stateChanged(var snapshot)
            property var requests: []
            property bool completeBeforeAck: false
            property string nextAckStatus: "accepted"
            function execute(request) {
                requests = requests.concat([JSON.parse(JSON.stringify(request))]);
                if (completeBeforeAck)
                    commandFinished({requestId: request.requestId, status: "applied", message: "early completion"});
                return Promise.resolve({requestId: request.requestId, status: nextAckStatus,
                    message: nextAckStatus === "rejected" ? "not supported" : ""});
            }
        }
    }
    Component {
        id: controllerComponent
        Panel.RuntimeCommandController { }
    }
    Component {
        id: actionSetComponent
        Panel.RuntimeCommandActionSet { }
    }
    Component {
        id: panelComponent
        Panel.RuntimeCommandPanel { }
    }
    Component { id: spyComponent; SignalSpy {} }
    Component {
        id: trayConfigureActionComponent
        QtObject {
            property string text: "Configure Wallpaper Engine Controls"
            property bool enabled: true
            property int triggerCount: 0
            function trigger() { ++triggerCount; }
        }
    }
    Component {
        id: desktopLauncherComponent
        QtObject {
            property int openCount: 0
            function open() { ++openCount; return true; }
        }
    }
    Component {
        id: tallTrayPopupHostComponent
        Item {
            width: 440
            height: 700
            ColumnLayout {
                anchors.fill: parent
                Panel.TrayPopup {
                    objectName: "tray-popup"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
        }
    }
    Component {
        id: trayActionsMenuComponent
        Panel.TrayActionsMenu { }
    }

    function readyController(client, targets) {
        const controller = createTemporaryObject(controllerComponent, testCase, {
            runtimeStatus: "ready", snapshot: snapshot(targets), commandClient: client,
            followBinding: {mode: "manualMapping", accountKey: "steam:76561190000000001",
                sourceKey: "config:Alice", identityVerified: false}
        });
        verify(controller);
        return controller;
    }

    function test_scopeAndCommandArgumentsMatchRuntimeContract() {
        const client = createTemporaryObject(clientComponent, testCase);
        const controller = readyController(client, [target("left", false), target("right", false)]);
        compare(controller.runtimeStatus, "ready");
        compare(controller.snapshot.epoch, "epoch-A");
        compare(controller.targets.length, 2);
        compare(controller.serviceReady, true);
        compare(controller.actionInfo("pause").enabled, true);
        controller.scopeMode = "selected";
        controller.selectedTargetId = "right";
        const pauseId = controller.runAction("pause");
        tryCompare(controller, "lastCommandText", "Command accepted; waiting for completion");
        compare(client.requests[0].schemaVersion, 1);
        compare(client.requests[0].epoch, "epoch-A");
        compare(client.requests[0].scope, "targets");
        compare(client.requests[0].targetIds, ["right"]);
        compare(client.requests[0].command, "SetPlayback");
        compare(client.requests[0].args.mode, "paused");
        client.commandFinished({requestId: pauseId, status: "applied"});
        compare(controller.statusFor(pauseId), "complete");

        controller.scopeMode = "selected";
        controller.selectedTargetId = "right";
        controller.runAction("muteAll");
        tryCompare(controller, "lastCommandText", "Command accepted; waiting for completion");
        compare(client.requests[1].scope, "allWallpaperTargets");
        compare(client.requests[1].targetIds, []);
        compare(client.requests[1].command, "SetGlobalAudioMuted");
        compare(client.requests[1].args.muted, true);
        compare(controller.actionInfo("sizingStretch").text, "Global sizing: Stretch");
        compare(controller.actionInfo("sizingStretch").scope, "global");
    }

    function test_actionDescriptorsMatchFixedCommandTable() {
        const client = createTemporaryObject(clientComponent, testCase);
        const controller = readyController(client, [target("left", false)]);
        const expected = {
            pause: ["SetPlayback", "targets", {mode: "paused"}],
            resume: ["SetPlayback", "targets", {mode: "automatic"}],
            playDespiteWindows: ["SetPlayback", "targets", {mode: "playDespiteWindows"}],
            muteAll: ["SetGlobalAudioMuted", "global", {muted: true}],
            next: ["NextWallpaper", "targets", {}],
            reload: ["ReloadWallpaper", "targets", {}],
            sizingOff: ["SetGlobalSizing", "global", {enabled: false, mode: "crop"}],
            sizingFill: ["SetGlobalSizing", "global", {enabled: true, mode: "crop"}],
            sizingStretch: ["SetGlobalSizing", "global", {enabled: true, mode: "stretch"}],
            configure: ["OpenConfiguration", "targets", {}]
        };
        for (const actionId of Object.keys(expected)) {
            const info = controller.actionInfo(actionId);
            compare(info.command, expected[actionId][0], actionId);
            compare(info.scope, expected[actionId][1], actionId);
            compare(JSON.stringify(info.args), JSON.stringify(expected[actionId][2]), actionId);
        }
    }

    function test_missingCapabilitiesAndAbsentServiceDisableActions() {
        const client = createTemporaryObject(clientComponent, testCase);
        const unsupported = target("left", false, ["SetPlayback"]);
        const controller = readyController(client, [unsupported]);
        verify(!controller.actionInfo("reload").enabled);
        verify(controller.actionInfo("reload").reason.includes("unsupported"));
        verify(!controller.actionInfo("muteAll").enabled);

        controller.runtimeStatus = "absent";
        verify(!controller.actionInfo("pause").enabled);
        compare(controller.actionInfo("pause").reason, "No active wallpaper runtime");
        compare(controller.runAction("pause"), "");
        compare(client.requests.length, 0);
    }

    function test_earlyCompletionIsBufferedUntilAcceptedAck() {
        const client = createTemporaryObject(clientComponent, testCase, {completeBeforeAck: true});
        const controller = readyController(client, [target("left", false)]);
        const requestId = controller.runAction("reload");
        tryVerify(() => controller.statusFor(requestId) === "complete");
        compare(controller.lastCommandText, "Command completed");
    }

    function test_rejectedAckAndFailedCompletionAreVisible() {
        const client = createTemporaryObject(clientComponent, testCase, {nextAckStatus: "rejected"});
        const controller = readyController(client, [target("left", false)]);
        const rejectedId = controller.runAction("pause");
        tryVerify(() => controller.statusFor(rejectedId) === "error");
        compare(controller.lastError, "not supported");

        client.nextAckStatus = "accepted";
        const failedId = controller.runAction("reload");
        tryVerify(() => controller.statusFor(failedId) === "pending");
        client.commandFinished({requestId: failedId, status: "rejected", errorCode: "backendError", message: "Renderer error"});
        compare(controller.statusFor(failedId), "error");
        compare(controller.lastError, "Renderer error");
    }

    function test_partialCompletionShowsAccessiblePerTargetOutcomes() {
        const client = createTemporaryObject(clientComponent, testCase);
        const controller = readyController(client, [target("left", false), target("right", false)]);
        const requestId = controller.runAction("pause");
        tryVerify(() => controller.statusFor(requestId) === "pending");
        client.commandFinished({requestId, status: "partial",
            message: "One or more wallpaper targets failed",
            targets: [
                {instanceId: "left", status: "applied", message: "Pause requested"},
                {instanceId: "right", status: "rejected", errorCode: "targetRemoved", message: "Target disappeared"}
            ]});
        compare(controller.statusFor(requestId), "partial");
        verify(controller.lastCommandText.includes("partially completed"));
        verify(controller.lastTargetResultText.includes("left: succeeded"));
        verify(controller.lastTargetResultText.includes("right: failed"));
        verify(controller.lastTargetResultText.includes("Target disappeared"));
        compare(controller.lastError, "");

        const actionSet = createTemporaryObject(actionSetComponent, testCase, {controller});
        const panel = createTemporaryObject(panelComponent, testCase, {
            controller, actions: actionSet.actions
        });
        const results = findChild(panel, "runtimeCommandTargetResults");
        verify(results && results.visible);
        compare(results.text, controller.lastTargetResultText);
        compare(results.Accessible.name, controller.lastTargetResultText);
    }

    function test_followDisablesNextAndUsesTargetScope() {
        const client = createTemporaryObject(clientComponent, testCase);
        const controller = readyController(client, [target("left", true)]);
        verify(!controller.actionInfo("next").enabled);
        verify(controller.actionInfo("next").reason.includes("Follow is active"));
        compare(controller.actionInfo("follow").text, "Stop following");
        controller.runAction("follow");
        tryCompare(controller, "lastCommandText", "Command accepted; waiting for completion");
        compare(client.requests[0].command, "SetFollow");
        compare(client.requests[0].scope, "targets");
        compare(client.requests[0].targetIds, ["left"]);
        compare(client.requests[0].args.enabled, false);
        compare(client.requests[0].args.mode, "manualMapping");
        compare(client.requests[0].args.accountKey, "steam:76561190000000001");
        compare(client.requests[0].args.sourceKey, "config:Alice");
    }

    function test_followSourceChoiceControlsFollowAvailability() {
        const client = createTemporaryObject(clientComponent, testCase);
        const controller = readyController(client, [target("left", false)]);
        controller.followBinding = {mode: "", accountKey: "", sourceKey: ""};
        verify(!controller.actionInfo("follow").enabled);
        const panel = createTemporaryObject(panelComponent, testCase, {
            controller,
            actions: createTemporaryObject(actionSetComponent, testCase, {controller}).actions,
            followChoices: [{mode: "manualMapping", accountKey: "steam:76561190000000001",
                sourceKey: "config:Alice", label: "Alice — saved selection"}]
        });
        const picker = findChild(panel, "runtimeFollowSourceSelector");
        verify(picker && picker.enabled);
        picker.activated(0);
        compare(controller.followBinding.mode, "manualMapping");
        compare(controller.followBinding.sourceKey, "config:Alice");
        verify(controller.actionInfo("follow").enabled);
    }

    function test_followModeIsExplicitAndAccountModeNeedsVerifiedIdentity() {
        const client = createTemporaryObject(clientComponent, testCase);
        const accountTarget = target("left", false);
        accountTarget.follow.choices = [
            {mode: "account", accountKey: "steam:76561190000000001",
                sourceKey: "config:Alice", identityVerified: true, label: "Alice account"},
            {mode: "manualMapping", accountKey: "steam:76561190000000001",
                sourceKey: "config:Alice", label: "Alice mapping"}
        ];
        const controller = readyController(client, [accountTarget]);
        controller.followBinding = {accountKey: "steam:76561190000000001", sourceKey: "config:Alice"};
        verify(!controller.actionInfo("follow").enabled); // missing mode never inferred
        controller.followBinding = {mode: "", accountKey: "steam:76561190000000001", sourceKey: "config:Alice"};
        verify(!controller.actionInfo("follow").enabled);
        controller.followBinding = {mode: "identifiedCurrent", accountKey: "steam:76561190000000001",
            sourceKey: "config:Alice", identityVerified: true};
        verify(!controller.actionInfo("follow").enabled); // G2 unsupported by tray client
        controller.followBinding = {mode: "account", accountKey: "steam:76561190000000001",
            sourceKey: "config:Alice", identityVerified: false};
        verify(!controller.actionInfo("follow").enabled);
        controller.followBinding = {mode: "account", accountKey: "steam:76561190000000001",
            sourceKey: "config:Alice", identityVerified: true};
        verify(controller.actionInfo("follow").enabled);
        controller.followBinding = {mode: "manualMapping", accountKey: "steam:76561190000000001",
            sourceKey: "config:Alice", identityVerified: false};
        const action = controller.actionInfo("follow");
        verify(action.enabled);
        compare(action.args.mode, "manualMapping");
    }

    function test_followChoicesDeriveForOneTargetAndSelectedScope() {
        const client = createTemporaryObject(clientComponent, testCase);
        const left = target("left", false);
        left.follow.choices.push({mode: "legacySession", accountKey: "",
            sourceKey: "config:steamuser", label: "Legacy session"});
        const right = target("right", false);
        right.follow.choices = [{mode: "legacySession", accountKey: "",
            sourceKey: "config:steamuser", label: "Legacy session"}];
        const controller = readyController(client, [left, right]);
        controller.scopeMode = "selected";
        controller.selectedTargetId = "left";
        compare(controller.followChoices.length, 2);
        controller.selectedTargetId = "right";
        compare(controller.followChoices.length, 1);
        compare(controller.followChoices[0].mode, "legacySession");
        compare(controller.followChoices[0].sourceKey, "config:steamuser");
    }

    function test_allTargetFollowChoicesAreExactIntersectionAndUnavailableFailsClosed() {
        const client = createTemporaryObject(clientComponent, testCase);
        const left = target("left", false);
        left.follow.choices = [{mode: "legacySession", accountKey: "",
            sourceKey: "config:shared", label: "Shared section"}];
        const right = target("right", false);
        right.follow.choices = [{mode: "legacySession", accountKey: "",
            sourceKey: "config:shared", label: "Shared section"},
            {mode: "manualMapping", accountKey: "steam:76561190000000001",
                sourceKey: "config:Alice", label: "Alice"}];
        const controller = readyController(client, [left, right]);
        compare(controller.followChoices.length, 1);
        compare(controller.followChoices[0].sourceKey, "config:shared");

        right.follow.choices = [{mode: "legacySession", accountKey: "",
            sourceKey: "config:different", label: "Different section"}];
        controller.snapshot = snapshot([left, right]);
        compare(controller.followChoices.length, 0);
        controller.followBinding = {mode: "legacySession", accountKey: "", sourceKey: "config:shared"};
        verify(!controller.actionInfo("follow").enabled);

        right.follow.choices = null;
        controller.snapshot = snapshot([left, right]);
        compare(controller.followChoices.length, 0);
        left.follow.enabled = true;
        right.follow.enabled = true;
        verify(controller.actionInfo("follow").enabled); // stopping needs no source catalog
    }

    function test_ownerLossClearsDerivedFollowChoices() {
        const client = createTemporaryObject(clientComponent, testCase);
        const controller = readyController(client, [target("left", false)]);
        compare(controller.followChoices.length, 1);
        client.ownerChanged(false);
        compare(controller.followChoices.length, 0);
        verify(!controller.actionInfo("follow").enabled);
    }

    function test_stoppingFollowDoesNotRequireReplacementSelection() {
        const client = createTemporaryObject(clientComponent, testCase);
        const controller = readyController(client, [target("left", true)]);
        controller.followBinding = {mode: "", accountKey: "", sourceKey: ""};
        verify(controller.actionInfo("follow").enabled);
        controller.runAction("follow");
        tryCompare(controller, "lastCommandText", "Command accepted; waiting for completion");
        compare(client.requests[0].args.enabled, false);
        compare(client.requests[0].args.mode, "legacySession");
        compare(client.requests[0].args.accountKey, "");
        compare(client.requests[0].args.sourceKey, "");
    }

    function test_ownerDisconnectReconnectRequiresFreshSnapshotAndTargetChoice() {
        const client = createTemporaryObject(clientComponent, testCase);
        const controller = readyController(client, [target("left", false)]);
        controller.scopeMode = "selected";
        controller.selectedTargetId = "left";
        const pendingId = controller.runAction("pause");
        tryVerify(() => controller.statusFor(pendingId) === "pending");

        client.ownerChanged(false);
        compare(controller.effectiveRuntimeStatus, "absent");
        compare(controller.effectiveSnapshot, null);
        compare(controller.statusFor(pendingId), "error");
        controller.handleAcknowledgement(pendingId, {requestId: pendingId, status: "accepted"});
        compare(controller.statusFor(pendingId), "error"); // late acknowledgement cannot revive it
        verify(!controller.actionInfo("pause").enabled);
        client.commandFinished({requestId: pendingId, status: "applied"});
        compare(controller.statusFor(pendingId), "error"); // stale completion ignored

        client.ownerChanged(true);
        compare(controller.effectiveRuntimeStatus, "loading");
        compare(controller.effectiveSnapshot, null);
        client.stateChanged(snapshot([target("right", false)]));
        compare(controller.effectiveRuntimeStatus, "ready");
        compare(controller.snapshot.epoch, "epoch-A");
        compare(controller.scopeMode, "selected");
        compare(controller.selectedTargetId, "");
        verify(!controller.actionInfo("pause").enabled);
        controller.selectedTargetId = "right"; // fresh user selection
        verify(controller.actionInfo("pause").enabled);
    }

    function test_hotpluggedAwayTargetIsNotRedirectedToAnotherScreen() {
        const client = createTemporaryObject(clientComponent, testCase);
        const controller = readyController(client, [target("left", false), target("right", false)]);
        controller.scopeMode = "selected";
        controller.selectedTargetId = "left";
        controller.snapshot = snapshot([target("right", false)]); // same epoch, left unplugged
        verify(!controller.actionInfo("pause").enabled);
        const actionSet = createTemporaryObject(actionSetComponent, testCase, {controller});
        const panel = createTemporaryObject(panelComponent, testCase, {controller, actions: actionSet.actions});
        const selector = findChild(panel, "runtimeTargetSelector");
        compare(selector.currentIndex, -1);
        compare(selector.displayText, "Selected wallpaper unavailable — choose a target");
        verify(!findChild(panel, "runtime-action-pause").enabled);
        compare(controller.runAction("pause"), "");
        compare(client.requests.length, 0);
        verify(controller.lastError.includes("Select an available wallpaper target"));
    }

    function test_newServiceEpochCannotReuseSelectedInstanceIdSilently() {
        const client = createTemporaryObject(clientComponent, testCase);
        const controller = readyController(client, [target("left", false)]);
        controller.scopeMode = "selected";
        controller.selectedTargetId = "left";
        const restarted = snapshot([target("left", false)]);
        restarted.epoch = "epoch-B";
        controller.snapshot = restarted;
        compare(controller.selectedTargetId, "");
        verify(!controller.actionInfo("pause").enabled);
        compare(controller.runAction("pause"), "");
        compare(client.requests.length, 0);
    }

    function test_configurationRequiresOneCapableTargetAndUsesFixedCommand() {
        const client = createTemporaryObject(clientComponent, testCase);
        const controller = readyController(client, [target("left", false), target("right", false)]);
        verify(!controller.actionInfo("configure").enabled);
        verify(controller.actionInfo("configure").reason.includes("exactly one"));
        controller.scopeMode = "selected";
        controller.selectedTargetId = "right";
        verify(controller.actionInfo("configure").enabled);
        controller.runAction("configure");
        tryCompare(controller, "lastCommandText", "Command accepted; waiting for completion");
        compare(client.requests[0].command, "OpenConfiguration");
        compare(client.requests[0].scope, "targets");
        compare(client.requests[0].targetIds, ["right"]);
        compare(JSON.stringify(client.requests[0].args), "{}");
    }

    function test_trayMenuHasFourActionsAndPlaybackAlwaysTargetsAllScreens() {
        const client = createTemporaryObject(clientComponent, testCase);
        const controller = readyController(client, [target("left", false), target("right", false)]);
        controller.scopeMode = "selected";
        controller.selectedTargetId = "right";
        const configureAction = createTemporaryObject(trayConfigureActionComponent, testCase);
        const desktopLauncher = createTemporaryObject(desktopLauncherComponent, testCase);
        const actionSet = createTemporaryObject(actionSetComponent, testCase, {
            controller, appletConfigureAction: configureAction, desktopSettingsInvoker: desktopLauncher
        });
        const popupHost = createTemporaryObject(tallTrayPopupHostComponent, testCase);
        const popup = findChild(popupHost, "tray-popup");
        popup.actions = actionSet.popupActions;
        tryVerify(() => popup.implicitHeight > 0);

        compare(actionSet.contextualActions.length, 3); // Plasma appends Configure Controls.
        compare(actionSet.popupActions.length, 4);
        compare(actionSet.contextualActions[0].text, "Launch Wallpaper Engine");
        compare(actionSet.contextualActions[1].text, "Pause");
        compare(actionSet.contextualActions[2].text, "Desktop and Wallpaper Settings");
        verify(actionSet.contextualActions.indexOf(configureAction) < 0);
        verify(popup.implicitHeight >= 60 && popup.implicitHeight <= 120,
            `Expected compact implicit height, got ${popup.implicitHeight}`);
        verify(popup.height >= 60 && popup.height <= 120,
            `Expected compact outer popup height in a 700px host, got ${popup.height}`);
        compare(findChild(popup, "tray-popup-action-0").text, "Launch Wallpaper Engine");
        compare(findChild(popup, "tray-popup-action-1").text, "Pause");
        compare(findChild(popup, "tray-popup-action-2").text, configureAction.text);
        compare(findChild(popup, "tray-popup-action-3").text, "Desktop and Wallpaper Settings");
        const firstButton = findChild(popup, "tray-popup-action-0");
        const secondButton = findChild(popup, "tray-popup-action-1");
        const thirdButton = findChild(popup, "tray-popup-action-2");
        verify(secondButton.y - (firstButton.y + firstButton.height) <= 8);
        verify(thirdButton.y - (secondButton.y + secondButton.height) <= 8);
        verify(findChild(popup, "tray-popup-action-0").enabled);
        verify(findChild(popup, "tray-popup-action-1").enabled);
        actionSet.popupActions[1].trigger();
        tryCompare(controller, "lastCommandText", "Command accepted; waiting for completion");
        compare(client.requests[0].scope, "targets");
        compare(client.requests[0].targetIds, ["left", "right"]);

        const pausedLeft = target("left", false);
        const pausedRight = target("right", false);
        pausedLeft.desiredPlayback = "paused";
        pausedRight.desiredPlayback = "paused";
        pausedLeft.effectivePlayback = "unknown";
        pausedRight.effectivePlayback = "playing";
        controller.scopeMode = "all";
        controller.snapshot = snapshot([pausedLeft, pausedRight]);
        compare(actionSet.playbackActionId, "resume");
        compare(actionSet.popupActions[1].text, "Resume");
        actionSet.popupActions[1].trigger();
        tryVerify(() => client.requests.length === 2);
        compare(client.requests[1].scope, "targets");
        compare(client.requests[1].targetIds, ["left", "right"]);
        compare(client.requests[1].args.mode, "automatic");
        actionSet.popupActions[2].trigger();
        compare(configureAction.triggerCount, 1);
        actionSet.popupActions[3].trigger();
        compare(desktopLauncher.openCount, 1);
    }

    function test_resumeTemporarilyOverridesOrdinaryWindowCoverage() {
        const client = createTemporaryObject(clientComponent, testCase);
        const left = target("left", false);
        const right = target("right", false);
        [left, right].forEach(targetState => {
            targetState.desiredPlayback = "automatic";
            targetState.effectivePlayback = "paused";
            targetState.pauseReasons = ["window"];
        });
        const controller = readyController(client, [left, right]);
        const actionSet = createTemporaryObject(actionSetComponent, testCase, {controller});

        compare(actionSet.playbackActionId, "playDespiteWindows");
        compare(actionSet.popupActions[1].text, "Resume");
        verify(actionSet.popupActions[1].enabled);
        actionSet.runAllPlaybackAction();
        tryVerify(() => client.requests.length === 1);
        compare(client.requests[0].args.mode, "playDespiteWindows");
        compare(client.requests[0].targetIds, ["left", "right"]);

        left.desiredPlayback = "paused";
        right.desiredPlayback = "paused";
        controller.snapshot = snapshot([left, right]);
        compare(actionSet.playbackActionId, "playDespiteWindows");
        actionSet.runAllPlaybackAction();
        tryVerify(() => client.requests.length === 2);
        compare(client.requests[1].args.mode, "playDespiteWindows");

        left.desiredPlayback = "playDespiteWindows";
        right.desiredPlayback = "playDespiteWindows";
        left.effectivePlayback = "playing";
        right.effectivePlayback = "playing";
        left.pauseReasons = [];
        right.pauseReasons = [];
        controller.snapshot = snapshot([left, right]);
        compare(actionSet.playbackActionId, "pause");
        actionSet.runAllPlaybackAction();
        tryVerify(() => client.requests.length === 3);
        compare(client.requests[2].args.mode, "paused");
    }

    function test_fullscreenResumeBypassesButOtherHardReasonsBlockAndMixedAllowsPause() {
        const client = createTemporaryObject(clientComponent, testCase);
        const left = target("left", false);
        const right = target("right", false);
        left.effectivePlayback = "paused";
        left.pauseReasons = ["fullscreen"];
        right.effectivePlayback = "paused";
        right.pauseReasons = ["fullscreen"];
        const controller = readyController(client, [left, right]);
        const actionSet = createTemporaryObject(actionSetComponent, testCase, {controller});

        compare(actionSet.playbackActionId, "playDespiteWindows");
        compare(actionSet.popupActions[1].text, "Resume");
        verify(actionSet.popupActions[1].enabled);
        actionSet.runAllPlaybackAction();
        tryVerify(() => client.requests.length === 1);
        compare(client.requests[0].args.mode, "playDespiteWindows");
        compare(client.requests[0].targetIds, ["left", "right"]);

        left.desiredPlayback = "paused";
        right.desiredPlayback = "paused";
        left.pauseReasons = ["manual", "fullscreen"];
        right.pauseReasons = ["manual", "fullscreen"];
        controller.snapshot = snapshot([left, right]);
        compare(actionSet.playbackActionId, "playDespiteWindows");
        actionSet.runAllPlaybackAction();
        tryVerify(() => client.requests.length === 2);
        compare(client.requests[1].args.mode, "playDespiteWindows");

        left.pauseReasons = ["fullscreen", "battery"];
        controller.snapshot = snapshot([left, right]);
        verify(!actionSet.popupActions[1].enabled);
        verify(actionSet.popupActions[1].stateReason.includes("battery"));
        compare(actionSet.runAllPlaybackAction(), "");
        compare(client.requests.length, 2);

        left.pauseReasons = ["fullscreen"];
        right.effectivePlayback = "playing";
        right.pauseReasons = [];
        right.desiredPlayback = "automatic";
        controller.snapshot = snapshot([left, right]);
        compare(actionSet.playbackActionId, "pause");
        verify(actionSet.popupActions[1].enabled);
        actionSet.runAllPlaybackAction();
        tryVerify(() => client.requests.length === 3);
        compare(client.requests[2].args.mode, "paused");
        compare(client.requests[2].targetIds, ["left", "right"]);
    }

    function test_mixedWindowCoveredAndPlayingTargetsPauseTogether() {
        const client = createTemporaryObject(clientComponent, testCase);
        const covered = target("covered", false);
        covered.effectivePlayback = "paused";
        covered.pauseReasons = ["window"];
        const playing = target("playing", false);
        const controller = readyController(client, [covered, playing]);
        const actionSet = createTemporaryObject(actionSetComponent, testCase, {controller});

        compare(actionSet.playbackActionId, "pause");
        actionSet.runAllPlaybackAction();
        tryVerify(() => client.requests.length === 1);
        compare(client.requests[0].args.mode, "paused");
        compare(client.requests[0].targetIds, ["covered", "playing"]);
    }

    function test_settingsActionsRemainAvailableWithoutRuntime() {
        const controller = createTemporaryObject(controllerComponent, testCase, {
            runtimeStatus: "absent"
        });
        const configureAction = createTemporaryObject(trayConfigureActionComponent, testCase);
        const desktopLauncher = createTemporaryObject(desktopLauncherComponent, testCase);
        const actionSet = createTemporaryObject(actionSetComponent, testCase, {
            controller, appletConfigureAction: configureAction, desktopSettingsInvoker: desktopLauncher
        });

        const openedUris = [];
        actionSet.externalUrlOpener = function(uri) {
            openedUris.push(uri);
            return true;
        };
        compare(actionSet.popupActions.length, 4);
        verify(actionSet.popupActions[0].enabled);
        verify(!actionSet.popupActions[1].enabled);
        verify(actionSet.popupActions[2].enabled);
        verify(actionSet.popupActions[3].enabled);
        actionSet.popupActions[3].trigger();
        compare(desktopLauncher.openCount, 1);
        actionSet.popupActions[0].trigger();
        compare(openedUris, ["steam://rungameid/431960"]);
        compare(actionSet.contextualActions.length, 3); // Plasma adds Configure once after these.
        compare(actionSet.contextualActions[0].text, "Launch Wallpaper Engine");
    }

    function test_nativeTrayMenuStaysCompactAndHasFourActions() {
        const controller = createTemporaryObject(controllerComponent, testCase, {
            runtimeStatus: "absent"
        });
        const configureAction = createTemporaryObject(trayConfigureActionComponent, testCase);
        const desktopLauncher = createTemporaryObject(desktopLauncherComponent, testCase);
        const actionSet = createTemporaryObject(actionSetComponent, testCase, {
            controller, appletConfigureAction: configureAction, desktopSettingsInvoker: desktopLauncher
        });
        const menu = createTemporaryObject(trayActionsMenuComponent, testCase, {
            actions: actionSet.popupActions
        });

        tryVerify(() => menu.count === 4 && menu.implicitHeight > 0);
        compare(menu.popupType, Popup.Window);
        compare(menu.itemAt(0).text, "Launch Wallpaper Engine");
        compare(menu.itemAt(1).text, "Pause");
        compare(menu.itemAt(2).text, configureAction.text);
        compare(menu.itemAt(3).text, "Desktop and Wallpaper Settings");
        verify(menu.implicitHeight <= 160,
            `Expected a compact window menu, got ${menu.implicitHeight}px`);
        verify(menu.implicitWidth <= 320,
            `Expected a compact menu width, got ${menu.implicitWidth}px`);
        verify(menu.itemAt(0).enabled);
        verify(!menu.itemAt(1).enabled);
        verify(menu.itemAt(2).enabled);
        verify(menu.itemAt(3).enabled);
        menu.itemAt(3).triggered();
        compare(desktopLauncher.openCount, 1);
    }

    function test_popupAndContextualActionUseSameActionMapping() {
        const client = createTemporaryObject(clientComponent, testCase);
        const controller = readyController(client, [target("left", false)]);
        const actionSet = createTemporaryObject(actionSetComponent, testCase, {controller});
        const panel = createTemporaryObject(panelComponent, testCase, {
            controller, actions: actionSet.actions
        });
        verify(actionSet && panel);
        compare(findChild(panel, "runtimeGlobalScopeNote").text,
            "Audio and sizing actions apply globally to all wallpaper screens.");
        const pauseButton = findChild(panel, "runtime-action-pause");
        const pauseContext = actionSet.actions.find(action => action.actionId === "pause");
        verify(pauseButton && pauseContext && pauseButton.enabled && pauseContext.enabled);
        pauseButton.clicked();
        tryCompare(controller, "lastCommandText", "Command accepted; waiting for completion");
        const popupRequest = client.requests[0];
        pauseContext.trigger();
        tryVerify(() => client.requests.length === 2);
        compare(client.requests[1].command, popupRequest.command);
        compare(client.requests[1].scope, popupRequest.scope);
        compare(client.requests[1].targetIds, popupRequest.targetIds);
        compare(client.requests[1].args.mode, popupRequest.args.mode);
    }
}
