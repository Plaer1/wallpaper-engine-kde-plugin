pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root
    visible: false
    width: 0
    height: 0

    // Injected adapter contract: execute(requestObject) returns a Promise of
    // the Runtime1 acknowledgement; commandFinished(result), ownerChanged(bool),
    // and stateChanged(snapshot) are optional facade signals. The tray never
    // owns or creates the native service.
    property string runtimeStatus: "absent"
    property var snapshot: null
    property var commandClient: null
    property var _clientOwnerAvailable: null
    property var _clientSnapshot: null
    property string _upstreamEpoch: ""
    property string scopeMode: "all" // all or selected
    property string selectedTargetId: ""
    property var followBinding: ({mode: "", accountKey: "", sourceKey: "", identityVerified: false})
    property string lastCommandText: ""
    property string lastError: ""
    property string lastTargetResultText: ""
    property string _requestSerial: "0"
    property var _requestStates: ({})
    property var _requestTargetLabels: ({})
    property var _earlyResults: ({})

    readonly property string effectiveRuntimeStatus: _clientOwnerAvailable === false ? "absent"
        : _clientOwnerAvailable === true ? (_clientSnapshot ? "ready" : "loading") : runtimeStatus
    readonly property var effectiveSnapshot: _clientOwnerAvailable === false ? null
        : _clientOwnerAvailable === true ? _clientSnapshot : snapshot
    readonly property var targets: effectiveSnapshot && effectiveSnapshot.schemaVersion === 1
        && effectiveSnapshot.targets && typeof effectiveSnapshot.targets.length === "number"
        ? effectiveSnapshot.targets : []
    readonly property bool serviceReady: effectiveRuntimeStatus === "ready"
        && effectiveSnapshot && effectiveSnapshot.schemaVersion === 1
        && typeof effectiveSnapshot.epoch === "string" && effectiveSnapshot.epoch.length > 0
    readonly property var followChoices: deriveFollowChoices()

    function observeUpstreamEpoch() {
        if (_clientOwnerAvailable !== null) return;
        const epoch = snapshot && snapshot.schemaVersion === 1
            && typeof snapshot.epoch === "string" ? snapshot.epoch : "";
        if (_upstreamEpoch && epoch && _upstreamEpoch !== epoch && scopeMode === "selected")
            selectedTargetId = "";
        _upstreamEpoch = epoch;
    }

    function failPendingOnDisconnect() {
        const next = Object.assign({}, _requestStates);
        let lastPending = "";
        Object.keys(next).forEach(requestId => {
            if (next[requestId] === "pending" || next[requestId] === "sending") {
                next[requestId] = "error";
                lastPending = requestId;
            }
        });
        _requestStates = next;
        if (lastPending) {
            lastError = qsTr("Wallpaper runtime disconnected before command completion");
            lastCommandText = qsTr("Command failed: %1").arg(lastError);
        }
    }

    function handleOwnerChanged(available) {
        if (typeof available !== "boolean") return;
        const replacement = _clientOwnerAvailable === true && available;
        if (_clientOwnerAvailable === available && !replacement) return;
        _clientOwnerAvailable = available;
        if (!available || replacement) {
            _clientSnapshot = null;
            failPendingOnDisconnect();
            if (scopeMode === "selected") selectedTargetId = "";
            return;
        }
        _clientSnapshot = null;
        // A per-target choice belongs to the previous service epoch. Require a
        // fresh target selection after owner loss before dispatching commands.
        if (scopeMode === "selected") selectedTargetId = "";
    }

    function handleClientSnapshot(value) {
        if (_clientOwnerAvailable !== true) return;
        const nextSnapshot = parseObject(value);
        if (!nextSnapshot || nextSnapshot.schemaVersion !== 1
                || typeof nextSnapshot.epoch !== "string" || !nextSnapshot.epoch
                || !nextSnapshot.targets || typeof nextSnapshot.targets.length !== "number") {
            _clientSnapshot = null;
            return;
        }
        if (_clientSnapshot && _clientSnapshot.epoch !== nextSnapshot.epoch
                && scopeMode === "selected") selectedTargetId = "";
        _clientSnapshot = nextSnapshot;
    }

    function selectedTargets() {
        if (scopeMode === "all") {
            const all = [];
            for (let i = 0; i < targets.length; ++i) all.push(targets[i]);
            return all;
        }
        if (!selectedTargetId) return [];
        const selected = [];
        for (let i = 0; i < targets.length; ++i)
            if (targets[i] && targets[i].instanceId === selectedTargetId) selected.push(targets[i]);
        return selected;
    }

    function deriveFollowChoices() {
        const selected = selectedTargets();
        if (!selected.length) return [];
        let intersection = null;
        let ordered = [];
        for (let i = 0; i < selected.length; ++i) {
            const follow = selected[i] && selected[i].follow;
            const choices = follow && follow.choices;
            if (!choices || typeof choices.length !== "number" || typeof choices === "string") return [];
            const current = Object.create(null);
            const currentOrder = [];
            for (let j = 0; j < choices.length; ++j) {
                const choice = choices[j];
                if (!choice || typeof choice !== "object") continue;
                const mode = choice.mode;
                const accountKey = choice.accountKey;
                const sourceKey = choice.sourceKey;
                if (!["legacySession", "account", "manualMapping"].includes(mode)
                        || typeof accountKey !== "string" || typeof sourceKey !== "string" || !sourceKey)
                    continue;
                // Account attribution is accepted only when the snapshot says
                // identity was verified. Never derive verification from labels or keys.
                if (mode === "account" && choice.identityVerified !== true) continue;
                const key = JSON.stringify([mode, accountKey, sourceKey]);
                if (current[key]) continue;
                const normalized = {mode, accountKey, sourceKey,
                    label: typeof choice.label === "string" && choice.label ? choice.label : sourceKey,
                    identityVerified: choice.identityVerified === true};
                current[key] = normalized;
                currentOrder.push(key);
            }
            if (intersection === null) {
                intersection = current;
                ordered = currentOrder;
            } else {
                Object.keys(intersection).forEach(key => {
                    if (!current[key]) delete intersection[key];
                });
            }
        }
        return ordered.filter(key => intersection && intersection[key]).map(key => intersection[key]);
    }

    function hasCapability(target, command) {
        const capabilities = target && target.capabilities;
        if (!capabilities || typeof capabilities.length !== "number") return false;
        for (let i = 0; i < capabilities.length; ++i)
            if (capabilities[i] === command) return true;
        return false;
    }

    function descriptorState(command, scope) {
        if (!serviceReady) return {enabled: false, reason: effectiveRuntimeStatus === "absent"
            ? qsTr("No active wallpaper runtime") : qsTr("Wallpaper runtime is not ready")};
        if (!commandClient || typeof commandClient.execute !== "function")
            return {enabled: false, reason: qsTr("Runtime command client is unavailable")};
        if (!targets.length) return {enabled: false, reason: qsTr("No active wallpaper targets")};

        const selected = scope === "global" ? targets : selectedTargets();
        if (!selected.length) return {enabled: false, reason: qsTr("Select an available wallpaper target")};
        if (scope !== "global" && selected.some(target => !target || !target.instanceId))
            return {enabled: false, reason: qsTr("A selected wallpaper target is unavailable")};

        // Runtime1 publishes capabilities as command-name string arrays. Missing
        // entries fail closed; UI does not infer support.
        let capable = true;
        for (let i = 0; i < selected.length; ++i)
            if (!hasCapability(selected[i], command)) capable = false;
        if (!capable) return {enabled: false, reason: qsTr("This command is unsupported for the selected target scope")};

        if (command === "OpenConfiguration" && selected.length !== 1)
            return {enabled: false, reason: qsTr("Choose exactly one wallpaper target")};
        if (command === "NextWallpaper") {
            for (let i = 0; i < selected.length; ++i)
                if (selected[i].follow && selected[i].follow.enabled)
                    return {enabled: false, reason: qsTr("Next wallpaper is unavailable while Follow is active")};
        }
        if (command === "SetFollow") {
            const binding = followBinding || {};
            const stopping = selected.every(target => target.follow && target.follow.enabled);
            // Stop is safe without picking a replacement source. Keep a valid
            // explicit mode when available; otherwise send the harmless legacy
            // enum because the service ignores identity fields for enabled=false.
            if (stopping) return {enabled: true, reason: ""};
            const available = followChoices.some(choice => choice.mode === binding.mode
                && choice.accountKey === binding.accountKey && choice.sourceKey === binding.sourceKey
                && choice.identityVerified === (binding.identityVerified === true));
            if (!available)
                return {enabled: false, reason: qsTr("Choose a verified or explicitly mapped follow source")};
        }
        return {enabled: true, reason: ""};
    }

    function actionInfo(actionId) {
        const selected = selectedTargets();
        let following = selected.length > 0;
        for (let i = 0; i < selected.length; ++i)
            if (!selected[i].follow || !selected[i].follow.enabled) following = false;
        let command = "", scope = "targets", args = ({}), text = "", enabled = false, reason = "";
        switch (actionId) {
        case "pause": command = "SetPlayback"; args = {mode: "paused"}; text = qsTr("Pause"); break;
        case "resume": command = "SetPlayback"; args = {mode: "automatic"}; text = qsTr("Resume automatically"); break;
        case "playDespiteWindows": command = "SetPlayback"; args = {mode: "playDespiteWindows"}; text = qsTr("Play despite window coverage"); break;
        case "muteAll": command = "SetGlobalAudioMuted"; scope = "global";
            args = {muted: !(effectiveSnapshot && effectiveSnapshot.global && effectiveSnapshot.global.muted === true)};
            text = args.muted ? qsTr("Mute all wallpaper audio") : qsTr("Unmute all wallpaper audio"); break;
        case "follow": command = "SetFollow"; args = {enabled: !following,
            mode: followBinding && ["legacySession", "account", "manualMapping"].includes(followBinding.mode)
                ? followBinding.mode : "legacySession",
            accountKey: followBinding ? followBinding.accountKey || "" : "",
            sourceKey: followBinding ? followBinding.sourceKey || "" : ""};
            text = following ? qsTr("Stop following") : qsTr("Follow selected source"); break;
        case "next": command = "NextWallpaper"; text = qsTr("Next wallpaper"); break;
        case "reload": command = "ReloadWallpaper"; text = qsTr("Reload current wallpaper"); break;
        case "sizingOff": command = "SetGlobalSizing"; scope = "global";
            args = {enabled: false, mode: "crop"}; text = qsTr("Global sizing: Off"); break;
        case "sizingFill": command = "SetGlobalSizing"; scope = "global";
            args = {enabled: true, mode: "crop"}; text = qsTr("Global sizing: Fill"); break;
        case "sizingStretch": command = "SetGlobalSizing"; scope = "global";
            args = {enabled: true, mode: "stretch"}; text = qsTr("Global sizing: Stretch"); break;
        case "configure": command = "OpenConfiguration"; text = qsTr("Configure wallpaper"); break;
        default: return {id: actionId, text: actionId, enabled: false, reason: qsTr("Unknown action")};
        }
        const state = descriptorState(command, scope);
        enabled = state.enabled;
        reason = state.reason;
        return {id: actionId, command, scope, args, text, enabled, reason};
    }

    function statusFor(requestId) {
        return _requestStates[requestId] || "";
    }

    function setRequestState(requestId, state, message) {
        const next = Object.assign({}, _requestStates);
        next[requestId] = state;
        _requestStates = next;
        if (state === "error") {
            lastError = message || qsTr("The wallpaper command failed");
            lastCommandText = qsTr("Command failed: %1").arg(lastError);
        } else if (state === "complete") {
            lastError = "";
            lastCommandText = qsTr("Command completed");
        } else if (state === "partial") {
            lastError = "";
            lastCommandText = message ? qsTr("Command partially completed: %1").arg(message)
                : qsTr("Command partially completed");
        } else if (state === "pending") {
            lastError = "";
            lastCommandText = qsTr("Command accepted; waiting for completion");
        } else if (state === "sending") {
            lastError = "";
            lastCommandText = qsTr("Sending command…");
        }
    }

    function parseObject(value) {
        if (typeof value === "string") {
            try { value = JSON.parse(value); } catch (e) { return null; }
        }
        return value && typeof value === "object" ? value : null;
    }

    function formatTargetResults(requestId, results) {
        if (!results || typeof results.length !== "number" || typeof results === "string") return "";
        const labels = _requestTargetLabels[requestId] || ({});
        const lines = [];
        for (let i = 0; i < results.length; ++i) {
            const result = results[i];
            if (!result || typeof result !== "object") continue;
            const instanceId = typeof result.instanceId === "string" ? result.instanceId : "";
            let label = labels[instanceId] || "";
            if (!label) {
                const target = targets.find(candidate => candidate && candidate.instanceId === instanceId);
                label = target ? (target.screenLabel || target.screenName || instanceId) : instanceId;
            }
            const status = result.status === "applied" ? qsTr("succeeded")
                : result.status === "rejected" ? qsTr("failed")
                : (typeof result.status === "string" && result.status ? result.status : qsTr("unknown"));
            const detail = typeof result.message === "string" && result.message
                ? result.message : (typeof result.errorCode === "string" ? result.errorCode : "");
            lines.push(detail ? qsTr("%1: %2 — %3").arg(label).arg(status).arg(detail)
                : qsTr("%1: %2").arg(label).arg(status));
        }
        return lines.join("\n");
    }

    function runAction(actionId) {
        const action = actionInfo(actionId);
        if (!action.enabled) {
            lastError = action.reason;
            lastCommandText = action.reason;
            return "";
        }
        const resolved = action.scope === "global" ? [] : selectedTargets().map(target => target.instanceId);
        // Freeze target IDs now; subsequent scope/snapshot changes cannot redirect.
        let staleTarget = false;
        for (let i = 0; i < resolved.length; ++i) {
            let found = false;
            for (let j = 0; j < targets.length; ++j)
                if (targets[j].instanceId === resolved[i]) found = true;
            if (!found) staleTarget = true;
        }
        if (action.scope === "targets" && (!resolved.length || staleTarget)) {
            lastError = qsTr("Selected wallpaper target disappeared; choose it again");
            lastCommandText = lastError;
            return "";
        }
        const id = `tray-${++_requestSerial}`;
        const targetLabels = Object.assign({}, _requestTargetLabels);
        targetLabels[id] = {};
        resolved.forEach(instanceId => {
            const target = targets.find(candidate => candidate && candidate.instanceId === instanceId);
            targetLabels[id][instanceId] = target
                ? (target.screenLabel || target.screenName || instanceId) : instanceId;
        });
        _requestTargetLabels = targetLabels;
        lastTargetResultText = "";
        const request = {
            schemaVersion: 1,
            epoch: effectiveSnapshot.epoch,
            requestId: id,
            scope: action.scope === "global" ? "allWallpaperTargets" : "targets",
            targetIds: resolved,
            command: action.command,
            args: action.args
        };
        setRequestState(id, "sending", "");
        try {
            const result = commandClient.execute(request);
            Promise.resolve(result).then(ack => handleAcknowledgement(id, parseObject(ack)))
                .catch(error => setRequestState(id, "error", String(error)));
        } catch (error) {
            setRequestState(id, "error", String(error));
        }
        return id;
    }

    function handleAcknowledgement(requestId, acknowledgement) {
        if (_requestStates[requestId] !== "sending") return;
        if (!acknowledgement || acknowledgement.requestId !== requestId) {
            setRequestState(requestId, "error", qsTr("Invalid command acknowledgement"));
            return;
        }
        if (acknowledgement.status === "applied") {
            setRequestState(requestId, "complete", acknowledgement.message || "");
            delete _earlyResults[requestId];
            return;
        }
        if (acknowledgement.status !== "accepted") {
            setRequestState(requestId, "error", acknowledgement.message || acknowledgement.errorCode || qsTr("Command was rejected"));
            delete _earlyResults[requestId];
            return;
        }
        setRequestState(requestId, "pending", "");
        const early = _earlyResults[requestId];
        if (early) {
            delete _earlyResults[requestId];
            handleCompletion(early);
        }
    }

    function handleCompletion(value) {
        const result = parseObject(value);
        if (!result || typeof result.requestId !== "string") return;
        if (_requestStates[result.requestId] === "sending") {
            const nextEarly = Object.assign({}, _earlyResults);
            nextEarly[result.requestId] = result;
            _earlyResults = nextEarly;
            return;
        }
        if (_requestStates[result.requestId] !== "pending") return;
        lastTargetResultText = formatTargetResults(result.requestId, result.targets);
        if (result.status === "applied") setRequestState(result.requestId, "complete", result.message || "");
        else if (result.status === "partial") setRequestState(result.requestId, "partial", result.message || "");
        else setRequestState(result.requestId, "error", result.message || result.errorCode || qsTr("Command completion failed"));
    }

    function runFromContext(actionId) { return runAction(actionId); }

    onSnapshotChanged: observeUpstreamEpoch()
    Component.onCompleted: observeUpstreamEpoch()

    Connections {
        target: root.commandClient
        ignoreUnknownSignals: true
        function onCommandFinished(result) { root.handleCompletion(result); }
        function onOwnerChanged(available) { root.handleOwnerChanged(available); }
        function onStateChanged(snapshot) { root.handleClientSnapshot(snapshot); }
    }
}
