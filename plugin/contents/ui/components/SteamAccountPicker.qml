import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: picker
    Layout.fillWidth: true
    spacing: 6

    property var catalog: null
    property string mode: "allDownloaded"
    property var accountKeys: []
    property bool includeSharedProjects: true
    // Integrators set this only for a genuinely fresh setup, never for migrated
    // settings whose All downloaded mode is intentional.
    property bool freshSetup: false
    property bool _soleDefaultSent: false

    signal browseScopeSelected(string mode, var accountKeys, bool includeSharedProjects)

    function normalizedCatalog() {
        if (!catalog) return null;
        try { return JSON.parse(JSON.stringify(catalog)); }
        catch (reason) { return null; }
    }
    readonly property var catalogData: normalizedCatalog()
    function normalizedKeys() {
        if (!accountKeys) return [];
        try { return JSON.parse(JSON.stringify(accountKeys)); }
        catch (reason) { return []; }
    }
    readonly property var accountKeysData: normalizedKeys()

    readonly property var accountChoices: {
        const data = catalogData;
        const source = data && Array.isArray(data.accounts) ? data.accounts : [];
        const selected = Array.isArray(accountKeysData)
            ? accountKeysData.filter(key => typeof key === "string") : [];
        const seen = new Set();
        const result = [];
        source.forEach(account => {
            if (!account || typeof account.key !== "string" || !account.key || seen.has(account.key))
                return;
            seen.add(account.key);
            result.push(account);
        });
        selected.forEach(key => {
            if (seen.has(key)) return;
            seen.add(key);
            result.push({key, label: "Unavailable account", identityStatus: "unresolved"});
        });
        const labelCounts = Object.create(null);
        result.forEach(account => {
            const label = typeof account.label === "string" && account.label.trim()
                ? account.label.trim() : "Steam account";
            labelCounts[label] = (labelCounts[label] || 0) + 1;
        });
        return result.map(account => {
            const label = typeof account.label === "string" && account.label.trim()
                ? account.label.trim() : "Steam account";
            const suffix = typeof account.steamId64 === "string" && account.steamId64.length >= 4
                ? account.steamId64.slice(-4)
                : typeof account.accountId32 === "string" && account.accountId32
                    ? account.accountId32 : account.key.slice(-6);
            const caches = data && Array.isArray(data.caches)
                ? data.caches.filter(cache => cache && cache.accountKey === account.key) : [];
            const unhealthy = caches.filter(cache =>
                ["missing", "invalid", "unavailable"].includes(cache.state));
            const hasLastGood = unhealthy.some(cache =>
                Array.isArray(cache.subscribedIds) && cache.subscribedIds.length > 0);
            let health = "Subscription data unavailable";
            if (!caches.length) health = "No subscription cache found";
            else if (unhealthy.length && hasLastGood) health = "Using last-known subscriptions; cache is unavailable";
            else if (unhealthy.length) health = "Subscription data unavailable";
            else if (caches.every(cache => cache.state === "empty")) health = "No subscriptions in cache";
            else health = "Subscriptions available";
            return {
                key: account.key,
                displayLabel: labelCounts[label] > 1 ? `${label} (…${suffix})` : label,
                health,
                known: source.some(candidate => candidate && candidate.key === account.key)
            };
        });
    }

    readonly property bool selectedMode: mode === "selectedAccounts"
    readonly property bool showEmptySelection: selectedMode && accountKeysData.length === 0

    function emitScope(nextMode, nextKeys, nextShared) {
        const keys = Array.isArray(nextKeys) ? JSON.parse(JSON.stringify(nextKeys)) : [];
        browseScopeSelected(nextMode, keys, nextShared);
    }
    function chooseMode(nextMode) {
        if (nextMode === "allDownloaded" || nextMode === "selectedAccounts")
            emitScope(nextMode, accountKeysData, includeSharedProjects);
    }
    function updateAccountKey(key, checked) {
        const keys = accountKeysData.slice();
        const index = keys.indexOf(key);
        if (checked && index < 0) keys.push(key);
        else if (!checked && index >= 0) keys.splice(index, 1);
        emitScope("selectedAccounts", keys, includeSharedProjects);
    }

    Timer {
        id: soleDefaultTimer
        interval: 0
        repeat: false
        onTriggered: {
            const catalogAccounts = picker.catalogData && Array.isArray(picker.catalogData.accounts)
                ? picker.catalogData.accounts : [];
            if (picker.freshSetup && !picker._soleDefaultSent && picker.mode === "allDownloaded"
                && picker.accountKeysData.length === 0
                && catalogAccounts.length === 1 && catalogAccounts[0].key) {
                picker._soleDefaultSent = true;
                picker.emitScope("selectedAccounts", [catalogAccounts[0].key], picker.includeSharedProjects);
            }
        }
    }
    Component.onCompleted: soleDefaultTimer.start()
    onCatalogChanged: soleDefaultTimer.restart()
    onFreshSetupChanged: soleDefaultTimer.restart()

    Label {
        text: "Show wallpapers from"
        font.bold: true
        Layout.fillWidth: true
    }
    RadioButton {
        objectName: "allDownloadedOption"
        text: "All downloaded wallpapers"
        checked: picker.mode === "allDownloaded"
        Accessible.name: text
        onToggled: if (checked) picker.chooseMode("allDownloaded")
    }
    RadioButton {
        objectName: "selectedAccountsOption"
        text: "Selected account subscriptions"
        checked: picker.mode === "selectedAccounts"
        Accessible.name: text
        onToggled: if (checked) picker.chooseMode("selectedAccounts")
    }
    ColumnLayout {
        objectName: "accountChoices"
        visible: picker.selectedMode
        Layout.fillWidth: true
        Layout.leftMargin: 24
        spacing: 2

        Repeater {
            model: picker.accountChoices
            delegate: ColumnLayout {
                id: accountRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 0

                CheckBox {
                    objectName: `account-${accountRow.modelData.key}`
                    Layout.fillWidth: true
                    text: accountRow.modelData.displayLabel
                    checked: picker.accountKeysData.includes(accountRow.modelData.key)
                    Accessible.name: text
                    Accessible.description: accountRow.modelData.health
                    onToggled: picker.updateAccountKey(accountRow.modelData.key, checked)
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 48
                    visible: picker.selectedMode
                    text: accountRow.modelData.health
                    opacity: 0.75
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
    Label {
        objectName: "emptyAccountSelection"
        visible: picker.showEmptySelection
        Layout.fillWidth: true
        Layout.leftMargin: 24
        text: "No accounts selected. No subscribed wallpapers will be shown."
        wrapMode: Text.WordWrap
    }
    Label {
        objectName: "noAccountsAvailable"
        visible: picker.selectedMode && picker.accountChoices.length === 0
        Layout.fillWidth: true
        Layout.leftMargin: 24
        text: "No Steam accounts are available. The selected account scope will remain empty."
        wrapMode: Text.WordWrap
    }
    CheckBox {
        objectName: "includeSharedProjects"
        text: "Include local and shared projects"
        checked: picker.includeSharedProjects
        Accessible.name: text
        onToggled: picker.emitScope(picker.mode, picker.accountKeysData, checked)
    }
}
