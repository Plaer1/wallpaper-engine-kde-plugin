import test from "node:test";
import assert from "node:assert/strict";
import * as FollowState from "../plugin/contents/ui/js/follow-source-state.mjs";

const bindingA = {mode: "account", accountKey: "steam:76561190000000001",
    sourceKey: "config:/tmp/we/config.json#account:76561190000000001"};
const bindingB = {mode: "account", accountKey: "steam:76561190000000002",
    sourceKey: "config:/tmp/we/config.json#account:76561190000000002"};

function result(binding, source, status = "Following Wallpaper Engine selection.") {
    const configPrefix = binding.sourceKey.split("#", 1)[0];
    return {
        selection: {source, workshopid: source.split("/").at(-1)},
        provenance: {mode: binding.mode, accountKey: binding.accountKey,
            sourceKey: `${configPrefix}#section:${binding.accountKey.slice(-2)}`,
            sourceType: "nativeAccount", identityStatus: "verified", label: binding.accountKey},
        status
    };
}

function sectionBinding(mode, section, accountKey = "", configPath = "/tmp/we/config.json") {
    return {mode, accountKey, sourceKey: `config:${configPath}#section:${section}`};
}
function withProvenance(base, patch) {
    return {...base, provenance: {...base.provenance, ...patch}};
}

test("initial enabled state uses only manual fallback until a valid selection arrives", () => {
    let state = FollowState.create({source: "file:///manual.pkg+scene", workshopid: "manual-id"}, true, bindingA);
    let displayed = FollowState.view(state);
    assert.equal(displayed.source, "file:///manual.pkg+scene");
    assert.equal(displayed.workshopid, "manual-id");
    assert.equal(displayed.origin, "manualFallback");
    assert.equal(displayed.provenance, null);
    assert.equal(displayed.hasLastFollowed, false);
    assert.equal(displayed.waitingForRequestedBinding, true);

    state = FollowState.acceptFollowerResult(state, result(bindingA, "file:///A/one.pkg+scene"));
    displayed = FollowState.view(state);
    assert.equal(displayed.source, "file:///A/one.pkg+scene");
    assert.equal(displayed.origin, "followed");
    assert.equal(displayed.provenance.accountKey, bindingA.accountKey);
});

test("account switch retains old wallpaper and its provenance until the new source resolves", () => {
    let state = FollowState.create("file:///manual.pkg+scene", true, bindingA);
    state = FollowState.acceptFollowerResult(state, result(bindingA, "file:///A/current.pkg+scene"));
    state = FollowState.setRequestedBinding(state, bindingB);

    let displayed = FollowState.view(state);
    assert.equal(displayed.source, "file:///A/current.pkg+scene");
    assert.equal(displayed.provenance.accountKey, bindingA.accountKey);
    assert.equal(displayed.requestedBinding.accountKey, bindingB.accountKey);
    assert.equal(displayed.origin, "previouslyFollowed");
    assert.equal(displayed.waitingForRequestedBinding, true);

    // A late A result is discarded after the binding changes.
    state = FollowState.acceptFollowerResult(state, result(bindingA, "file:///A/stale.pkg+scene"), bindingA);
    assert.equal(FollowState.view(state).source, "file:///A/current.pkg+scene");

    state = FollowState.acceptFollowerResult(state, result(bindingB, "file:///B/current.pkg+scene"));
    displayed = FollowState.view(state);
    assert.equal(displayed.source, "file:///B/current.pkg+scene");
    assert.equal(displayed.provenance.accountKey, bindingB.accountKey);
    assert.equal(displayed.origin, "followed");
    assert.equal(displayed.waitingForRequestedBinding, false);
});

test("malformed temporary writes retain last-good selection and provenance", () => {
    let state = FollowState.create("file:///manual.pkg+scene", true, bindingA);
    state = FollowState.acceptFollowerResult(state, result(bindingA, "file:///A/current.pkg+scene"));

    state = FollowState.acceptFollowerResult(state, {
        status: "Wallpaper Engine settings are incomplete.",
        selection: {},
        provenance: {mode: bindingA.mode, accountKey: bindingA.accountKey, sourceKey: ""}
    });
    const displayed = FollowState.view(state);
    assert.equal(displayed.source, "file:///A/current.pkg+scene");
    assert.equal(displayed.provenance.accountKey, bindingA.accountKey);
    assert.equal(displayed.status, "Wallpaper Engine settings are incomplete.");
    assert.equal(displayed.origin, "followed");
});

test("disabling Follow restores manual source and re-enable resumes last good while waiting", () => {
    let state = FollowState.create({source: "file:///manual/first.pkg+scene", workshopid: "first"}, true, bindingA);
    state = FollowState.acceptFollowerResult(state, result(bindingA, "file:///A/current.pkg+scene"));
    state = FollowState.setEnabled(state, false);
    assert.equal(FollowState.view(state).source, "file:///manual/first.pkg+scene");
    assert.equal(FollowState.view(state).origin, "manual");

    state = FollowState.setManualSelection(state, {source: "file:///manual/updated.pkg+scene", workshopid: "updated"});
    state = FollowState.setEnabled(state, true);
    state = FollowState.setRequestedBinding(state, bindingB);
    const displayed = FollowState.view(state);
    assert.equal(displayed.source, "file:///A/current.pkg+scene");
    assert.equal(displayed.provenance.accountKey, bindingA.accountKey);
    assert.equal(displayed.origin, "previouslyFollowed");
    assert.equal(displayed.waitingForRequestedBinding, true);
    assert.equal(displayed.hasLastFollowed, true);
});

test("same-account source switches require the matching captured request binding", () => {
    const otherSource = {...bindingA, sourceKey: "config:/tmp/other/config.json#account:76561190000000001"};
    let state = FollowState.create("file:///manual.pkg+scene", true, bindingA);
    state = FollowState.acceptFollowerResult(state, result(bindingA, "file:///A/current.pkg+scene"));
    state = FollowState.setRequestedBinding(state, otherSource);

    state = FollowState.acceptFollowerResult(state, result(bindingA, "file:///A/stale.pkg+scene"), bindingA);
    assert.equal(FollowState.view(state).source, "file:///A/current.pkg+scene");
    state = FollowState.acceptFollowerResult(state, result(otherSource, "file:///A/new-config.pkg+scene"), otherSource);
    assert.equal(FollowState.view(state).source, "file:///A/new-config.pkg+scene");
});

test("legacy native and F3 v1/v2 unassociated bridge provenance match only their exact section", () => {
    const binding = sectionBinding("legacySession", "steamuser");
    let state = FollowState.create("file:///manual.pkg+scene", true, binding);
    const native = withProvenance(result(binding, "file:///legacy/native.pkg+scene"), {
        sourceKey: binding.sourceKey,
        accountKey: "",
        sourceType: "legacyUnassociatedNative",
        identityStatus: "unassociated"
    });
    state = FollowState.acceptFollowerResult(state, native);
    assert.equal(FollowState.view(state).source, "file:///legacy/native.pkg+scene");

    const v1 = withProvenance(result(binding, "file:///legacy/v1.pkg+scene"), {
        sourceKey: `${binding.sourceKey}.general.browser.wekdeSelection.v1`,
        accountKey: "",
        sourceType: "legacyUnassociatedBridge",
        identityStatus: "unassociated"
    });
    state = FollowState.acceptFollowerResult(state, v1);
    assert.equal(FollowState.view(state).source, "file:///legacy/v1.pkg+scene");

    const v2 = withProvenance(result(binding, "file:///legacy/v2.pkg+scene"), {
        sourceKey: `${binding.sourceKey}.general.browser.wekdeSelection.unassociated`,
        accountKey: "",
        sourceType: "legacyUnassociatedBridge",
        identityStatus: "unassociated"
    });
    state = FollowState.acceptFollowerResult(state, v2);
    assert.equal(FollowState.view(state).source, "file:///legacy/v2.pkg+scene");

    const wrongSection = withProvenance(result(binding, "file:///legacy/wrong-section.pkg+scene"), {
        sourceKey: "config:/tmp/we/config.json#section:Other.general.browser.wekdeSelection.unassociated",
        accountKey: ""
    });
    state = FollowState.acceptFollowerResult(state, wrongSection);
    assert.equal(FollowState.view(state).source, "file:///legacy/v2.pkg+scene");

    const wrongConfig = withProvenance(result(binding, "file:///legacy/wrong-config.pkg+scene"), {
        sourceKey: "config:/tmp/other/config.json#section:steamuser.general.browser.wekdeSelection.unassociated",
        accountKey: ""
    });
    state = FollowState.acceptFollowerResult(state, wrongConfig);
    assert.equal(FollowState.view(state).source, "file:///legacy/v2.pkg+scene");

    const attributed = withProvenance(v2, {accountKey: bindingA.accountKey});
    state = FollowState.acceptFollowerResult(state, attributed);
    assert.equal(FollowState.view(state).source, "file:///legacy/v2.pkg+scene");
});

test("manual mapping accepts its native or v1 record but never another section/config", () => {
    const binding = sectionBinding("manualMapping", "steamuser", bindingA.accountKey);
    let state = FollowState.create("file:///manual.pkg+scene", true, binding);
    const native = withProvenance(result(binding, "file:///mapped/native.pkg+scene"), {
        sourceKey: binding.sourceKey, accountKey: binding.accountKey,
        sourceType: "manualMapping", identityStatus: "userAssociated"
    });
    state = FollowState.acceptFollowerResult(state, native);
    assert.equal(FollowState.view(state).source, "file:///mapped/native.pkg+scene");

    const v1 = withProvenance(result(binding, "file:///mapped/v1.pkg+scene"), {
        sourceKey: `${binding.sourceKey}.general.browser.wekdeSelection.v1`,
        accountKey: binding.accountKey, sourceType: "manualMapping", identityStatus: "userAssociated"
    });
    state = FollowState.acceptFollowerResult(state, v1);
    assert.equal(FollowState.view(state).source, "file:///mapped/v1.pkg+scene");

    const wrongSection = withProvenance(result(binding, "file:///mapped/wrong-section.pkg+scene"), {
        sourceKey: "config:/tmp/we/config.json#section:another.general.browser.wekdeSelection.v1",
        accountKey: binding.accountKey
    });
    state = FollowState.acceptFollowerResult(state, wrongSection);
    assert.equal(FollowState.view(state).source, "file:///mapped/v1.pkg+scene");

    const wrongConfig = withProvenance(result(binding, "file:///mapped/wrong-config.pkg+scene"), {
        sourceKey: "config:/tmp/other/config.json#section:steamuser.general.browser.wekdeSelection.v1",
        accountKey: binding.accountKey
    });
    state = FollowState.acceptFollowerResult(state, wrongConfig);
    assert.equal(FollowState.view(state).source, "file:///mapped/v1.pkg+scene");

    const wrongAccount = withProvenance(v1, {accountKey: bindingB.accountKey});
    state = FollowState.acceptFollowerResult(state, wrongAccount);
    assert.equal(FollowState.view(state).source, "file:///mapped/v1.pkg+scene");

    const unsupportedV2 = withProvenance(result(binding, "file:///mapped/v2-unassociated.pkg+scene"), {
        sourceKey: `${binding.sourceKey}.general.browser.wekdeSelection.unassociated`,
        accountKey: binding.accountKey
    });
    state = FollowState.acceptFollowerResult(state, unsupportedV2);
    assert.equal(FollowState.view(state).source, "file:///mapped/v1.pkg+scene");
});

test("account provenance must match the selected account and config path", () => {
    let state = FollowState.create("file:///manual.pkg+scene", true, bindingA);
    const valid = result(bindingA, "file:///A/current.pkg+scene");
    state = FollowState.acceptFollowerResult(state, valid);
    assert.equal(FollowState.view(state).source, "file:///A/current.pkg+scene");

    const otherAccount = withProvenance(result(bindingA, "file:///wrong/account.pkg+scene"), {
        accountKey: bindingB.accountKey
    });
    state = FollowState.acceptFollowerResult(state, otherAccount);
    assert.equal(FollowState.view(state).source, "file:///A/current.pkg+scene");

    const otherConfig = withProvenance(result(bindingA, "file:///wrong/config.pkg+scene"), {
        sourceKey: "config:/tmp/other/config.json#section:76561190000000001"
    });
    state = FollowState.acceptFollowerResult(state, otherConfig);
    assert.equal(FollowState.view(state).source, "file:///A/current.pkg+scene");
});
