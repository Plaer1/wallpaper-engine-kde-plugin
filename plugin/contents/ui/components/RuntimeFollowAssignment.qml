import QtQuick

// Applies a tray Follow request to exactly one wallpaper's KConfigPropertyMap.
// Completion requires readback and same-object valueChanged for each changed key.
Item {
    id: root
    width: 0
    height: 0
    visible: false

    property var configuration
    property bool targetVerified: false
    property bool configurationPreview: true
    property bool currentIdentityVerified: false
    property string currentSteamId64: ""
    property string instanceId: ""
    property string configPath: ""
    property var pending: ({})
    signal completed(string requestId, string instanceId, bool applied, bool enabled,
                     string mode, string accountKey, string sourceKey,
                     string errorCode, string message)

    function validSteamKey(key) {
        return typeof key === "string" && /^steam:[0-9]{17}$/.test(key)
    }

    function validSourceKey(mode, accountKey, sourceKey) {
        if (typeof sourceKey !== "string" || sourceKey.length === 0 || sourceKey.length > 4096)
            return false;
        const prefix = configPath ? "config:" + configPath : "";
        if (!prefix || !sourceKey.startsWith(prefix + "#")) return false;
        if (mode === "identifiedCurrent")
            return currentIdentityVerified && validSteamKey(accountKey)
                && accountKey === "steam:" + currentSteamId64
                && sourceKey === prefix + "#account:" + currentSteamId64;
        if (mode === "account")
            return validSteamKey(accountKey)
                && sourceKey === prefix + "#account:" + accountKey.slice(6);
        if (mode === "manualMapping" || mode === "legacySession") {
            const marker = prefix + "#section:";
            if (!sourceKey.startsWith(marker) || sourceKey.length === marker.length) return false;
            try { return decodeURIComponent(sourceKey.slice(marker.length)).length > 0; }
            catch (error) { return false; }
        }
        return false;
    }

    function fail(requestId, args, code, message) {
        completed(requestId, instanceId, false, args.enabled === true,
                  args.mode || "", args.accountKey || "", args.sourceKey || "", code, message);
    }

    function apply(requestId, requestedInstanceId, args) {
        if (typeof requestId !== "string" || !requestId || requestedInstanceId !== instanceId
                || !targetVerified || configurationPreview || !configuration) {
            fail(requestId, args || ({}), "unverifiedTarget", "The wallpaper target is no longer verified");
            return false;
        }
        if (!args || typeof args.enabled !== "boolean"
                || !["identifiedCurrent", "account", "legacySession", "manualMapping"].includes(args.mode)
                || typeof args.accountKey !== "string" || typeof args.sourceKey !== "string") {
            fail(requestId, args || ({}), "invalidArgument", "Follow request fields are invalid");
            return false;
        }
        if (args.enabled && !validSourceKey(args.mode, args.accountKey, args.sourceKey)) {
            fail(requestId, args, args.mode === "identifiedCurrent" && !currentIdentityVerified
                ? "unverifiedIdentity" : "invalidSource", "The Follow source is not verified or usable");
            return false;
        }
        const updates = args.enabled ? {
            FollowSourceMode: args.mode,
            FollowSourceAccountKey: args.accountKey,
            FollowSourceKey: args.sourceKey,
            WallpaperEngineAccount: args.mode === "legacySession"
                ? decodeURIComponent(args.sourceKey.slice(args.sourceKey.lastIndexOf("#section:") + 9))
                : String(configuration.WallpaperEngineAccount || ""),
            FollowWallpaperEngine: true
        } : {FollowWallpaperEngine: false};
        const changed = [];
        for (const key in updates)
            if (configuration[key] !== updates[key]) changed.push(key);
        pending[requestId] = {updates, changed, observed: ({}), enabled: args.enabled,
            mode: args.mode, accountKey: args.accountKey, sourceKey: args.sourceKey};
        // These are QML assignments on the target's own configuration map. Do not
        // replace the map or write cfg_* exports from the wallpaper process.
        for (const key in updates) configuration[key] = updates[key];
        Qt.callLater(function() { root.confirm(requestId); });
        confirmationTimer.restart();
        return true;
    }

    function confirm(requestId) {
        const record = pending[requestId];
        if (!record) return;
        for (const key in record.updates) {
            if (configuration[key] !== record.updates[key]) return;
            if (record.changed.includes(key) && record.observed[key] !== true) return;
        }
        delete pending[requestId];
        completed(requestId, instanceId, true, record.enabled, record.mode,
                  record.accountKey, record.sourceKey, "", "Follow configuration assignment confirmed");
        if (Object.keys(pending).length === 0) confirmationTimer.stop();
    }

    function observe(key, value) {
        for (const requestId in pending) {
            const record = pending[requestId];
            if (record.updates[key] === value) record.observed[key] = true;
            else if (Object.prototype.hasOwnProperty.call(record.updates, key)) {
                delete pending[requestId];
                completed(requestId, instanceId, false, record.enabled, record.mode,
                          record.accountKey, record.sourceKey, "assignmentConflict",
                          "A different Follow value arrived before confirmation");
            }
        }
    }

    Connections {
        target: root.configuration
        ignoreUnknownSignals: true
        function onValueChanged(key, value) { root.observe(key, value); }
    }

    Timer {
        id: confirmationTimer
        interval: 1500
        repeat: false
        onTriggered: {
            const ids = Object.keys(root.pending);
            for (const requestId of ids) {
                const record = root.pending[requestId];
                delete root.pending[requestId];
                root.completed(requestId, root.instanceId, false, record.enabled,
                    record.mode, record.accountKey, record.sourceKey,
                    "confirmationTimeout", "Configuration map did not confirm the Follow assignment");
            }
        }
    }
}
