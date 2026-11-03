import QtQuick

// Coordinates an on-demand refresh, candidate choice and confirmed config write.
// The caller supplies rows from the same filtered/sorted model as randomization.
QtObject {
    id: root

    property bool targetActive: false
    property string instanceId: ""
    property bool followEnabled: false
    property bool globalMode: false
    property string currentSource: ""
    property string currentWorkshopId: ""
    property int timeoutInterval: 8000
    property var randomValue: function() { return Math.random(); }

    property string pendingRequestId: ""
    property string pendingTargetId: ""
    property string phase: "idle" // idle, refreshing, applying
    property int generation: 0
    property string baselineSource: ""
    property string baselineWorkshopId: ""
    property bool baselineGlobalMode: false
    property string expectedSource: ""
    property string expectedWorkshopId: ""
    property bool expectedGlobalMode: false
    property Timer requestTimeout: Timer {
        interval: root.timeoutInterval
        repeat: false
        onTriggered: root.cancel("selectionUnconfirmed",
            "Wallpaper selection was not confirmed before timeout")
    }

    readonly property bool refreshing: phase === "refreshing"
    readonly property bool busy: phase !== "idle"

    signal refreshRequested(string requestId, int generation)
    signal selectionRequested(string requestId, string targetId, string source,
        string workshopId, bool globalMode)
    signal completed(string requestId, string targetId, bool applied,
        string errorCode, string message)

    function request(requestId, targetId) {
        if (!targetActive || !requestId || targetId !== instanceId) {
            completed(requestId, targetId, false, "unverifiedTarget",
                "Wallpaper target is no longer active");
            return false;
        }
        if (busy) {
            completed(requestId, targetId, false, "nextBusy",
                "A wallpaper selection is already pending");
            return false;
        }
        if (followEnabled) {
            completed(requestId, targetId, false, "followActive",
                "Disable Follow before selecting another wallpaper");
            return false;
        }
        pendingRequestId = requestId;
        pendingTargetId = targetId;
        phase = "refreshing";
        generation++;
        baselineSource = currentSource;
        baselineWorkshopId = currentWorkshopId;
        baselineGlobalMode = globalMode;
        expectedSource = "";
        expectedWorkshopId = "";
        expectedGlobalMode = false;
        requestTimeout.restart();
        refreshRequested(requestId, generation);
        return true;
    }

    function modelRefreshed(requestId, refreshGeneration, candidates) {
        if (phase !== "refreshing" || requestId !== pendingRequestId
                || refreshGeneration !== generation) return false;
        if (!targetActive || pendingTargetId !== instanceId) {
            finish(false, "targetRemoved", "Wallpaper target became inactive during refresh");
            return false;
        }
        if (followEnabled) {
            finish(false, "followActive", "Follow became active during wallpaper selection");
            return false;
        }
        if (currentSource !== baselineSource || currentWorkshopId !== baselineWorkshopId
                || globalMode !== baselineGlobalMode) {
            finish(false, "selectionChanged", "Wallpaper selection changed during catalog refresh");
            return false;
        }
        const sourceRows = [];
        const seen = new Set();
        (Array.isArray(candidates) ? candidates : []).forEach(candidate => {
            if (!candidate || typeof candidate.source !== "string" || !candidate.source
                    || typeof candidate.workshopId !== "string" || !candidate.workshopId)
                return;
            const key = candidate.source + "\n" + candidate.workshopId;
            if (seen.has(key) || (candidate.source === baselineSource
                    && candidate.workshopId === baselineWorkshopId)) return;
            seen.add(key);
            sourceRows.push({source: candidate.source, workshopId: candidate.workshopId});
        });
        if (sourceRows.length === 0) {
            finish(false, "noAlternative", "No eligible alternative wallpaper is available");
            return false;
        }
        const random = Number(root.randomValue());
        const index = Math.min(sourceRows.length - 1,
            Math.max(0, Math.floor((Number.isFinite(random) ? random : 0) * sourceRows.length)));
        const selected = sourceRows[index];
        expectedSource = selected.source;
        expectedWorkshopId = selected.workshopId;
        expectedGlobalMode = baselineGlobalMode;
        phase = "applying";
        selectionRequested(pendingRequestId, pendingTargetId, expectedSource,
            expectedWorkshopId, expectedGlobalMode);
        return true;
    }

    function confirmSelection(actualSource, actualWorkshopId, storedSource,
            storedWorkshopId, actualGlobalMode) {
        if (phase !== "applying") return false;
        if (!targetActive || pendingTargetId !== instanceId) {
            finish(false, "targetRemoved", "Wallpaper target became inactive before selection confirmation");
            return false;
        }
        if (followEnabled) {
            finish(false, "followActive", "Follow became active before selection confirmation");
            return false;
        }
        if (actualGlobalMode !== expectedGlobalMode) {
            finish(false, "selectionChanged", "Global wallpaper mode changed during selection");
            return false;
        }
        if (storedSource !== expectedSource || storedWorkshopId !== expectedWorkshopId
                || actualSource !== expectedSource || actualWorkshopId !== expectedWorkshopId)
            return false;
        finish(true, "", "New wallpaper selection was confirmed");
        return true;
    }

    function reject(requestId, targetId, errorCode, message) {
        if (!busy || requestId !== pendingRequestId || targetId !== pendingTargetId) return false;
        finish(false, errorCode || "selectionFailed", message || "Wallpaper selection failed");
        return true;
    }

    function cancel(errorCode, message) {
        if (!busy) return false;
        finish(false, errorCode || "selectionCancelled", message || "Wallpaper selection was cancelled");
        return true;
    }

    function sourceChanged() {
        if (phase === "refreshing" && (currentSource !== baselineSource
                || currentWorkshopId !== baselineWorkshopId || globalMode !== baselineGlobalMode))
            finish(false, "selectionChanged", "Wallpaper selection changed during catalog refresh");
    }

    function finish(applied, errorCode, message) {
        if (!busy) return false;
        const requestId = pendingRequestId;
        const targetId = pendingTargetId;
        pendingRequestId = "";
        pendingTargetId = "";
        phase = "idle";
        expectedSource = "";
        expectedWorkshopId = "";
        requestTimeout.stop();
        completed(requestId, targetId, applied, errorCode, message);
        return true;
    }

    onTargetActiveChanged: if (!targetActive) cancel("targetRemoved",
        "Wallpaper target became inactive during selection")
    onFollowEnabledChanged: if (followEnabled) cancel("followActive",
        "Follow became active during wallpaper selection")
    onCurrentSourceChanged: sourceChanged()
    onCurrentWorkshopIdChanged: sourceChanged()
    onGlobalModeChanged: sourceChanged()

}
