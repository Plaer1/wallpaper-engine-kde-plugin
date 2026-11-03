import QtQuick
import QtTest
import "../../panel/contents/ui" as Panel

TestCase {
    id: testCase
    name: "PanelRuntimeClientIntegration"
    when: windowShown
    visible: true
    width: 480
    height: 720

    function target(instanceId, screenLabel) {
        return {
            instanceId,
            containmentId: instanceId,
            screenName: screenLabel,
            screenLabel,
            desiredPlayback: "automatic",
            effectivePlayback: "playing",
            pauseReasons: [],
            wallpaper: {source: "workshop", title: screenLabel, workshopId: "42", backend: "scene"},
            follow: {enabled: false, accountKey: "", status: "disabled", provenance: ""},
            capabilities: ["SetPlayback", "SetGlobalAudioMuted", "SetGlobalSizing",
                "SetFollow", "NextWallpaper", "ReloadWallpaper", "OpenConfiguration"]
        };
    }

    function snapshot(epoch, targets) {
        return {schemaVersion: 1, epoch, revision: 1, targets,
            global: {muted: false, sizingEnabled: false, sizingMode: "crop"}};
    }

    Component {
        id: fakeClientComponent
        QtObject {
            signal ownerChanged(bool available)
            signal stateChanged(var snapshot)
            signal commandFinished(var result)
            property var requests: []
            function execute(request) {
                requests = requests.concat([JSON.parse(JSON.stringify(request))]);
                return Promise.resolve({requestId: request.requestId, status: "accepted"});
            }
        }
    }
    Component {
        id: bridgeComponent
        Panel.RuntimeClientBridge { }
    }
    Component {
        id: controllerComponent
        Panel.RuntimeCommandController { }
    }

    function test_nativeClientAbsentIsInactive() {
        const bridge = createTemporaryObject(bridgeComponent, testCase);
        verify(bridge);
        tryVerify(() => bridge.nativeAvailable === false);
        verify(bridge.activeClient);
        compare(typeof bridge.activeClient.execute, "function");

        const controller = createTemporaryObject(controllerComponent, testCase, {
            runtimeStatus: "absent",
            commandClient: bridge.activeClient
        });
        verify(controller);
        compare(controller.effectiveRuntimeStatus, "absent");
        compare(controller.effectiveSnapshot, null);
        verify(!controller.actionInfo("pause").enabled);
        compare(controller.actionInfo("pause").reason, "No active wallpaper runtime");
    }

    function test_injectedFacadeCommandsAndOwnerReplacementFailClosed() {
        const fakeClient = createTemporaryObject(fakeClientComponent, testCase);
        const bridge = createTemporaryObject(bridgeComponent, testCase, {
            injectedClient: fakeClient
        });
        verify(bridge);
        compare(bridge.activeClient, fakeClient);

        const initial = snapshot("epoch-A", [target("instance-A", "Studio")]);
        const controller = createTemporaryObject(controllerComponent, testCase, {
            runtimeStatus: "ready",
            snapshot: initial,
            commandClient: bridge.activeClient
        });
        verify(controller);
        compare(controller.effectiveRuntimeStatus, "ready");
        verify(controller.actionInfo("pause").enabled);
        const requestId = controller.runAction("pause");
        tryVerify(() => fakeClient.requests.length === 1);
        compare(fakeClient.requests[0].requestId, requestId);
        compare(fakeClient.requests[0].command, "SetPlayback");
        compare(fakeClient.requests[0].targetIds, ["instance-A"]);

        fakeClient.ownerChanged(false);
        compare(controller.effectiveRuntimeStatus, "absent");
        compare(controller.effectiveSnapshot, null);
        verify(!controller.actionInfo("pause").enabled);

        fakeClient.ownerChanged(true);
        compare(controller.effectiveRuntimeStatus, "loading");
        fakeClient.stateChanged(initial);
        compare(controller.effectiveRuntimeStatus, "ready");
        controller.scopeMode = "selected";
        controller.selectedTargetId = "instance-A";

        // The native watcher reports true again when the owner is replaced
        // without an availability gap. The old target choice must be cleared.
        fakeClient.ownerChanged(true);
        compare(controller.effectiveRuntimeStatus, "loading");
        compare(controller.effectiveSnapshot, null);
        compare(controller.selectedTargetId, "");
        fakeClient.stateChanged(snapshot("epoch-B", [target("instance-A", "Studio")]));
        compare(controller.effectiveRuntimeStatus, "ready");
        verify(!controller.actionInfo("pause").enabled);
        compare(controller.runAction("pause"), "");
        compare(fakeClient.requests.length, 1);
    }
}
