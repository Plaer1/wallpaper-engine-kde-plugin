import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: picker
    Layout.fillWidth: true
    spacing: 6

    property var catalog: null
    // Each candidate is a follow binding plus presentation/availability data:
    // {mode, accountKey, sourceKey, label, status, available, userConfirmed}.
    property var followSources: []
    property var selection: ({mode: "", accountKey: "", sourceKey: ""})
    // Only mappings confirmed in this picker are appended locally. Persistence
    // remains the owner's job through followSourceSelected.
    property var confirmedMappings: []
    property string mappingAccountKey: ""
    property string mappingSourceKey: ""
    // G2 remains a parent gate. Automatic identity is hidden until explicitly enabled.
    property bool allowIdentifiedCurrent: false

    signal followSourceSelected(var binding)

    function normalizedCatalog() {
        if (!catalog) return null;
        try { return JSON.parse(JSON.stringify(catalog)); }
        catch (reason) { return null; }
    }
    readonly property var catalogData: normalizedCatalog()
    function normalizedSources() {
        if (!followSources) return [];
        try { return JSON.parse(JSON.stringify(followSources)); }
        catch (reason) { return []; }
    }
    readonly property var sourceData: normalizedSources()

    function verifiedAccount(accountKey) {
        if (!catalogData || !Array.isArray(catalogData.accounts) || typeof accountKey !== "string")
            return null;
        return catalogData.accounts.find(account => account && account.key === accountKey
            && account.identityStatus === "verified"
            && typeof account.steamId64 === "string"
            && /^\d+$/.test(account.steamId64)
            && account.key === `steam:${account.steamId64}`) || null;
    }
    function sameBinding(a, b) {
        return Boolean(a && b && a.mode === b.mode
            && (a.accountKey || "") === (b.accountKey || "")
            && (a.sourceKey || "") === (b.sourceKey || ""));
    }
    function verifiedAccounts() {
        if (!catalogData || !Array.isArray(catalogData.accounts)) return [];
        return catalogData.accounts.filter(account => account && verifiedAccount(account.key))
            .map(account => ({key: account.key, label: typeof account.label === "string"
                && account.label.trim() ? account.label.trim() : account.key,
                accountId32: typeof account.accountId32 === "string" ? account.accountId32 : "",
                steamId64: account.steamId64}));
    }
    function unassociatedSections() {
        return sourceData.filter(source => source && source.mode === "legacySession"
            && !source.accountKey && typeof source.sourceKey === "string" && source.sourceKey
            && source.available !== false && source.associable !== false).map(source => ({sourceKey: source.sourceKey,
                label: typeof source.label === "string" && source.label ? source.label : source.sourceKey}));
    }
    readonly property var associationAccounts: verifiedAccounts()
    readonly property var associationSections: unassociatedSections()
    readonly property bool hasUnverifiedAccounts: Boolean(catalogData
        && Array.isArray(catalogData.accounts)
        && catalogData.accounts.some(account => account && !verifiedAccount(account.key)))
    readonly property bool canConfirmAssociation: Boolean(verifiedAccount(mappingAccountKey)
        && associationSections.some(section => section.sourceKey === mappingSourceKey))

    function confirmAssociation() {
        if (!canConfirmAssociation) return false;
        const account = verifiedAccount(mappingAccountKey);
        const section = associationSections.find(candidate => candidate.sourceKey === mappingSourceKey);
        if (!account || !section) return false;
        const mapping = {
            mode: "manualMapping",
            accountKey: account.key,
            sourceKey: section.sourceKey,
            label: `User-associated — ${account.label || account.key} — ${section.label}`,
            status: "Explicitly associated by the user.",
            available: true,
            userConfirmed: true
        };
        const next = Array.isArray(confirmedMappings) ? confirmedMappings.slice() : [];
        if (!next.some(item => item && item.mode === mapping.mode
                && item.accountKey === mapping.accountKey && item.sourceKey === mapping.sourceKey))
            next.push(mapping);
        confirmedMappings = next;
        followSourceSelected({mode: mapping.mode, accountKey: mapping.accountKey,
            sourceKey: mapping.sourceKey});
        return true;
    }
    function safeOptions() {
        const inputs = (Array.isArray(sourceData) ? sourceData : [])
            .concat(Array.isArray(confirmedMappings) ? confirmedMappings : []);
        return inputs.filter(source => {
            if (!source || !["identifiedCurrent", "account", "legacySession", "manualMapping"].includes(source.mode)
                || typeof source.sourceKey !== "string" || !source.sourceKey)
                return false;
            if (source.mode === "identifiedCurrent")
                return allowIdentifiedCurrent && Boolean(verifiedAccount(source.accountKey))
                    && source.available !== false;
            if (source.mode === "account")
                return Boolean(verifiedAccount(source.accountKey));
            if (source.mode === "legacySession")
                return !source.accountKey;
            if (source.mode === "manualMapping")
                return source.userConfirmed === true && Boolean(verifiedAccount(source.accountKey));
            return false;
        }).map(source => {
            const account = source.accountKey ? verifiedAccount(source.accountKey)
                || (catalogData && Array.isArray(catalogData.accounts)
                    ? catalogData.accounts.find(item => item && item.key === source.accountKey) : null) : null;
            let label = typeof source.label === "string" && source.label.trim()
                ? source.label.trim() : source.sourceKey;
            if (source.mode === "identifiedCurrent") label = `Automatic — ${label}`;
            else if (source.mode === "legacySession") label = `Unassociated — ${label}`;
            else if (source.mode === "manualMapping") label = `User-associated — ${label}`;
            else if (account && account.label) label = `${account.label} — ${label}`;
            return {
                binding: {mode: source.mode, accountKey: source.accountKey || "", sourceKey: source.sourceKey},
                label,
                status: source.status || "",
                available: source.available !== false,
                unavailableReason: source.unavailableReason || "This follow source is unavailable."
            };
        });
    }

    readonly property var choices: safeOptions()
    readonly property var currentChoice: choices.find(choice => sameBinding(choice.binding, selection)) || null
    readonly property var selectableChoices: choices.filter(choice => choice.available)
    readonly property string currentLabel: currentChoice ? currentChoice.label
        : selection && selection.sourceKey ? `Unavailable follow source — ${selection.sourceKey}`
        : "No follow source selected"
    readonly property string currentStatus: currentChoice
        ? (currentChoice.available ? currentChoice.status : currentChoice.unavailableReason)
        : selection && selection.sourceKey
            ? "The saved follow source is unavailable; no other account is substituted."
            : "No follow source is available."
    readonly property bool showComboBox: selectableChoices.length > 1
        || (selectableChoices.length === 1 && !sameBinding(selectableChoices[0].binding, selection))

    function selectChoice(index) {
        if (index < 0 || index >= choices.length || !choices[index].available)
            return;
        const selected = choices[index].binding;
        followSourceSelected({mode: selected.mode, accountKey: selected.accountKey, sourceKey: selected.sourceKey});
    }

    Label {
        text: "Follow Wallpaper Engine source"
        font.bold: true
        Layout.fillWidth: true
    }
    ComboBox {
        id: sourceCombo
        objectName: "followSourceCombo"
        visible: picker.showComboBox
        Layout.fillWidth: true
        Accessible.name: "Follow Wallpaper Engine source"
        model: picker.choices
        textRole: "label"
        currentIndex: picker.choices.findIndex(choice => picker.sameBinding(choice.binding, picker.selection))
        delegate: ItemDelegate {
            required property var modelData
            width: sourceCombo.width
            text: modelData.label + (modelData.available ? "" : " — unavailable")
            enabled: modelData.available
            Accessible.name: text
        }
        onActivated: index => picker.selectChoice(index)
        ToolTip.visible: hovered
        ToolTip.text: picker.currentStatus
    }
    Label {
        objectName: "followSourceSummary"
        visible: !picker.showComboBox
        Layout.fillWidth: true
        text: picker.currentLabel
        wrapMode: Text.WordWrap
    }
    Label {
        objectName: "followSourceStatus"
        Layout.fillWidth: true
        text: picker.currentStatus
        wrapMode: Text.WordWrap
        opacity: 0.75
    }

    Label {
        objectName: "manualMappingExplanation"
        Layout.fillWidth: true
        text: "Associate a verified Steam account with an unassociated Wallpaper Engine section. This only changes the Follow mapping after confirmation; it does not edit Steam."
        wrapMode: Text.WordWrap
    }
    Label {
        objectName: "manualMappingUnavailableStatus"
        visible: associationAccounts.length === 0
        Layout.fillWidth: true
        text: catalogData && Array.isArray(catalogData.accounts) && catalogData.accounts.length
            ? "No verified Steam account is available to associate."
            : "Load the Steam account catalog before creating an association."
        wrapMode: Text.WordWrap
        opacity: 0.75
    }
    Label {
        objectName: "manualMappingUnverifiedNotice"
        visible: picker.hasUnverifiedAccounts
        Layout.fillWidth: true
        text: "Unverified or local entries are not eligible for account association."
        wrapMode: Text.WordWrap
        opacity: 0.75
    }
    ComboBox {
        id: mappingAccountCombo
        objectName: "manualMappingAccountCombo"
        Layout.fillWidth: true
        enabled: picker.associationAccounts.length > 0
        Accessible.name: "Verified Steam account to associate"
        model: picker.associationAccounts
        textRole: "label"
        currentIndex: model.findIndex(account => account.key === picker.mappingAccountKey)
        displayText: currentIndex < 0 ? "Choose a verified Steam account" : currentText
        delegate: ItemDelegate {
            required property var modelData
            width: mappingAccountCombo.width
            text: modelData.label + (modelData.accountId32 ? ` (${modelData.accountId32})` : "")
            Accessible.name: text
        }
        onActivated: index => picker.mappingAccountKey = model[index].key
    }
    ComboBox {
        id: mappingSectionCombo
        objectName: "manualMappingSectionCombo"
        Layout.fillWidth: true
        enabled: picker.associationSections.length > 0
        Accessible.name: "Unassociated Wallpaper Engine section"
        model: picker.associationSections
        textRole: "label"
        currentIndex: model.findIndex(section => section.sourceKey === picker.mappingSourceKey)
        displayText: currentIndex < 0 ? "Choose an unassociated section" : currentText
        delegate: ItemDelegate {
            required property var modelData
            width: mappingSectionCombo.width
            text: modelData.label
            Accessible.name: text
        }
        onActivated: index => picker.mappingSourceKey = model[index].sourceKey
    }
    Button {
        objectName: "manualMappingConfirmButton"
        Layout.fillWidth: true
        text: "Associate account with section…"
        enabled: picker.canConfirmAssociation
        Accessible.name: text
        onClicked: associationDialog.open()
    }
    Dialog {
        id: associationDialog
        objectName: "manualMappingConfirmationDialog"
        title: "Confirm Follow account association"
        modal: true
        standardButtons: DialogButtonBox.Cancel | DialogButtonBox.Ok
        parent: Overlay.overlay
        width: Math.min(picker.Window.width - 48, 480)
        contentItem: Label {
            text: {
                const account = picker.associationAccounts.find(item => item.key === picker.mappingAccountKey);
                const section = picker.associationSections.find(item => item.sourceKey === picker.mappingSourceKey);
                return account && section
                    ? `Associate ${account.label} with ${section.label}? This uses that section as this account’s Wallpaper Engine source. It does not change Steam or the section contents.`
                    : "Choose a verified account and an unassociated section first.";
            }
            wrapMode: Text.WordWrap
            padding: 16
            Accessible.name: text
        }
        onAccepted: picker.confirmAssociation()
    }
}
