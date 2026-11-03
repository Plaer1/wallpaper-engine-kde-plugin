// Pure per-key state for staged cfg_ values. The Plasma host copies all cfg_ fields
// after saveConfig(), so callers must synchronously apply exports before returning.

function cloneMap(value) {
    const result = {};
    for (const key of Object.keys(value || {})) result[key] = value[key];
    return result;
}

function copyState(state) {
    return {
        baseline: cloneMap(state.baseline),
        live: cloneMap(state.live),
        draft: cloneMap(state.draft),
        dirty: cloneMap(state.dirty),
        conflicts: cloneMap(state.conflicts)
    };
}

function sameValue(left, right) {
    if (left === right) return true;
    if (left === undefined || right === undefined || left === null || right === null) return false;
    if (typeof left !== "object" || typeof right !== "object") return false;
    return JSON.stringify(left) === JSON.stringify(right);
}

export function create(initialValues) {
    const values = cloneMap(initialValues);
    return {
        baseline: cloneMap(values),
        live: cloneMap(values),
        draft: cloneMap(values),
        dirty: {},
        conflicts: {}
    };
}

export function stage(state, key, value) {
    const next = copyState(state);
    next.draft[key] = value;
    if (sameValue(value, next.baseline[key])) {
        delete next.dirty[key];
    } else {
        next.dirty[key] = true;
    }

    if (sameValue(value, next.live[key])) {
        next.baseline[key] = next.live[key];
        delete next.dirty[key];
        delete next.conflicts[key];
    } else if (next.dirty[key] && !sameValue(next.live[key], next.baseline[key])) {
        next.conflicts[key] = true;
    } else {
        delete next.conflicts[key];
    }
    return next;
}

export function updateLive(state, patch) {
    const next = copyState(state);
    for (const key of Object.keys(patch || {})) {
        const value = patch[key];
        next.live[key] = value;
        if (!next.dirty[key]) {
            next.baseline[key] = value;
            next.draft[key] = value;
            delete next.conflicts[key];
        } else if (sameValue(value, next.draft[key])) {
            // Both writers converged to the same value; no conflict remains.
            next.baseline[key] = value;
            next.draft[key] = value;
            delete next.dirty[key];
            delete next.conflicts[key];
        } else if (sameValue(value, next.baseline[key])) {
            // Live returned to the baseline; keep the draft, clear the conflict.
            delete next.conflicts[key];
        } else {
            next.conflicts[key] = true;
        }
    }
    return next;
}

export function conflict(state, key) {
    if (!state.conflicts[key]) return null;
    return {
        baseline: state.baseline[key],
        live: state.live[key],
        draft: state.draft[key]
    };
}

export function conflictKeys(state) {
    return Object.keys(state.conflicts).filter(key => state.conflicts[key]).sort();
}

export function resolveConflict(state, key, choice) {
    if (!state.conflicts[key]) return copyState(state);
    const next = copyState(state);
    if (choice === "keepLive") {
        next.baseline[key] = next.live[key];
        next.draft[key] = next.live[key];
        delete next.dirty[key];
        delete next.conflicts[key];
        return next;
    }
    if (choice === "useMyEdit") {
        next.baseline[key] = next.live[key];
        if (sameValue(next.draft[key], next.live[key])) {
            delete next.dirty[key];
        } else {
            next.dirty[key] = true;
        }
        delete next.conflicts[key];
        return next;
    }
    return next;
}

export function exports(state, keys) {
    const result = {};
    for (const key of keys) {
        if (state.conflicts[key]) result[key] = state.live[key];
        else if (state.dirty[key]) result[key] = state.draft[key];
        else result[key] = state.live[key];
    }
    return result;
}
