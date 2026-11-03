"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawnSync } = require("node:child_process");
const vm = require("node:vm");

const repository = path.resolve(__dirname, "../..");
const bridgeSource = fs.readFileSync(
    path.join(repository, "tools", "we-kde-selection-bridge.js"), "utf8");
const idA = "76561197960265729";
const idB = "76561197960265730";
const fileShared = "Z:/SteamLibrary/steamapps/workshop/content/431960/123/project.json";
const fileForB = "Z:/SteamLibrary/steamapps/workshop/content/431960/456/project.json";
const fileLegacy = "Z:/SteamLibrary/steamapps/workshop/content/431960/789/project.json";

function generateBridgeOutput() {
    let clock = Date.UTC(2026, 10, 2, 12, 0, 0);
    let persistenceWrites = 0;
    const scope = {
        browserRuntime: { usersteamid: idA },
        currentSelection: { file: fileShared, status: "installed", type: "scene" },
        selectedMonitor: { location: "Monitor0" },
        wallpaperConfigGroup: "wallpaper",
        browserSettings: {},
        $watchGroup(expressions, callback) {
            assert.ok(expressions.includes("browserRuntime.usersteamid"));
            this.bridgeDigest = callback;
        }
    };
    const previousV1 = { version: 1, file: fileLegacy, monitor: "Monitor0" };
    let stored = previousV1;
    Object.defineProperty(scope.browserSettings, "wekdeSelection", {
        get() { return stored; },
        set(value) { persistenceWrites += 1; stored = value; }
    });
    const rootEvents = {};
    const root = { $on(event, callback) { (rootEvents[event] || (rootEvents[event] = [])).push(callback); } };
    const angular = {
        module(name) {
            assert.equal(name, "wallpaperbrowserApp");
            return { run(definition) { definition[definition.length - 1](root); } };
        }
    };
    class FixedDate extends Date {
        constructor(...args) { super(...(args.length ? args : [clock])); }
    }
    const window = { angular, browseWallpapersCtrl: scope };
    vm.runInNewContext(bridgeSource, {
        window, angular, Date: FixedDate, Number, Object, Array, BigInt,
        setTimeout() { throw new Error("bridge unexpectedly scheduled a timer"); },
        setInterval() { throw new Error("bridge unexpectedly started polling"); }
    }, { filename: path.join(repository, "tools", "we-kde-selection-bridge.js") });

    const digest = () => scope.bridgeDigest();
    digest(); // A selects the shared file; v1 moves only into unassociated.
    clock += 1000;
    scope.browserRuntime.usersteamid = idB;
    digest(); // B selects the same file; keep separate identity provenance.
    const sameFileSnapshot = JSON.parse(JSON.stringify(stored));
    assert.equal(sameFileSnapshot.byUser[idA].file, fileShared);
    assert.equal(sameFileSnapshot.byUser[idB].file, fileShared);
    assert.ok(Object.hasOwn(sameFileSnapshot.byUser, idA));
    assert.ok(Object.hasOwn(sameFileSnapshot.byUser, idB));
    clock += 1000;
    scope.currentSelection.file = fileForB;
    digest(); // B then selects a different file.

    const output = JSON.parse(JSON.stringify(stored));
    assert.equal(output.version, 2);
    assert.equal(output.currentSteamId64, idB);
    assert.equal(output.byUser[idA].file, fileShared);
    assert.equal(output.byUser[idB].file, fileForB);
    assert.equal(output.unassociated.file, fileLegacy);
    assert.equal(persistenceWrites, 3);
    return output;
}

const testBinary = process.env.WEKDE_FOLLOWER_TEST_BIN
    || "/tmp/steam_keyvalues_build/tst_wallpaperenginefollower";
if (!fs.existsSync(testBinary)) {
    throw new Error("Build tst_wallpaperenginefollower and set WEKDE_FOLLOWER_TEST_BIN if needed.");
}
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "wekde-bridge-follower-"));
try {
    const fixturePath = path.join(temporary, "bridge-output.json");
    fs.writeFileSync(fixturePath, JSON.stringify(generateBridgeOutput()), { mode: 0o600 });
    const result = spawnSync(testBinary, ["bridgeOutputEndToEndSwitchesAccountsAndLegacy"], {
        encoding: "utf8",
        timeout: 30000,
        env: { ...process.env, WEKDE_BRIDGE_FOLLOWER_FIXTURE: fixturePath }
    });
    process.stdout.write(result.stdout || "");
    process.stderr.write(result.stderr || "");
    if (result.error) throw result.error;
    if (result.status !== 0) process.exitCode = result.status || 1;
} finally {
    fs.rmSync(temporary, { recursive: true, force: true });
}
