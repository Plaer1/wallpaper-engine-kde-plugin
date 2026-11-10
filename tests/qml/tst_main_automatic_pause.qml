import QtQuick
import QtTest
import "../../plugin/contents/ui/components" as Components

TestCase {
    name: "MainAutomaticPause"

    QtObject {
        id: fakeWindowModel
        property bool reqPause: false
        // Represents WindowModel's screen-unfiltered fullscreen task condition.
        property bool globalFullscreenPause: false
    }

    QtObject {
        id: fakePowerSource
        property bool reqPause: false
    }

    QtObject {
        id: fakeGameMode
        property bool reqPause: false
    }
    QtObject {
        id: fakeSession
        property bool sleeping: false
        property bool inactive: false
    }

    Component {
        id: resolverComponent
        Components.AutomaticPauseResolver {
            windowPause: fakeWindowModel.reqPause
            fullscreenPause: fakeWindowModel.globalFullscreenPause
            batteryPause: fakePowerSource.reqPause
            gameModePause: fakeGameMode.reqPause
            sleepPause: fakeSession.sleeping
            inactiveSessionPause: fakeSession.inactive
        }
    }

    Component {
        id: fakeBackendComponent
        QtObject {
            property string state: "new"
            function play() { state = "playing"; }
            function pause() { state = "paused"; }
        }
    }

    function applyGate(resolver, backend) {
        if (!backend) return;
        resolver.canPlay ? backend.play() : backend.pause();
    }

    function init() {
        fakeWindowModel.reqPause = false;
        fakeWindowModel.globalFullscreenPause = false;
        fakePowerSource.reqPause = false;
        fakeGameMode.reqPause = false;
        fakeSession.sleeping = false;
        fakeSession.inactive = false;
    }

    function test_crossScreenFullscreenPausesEvenWhenOrdinaryModeWouldNot() {
        const resolver = createTemporaryObject(resolverComponent, this);
        verify(resolver);
        compare(resolver.canPlay, true);

        // Ordinary PauseMode::Never yields reqPause=false, while another
        // output's fullscreen condition remains an independent pause input.
        fakeWindowModel.globalFullscreenPause = true;
        tryCompare(resolver, "canPlay", false);
        fakeWindowModel.globalFullscreenPause = false;
        tryCompare(resolver, "canPlay", true);
    }

    function test_sharedWindowPolicyPausesAllTargets() {
        const resolver = createTemporaryObject(resolverComponent, this);
        verify(resolver);
        // Primary coverage and the selected simple mode are one shared input.
        fakeWindowModel.reqPause = true;
        compare(resolver.canPlay, false);
        compare(resolver.pauseReasons, ["window"]);

        verify(resolver.setPlaybackMode("playDespiteWindows"));
        compare(resolver.canPlay, true);
        compare(resolver.pauseReasons, []);

        fakeWindowModel.reqPause = false;
        compare(resolver.canPlay, true);
    }

    function test_ordinaryWindowBatteryAndGameModeReasonsRemainEffective() {
        const resolver = createTemporaryObject(resolverComponent, this);
        verify(resolver);

        fakeWindowModel.reqPause = true;
        tryCompare(resolver, "canPlay", false);
        fakeWindowModel.reqPause = false;
        tryCompare(resolver, "canPlay", true);

        fakePowerSource.reqPause = true;
        tryCompare(resolver, "canPlay", false);
        fakePowerSource.reqPause = false;
        fakeGameMode.reqPause = true;
        tryCompare(resolver, "canPlay", false);
    }

    function test_playDespiteWindowsOverridesWindowAndFullscreenButKeepsHardPauses() {
        const resolver = createTemporaryObject(resolverComponent, this);
        verify(resolver);
        fakeWindowModel.reqPause = true;
        fakeWindowModel.globalFullscreenPause = true;
        verify(resolver.setPlaybackMode("playDespiteWindows"));
        compare(resolver.canPlay, true);
        compare(resolver.pauseReasons, []);

        fakeWindowModel.globalFullscreenPause = false;
        tryCompare(resolver, "canPlay", true);
        compare(resolver.pauseReasons, []);

        fakePowerSource.reqPause = true;
        tryCompare(resolver, "canPlay", false);
        fakePowerSource.reqPause = false;
        fakeGameMode.reqPause = true;
        tryCompare(resolver, "canPlay", false);
        compare(resolver.pauseReasons, ["gameMode"]);
        fakeGameMode.reqPause = false;

        fakeSession.sleeping = true;
        tryCompare(resolver, "canPlay", false);
        compare(resolver.pauseReasons, ["sleep"]);
        fakeSession.sleeping = false;
        fakeSession.inactive = true;
        tryCompare(resolver, "canPlay", false);
        compare(resolver.pauseReasons, ["inactiveSession"]);
        fakeSession.inactive = false;

        verify(resolver.setPlaybackMode("automatic"));
        tryCompare(resolver, "canPlay", false);
        compare(resolver.pauseReasons, ["window"]);
        fakeWindowModel.reqPause = false;
        tryCompare(resolver, "canPlay", true);
        verify(!resolver.setPlaybackMode("unsupported"));
        compare(resolver.playbackMode, "automatic");
    }

    function test_pauseReasonsDescribePolicyInStableOrder() {
        const resolver = createTemporaryObject(resolverComponent, this);
        verify(resolver);
        fakeWindowModel.reqPause = true;
        fakeWindowModel.globalFullscreenPause = true;
        fakePowerSource.reqPause = true;
        fakeGameMode.reqPause = true;
        verify(resolver.setPlaybackMode("paused"));
        compare(resolver.pauseReasons,
            ["manual", "battery", "gameMode", "fullscreen", "window"]);

        verify(resolver.setPlaybackMode("playDespiteWindows"));
        compare(resolver.pauseReasons, ["battery", "gameMode"]);
    }

    function test_manualPauseSurvivesBackendReplacementAndAutomaticResume() {
        const resolver = createTemporaryObject(resolverComponent, this);
        verify(resolver);
        verify(resolver.setPlaybackMode("paused"));

        const firstBackend = createTemporaryObject(fakeBackendComponent, this);
        verify(firstBackend);
        applyGate(resolver, firstBackend);
        compare(firstBackend.state, "paused");

        // The wallpaper root owns the resolver while the backend Loader
        // destroys/recreates only its renderer item.
        firstBackend.destroy();
        const replacementBackend = createTemporaryObject(fakeBackendComponent, this);
        verify(replacementBackend);
        applyGate(resolver, replacementBackend);
        compare(replacementBackend.state, "paused");

        verify(resolver.setPlaybackMode("automatic"));
        applyGate(resolver, replacementBackend);
        compare(replacementBackend.state, "playing");
        fakePowerSource.reqPause = true;
        applyGate(resolver, replacementBackend);
        compare(replacementBackend.state, "paused");
    }
}
