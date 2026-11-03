/* Wallpaper Engine KDE selection bridge, version 2.
 * Loaded after WE's scripts, before Angular bootstraps. No timers or polling.
 * Persistence still goes through WE's browserSettings watcher.
 */
(function () {
    "use strict";
    if (!window.angular || window.__wekdeSelectionBridge) return;
    window.__wekdeSelectionBridge = true;

    var minSteamId64 = "76561197960265729";
    var maxSteamId64 = "76561202255233023";
    var maxUsers = 128;

    function canonicalSteamId(value) {
        if (typeof value !== "string" || !/^[1-9][0-9]{16}$/.test(value)) return "";
        return value >= minSteamId64 && value <= maxSteamId64 ? value : "";
    }

    function recordShape(record) {
        return record && typeof record.file === "string" && record.file.length > 0
            && typeof record.monitor === "string";
    }

    function validSavedAt(value) {
        return typeof value === "string" && !Number.isNaN(Date.parse(value));
    }

    function copyRecords(previous) {
        var result = {};
        if (!previous || previous.version !== 2 || !previous.byUser
            || typeof previous.byUser !== "object" || Array.isArray(previous.byUser)) return result;
        Object.keys(previous.byUser).forEach(function (key) {
            var id = canonicalSteamId(key);
            var item = previous.byUser[key];
            if (!id || !recordShape(item)) return;
            result[id] = {
                file: item.file,
                monitor: item.monitor,
                savedAtUtc: validSavedAt(item.savedAtUtc) ? item.savedAtUtc : ""
            };
        });
        return result;
    }

    function trimOldest(records) {
        var keys = Object.keys(records);
        if (keys.length <= maxUsers) return;
        keys.sort(function (left, right) {
            var leftDate = Date.parse(records[left].savedAtUtc) || 0;
            var rightDate = Date.parse(records[right].savedAtUtc) || 0;
            if (leftDate !== rightDate) return leftDate - rightDate;
            return left < right ? -1 : (left > right ? 1 : 0);
        });
        keys.slice(0, keys.length - maxUsers).forEach(function (key) { delete records[key]; });
    }

    function sameRecord(left, right) {
        return !!left && !!right && left.file === right.file && left.monitor === right.monitor;
    }

    function legacyUnassociated(previous) {
        if (!previous || previous.version !== 1 || !recordShape(previous)) return null;
        return {
            file: previous.file,
            monitor: previous.monitor,
            savedAtUtc: new Date().toISOString()
        };
    }

    angular.module("wallpaperbrowserApp").run(["$rootScope", function (root) {
        function attach() {
            var scope = window.browseWallpapersCtrl;
            if (!scope || scope.$$destroyed || scope.__wekdeSelectionBridge) return;
            scope.__wekdeSelectionBridge = true;
            scope.$watchGroup([
                "browserRuntime.usersteamid",
                "currentSelection.file", "currentSelection.status", "currentSelection.type",
                "selectedMonitor.location", "wallpaperConfigGroup"
            ], function () {
                var selected = scope.currentSelection;
                if (scope.wallpaperConfigGroup !== "wallpaper" || !scope.browserSettings
                    || !selected || selected.status !== "installed" || typeof selected.file !== "string"
                    || !selected.file
                    || selected.type === "folder") return;

                var monitor = scope.selectedMonitor && scope.selectedMonitor.location || "";
                var rawIdentity = scope.browserRuntime && scope.browserRuntime.usersteamid;
                var identity = canonicalSteamId(rawIdentity);
                var previous = scope.browserSettings.wekdeSelection;
                var records = copyRecords(previous);
                var unassociated = previous && previous.version === 2 && recordShape(previous.unassociated)
                    ? {
                        file: previous.unassociated.file,
                        monitor: previous.unassociated.monitor,
                        savedAtUtc: validSavedAt(previous.unassociated.savedAtUtc)
                            ? previous.unassociated.savedAtUtc : ""
                    }
                    : legacyUnassociated(previous);

                var currentChanged = !previous || previous.version !== 2
                    || previous.currentSteamId64 !== identity;
                var changed = currentChanged || Object.keys(records).length > maxUsers;
                if (identity) {
                    var oldRecord = records[identity];
                    if (currentChanged || !sameRecord(oldRecord, { file: selected.file, monitor: monitor })) {
                        records[identity] = {
                            file: selected.file,
                            monitor: monitor,
                            savedAtUtc: new Date().toISOString()
                        };
                        changed = true;
                    }
                } else if (currentChanged || !sameRecord(unassociated, { file: selected.file, monitor: monitor })) {
                    unassociated = {
                        file: selected.file,
                        monitor: monitor,
                        savedAtUtc: new Date().toISOString()
                    };
                    changed = true;
                }

                if (!changed) return;
                trimOldest(records);
                var envelope = {
                    version: 2,
                    currentSteamId64: identity,
                    byUser: records,
                    unassociated: unassociated
                };
                scope.browserSettings.wekdeSelection = envelope;
            });
        }
        root.$on("$viewContentLoaded", attach);
        root.$on("onCppInit", attach);
        attach();
    }]);
})();
