import QtQuick
import QtTest
import "../../panel/contents/ui" as Panel

TestCase {
    id: testCase
    name: "PanelRuntimeSnapshot"
    when: windowShown
    visible: true
    width: 480
    height: 560

    function target(screenName, state, reasons, containmentId) {
        return {
            instanceId: "instance-1",
            containmentId: containmentId || 41,
            screenName,
            screenLabel: screenName,
            desiredPlayback: "automatic",
            effectivePlayback: state,
            pauseReasons: reasons || [],
            wallpaper: {source: "workshop", title: "Morning Garden", workshopId: "123", backend: "scene"},
            follow: {enabled: false, accountKey: "", status: "disabled", provenance: ""},
            capabilities: {}
        };
    }

    function snapshot(targets) {
        return {schemaVersion: 1, epoch: "epoch-1", revision: 1, targets, global: {}};
    }

    Component {
        id: viewComponent
        Panel.RuntimeSnapshotView {
            anchors.fill: parent
        }
    }

    Component {
        id: buttonComponent
        Panel.TrayStatusButton { }
    }

    Component {
        id: spyComponent
        SignalSpy { }
    }

    function test_absentAndLoadingStates() {
        const view = createTemporaryObject(viewComponent, testCase, {runtimeStatus: "absent"});
        verify(view);
        compare(view.statusSummary, "No active wallpaper runtime");
        compare(view.tooltipSummary, "No active wallpaper runtime");
        compare(findChild(view, "globalScopeLabel").visible, false);

        view.runtimeStatus = "loading";
        compare(view.statusSummary, "Connecting to wallpaper runtime…");
        compare(view.tooltipSummary, view.statusSummary);
    }

    function test_errorStateIsVisibleAndExplained() {
        const view = createTemporaryObject(viewComponent, testCase, {
            runtimeStatus: "error", errorMessage: "Service name vanished"
        });
        verify(view);
        compare(view.statusSummary, "Wallpaper runtime unavailable");
        compare(view.tooltipSummary, "Wallpaper runtime unavailable · Service name vanished");
        verify(findChild(view, "runtimeErrorMessage").visible);
        compare(findChild(view, "runtimeErrorMessage").text, "Service name vanished");
    }

    function test_activeAndTargetLabels() {
        const view = createTemporaryObject(viewComponent, testCase, {
            runtimeStatus: "ready", snapshot: snapshot([target("Living room", "playing")])
        });
        verify(view);
        compare(view.statusSummary, "Wallpaper active");
        compare(view.tooltipSummary, "Wallpaper active · Living room");
        verify(findChild(view, "globalScopeLabel").visible);
        compare(findChild(view, "globalScopeLabel").text, "Status for all wallpaper screens");
        compare(findChild(view, "targetLabel-41").text, "Target: Living room");
        compare(findChild(view, "targetPlayback-41").text, "Playing");
        compare(findChild(view, "targetWallpaper-41").text, "Morning Garden");
    }

    function test_pausedAndMultipleTargets() {
        const view = createTemporaryObject(viewComponent, testCase, {
            runtimeStatus: "ready",
            snapshot: snapshot([
                target("Studio display", "paused", ["window", "battery"]),
                target("Bedroom", "paused", ["manual"], 42)
            ])
        });
        verify(view);
        compare(view.statusSummary, "Paused");
        compare(view.tooltipSummary, "Paused · Studio display, Bedroom");
        compare(findChild(view, "targetLabel-41").text, "Target: Studio display");
        compare(findChild(view, "targetPauseReasons-41").text,
            "Paused: window coverage (fullscreen apps pause all wallpaper screens), battery");
        compare(findChild(view, "targetPauseReasons-42").text, "Paused: manual");
    }

    function test_targetLoadingErrorAndUnknownAreNotReportedAsPlaying() {
        const loading = createTemporaryObject(viewComponent, testCase, {
            runtimeStatus: "ready", snapshot: snapshot([target("Panel", "loading")])
        });
        verify(loading);
        compare(loading.statusSummary, "Wallpaper loading");
        compare(findChild(loading, "targetPlayback-41").text, "Loading");

        loading.snapshot = snapshot([target("Panel", "error")]);
        compare(loading.statusSummary, "Wallpaper error");
        compare(findChild(loading, "targetPlayback-41").text, "Error");

        loading.snapshot = snapshot([target("Panel", "unknown")]);
        compare(loading.statusSummary, "Wallpaper state unknown");
        compare(findChild(loading, "targetPlayback-41").text, "State unknown");
    }

    function test_workshopAndFolderLinksAcceptOnlyValidatedCurrentSources() {
        const validTarget = target("Studio", "playing", [], 51);
        validTarget.wallpaper.workshopId = "76561190000000001";
        validTarget.wallpaper.source = "file:///home/user/Wallpaper%20Folder/scene.pkg+scene";
        const view = createTemporaryObject(viewComponent, testCase, {
            runtimeStatus: "ready", snapshot: snapshot([validTarget])
        });
        verify(view);
        compare(view.validatedWorkshopUrl(validTarget.wallpaper),
            "https://steamcommunity.com/sharedfiles/filedetails/?id=76561190000000001");
        compare(view.validatedFolderUrl(validTarget.wallpaper), "file:///home/user/Wallpaper%20Folder");
        verify(findChild(view, "openWorkshop-51").visible);
        verify(findChild(view, "openFolder-51").visible);

        const invalidTarget = target("Untrusted", "playing", [], 52);
        invalidTarget.wallpaper.workshopId = "12345&redirect=https://evil.invalid";
        invalidTarget.wallpaper.source = "https://evil.invalid/../../folder";
        view.snapshot = snapshot([validTarget, invalidTarget]);
        compare(view.validatedWorkshopUrl(invalidTarget.wallpaper), "");
        compare(view.validatedFolderUrl(invalidTarget.wallpaper), "");
        compare(findChild(view, "openWorkshop-52").visible, false);
        compare(findChild(view, "openFolder-52").visible, false);
    }

    function test_emptyAndUnsupportedSnapshotsFailClosed() {
        const view = createTemporaryObject(viewComponent, testCase, {
            runtimeStatus: "ready", snapshot: snapshot([])
        });
        verify(view);
        compare(view.statusSummary, "No active wallpaper targets");
        view.snapshot = {schemaVersion: 99, targets: [target("Hidden", "playing")]};
        compare(view.statusSummary, "Wallpaper runtime state is unsupported");
        compare(view.targets.length, 0);
    }

    function test_compactButtonHasAccessibleTargetAndKeyboardActivation() {
        const button = createTemporaryObject(buttonComponent, testCase, {
            statusText: "Paused", targetText: "Paused · Study monitor",
            iconSource: "../../panel/contents/icons/eye-gear-1-light.svg"
        });
        verify(button);
        compare(button.Accessible.name, "Wallpaper Engine status: Paused. Open status popup");
        compare(button.Accessible.description, "Paused · Study monitor");
        const activated = createTemporaryObject(spyComponent, testCase, {
            target: button, signalName: "activated"
        });
        button.forceActiveFocus();
        keyClick(Qt.Key_Space);
        tryCompare(activated, "count", 1);
        button.forceActiveFocus();
        keyClick(Qt.Key_Return);
        tryCompare(activated, "count", 2);
    }
}
