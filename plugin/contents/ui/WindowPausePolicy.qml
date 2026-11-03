pragma Singleton
import QtQuick
import QtQml
import org.kde.taskmanager 0.1 as TaskManager
import com.github.catsout.wallpaperEngineKde 1.2
import "js/window-pause-policy.mjs" as WindowPolicyScan

Item {
    id: root
    visible: false
    width: 0
    height: 0

    readonly property int evaluationInterval: 100
    readonly property int defaultMode: 6
    readonly property int defaultResumeDelay: 300
    property int mode: defaultMode
    property int resumeDelay: defaultResumeDelay
    property bool windowPause: false
    property bool fullscreenPause: false
    property bool coverageSupported: false
    property string coverageUnavailableReason: "Primary screen coverage is not available."
    property string _lastCoverageDiagnosticKey: ""
    property int evaluationCount: 0
    property int lastRowsVisited: 0
    property int lastRoleReads: 0
    property int lastGeometryReads: 0

    property string _activity: ""
    property var _desktop: 0
    property bool _pending: false
    property bool _coverageWasPaused: false
    property bool _hadWindowPause: false
    property var _corona: null

    function attachCorona(value) {
        if (!value) return;
        if (_corona && _corona !== value) {
            console.warn("WindowPausePolicy received a second Plasma Corona; keeping the shared primary source.");
            return;
        }
        if (_corona === value) return;
        _corona = value;
        primaryScreenMonitor.corona = value;
        scheduleEvaluation();
    }

    function normalizeMode(value) {
        const parsed = Number(value);
        if (parsed === 5) return 0; // retired legacy per-output fullscreen mode
        return [0, 1, 2, 3, 4, 6].indexOf(parsed) >= 0 ? parsed : defaultMode;
    }

    function normalizeDelay(value) {
        const parsed = Number(value);
        if (!Number.isFinite(parsed)) return defaultResumeDelay;
        return Math.max(1, Math.min(60000, Math.round(parsed)));
    }

    function loadSettings() {
        mode = normalizeMode(globalConfig.get("WindowPauseMode", defaultMode));
        resumeDelay = normalizeDelay(globalConfig.get("WindowResumeDelay", defaultResumeDelay));
        scheduleEvaluation();
    }

    function scheduleEvaluation() {
        if (_pending) return;
        _pending = true;
        coalescer.start();
    }

    function roleId(name) {
        return TaskManager.AbstractTasksModel[name];
    }

    function roleChanged(roles) {
        if (roles === undefined || roles === null || roles.length === 0) return true;
        const relevant = ["IsWindow", "IsMinimized", "IsHidden", "IsFullScreen",
                          "Activities", "VirtualDesktops", "IsOnAllVirtualDesktops"];
        if (mode === 1) relevant.push("ScreenGeometry", "Geometry");
        if (mode === 2 || mode === 4) relevant.push("ScreenGeometry", "Geometry", "IsMaximized");
        if (mode === 3 || mode === 4) relevant.push("ScreenGeometry", "Geometry", "IsActive");
        if (mode === 6) relevant.push("Geometry");
        for (let i = 0; i < relevant.length; ++i) {
            const id = roleId(relevant[i]);
            if (id !== undefined && roles.indexOf(id) >= 0) return true;
        }
        return false;
    }

    function onTaskDataChanged(roles) {
        if (roleChanged(roles)) scheduleEvaluation();
    }

    function onPrimaryScreenChanged() {
        // Hysteresis history belongs to a specific primary target/workarea.
        _coverageWasPaused = false;
        scheduleEvaluation();
    }

    function eligibleCoverageSupported() {
        return mode === 6 && primaryScreenMonitor.valid && desktopVisibilityMonitor.known;
    }

    function reportCoverageState(supported, decision, reason, eligibleCount, fraction) {
        if (mode !== 6) return;
        const key = [primaryScreenMonitor.screenName, supported, decision, reason].join("|");
        if (key === _lastCoverageDiagnosticKey) return;
        _lastCoverageDiagnosticKey = key;
        const region = [];
        const sourceRegion = primaryScreenMonitor.usableRegion || [];
        for (let i = 0; i < sourceRegion.length; ++i) {
            const rect = sourceRegion[i];
            region.push(rect ? [rect.x, rect.y, rect.width, rect.height] : null);
        }
        const details = JSON.stringify({
            primaryScreen: primaryScreenMonitor.screenName,
            usableRegion: region,
            eligibleTaskCount: eligibleCount,
            fraction: fraction,
            decision: decision,
            supported: supported,
            supportReason: reason
        });
        if (!supported || decision === "unknown")
            console.warn("Primary desktop coverage unavailable:", details);
        else
            console.info("Primary desktop coverage state:", details);
    }

    function publishOrdinaryPause(value, immediate) {
        const next = !!value;
        if (next) {
            resumeTimer.stop();
            windowPause = true;
            _hadWindowPause = true;
            return;
        }
        if (!windowPause) {
            if (immediate) resumeTimer.stop();
            _hadWindowPause = false;
            return;
        }
        if (immediate || !_hadWindowPause || resumeDelay === 0) {
            resumeTimer.stop();
            windowPause = false;
            _hadWindowPause = false;
            return;
        }
        // Do not restart this timer while a paused->visible transition is
        // already waiting. Repeated model events cannot starve the resume.
        if (!resumeTimer.running) resumeTimer.start();
    }

    function evaluate() {
        _pending = false;
        evaluationCount++;
        coverageSupported = eligibleCoverageSupported();
        coverageUnavailableReason = coverageSupported ? ""
            : (mode !== 6 ? "" : !primaryScreenMonitor.valid
                ? "The primary screen usable region is unavailable."
                : "Show Desktop state is unavailable.");

        if (desktopVisibilityMonitor.known && desktopVisibilityMonitor.showingDesktop) {
            fullscreenPause = false;
            _coverageWasPaused = false;
            publishOrdinaryPause(false, true);
            reportCoverageState(coverageSupported, "playing", "showDesktop", 0, 0);
            lastRowsVisited = 0;
            lastRoleReads = 0;
            lastGeometryReads = 0;
            return;
        }

        const result = WindowPolicyScan.scan(tasksModel, mode, _activity,
                                              primaryScreenMonitor.screenGeometry);
        lastRowsVisited = result.rowsVisited;
        lastRoleReads = result.roleReads;
        lastGeometryReads = result.geometryReads;

        fullscreenPause = result.fullscreen && !desktopVisibilityMonitor.showingDesktop;

        if (mode === 6) {
            if (!coverageSupported) {
                _coverageWasPaused = false;
                publishOrdinaryPause(false, true);
                reportCoverageState(false, "unknown", coverageUnavailableReason, 0, 0);
                return;
            }
            const next = coverageCalculator.evaluate(
                primaryScreenMonitor.usableRegion, result.geometries,
                _coverageWasPaused, false, true);
            if (!next || next.decision === "unknown") {
                _coverageWasPaused = false;
                publishOrdinaryPause(false, true);
                reportCoverageState(true, "unknown", "coverage calculation returned unknown",
                                    result.geometries.length, 0);
                return;
            }
            _coverageWasPaused = next.decision === "paused";
            publishOrdinaryPause(_coverageWasPaused, false);
            reportCoverageState(true, next.decision, "", result.geometries.length,
                                Number(next.fraction) || 0);
            return;
        }

        _coverageWasPaused = false;
        publishOrdinaryPause(result.windowPause, false);
    }

    GlobalConfig {
        id: globalConfig
        onChanged: function(key, value) {
            if (key === "WindowPauseMode" || key === "WindowResumeDelay") root.loadSettings();
        }
    }
    WallpaperSyncBus {
        id: syncBus
        onGlobalConfigChanged: globalConfig.reload()
    }
    TaskManager.ActivityInfo {
        id: activityInfo
        onCurrentActivityChanged: {
            root._activity = currentActivity;
            root.scheduleEvaluation();
        }
        Component.onCompleted: root._activity = currentActivity
    }
    TaskManager.VirtualDesktopInfo {
        id: desktopInfo
        onCurrentDesktopChanged: {
            root._desktop = currentDesktop;
            root.scheduleEvaluation();
        }
        Component.onCompleted: root._desktop = currentDesktop
    }
    TaskManager.TasksModel {
        id: tasksModel
        sortMode: TaskManager.TasksModel.SortVirtualDesktop
        groupMode: TaskManager.TasksModel.GroupDisabled
        virtualDesktop: root._desktop
        filterByVirtualDesktop: true
        filterByScreen: false
        onCountChanged: root.scheduleEvaluation()
        onRowsInserted: root.scheduleEvaluation()
        onRowsRemoved: root.scheduleEvaluation()
        onModelReset: root.scheduleEvaluation()
        onLayoutChanged: root.scheduleEvaluation()
        onDataChanged: function(topLeft, bottomRight, roles) { root.onTaskDataChanged(roles); }
        onActiveTaskChanged: if (root.mode === 3 || root.mode === 4) root.scheduleEvaluation()
        function getProperty(index, property) {
            const role = TaskManager.AbstractTasksModel[property];
            return role === undefined ? undefined : data(index, role);
        }
    }
    PrimaryScreenMonitor {
        id: primaryScreenMonitor
        corona: root._corona
        onChanged: root.onPrimaryScreenChanged()
    }
    DesktopVisibilityMonitor {
        id: desktopVisibilityMonitor
        onKnownChanged: root.scheduleEvaluation()
        onShowingDesktopChanged: root.scheduleEvaluation()
    }
    DesktopCoverageCalculator { id: coverageCalculator }

    Timer {
        id: coalescer
        interval: root.evaluationInterval
        repeat: false
        onTriggered: root.evaluate()
    }
    Timer {
        id: resumeTimer
        interval: root.resumeDelay
        repeat: false
        onTriggered: {
            root.windowPause = false;
            root._hadWindowPause = false;
        }
    }

    onModeChanged: scheduleEvaluation()
    onResumeDelayChanged: {
        if (resumeTimer.running) resumeTimer.restart();
    }
    Component.onCompleted: loadSettings()
}
