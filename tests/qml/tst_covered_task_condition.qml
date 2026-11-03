import QtQuick
import QtTest
import "../../plugin/contents/ui/components" as Components

TestCase {
    name: "CoveredTaskCondition"

    QtObject {
        id: fakeTasks
        property var tasks: []
        property int count: tasks.length
        property int propertyReads: 0
        signal dataChanged()

        function getProperty(row, role) {
            propertyReads++;
            return tasks[row] ? tasks[row][role] : undefined;
        }
        function replaceTasks(value) {
            tasks = value;
            dataChanged();
        }
    }

    QtObject {
        id: fakeCalculator
        property int calls: 0
        property var lastTargets: []
        property var lastWindows: []
        property var lastWasPaused: undefined
        property var lastShowingDesktop: undefined
        property var lastRegionVerified: undefined

        function unionCoverage(target, windows) {
            if (!target || target.width <= 0 || target.height <= 0)
                return {area: 0, total: 0};
            const left = target.x;
            const right = target.x + target.width;
            const top = target.y;
            const bottom = target.y + target.height;
            const xEdges = [left, right];
            windows.forEach(function(window) {
                if (!window || typeof window.x !== "number") return;
                const x1 = Math.max(left, window.x);
                const x2 = Math.min(right, window.x + window.width);
                if (x2 > x1) xEdges.push(x1, x2);
            });
            xEdges.sort((a, b) => a - b);
            let area = 0;
            for (let i = 0; i + 1 < xEdges.length; ++i) {
                const x1 = xEdges[i];
                const x2 = xEdges[i + 1];
                if (x2 <= x1) continue;
                const middle = (x1 + x2) / 2;
                const intervals = [];
                windows.forEach(function(window) {
                    if (!window || middle < window.x || middle >= window.x + window.width) return;
                    const y1 = Math.max(top, window.y);
                    const y2 = Math.min(bottom, window.y + window.height);
                    if (y2 > y1) intervals.push([y1, y2]);
                });
                intervals.sort((a, b) => a[0] - b[0]);
                let coveredY = 0;
                let start = null;
                let end = null;
                intervals.forEach(function(interval) {
                    if (start === null) {
                        start = interval[0];
                        end = interval[1];
                    } else if (interval[0] <= end) {
                        end = Math.max(end, interval[1]);
                    } else {
                        coveredY += end - start;
                        start = interval[0];
                        end = interval[1];
                    }
                });
                if (start !== null) coveredY += end - start;
                area += (x2 - x1) * coveredY;
            }
            return {area: area, total: target.width * target.height};
        }

        function evaluate(targets, windows, wasPaused, showingDesktop, regionVerified) {
            calls++;
            lastTargets = targets;
            lastWindows = windows;
            lastWasPaused = wasPaused;
            lastShowingDesktop = showingDesktop;
            lastRegionVerified = regionVerified;

            if (showingDesktop === undefined || typeof showingDesktop !== "boolean")
                return {decision: "unknown", known: false, fraction: 0};
            if (showingDesktop)
                return {decision: "visible", known: true, fraction: 0, override: "showDesktop"};
            if (!regionVerified || !targets.length || typeof wasPaused !== "boolean")
                return {decision: "unknown", known: false, fraction: 0};

            const coverage = unionCoverage(targets[0], windows);
            const fraction = coverage.total ? coverage.area / coverage.total : 0;
            const paused = wasPaused ? fraction >= 0.95 : fraction >= 0.98;
            return {decision: paused ? "paused" : "visible", known: true, fraction: fraction};
        }
    }

    Component {
        id: conditionComponent
        Components.CoveredTaskCondition {
            taskModel: fakeTasks
            coverageCalculator: fakeCalculator
            enabled: true
            screenScoped: true
            usableRegion: [Qt.rect(0, 0, 100, 100)]
            screenOrigin: Qt.point(-100, 50)
            usableRegionVerified: true
            showingDesktopKnown: true
            showingDesktop: false
            currentVirtualDesktop: 1
            activity: "current-activity"
        }
    }

    function task(overrides) {
        return Object.assign({
            IsWindow: true,
            IsMinimized: false,
            Geometry: Qt.rect(-100, 50, 10, 10),
            VirtualDesktops: [1],
            Activities: ["current-activity"],
            ScreenGeometry: Qt.rect(-100, 50, 100, 100)
        }, overrides || {});
    }

    function init() {
        fakeTasks.replaceTasks([]);
        fakeTasks.propertyReads = 0;
        fakeCalculator.calls = 0;
        fakeCalculator.lastTargets = [];
        fakeCalculator.lastWindows = [];
        fakeCalculator.lastWasPaused = undefined;
        fakeCalculator.lastShowingDesktop = undefined;
        fakeCalculator.lastRegionVerified = undefined;
    }

    function makeCondition() {
        const condition = createTemporaryObject(conditionComponent, this);
        verify(condition);
        tryVerify(() => fakeCalculator.calls === 1, 1000);
        return condition;
    }

    function test_translatesUsableRegionAndUnionsTiledOverlappingWindows() {
        const condition = makeCondition();
        fakeTasks.replaceTasks([
            task({Geometry: Qt.rect(-100, 50, 50, 100)}),
            task({Geometry: Qt.rect(-75, 50, 50, 100)})
        ]);
        tryVerify(() => fakeCalculator.calls === 2, 500);
        compare(condition.decision, "visible"); // 100% summed area, but only 75% union.

        fakeTasks.replaceTasks([
            task({Geometry: Qt.rect(-100, 50, 50, 100)}),
            task({Geometry: Qt.rect(-50, 50, 50, 100),
                ScreenGeometry: Qt.rect(0, 50, 100, 100)}),
            task({Geometry: Qt.rect(-100, 50, 50, 100)})
        ]);
        tryVerify(() => fakeCalculator.calls === 3, 500);

        compare(condition.decision, "paused");
        compare(fakeCalculator.lastTargets.length, 1);
        compare(fakeCalculator.lastTargets[0].x, -100);
        compare(fakeCalculator.lastTargets[0].y, 50);
        compare(fakeCalculator.lastWindows.length, 3);
        compare(fakeCalculator.lastWindows[1].x, -50); // Cross-output geometry is retained.
        verify(fakeCalculator.lastWindows[1].x + fakeCalculator.lastWindows[1].width > -1);
    }

    function test_globalPrimaryWorkAreaUsesGlobalTaskGeometryAcrossOutputs() {
        const condition = makeCondition();
        // Plasma Corona supplies the primary usable region in global logical
        // coordinates; do not translate it by a wallpaper output origin.
        condition.screenOrigin = Qt.point(0, 0);
        condition.usableRegion = [Qt.rect(0, 40, 100, 60)];
        condition.outputCount = 2;
        condition.mirrorScene = false;
        condition.spanScreens = false;
        fakeTasks.replaceTasks([
            task({Geometry: Qt.rect(0, 40, 50, 60)}),
            task({Geometry: Qt.rect(50, 40, 50, 60)}),
            task({Geometry: Qt.rect(100, 40, 100, 60),
                ScreenGeometry: Qt.rect(100, 0, 100, 100)})
        ]);
        tryVerify(() => fakeCalculator.calls === 2, 500);

        verify(condition.supported);
        verify(condition.active);
        compare(fakeCalculator.lastTargets.length, 1);
        compare(fakeCalculator.lastTargets[0].x, 0);
        compare(fakeCalculator.lastTargets[0].y, 40);
        compare(fakeCalculator.lastWindows.length, 3);
        compare(fakeCalculator.lastWindows[0].x, 0);
        compare(fakeCalculator.lastWindows[1].x, 50);
    }

    function test_minimized_otherActivityAndOtherDesktopAreExcluded() {
        const condition = makeCondition();
        fakeTasks.replaceTasks([
            task({Geometry: Qt.rect(-100, 50, 100, 100), IsMinimized: true}),
            task({Geometry: Qt.rect(-100, 50, 100, 100), Activities: ["other-activity"]}),
            task({Geometry: Qt.rect(-100, 50, 100, 100), VirtualDesktops: [2]}),
            task({Geometry: Qt.rect(-100, 50, 25, 100)})
        ]);
        tryVerify(() => fakeCalculator.calls === 2, 500);

        compare(fakeCalculator.lastWindows.length, 1);
        compare(condition.decision, "visible");
    }

    function test_showDesktopOverridesCoverageAndUnknownStateStaysUnknown() {
        const condition = makeCondition();
        fakeTasks.replaceTasks([task({Geometry: Qt.rect(-100, 50, 100, 100)})]);
        tryVerify(() => fakeCalculator.calls === 2, 500);
        compare(condition.decision, "paused");

        condition.usableRegionVerified = false;
        condition.showingDesktop = true;
        tryVerify(() => condition.decision === "visible", 500);
        compare(fakeCalculator.lastRegionVerified, false);
        compare(fakeCalculator.lastShowingDesktop, true);

        condition.showingDesktopKnown = false;
        tryVerify(() => condition.decision === "unknown", 500);
        compare(fakeCalculator.lastShowingDesktop, undefined);
    }

    function test_unscopedCoveredPauseIsUnsupportedAndNeverPausesFromOneOutput() {
        const condition = makeCondition();
        fakeTasks.replaceTasks([task({Geometry: Qt.rect(-100, 50, 100, 100)})]);
        tryVerify(() => fakeCalculator.calls === 2, 500);
        compare(condition.decision, "paused");
        verify(condition.active);

        condition.screenScoped = false;
        tryVerify(() => condition.decision === "unknown", 500);
        verify(!condition.supported);
        verify(!condition.active);
        verify(condition.unavailableReason.includes("all-output"));
        compare(fakeCalculator.lastRegionVerified, false);

        // Show Desktop remains an unconditional resume override even though
        // all-output coverage aggregation is unsupported.
        condition.showingDesktop = true;
        tryVerify(() => condition.decision === "visible", 500);
        verify(!condition.active);
    }

    function test_mirroredOrSpannedMultiOutputCoverageFailsOpen() {
        const condition = makeCondition();
        fakeTasks.replaceTasks([task({Geometry: Qt.rect(-100, 50, 100, 100)})]);
        tryVerify(() => fakeCalculator.calls === 2, 500);
        verify(condition.active);

        condition.outputCount = 2;
        tryVerify(() => condition.decision === "paused", 500);
        verify(condition.active); // Independent wallpaper coverage remains per output.

        condition.mirrorScene = true;
        tryVerify(() => condition.decision === "unknown", 500);
        verify(!condition.supported);
        verify(!condition.active);
        verify(condition.unavailableReason.includes("mirrored or spanned"));
        compare(fakeCalculator.lastRegionVerified, false);
    }

    function test_eventBurstsCoalesceAndContinuousUpdatesDoNotStarve() {
        const condition = makeCondition();
        const start = fakeCalculator.calls;
        for (let i = 0; i < 8; ++i)
            fakeTasks.dataChanged();
        tryVerify(() => fakeCalculator.calls === start + 1, 500);
        wait(150);
        compare(fakeCalculator.calls, start + 1);

        const continuousStart = fakeCalculator.calls;
        for (let i = 0; i < 12; ++i) {
            fakeTasks.dataChanged();
            wait(25);
        }
        tryVerify(() => fakeCalculator.calls >= continuousStart + 2, 500);
    }

    function test_noIdlePolling() {
        const condition = makeCondition();
        fakeTasks.replaceTasks([task()]);
        tryVerify(() => fakeCalculator.calls === 2, 500);
        const reads = fakeTasks.propertyReads;
        const calls = fakeCalculator.calls;
        wait(250);
        compare(fakeTasks.propertyReads, reads);
        compare(fakeCalculator.calls, calls);
        verify(condition !== null);
    }
}
