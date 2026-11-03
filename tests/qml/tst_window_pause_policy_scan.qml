import QtQuick
import QtTest
import "../../plugin/contents/ui/js/window-pause-policy.mjs" as Policy

TestCase {
    name: "WindowPausePolicyScan"

    function model(tasks) {
        return {
            count: tasks.length,
            reads: [],
            makeModelIndex: function(row) { return row; },
            getProperty: function(row, role) {
                this.reads.push(role);
                return tasks[row][role];
            }
        };
    }

    function task(overrides) {
        return Object.assign({
            IsWindow: true,
            IsMinimized: false,
            IsHidden: false,
            Activities: ["current"],
            IsFullScreen: false,
            IsMaximized: false,
            IsActive: false,
            ScreenGeometry: Qt.rect(0, 0, 1920, 1080),
            Geometry: Qt.rect(0, 30, 1920, 1050)
        }, overrides || {});
    }

    function test_fullscreenIsGlobalAndShortCircuits() {
        const source = model([
            task({ScreenGeometry: Qt.rect(1920, 0, 1920, 1080)}),
            task({IsFullScreen: true, ScreenGeometry: Qt.rect(1920, 0, 1920, 1080)})
        ]);
        const result = Policy.scan(source, 3, "current", Qt.rect(0, 0, 1920, 1080));
        verify(result.fullscreen);
        verify(!result.windowPause);
        compare(result.rowsVisited, 2);
        verify(result.geometryReads === 0);
    }

    function test_simpleModesUsePrimaryAndEarlyExit() {
        const source = model([
            task({ScreenGeometry: Qt.rect(1920, 0, 1920, 1080), IsActive: true}),
            task({IsActive: true}),
            task({IsActive: true})
        ]);
        const result = Policy.scan(source, 3, "current", Qt.rect(0, 0, 1920, 1080));
        verify(result.windowPause);
        compare(result.rowsVisited, 3);
        compare(result.geometryReads, 0);
        verify(source.reads.indexOf("Geometry") < 0);
    }

    function test_laterSecondaryFullscreenStillHardPauses() {
        const source = model([
            task({IsActive: true}),
            task({IsFullScreen: true, ScreenGeometry: Qt.rect(1920, 0, 1920, 1080)})
        ]);
        const result = Policy.scan(source, 3, "current", Qt.rect(0, 0, 1920, 1080));
        verify(result.windowPause);
        verify(result.fullscreen);
        compare(result.rowsVisited, 2);
        compare(result.geometryReads, 0);
    }

    function test_modeZeroOnlyScansFullscreen() {
        const source = model([task({IsActive: true, IsMaximized: true})]);
        const result = Policy.scan(source, 0, "current", Qt.rect(0, 0, 1920, 1080));
        verify(!result.fullscreen);
        verify(!result.windowPause);
        compare(result.geometryReads, 0);
        verify(source.reads.indexOf("ScreenGeometry") < 0);
        verify(source.reads.indexOf("Geometry") < 0);
        verify(source.reads.indexOf("IsActive") < 0);
        verify(source.reads.indexOf("IsMaximized") < 0);
    }

    function test_eachSimplePauseModePredicate() {
        const screen = Qt.rect(0, 0, 1920, 1080);
        const any = Policy.scan(model([task()]), 1, "current", screen);
        verify(any.windowPause);

        const max = Policy.scan(model([task({IsMaximized: true})]), 2, "current", screen);
        verify(max.windowPause);

        const focus = Policy.scan(model([task({IsActive: true})]), 3, "current", screen);
        verify(focus.windowPause);

        const focusOrMax = Policy.scan(model([task({IsMaximized: true})]), 4,
                                       "current", screen);
        verify(focusOrMax.windowPause);

        const otherOutput = Policy.scan(model([task({
            IsActive: true, IsMaximized: true, ScreenGeometry: Qt.rect(1920, 0, 1920, 1080)
        })]), 4, "current", screen);
        verify(!otherOutput.windowPause);
    }

    function test_nonmatchingPredicateSkipsScreenGeometryAndMaxShortCircuitsFocus() {
        const screen = Qt.rect(0, 0, 1920, 1080);
        const inactive = model([task()]);
        const noFocus = Policy.scan(inactive, 3, "current", screen);
        verify(!noFocus.windowPause);
        verify(inactive.reads.indexOf("ScreenGeometry") < 0);
        compare(noFocus.geometryReads, 0);

        const maximized = model([task({IsMaximized: true})]);
        const focusOrMax = Policy.scan(maximized, 4, "current", screen);
        verify(focusOrMax.windowPause);
        verify(maximized.reads.indexOf("IsActive") < 0);
    }

    function test_coveredModeCollectsOnlyEligibleGeometry() {
        const source = model([
            task({IsMinimized: true}),
            task({Activities: ["other"]}),
            task(), task()
        ]);
        const result = Policy.scan(source, 6, "current", Qt.rect(0, 0, 1920, 1080));
        compare(result.geometries.length, 2);
        compare(result.geometryReads, 2);
        verify(source.reads.indexOf("ScreenGeometry") < 0);
        verify(source.reads.indexOf("IsActive") < 0);
        verify(source.reads.indexOf("IsMaximized") < 0);
    }

    function test_missingScreenGeometryUsesContainedFallback() {
        const source = model([task({ScreenGeometry: undefined})]);
        const result = Policy.scan(source, 1, "current", Qt.rect(0, 0, 1920, 1080));
        verify(result.windowPause);
        compare(result.geometryReads, 1);
    }

    function test_largeSimpleScanAvoidsModeArraysAndCoverageGeometry() {
        const tasks = [];
        for (let i = 0; i < 100; ++i)
            tasks.push(task({IsActive: i === 0}));
        const source = model(tasks);
        const result = Policy.scan(source, 3, "current", Qt.rect(0, 0, 1920, 1080));
        verify(result.windowPause);
        compare(result.rowsVisited, 100); // all rows still check global fullscreen
        compare(result.geometryReads, 0);
        compare(result.roleReads, 502); // 7 roles for match, 5 for each remaining row
        verify(source.reads.indexOf("IsMaximized") < 0);
        verify(source.reads.indexOf("Geometry") < 0);

        const inactiveTasks = [];
        for (let i = 0; i < 100; ++i) inactiveTasks.push(task());
        const inactiveSource = model(inactiveTasks);
        const inactiveResult = Policy.scan(inactiveSource, 3, "current",
                                            Qt.rect(0, 0, 1920, 1080));
        verify(!inactiveResult.windowPause);
        compare(inactiveResult.roleReads, 600);
        compare(inactiveResult.geometryReads, 0);
        verify(inactiveSource.reads.indexOf("ScreenGeometry") < 0);

        const coveredSource = model(tasks);
        const covered = Policy.scan(coveredSource, 6, "current",
                                    Qt.rect(0, 0, 1920, 1080));
        compare(covered.geometries.length, 100);
        compare(covered.geometryReads, 100);
        compare(covered.roleReads, 600);
    }
}
