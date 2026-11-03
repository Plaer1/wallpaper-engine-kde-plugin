import QtQuick
import QtTest
import "../../plugin/contents/ui" as WallpaperUi

TestCase {
    name: "WindowPausePolicySingleton"

    function test_sharedPolicyLoadsAndNormalizesSharedSettings() {
        compare(WallpaperUi.WindowPausePolicy.evaluationInterval, 100);
        compare(WallpaperUi.WindowPausePolicy.normalizeMode(5), 0);
        compare(WallpaperUi.WindowPausePolicy.normalizeMode(6), 6);
        compare(WallpaperUi.WindowPausePolicy.normalizeDelay(0), 1);
        compare(WallpaperUi.WindowPausePolicy.normalizeDelay(60001), 60000);
    }

    function test_roleFilteringTreatsEmptyRolesAsAllAndSkipsTitles() {
        const policy = WallpaperUi.WindowPausePolicy;
        compare(policy.roleChanged([]), true);
        compare(policy.roleChanged([policy.roleId("AppName")]), false);
        compare(policy.roleChanged([policy.roleId("Geometry")]), true);
    }

    function test_fixedWindowCoalescingHasNoIdleEvaluations() {
        const policy = WallpaperUi.WindowPausePolicy;
        wait(140);
        const baseline = policy.evaluationCount;
        policy.scheduleEvaluation();
        policy.scheduleEvaluation();
        policy.scheduleEvaluation();
        tryCompare(policy, "evaluationCount", baseline + 1, 500);
        wait(150);
        compare(policy.evaluationCount, baseline + 1);
    }

    function test_resumeDelayStartsOnceForPausedToVisibleTransition() {
        const policy = WallpaperUi.WindowPausePolicy;
        const oldDelay = policy.resumeDelay;
        policy.resumeDelay = 120;
        policy.publishOrdinaryPause(true, true);
        policy.publishOrdinaryPause(false, false);
        wait(70);
        policy.publishOrdinaryPause(false, false);
        wait(70);
        compare(policy.windowPause, false);
        policy.resumeDelay = oldDelay;
        policy.publishOrdinaryPause(false, true);
    }

    function test_primaryWorkareaChangeResetsCoverageHysteresis() {
        const policy = WallpaperUi.WindowPausePolicy;
        policy._coverageWasPaused = true;
        policy.onPrimaryScreenChanged();
        compare(policy._coverageWasPaused, false);
    }
}
