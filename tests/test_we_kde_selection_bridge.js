"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const bridgePath = path.join(__dirname, "..", "tools", "we-kde-selection-bridge.js");
const bridgeSource = fs.readFileSync(bridgePath, "utf8");
const migratedFixture = JSON.parse(fs.readFileSync(
    path.join(__dirname, "fixtures", "we_kde_selection_v1_migrated.json"), "utf8"));
const idA = "76561197960265729";
const idB = "76561197960265730";

function makeHarness(initialSelection) {
    let persistWrites = 0;
    let now = Date.UTC(2026, 10, 2, 12, 0, 0);
    let timerCalls = 0;
    const listeners = {};
    const groupWatches = [];
    let lastValues = null;
    const nativeStore = { value: initialSelection || null };
    const browserSettings = {};
    Object.defineProperty(browserSettings, "wekdeSelection", {
        get() { return nativeStore.value; },
        set(value) { persistWrites += 1; nativeStore.value = value; }
    });
    const scope = {
        browserRuntime: { usersteamid: idA },
        currentSelection: { file: "/wallpapers/431960/100/project.json", status: "installed", type: "scene" },
        selectedMonitor: { location: "Monitor0" },
        wallpaperConfigGroup: "wallpaper",
        browserSettings,
        $watchGroup(expressions, callback) { groupWatches.push({ expressions, callback }); },
        $$destroyed: false
    };
    const root = {
        $on(event, callback) { (listeners[event] || (listeners[event] = [])).push(callback); }
    };
    const angular = {
        module(name) {
            assert.equal(name, "wallpaperbrowserApp");
            return { run(definition) { definition[definition.length - 1](root); } };
        }
    };
    class FakeDate extends Date {
        constructor(...args) { super(...(args.length ? args : [now])); }
        static now() { return now; }
    }
    const window = { angular, browseWallpapersCtrl: scope };
    const sandbox = {
        window,
        angular,
        Date: FakeDate,
        Number,
        Object,
        Array,
        BigInt,
        console,
        setTimeout() { timerCalls += 1; throw new Error("bridge must not poll or schedule timers"); },
        setInterval() { timerCalls += 1; throw new Error("bridge must not poll or schedule timers"); },
        clearTimeout() {},
        clearInterval() {}
    };
    vm.runInNewContext(bridgeSource, sandbox, { filename: bridgePath });

    function valueAt(expression) {
        return expression.split(".").reduce((value, key) => value == null ? undefined : value[key], scope);
    }
    function digest() {
        assert.equal(groupWatches.length, 1, "bridge registers exactly one event-driven watch group");
        const watch = groupWatches[0];
        const values = watch.expressions.map(valueAt);
        if (!lastValues || values.some((value, index) => !Object.is(value, lastValues[index]))) {
            const previous = lastValues;
            lastValues = values;
            watch.callback(values, previous);
        }
    }
    function tick() { now += 1000; }
    return {
        scope,
        digest,
        tick,
        get writes() { return persistWrites; },
        get timers() { return timerCalls; },
        get selection() { return nativeStore.value; },
        get expressions() { return groupWatches[0] && groupWatches[0].expressions; },
        trigger(event) { (listeners[event] || []).forEach(callback => callback()); }
    };
}

test("watches identity with selection and persists string-keyed v2 data", () => {
    const h = makeHarness();
    h.digest();
    assert.equal(h.writes, 1);
    assert.equal(h.selection.version, 2);
    assert.equal(h.selection.currentSteamId64, idA);
    assert.equal(h.selection.byUser[idA].file, "/wallpapers/431960/100/project.json");
    assert.equal(h.selection.byUser[idA].monitor, "Monitor0");
    assert.equal(typeof Object.keys(h.selection.byUser)[0], "string");
    assert.ok(h.expressions.includes("browserRuntime.usersteamid"));
});

test("does not persist again without identity, file, or monitor change", () => {
    const h = makeHarness();
    h.digest();
    const savedAt = h.selection.byUser[idA].savedAtUtc;
    h.tick();
    h.digest();
    h.scope.currentSelection.status = "downloadable";
    h.digest();
    h.scope.currentSelection.status = "installed";
    h.digest();
    assert.equal(h.writes, 1);
    assert.equal(h.selection.byUser[idA].savedAtUtc, savedAt);
    assert.equal(h.timers, 0);
});

test("same file under another account creates a distinct identity record", () => {
    const h = makeHarness();
    h.digest();
    h.tick();
    h.scope.browserRuntime.usersteamid = idB;
    h.digest();
    assert.equal(h.writes, 2);
    assert.equal(h.selection.currentSteamId64, idB);
    assert.equal(h.selection.byUser[idA].file, h.selection.byUser[idB].file);
    assert.notEqual(h.selection.byUser[idA].savedAtUtc, h.selection.byUser[idB].savedAtUtc);
});

test("watches file and monitor changes for the current identity", () => {
    const h = makeHarness();
    h.digest();
    h.tick();
    h.scope.currentSelection.file = "/wallpapers/431960/200/project.json";
    h.digest();
    const fileChangeTime = h.selection.byUser[idA].savedAtUtc;
    assert.equal(h.writes, 2);
    assert.equal(h.selection.byUser[idA].file, "/wallpapers/431960/200/project.json");
    h.tick();
    h.scope.selectedMonitor.location = "Monitor1";
    h.digest();
    assert.equal(h.writes, 3);
    assert.equal(h.selection.byUser[idA].monitor, "Monitor1");
    assert.notEqual(h.selection.byUser[idA].savedAtUtc, fileChangeTime);
});

test("unsafe and noncanonical identities use the unassociated record", () => {
    const invalidIds = [0, 76561197960265729, "0", "1", "076561197960265729",
        "76561197960265728", "76561202255233024", "18446744073709551616"];
    for (const invalid of invalidIds) {
        const h = makeHarness();
        h.scope.browserRuntime.usersteamid = invalid;
        h.digest();
        assert.equal(h.selection.currentSteamId64, "", `identity ${String(invalid)} must not be trusted`);
        assert.deepEqual(Object.keys(h.selection.byUser), []);
        assert.equal(h.selection.unassociated.file, "/wallpapers/431960/100/project.json");
    }
});

test("legacy v1 migrates only to the unassociated slot", () => {
    const h = makeHarness({
        version: 1,
        file: "Z:/SteamLibrary/steamapps/workshop/content/431960/456/project.json",
        monitor: "Monitor1"
    });
    h.scope.currentSelection.file = "Z:/SteamLibrary/steamapps/workshop/content/431960/123/project.json";
    h.digest();
    assert.deepEqual(JSON.parse(JSON.stringify(h.selection)), migratedFixture);
    assert.equal(h.selection.version, 2);
    assert.equal(h.selection.unassociated.file,
        "Z:/SteamLibrary/steamapps/workshop/content/431960/456/project.json");
    assert.equal(h.selection.unassociated.monitor, "Monitor1");
    assert.equal(h.selection.byUser[idA].file,
        "Z:/SteamLibrary/steamapps/workshop/content/431960/123/project.json");
});

test("retains at most 128 users and evicts oldest saved entry deterministically", () => {
    const records = {};
    for (let i = 1; i <= 128; i += 1) {
        const id = (BigInt("76561197960265728") + BigInt(i)).toString();
        records[id] = {
            file: `/old/${i}.json`, monitor: "Monitor0",
            savedAtUtc: new Date(Date.UTC(2026, 0, 1, 0, 0, i)).toISOString()
        };
    }
    const previousId = Object.keys(records)[0];
    const newId = (BigInt("76561197960265728") + 129n).toString();
    const h = makeHarness({ version: 2, currentSteamId64: previousId, byUser: records, unassociated: null });
    h.scope.browserRuntime.usersteamid = newId;
    h.digest();
    assert.equal(Object.keys(h.selection.byUser).length, 128);
    assert.equal(Object.hasOwn(h.selection.byUser, previousId), false);
    assert.equal(Object.hasOwn(h.selection.byUser, newId), true);
});

test("ignores incomplete selections and never starts polling", () => {
    const h = makeHarness();
    h.scope.currentSelection = { file: "/bad", status: "downloading", type: "scene" };
    h.digest();
    assert.equal(h.writes, 0);
    h.scope.currentSelection.status = "installed";
    h.scope.currentSelection.type = "folder";
    h.digest();
    assert.equal(h.writes, 0);
    assert.equal(h.timers, 0);
    assert.equal(h.expressions.length, 6);
});
