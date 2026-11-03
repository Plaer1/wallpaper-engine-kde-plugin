import QtQuick
import QtTest
import "../../plugin/contents/ui/components" as Components

TestCase {
    name: "RuntimeNextController"

    property var completions: []
    property var refreshes: []
    property var selections: []

    Components.RuntimeNextController {
        id: controller
        targetActive: true
        instanceId: "target-a"
        currentSource: "source-a"
        currentWorkshopId: "1"
        randomValue: () => 0.99
        onRefreshRequested: (requestId, generation) => {
            const copy = refreshes.slice(); copy.push({requestId, generation}); refreshes = copy;
        }
        onSelectionRequested: (requestId, targetId, source, workshopId, globalMode) => {
            const copy = selections.slice();
            copy.push({requestId, targetId, source, workshopId, globalMode});
            selections = copy;
        }
        onCompleted: (requestId, targetId, applied, errorCode, message) => {
            const copy = completions.slice();
            copy.push({requestId, targetId, applied, errorCode, message});
            completions = copy;
        }
    }

    function init() {
        completions = [];
        refreshes = [];
        selections = [];
        controller.targetActive = true;
        controller.instanceId = "target-a";
        controller.followEnabled = false;
        controller.globalMode = false;
        controller.currentSource = "source-a";
        controller.currentWorkshopId = "1";
        controller.randomValue = () => 0.99;
        controller.cancel("testReset", "reset");
    }

    function test_selectsAlternativeFromFilteredModelAndWaitsForReadback() {
        verify(controller.request("next-1", "target-a"));
        compare(refreshes.length, 1);
        const generation = refreshes[0].generation;
        verify(controller.modelRefreshed("next-1", generation, [
            {source: "source-a", workshopId: "1"}, // current selection is excluded
            {source: "source-b", workshopId: "2"},
            {source: "source-b", workshopId: "2"}, // duplicates are collapsed
            {source: "", workshopId: "3"}
        ]));
        compare(selections.length, 1);
        compare(selections[0].source, "source-b");
        compare(selections[0].workshopId, "2");
        compare(completions.length, 0);
        verify(!controller.confirmSelection("source-a", "1", "source-b", "2", false));
        verify(controller.confirmSelection("source-b", "2", "source-b", "2", false));
        compare(completions.length, 1);
        verify(completions[0].applied);
    }

    function test_followNoAlternativeAndOverlappingRequestsReject() {
        controller.followEnabled = true;
        verify(!controller.request("follow-blocked", "target-a"));
        compare(completions[0].errorCode, "followActive");
        controller.followEnabled = false;

        verify(controller.request("next-empty", "target-a"));
        const generation = refreshes[0].generation;
        verify(!controller.request("next-overlap", "target-a"));
        compare(completions[1].errorCode, "nextBusy");
        verify(!controller.modelRefreshed("next-empty", generation, [
            {source: "source-a", workshopId: "1"}
        ]));
        compare(completions[2].errorCode, "noAlternative");
        compare(selections.length, 0);
    }

    function test_staleRefreshSourceChangeAndTargetRemovalAreRejected() {
        verify(controller.request("next-stale", "target-a"));
        const generation = refreshes[0].generation;
        verify(!controller.modelRefreshed("other-request", generation, [
            {source: "source-b", workshopId: "2"}
        ]));
        controller.currentSource = "source-external";
        compare(completions[0].errorCode, "selectionChanged");
        verify(!controller.modelRefreshed("next-stale", generation, [
            {source: "source-b", workshopId: "2"}
        ]));

        controller.currentSource = "source-a";
        verify(controller.request("next-remove", "target-a"));
        controller.targetActive = false;
        compare(completions[1].errorCode, "targetRemoved");
    }

    function test_globalModeIsCapturedAndMustRemainEnabledToConfirm() {
        controller.globalMode = true;
        verify(controller.request("next-global", "target-a"));
        const generation = refreshes[0].generation;
        verify(controller.modelRefreshed("next-global", generation,
            [{source: "shared-source", workshopId: "9"}]));
        compare(selections[0].globalMode, true);
        verify(!controller.confirmSelection("shared-source", "9", "shared-source", "9", false));
        compare(completions[0].errorCode, "selectionChanged");
    }
}
