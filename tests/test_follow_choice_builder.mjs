import test from "node:test";
import assert from "node:assert/strict";
import {buildFollowChoices} from "../plugin/contents/ui/js/follow-choice-builder.mjs";

const idA = "76561197960265729";
const idB = "76561197960265730";
const configPath = "/tmp/Wallpaper Engine/config.json";

function verifiedAccount(id, accountId32, label) {
    return {key: `steam:${id}`, steamId64: id, accountId32, label, identityStatus: "verified"};
}

function catalog(accounts, generation = 3) {
    return {schemaVersion: 1, generation, installations: [], accounts, caches: []};
}

test("builds deterministic verified account choices and explicitly unassociated legacy choices", () => {
    const a = verifiedAccount(idA, "1", "Shared persona");
    const b = verifiedAccount(idB, "2", "Shared persona");
    const choices = buildFollowChoices(catalog([b, a]), ["steamuser", "Other", "steamuser"], configPath);
    assert.deepEqual(choices, [
        {mode: "account", accountKey: `steam:${idA}`,
            sourceKey: `config:${configPath}#account:${idA}`,
            label: "Saved selection (1)", identityVerified: true},
        {mode: "account", accountKey: `steam:${idB}`,
            sourceKey: `config:${configPath}#account:${idB}`,
            label: "Saved selection (2)", identityVerified: true},
        {mode: "legacySession", accountKey: "",
            sourceKey: `config:${configPath}#section:Other`,
            label: "Unassociated Wallpaper Engine session — Other", identityVerified: false},
        {mode: "legacySession", accountKey: "",
            sourceKey: `config:${configPath}#section:steamuser`,
            label: "Unassociated Wallpaper Engine session — steamuser", identityVerified: false}
    ]);
    assert.equal(new Set(choices.map(choice => `${choice.mode}|${choice.accountKey}|${choice.sourceKey}`)).size,
        choices.length);
    assert.equal(choices.some(choice => choice.mode === "identifiedCurrent"), false);
    assert.deepEqual(buildFollowChoices(catalog([a, b]), ["Other", "steamuser"], configPath), choices);
});

test("keeps only canonical, matching, available verified catalog identities", () => {
    const accepted = verifiedAccount(idA, "1", "One");
    const choices = buildFollowChoices(catalog([
        accepted,
        {...verifiedAccount(idB, "2", "Unresolved"), identityStatus: "unresolved"},
        {...verifiedAccount("1", "1", "Short"), key: "steam:1"},
        {...verifiedAccount("076561197960265729", "1", "Leading zero"), key: "steam:076561197960265729"},
        {...accepted, key: "steam:76561197960265730"},
        {...accepted, accountId32: "2"},
        {...accepted, steamId64: 76561197960265729},
        {...verifiedAccount("76561202255233024", "4294967296", "Overflow account"),
            key: "steam:76561202255233024"}
    ]), [], configPath);
    assert.equal(choices.length, 1);
    assert.equal(choices[0].accountKey, `steam:${idA}`);
    assert.equal(choices[0].identityVerified, true);
});

test("missing, loading, unavailable, or malformed catalog does not create account choices", () => {
    const onlyLegacy = [{mode: "legacySession", accountKey: "",
        sourceKey: `config:${configPath}#section:steamuser`,
        label: "Unassociated Wallpaper Engine session — steamuser", identityVerified: false}];
    for (const unavailable of [null, undefined, {},
        {schemaVersion: 1, generation: 0, accounts: [verifiedAccount(idA, "1", "A")]},
        {schemaVersion: 2, generation: 3, accounts: [verifiedAccount(idA, "1", "A")]},
        {schemaVersion: 1, generation: 3, accounts: "not-an-array"}]) {
        assert.deepEqual(buildFollowChoices(unavailable, ["steamuser"], configPath), onlyLegacy);
    }
    assert.deepEqual(buildFollowChoices(catalog([verifiedAccount(idA, "1", "A")]), [], "relative/config.json"), []);
});

test("legacy section source keys preserve config provenance and percent-encode exact section names", () => {
    const choices = buildFollowChoices(null, ["A#B", "", "A#B", " Héllo "], "/tmp/root/../WE/config.json");
    assert.deepEqual(choices.map(choice => choice.sourceKey), [
        "config:/tmp/WE/config.json#section:%20H%C3%A9llo%20",
        "config:/tmp/WE/config.json#section:A%23B"
    ]);
    assert.ok(choices.every(choice => choice.accountKey === "" && choice.identityVerified === false));
    assert.ok(choices.every(choice => choice.label.startsWith("Unassociated Wallpaper Engine session")));
});

test("accepts bounded array-like Qt lists and rejects malformed list wrappers", () => {
    const fromQtList = buildFollowChoices(null,
        {0: "steamuser", 1: "Other", length: 2}, configPath);
    assert.deepEqual(fromQtList.map(choice => choice.sourceKey), [
        `config:${configPath}#section:Other`,
        `config:${configPath}#section:steamuser`
    ]);
    assert.deepEqual(buildFollowChoices(null, {0: "steamuser", length: "1"}, configPath), []);
    assert.deepEqual(buildFollowChoices(null, {0: "steamuser", length: 5000}, configPath), []);
    const throwingLength = Object.defineProperty({}, "length", {
        get() { throw new Error("invalid list wrapper"); }
    });
    assert.deepEqual(buildFollowChoices(null, throwingLength, configPath), []);
    assert.deepEqual(buildFollowChoices(null, "steamuser", configPath), []);
});

test("includes only an exact saved user-associated mapping still present in catalog and legacy sections", () => {
    const account = verifiedAccount(idA, "1", "Account One");
    const available = catalog([account]);
    const validBinding = {
        mode: "manualMapping",
        accountKey: `steam:${idA}`,
        sourceKey: `config:${configPath}#section:steamuser`
    };
    const choices = buildFollowChoices(available, ["steamuser"], configPath, validBinding);
    const mappings = choices.filter(choice => choice.mode === "manualMapping");
    assert.deepEqual(mappings, [{
        mode: "manualMapping",
        accountKey: `steam:${idA}`,
        sourceKey: validBinding.sourceKey,
        label: "User-associated mapping — Account One → steamuser",
        identityVerified: false,
        userConfirmed: true
    }]);

    const invalidBindings = [
        {...validBinding, accountKey: `steam:${idB}`}, // no longer catalog verified
        {...validBinding, sourceKey: `config:${configPath}#section:Removed`}, // stale section
        {...validBinding, sourceKey: "config:/tmp/other/config.json#section:steamuser"},
        {...validBinding, sourceKey: `config:${configPath}#section:%ZZ`},
        {...validBinding, accountKey: 76561197960265729},
        {mode: "account", accountKey: `steam:${idA}`, sourceKey: validBinding.sourceKey},
        "malformed"
    ];
    for (const binding of invalidBindings) {
        assert.deepEqual(buildFollowChoices(available, ["steamuser"], configPath, binding)
            .filter(choice => choice.mode === "manualMapping"), []);
    }
});
