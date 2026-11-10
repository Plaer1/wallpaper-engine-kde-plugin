import QtQuick 2.6
import QtQuick.Controls 2.3
import QtQuick.Controls 2.3 as QQC
import QtQuick.Controls 2.15 as QQC15
import QtQuick.Window 2.0 // for Screen
import QtQuick.Dialogs
import QtQuick.Layouts 1.5

import ".."
import "../components"

import "../js/utils.mjs" as Utils

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents
// for kcm gridview
import org.kde.kcmutils as KCM
import org.kde.kirigami 2.6 as Kirigami
import org.kde.kquickcontrolsaddons 2.0

QQC15.SplitView {
    id: page
    Layout.fillWidth: true
    orientation: Qt.Horizontal

    // On narrow config dialogs, Details becomes a second reachable view instead
    // of disappearing with the width-constrained split inspector.
    property int detailsBreakpoint: 1040
    property bool detailsView: false
    property bool settingsExpanded: false
    property bool filtersView: false
    property bool filterSidebarExpanded: true
    property bool accountScopeExpanded: false
    property bool inspectorExpanded: true
    property int inspectorPreferredWidth: 280
    property string activationStatus: ""
    property var immediateApplyHandler: null
    readonly property real splitInspectorMinimumWidth: right_content.image_size
        + 2 * right_content.content_margin + right_content_scrollbar.width
    readonly property bool narrowLayout: width < Math.max(detailsBreakpoint,
        3 * splitInspectorMinimumWidth)

    // Injectable snapshots/bindings keep this page testable and leave config
    // persistence to config.qml, which owns the Plasma cfg_ exports.
    property var accountCatalogSnapshot: null
    // The host may inject its model explicitly; the legacy context name stays
    // as a compatibility fallback for existing config.qml callers.
    property var wallpaperListModel: null
    property var browseScope: ({mode: "allDownloaded", accountKeys: [], includeSharedProjects: true})
    property var followSources: []
    property var followSelection: ({mode: "legacySession", accountKey: "", sourceKey: "config:steamuser"})
    property var browserFilters: ({query: "", includedTags: [], excludedTags: [], sourceKind: ""})
    property var browserTagCatalog: []
    // config.qml supplies the editor draft so this page never reads or edits
    // the reconciled cfg_ export directly for Follow fields.
    property var followEnabled: undefined
    property var followAccount: undefined
    property bool useFollowSourcePicker: false
    property bool freshAccountSetup: false
    // G2 remains closed until the integrator has live verified-string evidence.
    property bool allowIdentifiedCurrent: false
    property var _activeBrowseScope: normalizeBrowseScope(browseScope)
    property var _activeFollowSelection: normalizeFollowSelection(followSelection)
    property var _activeBrowserFilters: normalizeBrowserFilters(browserFilters)
    property var pendingManualSelection: null

    signal browseScopeSelected(var scope)
    signal followSourceSelected(var binding)
    signal followEnabledSelected(bool enabled)
    signal followAccountSelected(string account)
    signal manualWallpaperSelected(var wallpaper)
    signal browserFiltersSelected(var filters)

    readonly property bool accountCatalogAvailable: accountCatalogSnapshot
        && accountCatalogSnapshot.schemaVersion === 1
    readonly property bool editorFollowEnabled: followEnabled === undefined
        ? cfg_FollowWallpaperEngine : Boolean(followEnabled)
    readonly property string editorFollowAccount: followAccount === undefined
        ? cfg_WallpaperEngineAccount : String(followAccount)
    readonly property var legacyFilterValues: Common.filterModel.getValueArray(cfg_FilterStr)
    readonly property real filterPaneWidth: 248

    function librarySummary() {
        const path = String(steamDetect.selectedLibrary || cfg_SteamLibraryPath || "");
        if (!path) return qsTr("No Steam library");
        const parts = path.split(/[\\/]/).filter(part => part.length > 0);
        return parts.length ? parts[parts.length - 1] : path;
    }
    function browseScopeSummary() {
        if (_activeBrowseScope.mode !== "selectedAccounts")
            return qsTr("All downloaded");
        const count = _activeBrowseScope.accountKeys.length;
        return count === 1 ? qsTr("1 account") : qsTr("%1 accounts").arg(count);
    }

    function listModelAdapter() {
        return wallpaperListModel || wpListModel;
    }

    function normalizeBrowseScope(scope) {
        const source = scope && typeof scope === "object" ? scope : {};
        const sourceKeys = source.accountKeys;
        const accountKeys = [];
        if (sourceKeys && typeof sourceKeys !== "string"
            && typeof sourceKeys.length === "number") {
            for (let i = 0; i < sourceKeys.length; ++i) {
                if (typeof sourceKeys[i] === "string") accountKeys.push(sourceKeys[i]);
            }
        }
        return {
            mode: source.mode === "selectedAccounts" ? "selectedAccounts" : "allDownloaded",
            accountKeys,
            includeSharedProjects: source.includeSharedProjects !== false
        };
    }
    function normalizeFollowSelection(binding) {
        const source = binding && typeof binding === "object" ? binding : {};
        return {
            mode: typeof source.mode === "string" ? source.mode : "",
            accountKey: typeof source.accountKey === "string" ? source.accountKey : "",
            sourceKey: typeof source.sourceKey === "string" ? source.sourceKey : ""
        };
    }
    function normalizeBrowserFilters(filters) {
        const source = filters && typeof filters === "object" ? filters : {};
        const normalizeTags = value => {
            if (!Array.isArray(value)) return [];
            return Array.from(new Set(value.filter(tag => typeof tag === "string" && tag.trim())
                .map(tag => tag.trim()))).sort((a, b) => a.localeCompare(b));
        };
        const sourceKind = ["workshop", "shared"].includes(source.sourceKind)
            ? source.sourceKind : "";
        return {
            query: typeof source.query === "string" ? source.query : "",
            includedTags: normalizeTags(source.includedTags),
            excludedTags: normalizeTags(source.excludedTags),
            sourceKind
        };
    }
    function acceptBrowserFilters(filters) {
        const next = normalizeBrowserFilters(filters);
        _activeBrowserFilters = next;
        listModelAdapter().browserFilters = next;
        browserFiltersSelected(next);
    }
    function updateBrowserFilters(key, value) {
        const next = Object.assign({}, _activeBrowserFilters);
        next[key] = value;
        acceptBrowserFilters(next);
    }
    function updateBrowserTagList(key, tag, include) {
        const tags = _activeBrowserFilters[key].slice();
        const index = tags.indexOf(tag);
        if (include && index < 0) tags.push(tag);
        else if (!include && index >= 0) tags.splice(index, 1);
        updateBrowserFilters(key, tags);
    }
    function setLegacyFilter(index, checked) {
        const values = legacyFilterValues.slice();
        const item = Common.filterModel.get(index);
        values[index] = item.type === "tags" ? (checked ? 0 : 1) : Number(checked);
        cfg_FilterStr = Utils.intArrayToStr(values);
    }
    function resetFilters() {
        const defaults = Common.filterModel.map(item => item.def);
        cfg_FilterStr = Utils.intArrayToStr(defaults);
        acceptBrowserFilters({query: "", includedTags: [], excludedTags: [], sourceKind: ""});
        acceptBrowseScope("allDownloaded", [], true);
        activationStatus = "";
    }
    function acceptBrowseScope(mode, accountKeys, includeSharedProjects) {
        const next = normalizeBrowseScope({mode, accountKeys, includeSharedProjects});
        _activeBrowseScope = next;
        listModelAdapter().browseScope = next;
        browseScopeSelected(next);
    }
    function acceptFollowSource(binding) {
        const next = normalizeFollowSelection(binding);
        _activeFollowSelection = next;
        followSourceSelected(next);
    }
    function selectWallpaper(model) {
        // Keep file/path/type fields from the grid delegate; the inspector's
        // wpitemFromQtObject projection intentionally omits the project file.
        const selected = Object.assign({}, model);
        if (page.editorFollowEnabled) {
            pendingManualSelection = selected;
            return;
        }
        cfg_WallpaperSource = Common.packWallpaperSource(selected);
        cfg_WallpaperWorkShopId = selected.workshopid;
        pendingManualSelection = null;
    }
    function applyPendingManualSelection() {
        if (!page.editorFollowEnabled || !pendingManualSelection)
            return;
        const selected = pendingManualSelection;
        cfg_WallpaperSource = Common.packWallpaperSource(selected);
        cfg_WallpaperWorkShopId = selected.workshopid;
        if (page.followEnabled === undefined)
            cfg_FollowWallpaperEngine = false;
        else
            page.followEnabledSelected(false);
        pendingManualSelection = null;
        manualWallpaperSelected(selected);
    }

    function activateWallpaper(model) {
        const selected = Object.assign({}, model || {});
        if (typeof immediateApplyHandler !== "function") {
            activationStatus = qsTr("Immediate Apply is unavailable here. Use Apply to save this selection.");
            return false;
        }
        const result = immediateApplyHandler(selected);
        const succeeded = result === true || (result && result.applied === true);
        if (succeeded) {
            pendingManualSelection = null;
            activationStatus = qsTr("Wallpaper applied. Other edits are still waiting for Apply.");
            return true;
        }
        activationStatus = result && typeof result.message === "string" && result.message
            ? result.message : qsTr("Immediate Apply failed. Use Apply to save this selection.");
        return false;
    }

    function listEntryAt(list, index) {
        const count = list && typeof list.length === "number" ? list.length
            : list && typeof list.count === "number" ? list.count : 0;
        if (index < 0 || index >= count) return null;
        return typeof list.get === "function" ? list.get(index) : list[index];
    }

    function listCount(list) {
        if (!list) return 0;
        if (typeof list.length === "number") return list.length;
        return typeof list.count === "number" ? list.count : 0;
    }

    onBrowseScopeChanged: {
        _activeBrowseScope = normalizeBrowseScope(browseScope);
        listModelAdapter().browseScope = _activeBrowseScope;
    }
    onBrowserFiltersChanged: {
        _activeBrowserFilters = normalizeBrowserFilters(browserFilters);
        listModelAdapter().browserFilters = _activeBrowserFilters;
    }
    onFollowSelectionChanged: _activeFollowSelection = normalizeFollowSelection(followSelection)
    Component.onCompleted: {
        listModelAdapter().browseScope = _activeBrowseScope;
        listModelAdapter().browserFilters = _activeBrowserFilters;
    }

    // Наследуем тему от родителя
    Kirigami.Theme.inherit: true

    function saveConfig() {
        right_opts.save_changes();
    }

    Timer {
        id: browserQueryDebounce
        interval: 180
        repeat: false
        onTriggered: page.updateBrowserFilters("query", browserQueryField.text)
    }

    Control {
        id: browserFilterSidebar
        objectName: "browserFilterSidebar"
        QQC15.SplitView.preferredWidth: page.narrowLayout ? page.width : page.filterPaneWidth
        QQC15.SplitView.minimumWidth: page.narrowLayout ? 0 : 200
        QQC15.SplitView.maximumWidth: page.narrowLayout ? page.width : page.filterPaneWidth
        QQC15.SplitView.fillWidth: page.narrowLayout
        Layout.fillHeight: true
        visible: page.narrowLayout ? page.filtersView : page.filterSidebarExpanded
        topPadding: 6
        bottomPadding: 6
        leftPadding: 8
        rightPadding: 8

        contentItem: ScrollView {
            id: browserFilterScroll
            clip: true
            contentWidth: availableWidth
            QQC15.ScrollBar.vertical.policy: QQC15.ScrollBar.AsNeeded

            ColumnLayout {
                width: browserFilterScroll.availableWidth
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Filters")
                        font.bold: true
                    }
                    Button {
                        objectName: "resetBrowserFiltersButton"
                        text: qsTr("Reset")
                        onClicked: page.resetFilters()
                    }
                }
                Button {
                    objectName: "backFromFiltersButton"
                    visible: page.narrowLayout
                    Layout.fillWidth: true
                    text: qsTr("Back to wallpapers")
                    icon.name: "go-previous"
                    onClicked: page.filtersView = false
                }

                Label { text: qsTr("Search"); font.bold: true }
                TextField {
                    id: browserQueryField
                    objectName: "browserQueryField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Name, tags, Workshop ID")
                    text: page._activeBrowserFilters.query
                    onTextEdited: browserQueryDebounce.restart()
                    onAccepted: {
                        browserQueryDebounce.stop();
                        page.updateBrowserFilters("query", text);
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Search wallpaper names, tags, and Workshop IDs")
                }

                Label { text: qsTr("Source"); font.bold: true }
                ComboBox {
                    objectName: "browserSourceKindCombo"
                    Layout.fillWidth: true
                    model: [
                        {text: qsTr("All sources"), value: ""},
                        {text: qsTr("Workshop"), value: "workshop"},
                        {text: qsTr("Local and shared"), value: "shared"}
                    ]
                    textRole: "text"
                    currentIndex: Math.max(0, model.findIndex(item =>
                        item.value === page._activeBrowserFilters.sourceKind))
                    onActivated: index => page.updateBrowserFilters("sourceKind", model[index].value)
                }

                Label { text: qsTr("Tags"); font.bold: true }
                ComboBox {
                    id: browserTagChoice
                    objectName: "browserTagChoice"
                    Layout.fillWidth: true
                    model: page.browserTagCatalog
                    displayText: currentIndex < 0 ? qsTr("Choose a tag") : currentText
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        objectName: "includeBrowserTagButton"
                        Layout.fillWidth: true
                        text: qsTr("Require")
                        enabled: browserTagChoice.currentIndex >= 0
                        onClicked: page.updateBrowserTagList("includedTags",
                            browserTagChoice.currentText, true)
                    }
                    Button {
                        objectName: "excludeBrowserTagButton"
                        Layout.fillWidth: true
                        text: qsTr("Exclude")
                        enabled: browserTagChoice.currentIndex >= 0
                        onClicked: page.updateBrowserTagList("excludedTags",
                            browserTagChoice.currentText, true)
                    }
                }
                Label {
                    visible: page._activeBrowserFilters.includedTags.length > 0
                    text: qsTr("Must include all")
                    opacity: 0.75
                }
                Flow {
                    Layout.fillWidth: true
                    visible: page._activeBrowserFilters.includedTags.length > 0
                    Repeater {
                        model: page._activeBrowserFilters.includedTags
                        delegate: ToolButton {
                            required property string modelData
                            text: `+ ${modelData} ×`
                            onClicked: page.updateBrowserTagList("includedTags", modelData, false)
                        }
                    }
                }
                Label {
                    visible: page._activeBrowserFilters.excludedTags.length > 0
                    text: qsTr("Exclude any")
                    opacity: 0.75
                }
                Flow {
                    Layout.fillWidth: true
                    visible: page._activeBrowserFilters.excludedTags.length > 0
                    Repeater {
                        model: page._activeBrowserFilters.excludedTags
                        delegate: ToolButton {
                            required property string modelData
                            text: `− ${modelData} ×`
                            onClicked: page.updateBrowserTagList("excludedTags", modelData, false)
                        }
                    }
                }

                Label {
                    text: qsTr("Type, rating, favorites, playlists")
                    font.bold: true
                    Layout.topMargin: 4
                }
                Repeater {
                    model: Common.filterModel
                    delegate: ColumnLayout {
                        required property int index
                        readonly property var entry: Common.filterModel.get(index)
                        Layout.fillWidth: true
                        Label {
                            visible: entry.type === "_nocheck"
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            text: entry.text
                            font.bold: true
                        }
                        CheckBox {
                            objectName: `legacyBrowserFilter-${index}`
                            visible: entry.type !== "_nocheck"
                            Layout.fillWidth: true
                            text: entry.type === "tags" ? qsTr("Exclude %1").arg(entry.key)
                                : entry.type === "favor" ? qsTr("Favorites only") : entry.text
                            checked: entry.type === "tags"
                                ? Number(page.legacyFilterValues[index]) === 0
                                : Number(page.legacyFilterValues[index]) !== 0
                            onToggled: page.setLegacyFilter(index, checked)
                        }
                    }
                }

                ToolButton {
                    objectName: "accountScopeDisclosureButton"
                    Layout.fillWidth: true
                    text: page.accountScopeExpanded ? qsTr("Hide account scope") : qsTr("Account scope")
                    icon.name: page.accountScopeExpanded ? "arrow-up" : "arrow-down"
                    onClicked: page.accountScopeExpanded = !page.accountScopeExpanded
                }
                SteamAccountPicker {
                    objectName: "browseAccountPicker"
                    visible: page.accountScopeExpanded && page.accountCatalogAvailable
                    catalog: page.accountCatalogSnapshot
                    mode: page._activeBrowseScope.mode
                    accountKeys: page._activeBrowseScope.accountKeys
                    includeSharedProjects: page._activeBrowseScope.includeSharedProjects
                    freshSetup: page.freshAccountSetup
                    onBrowseScopeSelected: (mode, accountKeys, includeSharedProjects) =>
                        page.acceptBrowseScope(mode, accountKeys, includeSharedProjects)
                }
            }
        }
    }

    Control {
        id: left_content
        objectName: "wallpaperBrowseView"
        QQC15.SplitView.minimumWidth: 320
        QQC15.SplitView.fillWidth: true
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: !page.narrowLayout || (!page.detailsView && !page.filtersView)
        topPadding: 8
        leftPadding: 0
        rightPadding: 0
        bottomPadding: 0

        contentItem: ColumnLayout {
            id: wpselsect
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 4

            RowLayout {
                objectName: "wallpaperBrowserHeader"
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight

                Label {
                    objectName: "libraryScopeSummary"
                    Layout.fillWidth: true
                    text: qsTr("Library: %1 · %2").arg(page.librarySummary()).arg(page.browseScopeSummary())
                    elide: Text.ElideMiddle
                }
                ToolButton {
                    objectName: "browserSettingsButton"
                    text: page.settingsExpanded ? qsTr("Done") : qsTr("Settings")
                    icon.name: page.settingsExpanded ? "dialog-ok" : "settings-configure"
                    Accessible.name: page.settingsExpanded ? qsTr("Close browser settings") : qsTr("Browser settings")
                    onClicked: page.settingsExpanded = !page.settingsExpanded
                }
            }

            CheckBox {
                objectName: "followWallpaperEngineCheckBox"
                text: "Follow Wallpaper Engine’s current selection"
                checked: page.editorFollowEnabled
                onToggled: {
                    if (page.followEnabled === undefined)
                        cfg_FollowWallpaperEngine = checked;
                    else
                        page.followEnabledSelected(checked);
                    if (!checked) page.pendingManualSelection = null;
                }
            }

            ColumnLayout {
                objectName: "browserSettingsPanel"
                Layout.fillWidth: true
                visible: page.settingsExpanded
                spacing: 4

                SteamLibraryPicker {
                    Layout.fillWidth: true
                    libraries: steamDetect.libraries
                    selectedPath: steamDetect.selectedLibrary || cfg_SteamLibraryPath
                    onLibrarySelected: path => { cfg_SteamLibraryPath = path; wpListModel.refresh(); }
                    onRescanRequested: root.rescanSteam()
                    onBrowseRequested: wpDialog.open()
                }

                FollowSourcePicker {
                    objectName: "followSourcePicker"
                    visible: page.useFollowSourcePicker
                    catalog: page.accountCatalogSnapshot
                    followSources: page.followSources
                    selection: page._activeFollowSelection
                    allowIdentifiedCurrent: page.allowIdentifiedCurrent
                    onFollowSourceSelected: binding => page.acceptFollowSource(binding)
                }
                Label {
                    Layout.fillWidth: true
                    visible: page.editorFollowEnabled
                    wrapMode: Text.WordWrap
                    text: {
                        if (followPreview.selection.source && !(root.wallpaperConfiguration
                            && root.wallpaperConfiguration.FollowWallpaperEngine))
                            return "Selection detected. Click Apply to start following on this desktop.";
                        return followPreview.status || "Waiting for Wallpaper Engine…";
                    }
                }
                ComboBox {
                    Layout.fillWidth: true
                    visible: !page.useFollowSourcePicker
                        && page.editorFollowEnabled && followPreview.accounts.length > 1
                    model: followPreview.accounts
                    currentIndex: followPreview.accounts.indexOf(page.editorFollowAccount || "steamuser")
                    displayText: currentIndex < 0 ? "Choose Wallpaper Engine account" : currentText
                    onActivated: {
                        if (page.followAccount === undefined)
                            cfg_WallpaperEngineAccount = currentText;
                        else
                            page.followAccountSelected(currentText);
                    }
                }
            }
            Label {
                objectName: "pendingManualWallpaperLabel"
                visible: page.editorFollowEnabled && page.pendingManualSelection !== null
                Layout.fillWidth: true
                text: page.pendingManualSelection
                    ? qsTr("Selected wallpaper: %1").arg(page.pendingManualSelection.title) : ""
                wrapMode: Text.WordWrap
            }
            Button {
                objectName: "applyManualWallpaperButton"
                visible: page.editorFollowEnabled && page.pendingManualSelection !== null
                Layout.fillWidth: true
                text: page.pendingManualSelection
                    ? qsTr("Use %1 and stop following on Apply").arg(page.pendingManualSelection.title)
                    : qsTr("Use this wallpaper and stop following on Apply")
                onClicked: page.applyPendingManualSelection()
            }
            Label {
                objectName: "wallpaperActivationStatus"
                visible: page.activationStatus.length > 0
                Layout.fillWidth: true
                text: page.activationStatus
                wrapMode: Text.WordWrap
                opacity: 0.85
            }

            RowLayout {
                id: infoRow
                Layout.alignment: Qt.AlignHCenter
                spacing: 5

                ToolButton {
                    objectName: "toggleBrowserFiltersButton"
                    visible: !page.narrowLayout || (!page.detailsView && !page.filtersView)
                    text: qsTr("Filters")
                    icon.name: page.filterSidebarExpanded ? "sidebar-collapse-left" : "view-filter"
                    onClicked: {
                        if (page.narrowLayout)
                            page.filtersView = true;
                        else
                            page.filterSidebarExpanded = !page.filterSidebarExpanded;
                    }
                }

                Kirigami.ActionToolBar {
                    Layout.fillWidth: true
                    alignment: Qt.AlignRight
                    flat: false

                    ActionGroup {
                        id: group_sort
                        exclusive: true
                    }
                    Component {
                        id: comp_action_sort
                        Kirigami.Action {
                            ActionGroup.group: group_sort

                            property int act_value;
                            checkable: true
                            checked: {
                                checked = cfg_SortMode == act_value;
                            }
                            onTriggered: cfg_SortMode = act_value

                            Component.onCompleted: comp_action_sort.Component.destruction.connect(this.destroy)
                        }
                    }

                    actions: [
                        Kirigami.Action {
                            id: action_cb_sort
                            text: model[currentIndex].short
                            icon.name: "view-sort-descending-symbolic"
                            property int currentIndex: Common.modelIndexOfValue(model, cfg_SortMode)
                            readonly property var model: [
                                {
                                    text: "Sort By Workshop Id",
                                    short: "Id",
                                    value: Common.SortMode.Id
                                },
                                {
                                    text: "Sort Alphabetically By Name",
                                    short: "Alphabetical",
                                    value: Common.SortMode.Name
                                },
                                {
                                    text: "Show Newest Modified First",
                                    short: "Modified",
                                    value: Common.SortMode.Modified
                                }
                            ]
                            children: model.map((el, index) => comp_action_sort.createObject(null, {text: el.text, act_value: el.value}))
                        },
                        Kirigami.Action {
                            icon.name: "view-refresh-symbolic"
                            text: 'Refresh'
                            onTriggered: root.rescanSteam()
                        }
                    ]
                }

                Button {
                    objectName: "showDetailsButton"
                    visible: page.narrowLayout
                    text: qsTr("Details")
                    icon.name: "document-properties"
                    onClicked: page.detailsView = true
                }
                ToolButton {
                    objectName: "toggleWallpaperDetailsButton"
                    visible: !page.narrowLayout
                    text: page.inspectorExpanded ? qsTr("Hide details") : qsTr("Show details")
                    icon.name: "document-properties"
                    onClicked: page.inspectorExpanded = !page.inspectorExpanded
                }
            }

            Loader {
                id: picViewLoader
                objectName: "wallpaperGridLoader"
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                // over wite the implicitWidth to 0
                Layout.preferredHeight: 0

                asynchronous: false
                sourceComponent: picViewCom
                visible: status == Loader.Ready

                Component.onCompleted: {
                    const refreshIndex = () => {
                        this.item.view.model = wpListModel.model; 
                        if(this.status == Loader.Ready) {
                            this.item.setCurIndex(wpListModel.model);
                        }
                    }
                    wpListModel.modelStartSync.connect(this.item.backtoBegin);
                    wpListModel.modelRefreshed.connect(refreshIndex.bind(this));
                }

                Kirigami.Heading {
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.largeSpacing
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WordWrap
                    visible: picViewLoader.item && picViewLoader.item.view.count === 0
                    level: 2
                    text: {
                        if(!cfg_SteamLibraryPath)
                            return "No Steam library found. Open Steam and choose Rescan, or use Browse.";
                        if(wpListModel.countNoFilter > 0)
                            return `Found ${wpListModel.countNoFilter} wallpapers, but none of them matched filters`;
                        return `No wallpapers in this library. Install Wallpaper Engine and download wallpapers in Steam, then choose Rescan.`;
                    }
                    opacity: 0.5
                }
            }
            Component { 
                id: picViewCom
                KCM.GridView {
                    id: picViewGrid
                    anchors.fill: parent

                    readonly property var currentModel: view.model.get(view.currentIndex)
                    readonly property var defaultModel: ListModel {}
                    visible: view.count > 0

                    // from org.kde.image
                    view.implicitCellWidth: Math.max(160, Math.min(235, left_content.width / 4.2))
                    // Match KCM GridDelegate's landscape thumbnail plus its
                    // fixed label/actions allowance instead of a near-square tile.
                    view.implicitCellHeight: view.implicitCellWidth / 1.6
                        + Kirigami.Units.gridUnit * 3
                    view.model: defaultModel
                    view.delegate: KCM.GridDelegate {
                        // path is file://, safe to concat with '/'
                        text: title
                        hoverEnabled: true
                        actions: [
                            Kirigami.Action {
                                icon.name: favor?"user-bookmarks-symbolic":"bookmark-add-symbolic"
                                tooltip: favor?"Remove from favorites":"Add to favorites"
                                onTriggered: picViewLoader.item.toggleFavor(model, index)
                            },
                            Kirigami.Action {
                                icon.name: "folder-remote-symbolic"
                                tooltip: "Open Workshop Link"
                                enabled: workshopid.match(Common.regex_workshop_online)
                                onTriggered: Qt.openUrlExternally(Common.getWorkshopUrl(workshopid))
                            }
                        ]
                        thumbnail: Rectangle {
                            anchors.fill: parent
                            Kirigami.Icon {
                                anchors.centerIn: parent
                                width: root.iconSizes.large
                                height: width
                                source: "view-preview"
                                visible: !imgPre.visible
                            }
                            Image {
                                id: imgPre
                                anchors.fill: parent
                                source: Common.getWpModelPreviewSource(model);
                                sourceSize.width: parent.width
                                sourceSize.height: parent.height
                                fillMode: Image.PreserveAspectCrop//Image.Stretch
                                cache: false
                                asynchronous: true
                                smooth: true
                                visible: Boolean(preview)
                            }
                        }
                        onClicked: {
                            page.selectWallpaper(model);
                            view.currentIndex = index;
                        }
                        onDoubleClicked: page.activateWallpaper(model)
                    }

     
                    function backtoBegin() {
                        view.model = defaultModel
                        //view.positionViewAtBeginning();
                    }

                    function setCurIndex(model) {
                        // model, ListModel
                        new Promise((reoslve, reject) => {
                            for(let i=0;i < model.count;i++) {
                                if(model.get(i).workshopid === cfg_WallpaperWorkShopId) {
                                    view.currentIndex = i;
                                    break;
                                }
                            }
                            if(view.currentIndex == -1 && model.count != 0)
                                view.currentIndex = 0;

                            if(!cfg_WallpaperSource)
                                if(view.currentIndex != -1)
                                    view.currentItem.onClicked();

                            resolve();
                        });
                    }
                    function toggleFavor(model, index) {
                        if(!index) index = view.currentIndex;

                        if(model.favor) {
                            root.customConf.favor.delete(model.workshopid);
                        } else {
                            root.customConf.favor.add(model.workshopid);
                        }
                        this.view.model.assignModel(index, {favor: !model.favor});
                        root.saveCustomConf();

                        if(index == view.currentIndex) this.view.currentIndexChanged();
                    }

                }
            }

            FolderDialog {
                id: wpDialog
                title: "Select the Steam library folder containing steamapps"
                onAccepted: {
                    const path = Utils.trimCharR(Common.urlNative(wpDialog.selectedFolder), '/');
                    cfg_SteamLibraryPath = path;
                    return root.rescanSteam();
                }
            }
        }
    }

    Control {
        id: right_content
        objectName: "wallpaperDetailsView"
        QQC15.SplitView.preferredWidth: page.narrowLayout ? page.width : page.inspectorPreferredWidth
        QQC15.SplitView.minimumWidth: page.narrowLayout ? 0 : 220
        QQC15.SplitView.maximumWidth: page.narrowLayout ? page.width : 440
        QQC15.SplitView.fillWidth: page.narrowLayout
        Layout.fillHeight: true

        readonly property int image_size: Math.max(120, Math.min(200, width - 48))
        readonly property int content_margin: 12
        property var wpmodel: { 
            return picViewLoader.item.currentModel
            ? Common.wpitemFromQtObject(picViewLoader.item.currentModel)
            : Common.wpitem_template;
        }

        visible: page.narrowLayout ? page.detailsView : page.inspectorExpanded

        topPadding: 0
        leftPadding: 0
        rightPadding: 0
        bottomPadding: 0

        background: Rectangle {
            color: Kirigami.Theme.backgroundColor
        }

        contentItem: Flickable {
            anchors.fill: parent

            ScrollBar.vertical: ScrollBar {
                id: right_content_scrollbar
                objectName: "wallpaperDetailsScrollBar"
            }

            contentWidth: width - (right_content_scrollbar.visible ? right_content_scrollbar.width : 0)
            contentHeight: flick_content.implicitHeight

            clip: true
            boundsBehavior: Flickable.OvershootBounds

            ColumnLayout {
                id: flick_content
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: right_content.content_margin
                anchors.rightMargin: anchors.leftMargin
                spacing: 8

                Button {
                    objectName: "backToWallpapersButton"
                    visible: page.narrowLayout
                    Layout.fillWidth: true
                    text: qsTr("Back to wallpapers")
                    icon.name: "go-previous"
                    onClicked: page.detailsView = false
                }

                AnimatedImage { 
                    id: animated_image; 
                    Layout.topMargin: right_content.content_margin
                    Layout.preferredWidth: right_content.image_size
                    Layout.preferredHeight: Layout.preferredWidth
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignTop

                    source: Common.getWpModelPreviewSource(right_content.wpmodel)
                    fillMode: Image.PreserveAspectFit
                    cache: true
                    asynchronous: true
                    onStatusChanged: playing = (status == AnimatedImage.Ready)
                }

                Text {
                    Layout.alignment: Qt.AlignTop
                    Layout.minimumWidth: 0
                    Layout.fillWidth: true
                    Layout.minimumHeight: implicitHeight

                    text: right_content.wpmodel.title
                    color: Kirigami.Theme.textColor
                    font.bold: true
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                    spacing: 8

                    Control {
                        leftPadding: 8
                        topPadding: 4

                        rightPadding: leftPadding
                        bottomPadding: topPadding

                        background: Rectangle {
                            color: Kirigami.Theme.positiveBackgroundColor
                            radius: 8
                        }
                        contentItem: Text {
                            color: Kirigami.Theme.textColor
                            font.capitalization: Font.Capitalize
                            text: right_content.wpmodel.type
                        }
                    }

                    Control {
                        id: control_dir_size
                        leftPadding: 8
                        topPadding: 4

                        rightPadding: leftPadding
                        bottomPadding: topPadding
                        visible: false

                        background: Rectangle {
                            color: Kirigami.Theme.positiveBackgroundColor
                            radius: 8
                        }
                        contentItem: Text {
                            color: Kirigami.Theme.textColor
                            font.capitalization: Font.Capitalize
                            readonly property bool _set_text: {
                                const dir = right_content.wpmodel.path;
                                if(!dir.match(Common.regex_path_check)) {
                                    control_dir_size.visible = false;
                                    return false;
                                }
                                pyext.get_dir_size(Common.urlNative(dir)).then(res => {
                                    this.text = Utils.prettyBytes(res);
                                    control_dir_size.visible = true;
                                }).catch(reason => console.error(reason));
                                return true;
                            }
                        }
                    }

                    Kirigami.ActionToolBar {
                        Layout.fillWidth: false
                        Layout.preferredWidth: implicitWidth
                        flat: true

                        actions: [
                            Kirigami.Action {
                                id: right_act_favor
                                
                                icon.name: right_content.wpmodel.favor 
                                    ? "bookmarks-symbolic"
                                    : "bookmark-add-symbolic"
                                tooltip: right_content.wpmodel.favor
                                    ? 'Remove from favorites'
                                    : 'Add to favorites'
                                onTriggered: picViewLoader.item.toggleFavor(right_content.wpmodel)
                            },
                            Kirigami.Action {
                                icon.name: "emblem-symbolic-link"
                                tooltip: "Open Workshop Link"
                                enabled: right_content.wpmodel.workshopid.match(Common.regex_workshop_online)
                                onTriggered: Qt.openUrlExternally(Common.getWorkshopUrl(right_content.wpmodel.workshopid))
                            },
                            Kirigami.Action {
                                icon.name: "folder-symbolic"
                                tooltip: "Open Containing Folder"
                                onTriggered: Qt.openUrlExternally(right_content.wpmodel.path) 
                            }
                        ]
                    }
                }

                ListView {
                    objectName: "wallpaperTagsAndPlaylists"
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36

                    orientation: ListView.Horizontal
                    model: ListModel {}
                    readonly property bool _set_model: {
                        const wpmodel = right_content.wpmodel;
                        const tags = right_content.wpmodel.tags || [];
                        const playlists = right_content.wpmodel.playlists || [];
                        const _model = this.model;
                        _model.clear();
                        for (let i = 0; i < page.listCount(tags); ++i) {
                            const tag = page.listEntryAt(tags, i);
                            if (tag !== null) _model.append(tag);
                        }
                        for (let i = 0; i < page.listCount(playlists); ++i) {
                            const playlist = page.listEntryAt(playlists, i);
                            if (playlist !== null) _model.append(playlist);
                        }
                        _model.append({key: wpmodel.contentrating});
                        return true;
                    }
                    clip: false
                    spacing: 8

                    delegate: Control {
                        objectName: `wallpaper-chip-${model.key || index}`
                        leftPadding: 8
                        topPadding: 4
                        rightPadding: leftPadding
                        bottomPadding: topPadding

                        background: Rectangle {
                            color: Kirigami.Theme.activeBackgroundColor
                            radius: 8
                        }
                        contentItem: Text {
                            color: Kirigami.Theme.textColor
                            text: model.text || model.key
                        }
                    }
                }

                Component {
                    id: right_opt_combox
                    ComboBox {
                        property var def_val

                        textRole: "text"
                        onActivated: {}

                        property var res_val: currentIndex >= 0 ? Common.cbCurrentValue(this) : def_val
                        function finish() {
                            currentIndex = Common.cbIndexOfValue(this, def_val);
                        }
                    }
                }
                Component {
                    id: right_opt_switch
                    Switch {
                        property bool def_val
                        property bool res_val: checked
                        function finish() {
                            checked = def_val;
                        }
                    }
                }
                Component {
                    id: right_opt_spinbox
                    SpinBox {
                        property int def_val
                        property int res_val: value
                        function finish() {
                            value = def_val;
                        }
                    }
                }
                Component {
                    id: right_opt_dspinbox
                    DoubleSpinBox {
                        property real def_val
                        property real res_val: dValue
                        function finish() {
                            dValue = def_val;
                        }
                    }
                }

                OptionGroup {
                    id: right_opts
                    objectName: "wallpaperOptions"
                    Layout.fillWidth: true

                    readonly property string workshopid: right_content.wpmodel.workshopid

                    property var config_resets: new Set()
                    property var config_changes: ({})
                    property var config: ({})

                    onWorkshopidChanged: {
                        if (workshopid) {
                            pyext.read_wallpaper_config(workshopid).then(res => {
                                config = res;
                            });
                        } else {
                            config = {};
                        }
                    }
                    Component.onCompleted: {
                        if (workshopid) {
                            pyext.read_wallpaper_config(workshopid).then(res => {
                                config = res;
                            });
                        }
                    }
                    function save_changes() {
                        console.log("save_changes called, config_changes:", JSON.stringify(config_changes));
                        config_resets.forEach((wid) => {
                            pyext.reset_wallpaper_config(wid).then(res => {});
                        });
                        Object.entries(config_changes).forEach(([wid, cfg]) => {
                            console.log("saving config for wid:", wid, "cfg:", JSON.stringify(cfg));
                            pyext.write_wallpaper_config(wid, cfg);
                        });
                        // Clear after saving
                        config_changes = {};
                        config_resets.clear();
                    }
                    function set_config(key, val) {
                        console.log("set_config called, key:", key, "val:", val, "workshopid:", workshopid);
                        if(!key || !workshopid) return;

                        if (!config_changes[workshopid])
                            config_changes[workshopid] = {}
                        config_changes[workshopid][key] = val;

                        this.config_changesChanged();
                        console.log("set_config: cfg_PerOptChanged before:", cfg_PerOptChanged);
                        cfg_PerOptChanged++;
                        console.log("set_config: cfg_PerOptChanged after:", cfg_PerOptChanged);
                    }
                    function reset_config() {
                        config_resets.add(workshopid);
                        delete config_changes[workshopid];
                        config = {}
                        cfg_PerOptChanged++;
                    }
                    function in_config_changes(key) {
                        return config_changes.hasOwnProperty(workshopid) && config_changes[workshopid].hasOwnProperty(key);
                    }

                    function get_config_val(key) {
                        if (in_config_changes(key))
                            return config_changes[workshopid][key];
                        if (config.hasOwnProperty(key))
                            return config[key];
                        return null;
                    }
                    function has_change(key) {
                        return config.hasOwnProperty(key) || in_config_changes(key);
                    }

                    header.text: 'Option'
                    header.text_color: Kirigami.Theme.textColor
                    header.icon: '../../images/cheveron-down.svg'
                    header.color: Kirigami.Theme.activeBackgroundColor

                    header.actor: Kirigami.ActionToolBar {
                        Layout.fillWidth: true
                        alignment: Qt.AlignRight
                        flat: true
                        actions: [
                            Kirigami.Action {
                                text: 'Reset'
                                onTriggered: {
                                    right_opts.reset_config();
                                }
                            }
                        ]
                    }
                    Repeater {
                        property bool markModel: false;
                        model: [
                            {
                                mark_: markModel,
                                text: 'Display',
                                config_key: 'display_mode',
                                comp: right_opt_combox,
                                props: {
                                    model: [
                                        {
                                            text: "Keep Aspect Ratio",
                                            value: Common.DisplayMode.Aspect
                                        },
                                        {
                                            text: "Scale and Crop",
                                            value: Common.DisplayMode.Crop
                                        },
                                        {
                                            text: "Scale to Fill",
                                            value: Common.DisplayMode.Scale
                                        },
                                    ],
                                    def_val: cfg_DisplayMode,
                                }
                            },
                            {
                                text: 'Mute Audio',
                                config_key: 'mute_audio',
                                comp: right_opt_switch,
                                props: {
                                    def_val: cfg_MuteAudio
                                },
                            },
                            {
                                text: 'Volume',
                                config_key: 'volume', 
                                comp: right_opt_spinbox,
                                props: {
                                    def_val: cfg_Volume,
                                    from: 1,
                                    to: 100,
                                    stepSize: 1,
                                },
                            },
                            {
                                text: 'Speed',
                                config_key: 'speed', 
                                comp: right_opt_dspinbox,
                                props: {
                                    def_val: cfg_Speed,
                                    dFrom: 0.1,
                                    dTo: 16,
                                    dStepSize: 0.1,
                                },
                            },

                        ]
                        OptionItem {
                            text: modelData.text
                            text_color: Kirigami.Theme.textColor

                            property bool is_changed: right_opts.config && 
                                right_opts.config_changes && 
                                right_opts.has_change(modelData.config_key)

                            icon: is_changed ? Qt.resolvedUrl('../../images/edit-pencil.svg') : ''
                            actor: Loader {
                                sourceComponent: modelData.comp
                                onLoaded: {
                                    Object.entries(modelData.props).forEach(([key, value]) => {
                                        this.item[key] = value;
                                    });
                                    const changed_val = right_opts.get_config_val(modelData.config_key);
                                    if(changed_val !== null) {
                                        this.item['def_val'] = changed_val;
                                    }

                                    this.item.finish();
                                    this.item.onRes_valChanged.connect(() => {
                                        right_opts.set_config(modelData.config_key, this.item.res_val);
                                    });
                                }
                            }
                        }
                        Component.onCompleted: {
                            right_opts.onConfigChanged.connect(() => {
                                markModel = !markModel;
                            });
                        }
                    }
                }
                // User Properties from project.json
                Component {
                    id: user_prop_color
                    ColorButton { }
                }

                OptionGroup {
                    id: user_props_group
                    Layout.fillWidth: true
                    visible: user_props_repeater.model.length > 0

                    readonly property string workshopid: right_content.wpmodel.workshopid
                    onWorkshopidChanged: {
                        // Reset state when switching wallpapers
                        userProperties = [];
                        propConfig = {};
                        propChanges = {};
                    }

                    property var userProperties: []
                    property var propConfig: ({})
                    property var propChanges: ({})

                    property bool _loadProps: {
                        // Force re-evaluation when workshopid changes
                        const wid = workshopid;
                        const wpPath = right_content.wpmodel.path;
                        const projectPath = Common.getWpModelProjectPath(right_content.wpmodel);
                        if (wid && wpPath && wpPath.match(Common.regex_path_check)) {
                            pyext.readfile(Common.urlNative(projectPath)).then(value => {
                                const project = Utils.parseJson(value);
                                if (project && project.general && project.general.properties) {
                                    const props = project.general.properties;
                                    const arr = [];
                                    // Helper to convert localization keys to readable text
                                    function formatLabel(text, key) {
                                        if (!text) return key;
                                        // Convert "ui_browse_properties_XXX" to "Xxx Yyy"
                                        if (text.startsWith('ui_browse_properties_')) {
                                            const suffix = text.replace('ui_browse_properties_', '');
                                            // Split by underscore, capitalize each word
                                            return suffix.split('_').map(w =>
                                                w.charAt(0).toUpperCase() + w.slice(1).toLowerCase()
                                            ).join(' ');
                                        }
                                        return text;
                                    }
                                    for (const key in props) {
                                        const prop = props[key];
                                        if (prop.type) {
                                            arr.push({
                                                key: key,
                                                text: formatLabel(prop.text, key),
                                                type: prop.type,
                                                value: prop.value,
                                                min: prop.min,
                                                max: prop.max,
                                                options: prop.options
                                            });
                                        }
                                    }
                                    userProperties = arr;
                                } else {
                                    userProperties = [];
                                }
                            }).catch(reason => {
                                console.error(`read project.json error: ${reason}`);
                                userProperties = [];
                            });
                        } else {
                            userProperties = [];
                        }
                        return true;
                    }

                    property bool _loadConfig: {
                        if (workshopid) {
                            pyext.read_wallpaper_config(workshopid).then(res => {
                                propConfig = res || {};
                            });
                        }
                        return true;
                    }

                    function savePropChange(key, val) {
                        if (!workshopid) return;

                        // Create new object to trigger QML binding update
                        const newChanges = Object.assign({}, propChanges);
                        newChanges[key] = val;
                        propChanges = newChanges;

                        // Build config to save: merge with existing under 'user_props' key
                        const userPropsConfig = {};
                        userPropsConfig['user_props'] = Object.assign({}, propConfig['user_props'] || {}, propChanges);
                        pyext.write_wallpaper_config(workshopid, userPropsConfig);

                        // Signal main.qml to reload config
                        cfg_PerOptChanged++;
                    }

                    function getPropValue(key, defaultVal) {
                        if (propChanges.hasOwnProperty(key)) return propChanges[key];
                        if (propConfig['user_props'] && propConfig['user_props'].hasOwnProperty(key))
                            return propConfig['user_props'][key];
                        return defaultVal;
                    }

                    function resetUserProps() {
                        propChanges = {};
                        const resetConfig = { 'user_props': {} };
                        pyext.write_wallpaper_config(workshopid, resetConfig);
                        propConfig = {};

                        // Signal main.qml to reload config
                        cfg_PerOptChanged++;
                    }

                    header.text: 'User Properties'
                    header.text_color: Kirigami.Theme.textColor
                    header.icon: '../../images/cheveron-down.svg'
                    header.color: Kirigami.Theme.activeBackgroundColor

                    header.actor: Kirigami.ActionToolBar {
                        Layout.fillWidth: true
                        alignment: Qt.AlignRight
                        flat: true
                        actions: [
                            Kirigami.Action {
                                text: 'Reset'
                                onTriggered: user_props_group.resetUserProps()
                            }
                        ]
                    }

                    Repeater {
                        id: user_props_repeater
                        model: user_props_group.userProperties

                        OptionItem {
                            text: modelData.text
                            text_color: Kirigami.Theme.textColor

                            property bool is_changed: user_props_group.propChanges.hasOwnProperty(modelData.key)
                            icon: is_changed ? Qt.resolvedUrl('../../images/edit-pencil.svg') : ''

                            actor: Loader {
                                sourceComponent: {
                                    switch(modelData.type) {
                                        case 'bool':
                                            return right_opt_switch;
                                        case 'slider':
                                            // Use int spinbox if min/max are integers
                                            const hasDecimals = (modelData.min && modelData.min % 1 !== 0) ||
                                                               (modelData.max && modelData.max % 1 !== 0) ||
                                                               (modelData.value && modelData.value % 1 !== 0);
                                            return hasDecimals ? right_opt_dspinbox : right_opt_spinbox;
                                        case 'color':
                                            return user_prop_color;
                                        case 'combo':
                                            return right_opt_combox;
                                        default:
                                            return null;
                                    }
                                }

                                onLoaded: {
                                    const savedVal = user_props_group.getPropValue(modelData.key, null);
                                    const defVal = savedVal !== null ? savedVal : modelData.value;

                                    switch(modelData.type) {
                                        case 'bool':
                                            this.item.def_val = Boolean(defVal);
                                            break;
                                        case 'slider':
                                            const hasDecimals = (modelData.min && modelData.min % 1 !== 0) ||
                                                               (modelData.max && modelData.max % 1 !== 0) ||
                                                               (modelData.value && modelData.value % 1 !== 0);
                                            if (hasDecimals) {
                                                this.item.dFrom = modelData.min || 0;
                                                this.item.dTo = modelData.max || 100;
                                                this.item.dStepSize = (modelData.max - modelData.min) / 100 || 0.01;
                                                this.item.def_val = defVal || 0;
                                            } else {
                                                this.item.from = Math.floor(modelData.min || 0);
                                                this.item.to = Math.floor(modelData.max || 100);
                                                this.item.stepSize = 1;
                                                this.item.def_val = Math.floor(defVal || 0);
                                            }
                                            break;
                                        case 'color':
                                            // Color can be string "0.1 0.2 0.3" or object {r,g,b}
                                            let colorVal = "#ffffff";
                                            if (typeof defVal === 'string' && defVal.includes(' ')) {
                                                const parts = defVal.split(' ').map(Number);
                                                if (parts.length >= 3) {
                                                    colorVal = Qt.rgba(parts[0], parts[1], parts[2], 1.0);
                                                }
                                            } else if (defVal && typeof defVal === 'string') {
                                                colorVal = defVal;
                                            }
                                            this.item.def_val = colorVal;
                                            this.item.colorValue = colorVal;
                                            break;
                                        case 'combo':
                                            // Build model from options
                                            if (modelData.options) {
                                                const comboModel = [];
                                                for (const opt of modelData.options) {
                                                    let label = opt.label || opt.value;
                                                    // Convert localization keys to readable text
                                                    if (label && label.startsWith('ui_browse_properties_')) {
                                                        const suffix = label.replace('ui_browse_properties_', '');
                                                        label = suffix.split('_').map(w =>
                                                            w.charAt(0).toUpperCase() + w.slice(1).toLowerCase()
                                                        ).join(' ');
                                                    }
                                                    comboModel.push({
                                                        text: label,
                                                        value: opt.value
                                                    });
                                                }
                                                this.item.model = comboModel;
                                                this.item.def_val = defVal || 0;
                                            }
                                            break;
                                    }

                                    this.item.finish();
                                    this.item.onRes_valChanged.connect(() => {
                                        user_props_group.savePropChange(modelData.key, this.item.res_val);
                                    });
                                }
                            }
                        }
                    }
                }

                WallpaperDescription {
                    id: descriptionTextArea
                    objectName: "wallpaperDescription"
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.minimumHeight: implicitHeight
                    projectUrl: Common.urlNative(Common.getWpModelProjectPath(right_content.wpmodel))
                    readFile: path => pyext.readfile(path)
                }
            }
        }
    }
}
