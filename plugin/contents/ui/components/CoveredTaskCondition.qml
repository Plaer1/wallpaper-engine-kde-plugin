import QtQuick 2.5

// Event-driven adapter between a TaskManager-shaped model and the native
// DesktopCoverageCalculator. The source must expose count and either
// getProperty(row, role) or data(makeModelIndex(row), role).
Item {
    id: root

    property var taskModel: null
    property var coverageCalculator: null
    property var usableRegion: []
    property var screenOrigin: null
    property bool usableRegionVerified: false
    property bool showingDesktop: false
    property bool showingDesktopKnown: false
    property bool wasPaused: false
    property var currentVirtualDesktop: undefined
    property string activity: ""
    property bool enabled: true
    property bool screenScoped: true
    property bool mirrorScene: false
    property bool spanScreens: false
    property int outputCount: 1

    property int debounceInterval: 100
    property int evaluationCount: 0
    property bool _evaluationPending: false
    property var result: ({
        decision: "unknown",
        known: false,
        fraction: 0,
        coveredArea: "0",
        targetArea: "0"
    })

    readonly property string decision: result && result.decision ? result.decision : "unknown"
    readonly property bool groupOwnershipObservable: !(mirrorScene || spanScreens)
        || outputCount === 1
    readonly property bool supported: enabled && screenScoped && groupOwnershipObservable
        && usableRegionVerified && showingDesktopKnown
    readonly property bool active: supported && decision === "paused"
    readonly property string unavailableReason: !enabled ? ""
        : !screenScoped ? "Covered pause is inactive because all-output usable-region aggregation is unavailable."
        : !groupOwnershipObservable ? "Covered pause is inactive because mirrored or spanned wallpaper visibility across outputs is unknown."
        : !usableRegionVerified ? "The desktop usable region is unavailable or invalid."
        : !showingDesktopKnown ? "Show Desktop state is unavailable."
        : decision === "unknown" ? "Window coverage could not be determined."
        : ""

    signal evaluated(var result)

    function listValues(value) {
        if (value === null || value === undefined || typeof value === "string"
                || typeof value.length !== "number")
            return [];
        const values = [];
        for (let i = 0; i < value.length; ++i)
            values.push(value[i]);
        return values;
    }

    function translatedUsableRects() {
        const regions = listValues(usableRegion);
        const origin = screenOrigin;
        const originValid = origin && typeof origin.x === "number" && isFinite(origin.x)
            && typeof origin.y === "number" && isFinite(origin.y);
        const verified = usableRegionVerified && originValid && regions.length > 0;
        if (!verified)
            return {rects: [], verified: false};

        const translated = regions.map(function(rect) {
            if (rect && typeof rect.x === "number" && isFinite(rect.x)
                    && typeof rect.y === "number" && isFinite(rect.y)
                    && typeof rect.width === "number" && isFinite(rect.width)
                    && typeof rect.height === "number" && isFinite(rect.height)) {
                return Qt.rect(rect.x + origin.x, rect.y + origin.y, rect.width, rect.height);
            }
            // Preserve malformed values so the strict calculator returns Unknown.
            return rect;
        });
        return {rects: translated, verified: true};
    }

    function taskValue(row, role) {
        const model = taskModel;
        if (!model)
            return undefined;
        if (typeof model.getProperty === "function") {
            const index = typeof model.makeModelIndex === "function" ? model.makeModelIndex(row) : row;
            return model.getProperty(index, role);
        }
        if (typeof model.data === "function" && typeof model.makeModelIndex === "function")
            return model.data(model.makeModelIndex(row), role);
        if (typeof model.get === "function") {
            const task = model.get(row);
            return task ? task[role] : undefined;
        }
        return undefined;
    }

    function taskIsEligible(row) {
        if (taskValue(row, "IsWindow") !== true || taskValue(row, "IsMinimized") !== false)
            return false;

        if (currentVirtualDesktop !== undefined && currentVirtualDesktop !== null
                && taskValue(row, "IsOnAllVirtualDesktops") !== true) {
            const desktops = listValues(taskValue(row, "VirtualDesktops"));
            if (desktops.length > 0 && desktops.indexOf(currentVirtualDesktop) < 0)
                return false;
        }

        if (activity) {
            const activities = listValues(taskValue(row, "Activities"));
            if (activities.length > 0 && activities.indexOf(activity) < 0)
                return false;
        }
        return true;
    }

    function collectWindowRects() {
        const model = taskModel;
        if (!model || typeof model.count !== "number"
                || (typeof model.getProperty !== "function"
                    && !(typeof model.data === "function" && typeof model.makeModelIndex === "function")
                    && typeof model.get !== "function")) {
            return [undefined];
        }

        const windows = [];
        for (let row = 0; row < model.count; ++row) {
            if (!taskIsEligible(row))
                continue;
            windows.push(taskValue(row, "Geometry"));
        }
        return windows;
    }

    function eligibleWindowCount() {
        return collectWindowRects().length;
    }

    function publishResult(value) {
        result = value || ({decision: "unknown", known: false, fraction: 0,
            coveredArea: "0", targetArea: "0"});
        evaluationCount++;
        evaluated(result);
    }

    function evaluateNow() {
        if (!enabled)
            return;
        const calculator = coverageCalculator;
        if (!calculator || typeof calculator.evaluate !== "function") {
            publishResult(null);
            return;
        }

        const usable = translatedUsableRects();
        const showing = showingDesktopKnown ? showingDesktop : undefined;
        const next = calculator.evaluate(usable.rects,
            collectWindowRects(), wasPaused, showing,
            usable.verified && screenScoped && groupOwnershipObservable);
        publishResult(next);
    }

    // Fixed-window coalescing: events during a pending interval do not restart
    // the one-shot timer. Continuous movement therefore still yields a result
    // at least once per interval instead of starving behind a trailing debounce.
    function scheduleEvaluation() {
        if (!enabled)
            return;
        if (_evaluationPending)
            return;
        _evaluationPending = true;
        debounce.start();
    }

    Timer {
        id: debounce
        interval: root.debounceInterval
        repeat: false
        onTriggered: {
            root._evaluationPending = false;
            root.evaluateNow();
        }
    }

    Connections {
        target: root.taskModel
        ignoreUnknownSignals: true
        function onDataChanged() { root.scheduleEvaluation(); }
        function onCountChanged() { root.scheduleEvaluation(); }
        function onRowsInserted() { root.scheduleEvaluation(); }
        function onRowsRemoved() { root.scheduleEvaluation(); }
        function onModelReset() { root.scheduleEvaluation(); }
        function onLayoutChanged() { root.scheduleEvaluation(); }
    }

    onEnabledChanged: {
        if (enabled) {
            scheduleEvaluation();
        } else {
            debounce.stop();
            _evaluationPending = false;
        }
    }
    onScreenScopedChanged: scheduleEvaluation()
    onMirrorSceneChanged: scheduleEvaluation()
    onSpanScreensChanged: scheduleEvaluation()
    onOutputCountChanged: scheduleEvaluation()
    onTaskModelChanged: scheduleEvaluation()
    onCoverageCalculatorChanged: scheduleEvaluation()
    onUsableRegionChanged: scheduleEvaluation()
    onScreenOriginChanged: scheduleEvaluation()
    onUsableRegionVerifiedChanged: scheduleEvaluation()
    onShowingDesktopChanged: scheduleEvaluation()
    onShowingDesktopKnownChanged: scheduleEvaluation()
    onWasPausedChanged: scheduleEvaluation()
    onCurrentVirtualDesktopChanged: scheduleEvaluation()
    onActivityChanged: scheduleEvaluation()

    Component.onCompleted: scheduleEvaluation()
}
