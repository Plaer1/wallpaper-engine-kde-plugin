// Pure display-source state for identity-aware Follow. Callers retain manual
// settings separately and feed follower snapshots into acceptFollowerResult().

const FOLLOW_MODES = new Set(["identifiedCurrent", "account", "legacySession", "manualMapping"]);

function clone(value) {
    if (value === undefined || value === null || typeof value !== "object") return value;
    return JSON.parse(JSON.stringify(value));
}

function normalizeSelection(value) {
    if (typeof value === "string") return {source: value, workshopid: "", provenance: null};
    const selection = value && typeof value === "object" ? value : {};
    return {
        source: typeof selection.source === "string" ? selection.source : "",
        workshopid: typeof selection.workshopid === "string" ? selection.workshopid : "",
        provenance: selection.provenance && typeof selection.provenance === "object"
            ? clone(selection.provenance) : null
    };
}

function normalizeBinding(value) {
    const source = value && typeof value === "object" ? value : {};
    return {
        mode: FOLLOW_MODES.has(source.mode) ? source.mode : "legacySession",
        accountKey: typeof source.accountKey === "string" ? source.accountKey : "",
        sourceKey: typeof source.sourceKey === "string" ? source.sourceKey : ""
    };
}

function sameBinding(left, right) {
    return left.mode === right.mode && left.accountKey === right.accountKey
        && left.sourceKey === right.sourceKey;
}

function configSourcePrefix(sourceKey) {
    if (typeof sourceKey !== "string" || !sourceKey.startsWith("config:")) return "";
    const sectionMarker = sourceKey.indexOf("#");
    return sectionMarker > 0 ? sourceKey.slice(0, sectionMarker) : "";
}

function matchesSourceKey(binding, provenance) {
    const requested = binding.sourceKey;
    const actual = provenance.sourceKey;
    const requestedPrefix = configSourcePrefix(requested);
    if (!requestedPrefix || configSourcePrefix(actual) !== requestedPrefix) return false;

    if (binding.mode === "account" || binding.mode === "identifiedCurrent")
        return true; // The resolver finds the matching account section/bridge record.

    if (actual === requested) return true; // Native section selection.
    if (!actual.startsWith(requested)) return false;
    const suffix = actual.slice(requested.length);
    if (binding.mode === "legacySession") {
        return suffix === ".general.browser.wekdeSelection.v1"
            || suffix === ".general.browser.wekdeSelection.unassociated";
    }
    if (binding.mode === "manualMapping")
        return suffix === ".general.browser.wekdeSelection.v1";
    return false;
}

function matchesProvenance(binding, provenance) {
    if (!provenance || provenance.mode !== binding.mode) return false;
    if (provenance.accountKey !== binding.accountKey) return false;
    // Native/bridge output can name its concrete section/record after the
    // selected binding's section. Keep config path and section anchored, while
    // allowing only source forms the F3 resolver can actually emit.
    return matchesSourceKey(binding, provenance);
}

function copyState(state) {
    return {
        enabled: Boolean(state.enabled),
        manual: normalizeSelection(state.manual),
        requestedBinding: normalizeBinding(state.requestedBinding),
        lastFollowed: state.lastFollowed ? clone(state.lastFollowed) : null,
        status: typeof state.status === "string" ? state.status : "",
        revision: Number.isSafeInteger(state.revision) ? state.revision : 0
    };
}

export function create(manualSelection = "", enabled = false, binding = null) {
    return {
        enabled: Boolean(enabled),
        manual: normalizeSelection(manualSelection),
        requestedBinding: normalizeBinding(binding),
        lastFollowed: null,
        status: enabled ? "Waiting for Wallpaper Engine…" : "",
        revision: 0
    };
}

export function setManualSelection(state, selection) {
    const next = copyState(state);
    next.manual = normalizeSelection(selection);
    return next;
}

export function setEnabled(state, enabled) {
    const next = copyState(state);
    next.enabled = Boolean(enabled);
    next.status = next.enabled ? "Waiting for Wallpaper Engine…" : "";
    return next;
}

export function setRequestedBinding(state, binding) {
    const next = copyState(state);
    const requested = normalizeBinding(binding);
    if (!sameBinding(next.requestedBinding, requested)) {
        next.requestedBinding = requested;
        next.revision++;
        if (next.enabled) next.status = "Waiting for the selected Wallpaper Engine source…";
    }
    return next;
}

// Optional bindingAtRead lets integrations reject an asynchronous completion
// that began under a superseded binding. Native follower implementations that
// already discard stale generations may omit it; provenance is still checked.
export function acceptFollowerResult(state, result, bindingAtRead = null) {
    const next = copyState(state);
    if (!next.enabled) return next;

    const resultBinding = bindingAtRead ? normalizeBinding(bindingAtRead) : next.requestedBinding;
    if (!sameBinding(resultBinding, next.requestedBinding)) return next;

    const selection = result && result.selection && typeof result.selection === "object"
        ? result.selection : null;
    const provenance = result && result.provenance && typeof result.provenance === "object"
        ? result.provenance : null;
    const usable = selection && typeof selection.source === "string" && selection.source.length > 0
        && matchesProvenance(next.requestedBinding, provenance);
    if (usable) {
        next.lastFollowed = {
            selection: clone(selection),
            provenance: clone(provenance),
            binding: clone(next.requestedBinding)
        };
    }
    if (result && typeof result.status === "string" && result.status)
        next.status = result.status;
    return next;
}

export function view(state) {
    const current = copyState(state);
    const followed = current.enabled && current.lastFollowed;
    const displayed = followed ? current.lastFollowed.selection : current.manual;
    const provenance = followed ? current.lastFollowed.provenance : current.manual.provenance;
    return {
        enabled: current.enabled,
        source: displayed.source,
        workshopid: displayed.workshopid,
        selection: clone(displayed),
        provenance: provenance ? clone(provenance) : null,
        origin: followed
            ? (sameBinding(current.lastFollowed.binding, current.requestedBinding)
                ? "followed" : "previouslyFollowed")
            : (current.enabled ? "manualFallback" : "manual"),
        status: current.status,
        requestedBinding: clone(current.requestedBinding),
        waitingForRequestedBinding: Boolean(current.enabled && (!current.lastFollowed
            || !sameBinding(current.lastFollowed.binding, current.requestedBinding))),
        hasLastFollowed: Boolean(current.lastFollowed)
    };
}
