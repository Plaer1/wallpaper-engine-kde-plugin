import QtQuick
import QtTest
import "../../plugin/contents/ui/components" as Components

TestCase {
    name: "RuntimeReloadController"
    when: windowShown
    visible: true
    width: 320
    height: 240

    property var generations: []
    property var completions: []
    property string sessionMode: "paused"

    Components.RuntimeReloadController {
        id: controller
        targetActive: true
        instanceId: "target-a"
        timeoutInterval: 40
        onReloadBackendRequested: generation => {
            const next = generations.slice();
            next.push(generation); generations = next;
        }
        onCompleted: (requestId, targetId, applied, errorCode, message) => {
            const next = completions.slice();
            next.push({requestId, targetId, applied, errorCode, message});
            completions = next;
        }
    }

    function init() {
        generations = [];
        completions = [];
        sessionMode = "paused";
        controller.targetActive = true;
        controller.generation = 0;
    }

    function test_fakeBackendFirstFrameCompletesOnlyMatchingGeneration() {
        verify(controller.request("reload-1", "target-a"));
        compare(generations.length, 1);
        const requestedGeneration = generations[0];
        verify(!controller.observeFirstFrame(requestedGeneration - 1, "late-old-backend"));
        compare(completions.length, 0);
        verify(controller.observeFirstFrame(requestedGeneration, "scene"));
        compare(completions.length, 1);
        verify(completions[0].applied);
        compare(completions[0].requestId, "reload-1");
        compare(sessionMode, "paused"); // Reload does not change session intent.
    }

    function test_fakeBackendFailureTimesOutAndTargetRemovalFailsPending() {
        verify(controller.request("reload-timeout", "target-a"));
        tryVerify(() => completions.length === 1, 500);
        verify(!completions[0].applied);
        compare(completions[0].errorCode, "reloadUnconfirmed");

        verify(controller.request("reload-remove", "target-a"));
        controller.targetActive = false;
        compare(completions.length, 2);
        compare(completions[1].errorCode, "targetRemoved");
    }

    function test_wrongTargetRejectedAndNoSelectionMutation() {
        verify(!controller.request("reload-stale", "target-old"));
        compare(completions.length, 1);
        compare(completions[0].errorCode, "unverifiedTarget");
        compare(sessionMode, "paused");
    }
}
