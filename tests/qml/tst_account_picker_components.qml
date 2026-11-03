import QtQuick
import QtTest
import "../../plugin/contents/ui/components" as Components

TestCase {
    id: testCase
    name: "AccountPickerComponents"
    when: windowShown
    visible: true
    width: 560
    height: 640

    function verified(key, id, label) {
        return {key, steamId64: id, accountId32: id.slice(-8), label,
            identityStatus: "verified", cacheKeys: [key + ":cache"]};
    }
    function unresolved(key, label) {
        return {key, steamId64: "", accountId32: key.split(":").pop(), label,
            identityStatus: "unresolved", cacheKeys: []};
    }
    function catalog(accounts, caches) {
        return {schemaVersion: 1, generation: 1, installations: [], accounts, caches};
    }
    function cache(accountKey, state, subscribedIds) {
        return {key: accountKey + ":cache", accountKey, state, subscribedIds, disabledIds: []};
    }
    function sources() {
        return [
            {mode: "account", accountKey: "steam:76561190000000001",
                sourceKey: "config:Alice", label: "Wallpaper Engine saved selection", available: true,
                status: "Following Alice's verified saved selection."},
            {mode: "account", accountKey: "steam:76561190000000002",
                sourceKey: "config:Bob", label: "Wallpaper Engine saved selection", available: true},
            {mode: "account", accountKey: "userdata:root:12",
                sourceKey: "config:unresolved", label: "Unresolved section", available: true},
            {mode: "legacySession", accountKey: "", sourceKey: "config:steamuser",
                label: "Wallpaper Engine session", available: true},
            {mode: "identifiedCurrent", accountKey: "steam:76561190000000001",
                sourceKey: "current:Alice", label: "Current identified user", available: true},
            {mode: "manualMapping", accountKey: "userdata:root:12",
                sourceKey: "config:legacy", label: "Legacy steamuser section", userConfirmed: false, available: true},
            {mode: "manualMapping", accountKey: "steam:76561190000000001",
                sourceKey: "config:manual", label: "Legacy section mapped by user", userConfirmed: true, available: true}
        ];
    }

    Component {
        id: accountPickerComponent
        Components.SteamAccountPicker { }
    }
    Component {
        id: followPickerComponent
        Components.FollowSourcePicker { }
    }
    Component {
        id: signalSpyComponent
        SignalSpy { }
    }

    function spy(target, signalName) {
        const result = createTemporaryObject(signalSpyComponent, testCase,
            {target, signalName});
        verify(result);
        return result;
    }

    function test_singleAccountUsesDirectChoiceAndFreshSetupDefaultsToIt() {
        const a = verified("steam:76561190000000001", "76561190000000001", "Alice");
        const picker = createTemporaryObject(accountPickerComponent, testCase, {
            catalog: catalog([a], [cache(a.key, "valid", ["11"])]),
            freshSetup: true
        });
        verify(picker);
        const changed = spy(picker, "browseScopeSelected");
        tryCompare(changed, "count", 1);
        const args = changed.signalArguments[0];
        compare(args[0], "selectedAccounts");
        compare(args[1], [a.key]);
        compare(args[2], true);
        compare(findChild(picker, "account-steam:76561190000000001").text, "Alice");
        compare(findChild(picker, "allDownloadedOption").text, "All downloaded wallpapers");
        verify(!findChild(picker, "accountCombo")); // no dropdown for one user
    }

    function test_multiAccountCheckboxesAreKeyboardAccessibleAndIndependentFromFollow() {
        const a = verified("steam:76561190000000001", "76561190000000001", "Shared persona");
        const b = verified("steam:76561190000000002", "76561190000000002", "Shared persona");
        const local = unresolved("userdata:root:12", "Local user");
        const c = catalog([a, b, local], [cache(a.key, "valid", ["1"]), cache(b.key, "valid", ["2"])]);
        const browse = createTemporaryObject(accountPickerComponent, testCase, {
            catalog: c, mode: "selectedAccounts", accountKeys: [a.key]
        });
        const follow = createTemporaryObject(followPickerComponent, testCase, {
            catalog: c, followSources: sources(),
            selection: {mode: "account", accountKey: a.key, sourceKey: "config:Alice"}
        });
        verify(browse && follow);
        const browseSignal = spy(browse, "browseScopeSelected");
        const followSignal = spy(follow, "followSourceSelected");
        compare(browse.accountChoices[0].displayLabel, "Shared persona (…0001)");
        compare(browse.accountChoices[1].displayLabel, "Shared persona (…0002)");

        tryVerify(() => findChild(browse, "account-" + b.key));
        const bob = findChild(browse, "account-" + b.key);
        verify(bob);
        bob.forceActiveFocus();
        keyClick(Qt.Key_Space);
        tryCompare(browseSignal, "count", 1);
        compare(browseSignal.signalArguments[0][0], "selectedAccounts");
        compare(browseSignal.signalArguments[0][1], [a.key, b.key]);
        compare(follow.selection.accountKey, a.key);
        compare(follow.selection.sourceKey, "config:Alice");
        compare(followSignal.count, 0);

        const combo = findChild(follow, "followSourceCombo");
        verify(combo && combo.visible);
        combo.forceActiveFocus();
        keyClick(Qt.Key_Space);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Enter);
        tryCompare(followSignal, "count", 1);
        verify(["steam:"].every(prefix => browse.accountKeys[0].startsWith(prefix)));
        compare(browse.accountKeys, [a.key]); // changing Follow never writes browse selection
        compare(followSignal.signalArguments[0][0].mode, "account");
        compare(followSignal.signalArguments[0][0].accountKey, b.key);
    }

    function test_emptyAndUnavailableBrowseScopesRemainExplicit() {
        const a = verified("steam:76561190000000001", "76561190000000001", "Alice");
        const picker = createTemporaryObject(accountPickerComponent, testCase, {
            catalog: catalog([a], [cache(a.key, "unavailable", [])]),
            mode: "selectedAccounts", accountKeys: ["steam:76561190000000099"]
        });
        verify(picker);
        compare(findChild(picker, "emptyAccountSelection").visible, false);
        tryVerify(() => findChild(picker, "account-steam:76561190000000099"));
        verify(findChild(picker, "account-steam:76561190000000099").text.includes("Unavailable"));
        compare(findChild(picker, "account-steam:76561190000000099").checked, true);
        compare(picker.accountChoices[0].health, "Subscription data unavailable");

        picker.accountKeys = [];
        compare(findChild(picker, "emptyAccountSelection").visible, true);
        const changed = spy(picker, "browseScopeSelected");
        const all = findChild(picker, "allDownloadedOption");
        all.forceActiveFocus();
        keyClick(Qt.Key_Space);
        tryCompare(changed, "count", 1);
        compare(changed.signalArguments[0][0], "allDownloaded");
        compare(changed.signalArguments[0][1], []);

        const noAccounts = createTemporaryObject(accountPickerComponent, testCase, {
            catalog: catalog([], []), mode: "selectedAccounts", accountKeys: []
        });
        verify(noAccounts);
        compare(findChild(noAccounts, "noAccountsAvailable").visible, true);
        compare(findChild(noAccounts, "emptyAccountSelection").visible, true);
    }

    function test_followSourceVerificationManualMappingAndUnavailableState() {
        const a = verified("steam:76561190000000001", "76561190000000001", "Alice");
        const b = verified("steam:76561190000000002", "76561190000000002", "Bob");
        const local = unresolved("userdata:root:12", "Local user");
        const picker = createTemporaryObject(followPickerComponent, testCase, {
            catalog: catalog([a, b, local], []),
            followSources: sources(),
            selection: {mode: "account", accountKey: a.key, sourceKey: "config:Alice"}
        });
        verify(picker);
        compare(picker.choices.length, 4); // two verified, legacy, and explicit manual mapping
        verify(!picker.choices.some(choice => choice.binding.mode === "identifiedCurrent"));
        verify(!picker.choices.some(choice => choice.binding.sourceKey === "config:unresolved"));
        verify(!picker.choices.some(choice => choice.binding.sourceKey === "config:legacy"));
        verify(picker.choices.some(choice => choice.label.startsWith("Unassociated —")));
        verify(picker.choices.some(choice => choice.label.startsWith("User-associated —")));
        verify(findChild(picker, "manualMappingExplanation").text.includes("does not edit Steam"));

        const current = createTemporaryObject(followPickerComponent, testCase, {
            catalog: catalog([a], []),
            followSources: [sources()[4]],
            selection: {mode: "identifiedCurrent", accountKey: a.key, sourceKey: "current:Alice"}
        });
        verify(current);
        verify(!current.choices.length); // G2 stays closed unless parent explicitly enables it.
        current.allowIdentifiedCurrent = true;
        compare(current.choices.length, 1);
        compare(current.showComboBox, false); // one selected source is a summary
        verify(findChild(current, "followSourceSummary").visible);
        verify(findChild(current, "followSourceSummary").text.includes("Automatic"));

        picker.selection = {mode: "account", accountKey: b.key, sourceKey: "config:gone"};
        compare(picker.currentLabel, "Unavailable follow source — config:gone");
        verify(picker.currentStatus.includes("no other account is substituted"));
        compare(picker.showComboBox, true); // other eligible sources remain reachable
    }

    function test_manualAssociationRequiresVerifiedAccountAndExplicitConfirmation() {
        const a = verified("steam:76561190000000001", "76561190000000001", "Alice");
        const local = unresolved("userdata:root:12", "Unverified local user");
        const sectionKey = "config:/tmp/loginusers.vdf#section:steamuser";
        const picker = createTemporaryObject(followPickerComponent, testCase, {
            catalog: catalog([a, local], []),
            followSources: [
                {mode: "legacySession", accountKey: "", sourceKey: sectionKey,
                    label: "Unassociated Wallpaper Engine section steamuser", available: true},
                {mode: "manualMapping", accountKey: local.key, sourceKey: sectionKey,
                    label: "Unverified association", userConfirmed: true, available: true}
            ],
            selection: {mode: "legacySession", accountKey: "", sourceKey: sectionKey}
        });
        verify(picker);
        compare(picker.choices.length, 1); // unverified manual candidate is rejected
        compare(picker.associationAccounts.length, 1);
        compare(findChild(picker, "manualMappingAccountCombo").Accessible.name,
            "Verified Steam account to associate");
        verify(findChild(picker, "manualMappingUnverifiedNotice").visible);
        verify(findChild(picker, "manualMappingUnverifiedNotice").text.includes("not eligible"));
        verify(findChild(picker, "manualMappingExplanation").text.includes("confirmation"));

        const selected = spy(picker, "followSourceSelected");
        const accountCombo = findChild(picker, "manualMappingAccountCombo");
        const sectionCombo = findChild(picker, "manualMappingSectionCombo");
        accountCombo.activated(0);
        sectionCombo.activated(0);
        compare(picker.canConfirmAssociation, true);
        compare(selected.count, 0); // choosing values alone does not map anything
        const confirmButton = findChild(picker, "manualMappingConfirmButton");
        verify(confirmButton.enabled);
        mouseClick(confirmButton);
        const dialog = findChild(picker, "manualMappingConfirmationDialog");
        verify(dialog && dialog.visible);
        verify(dialog.contentItem.text.includes("Associate Alice"));
        dialog.reject();
        compare(selected.count, 0);
        verify(!picker.choices.some(choice => choice.binding.mode === "manualMapping"));

        mouseClick(confirmButton);
        verify(dialog.visible);
        dialog.accept(); // the explicit confirmation action
        tryCompare(selected, "count", 1);
        const binding = selected.signalArguments[0][0];
        compare(binding.mode, "manualMapping");
        compare(binding.accountKey, a.key);
        compare(binding.sourceKey, sectionKey);
        verify(picker.choices.some(choice => choice.binding.mode === "manualMapping"
            && choice.label.startsWith("User-associated —")));
    }
}
