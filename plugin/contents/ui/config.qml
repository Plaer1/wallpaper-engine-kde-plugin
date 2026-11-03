import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.5
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.kirigami 2.4 as Kirigami
import com.github.catsout.wallpaperEngineKde

import "page"
import "js/config-draft-reconciler.mjs" as FollowDraft

ColumnLayout {
    id: root
    spacing: 5

    // Устанавливаем тему для всех дочерних элементов
    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false

    // Required by Plasma 6
    property var configDialog
    property var wallpaperConfiguration

    // Plasma copies these cfg_ fields immediately after saveConfig(). Keep the
    // editor draft separate from the safe values exported for that copy.
    readonly property var followFields: [
        "FollowWallpaperEngine", "WallpaperEngineAccount", "FollowSourceMode",
        "FollowSourceAccountKey", "FollowSourceKey"
    ]
    property var followDraftState: null
    property bool syncingFollowExports: false
    readonly property var followEditorValues: followDraftState
        ? followDraftState.draft : followValuesForPreview()

    property bool cfg_FollowWallpaperEngine
    property string cfg_WallpaperEngineAccount
    property string cfg_BrowseScope
    property string cfg_FollowSourceMode
    property string cfg_FollowSourceAccountKey
    property string cfg_FollowSourceKey
    property string cfg_SteamLibraryPath
    property string cfg_WallpaperWorkShopId
    property string cfg_WallpaperSource
    property string cfg_FilterStr
    property int    cfg_SortMode

    property alias  cfg_Fps:                 settingPage.cfg_Fps
    property alias  cfg_Volume:              settingPage.cfg_Volume
    property alias  cfg_MpvStats:            settingPage.cfg_MpvStats
    property alias  cfg_SceneCachePasses:    settingPage.cfg_SceneCachePasses
    property alias  cfg_ShareGpuContext:     settingPage.cfg_ShareGpuContext
    property alias  cfg_MirrorScene:         settingPage.cfg_MirrorScene
    property alias  cfg_SpanScreens:         settingPage.cfg_SpanScreens
    property alias  cfg_Speed:               settingPage.cfg_Speed
    property alias  cfg_MuteAudio:           settingPage.cfg_MuteAudio
    property alias  cfg_MouseInput:          settingPage.cfg_MouseInput
    // Legacy per-wallpaper exports remain for Plasma config compatibility;
    // playback policy now reads the shared WindowPauseMode/WindowResumeDelay.
    property int    cfg_ResumeTime:          300
    property alias  cfg_SwitchTimer:         settingPage.cfg_SwitchTimer
    property alias  cfg_RandomizeWallpaper:  settingPage.cfg_RandomizeWallpaper
    property alias  cfg_NoRandomWhilePaused: settingPage.cfg_NoRandomWhilePaused
    property alias  cfg_GlobalMode:          settingPage.cfg_GlobalMode
    property bool   cfg_PauseFilterByScreen: true
    property alias  cfg_PauseOnBatPower:     settingPage.cfg_PauseOnBatPower
    property alias  cfg_PauseBatPercent:     settingPage.cfg_PauseBatPercent
    property alias  cfg_PauseOnGamemode:     settingPage.cfg_PauseOnGamemode
    property int    cfg_DisplayMode
    property int    cfg_PauseMode
    property int    cfg_VideoBackend
    property int    draftWindowPauseMode: 6
    property int    draftWindowResumeDelay: 300
    property var    windowPauseModeBaseline: 6
    property var    windowResumeDelayBaseline: 300

    readonly property bool windowPauseModeUserDirty:
        draftWindowPauseMode !== normalizeWindowPauseMode(windowPauseModeBaseline)
    readonly property bool windowResumeDelayUserDirty:
        draftWindowResumeDelay !== normalizeWindowResumeDelay(windowResumeDelayBaseline)
    readonly property bool windowPauseModeNeedsCommit:
        draftWindowPauseMode !== Number(windowPauseModeBaseline)
    readonly property bool windowResumeDelayNeedsCommit:
        draftWindowResumeDelay !== Number(windowResumeDelayBaseline)

    property int    cfg_PerOptChanged: 0

    //property alias  cfg_UseMpv
    //property string cfg_BackgroundColor: "black"
    //property alias  cfg_FilterMode: wallpaperPage.cfg_FilterMode

    property string cfg_CustomConf
    property var customConf: {
        customConf = Common.loadCustomConf(cfg_CustomConf);
    }

    property var iconSizes: {
        if(PlasmaCore.Units) {
            iconSizes = PlasmaCore.Units.iconSizes;
        } else {
            iconSizes = {
                large: 48
            }
        }
    }
    // property var themeWidth: {
    //     if(PlasmaCore.Theme && PlasmaCore.Theme.mSize) {
    //         themeWidth = PlasmaCore.Theme.mSize(theme.defaultFont).width;
    //     } else if(theme) {
    //         themeWidth = theme.mSize(theme.defaultFont).width;
    //     } else {
    //         themeWidth = font.pixelSize;
    //     }
    // }

    property var libcheck: ({
        wallpaper: Common.checklib_wallpaper(root),
        qtwebchannel: Common.checklib_webchannel(root)
    })


                    
    property var plugin_info: {
        if(!libcheck.wallpaper) {
            plugin_info = {
                version: "-",
                cache_path: null
            }
        } else {
            plugin_info = Qt.createQmlObject(`
                import QtQuick 2.0;
                import com.github.catsout.wallpaperEngineKde 1.2
                PluginInfo {}
            `, this);
        }
    }

    property var pyext: {
        // FileHelper-based Pyext (no Python/WebSocket dependency)
        pyext = Qt.createQmlObject(`
            import QtQuick 2.0;
            Pyext {}
        `, this);
    }

    // Steam libraries found on this machine, see FileHelper::detectSteam
    property int steamScanRevision: 0
    property var steamDetect: {
        steamScanRevision;
        return pyext.detect_steam(cfg_SteamLibraryPath, true);
    }
    function rescanSteam() {
        steamScanRevision++;
        wpListModel.refresh();
        catalogRequested = true;
        steamCatalogProvider.rescan();
    }

    // Account scope and follow source are separate KConfig values. Empty values
    // are the migration marker for pre-picker installations.
    function configuredBrowseScope() {
        const defaults = {mode: "allDownloaded", accountKeys: [], includeSharedProjects: true};
        if (typeof cfg_BrowseScope !== "string" || !cfg_BrowseScope.trim()) return defaults;
        try {
            const saved = JSON.parse(cfg_BrowseScope);
            if (!saved || typeof saved !== "object") return defaults;
            return {
                mode: saved.mode === "selectedAccounts" ? "selectedAccounts" : "allDownloaded",
                accountKeys: Array.isArray(saved.accountKeys)
                    ? Array.from(new Set(saved.accountKeys.filter(key => typeof key === "string"))) : [],
                includeSharedProjects: saved.includeSharedProjects !== false
            };
        } catch (reason) { return defaults; }
    }

    // Signal boundaries may expose JavaScript arrays as QVariantList values.
    // Accept either representation, while keeping malformed scalar input empty.
    function normalizedStringList(value) {
        if (!value || typeof value === "string" || typeof value.length !== "number")
            return [];
        const result = [];
        for (let i = 0; i < value.length; ++i) {
            if (typeof value[i] === "string") result.push(value[i]);
        }
        return result;
    }

    function followValuesFromCfg() {
        return {
            FollowWallpaperEngine: Boolean(cfg_FollowWallpaperEngine),
            WallpaperEngineAccount: String(cfg_WallpaperEngineAccount || ""),
            FollowSourceMode: String(cfg_FollowSourceMode || ""),
            FollowSourceAccountKey: String(cfg_FollowSourceAccountKey || ""),
            FollowSourceKey: String(cfg_FollowSourceKey || "")
        };
    }

    function followValuesForPreview() {
        const values = followValuesFromCfg();
        for (let i = 0; i < followFields.length; ++i) {
            const key = followFields[i];
            const live = readFollowLiveValue(key);
            if (live !== undefined) values[key] = normalizeFollowValue(key, live);
        }
        return values;
    }

    function legacyFollowSection() {
        const account = followEditorValues.WallpaperEngineAccount;
        if (typeof account === "string" && account) return account;
        const sections = followPreview && followPreview.accounts
            ? Array.from(followPreview.accounts) : [];
        if (sections.includes("steamuser")) return "steamuser";
        return sections.length === 1 ? sections[0] : "steamuser";
    }

    function followSectionSourceKey(section) {
        const path = steamDetect.configFile || "config.json";
        return `config:${path}#section:${encodeURIComponent(section || "steamuser")}`;
    }

    function configuredFollowSelection() {
        const values = followEditorValues;
        const mode = ["identifiedCurrent", "account", "legacySession", "manualMapping"]
            .includes(values.FollowSourceMode) ? values.FollowSourceMode : "legacySession";
        const accountKey = typeof values.FollowSourceAccountKey === "string"
            ? values.FollowSourceAccountKey : "";
        const sourceKey = typeof values.FollowSourceKey === "string" && values.FollowSourceKey
            ? values.FollowSourceKey : followSectionSourceKey(legacyFollowSection());
        return {mode, accountKey, sourceKey};
    }

    function normalizeFollowValue(key, value) {
        return key === "FollowWallpaperEngine" ? Boolean(value)
            : typeof value === "string" ? value : "";
    }

    function readFollowLiveValue(key) {
        const liveMap = wallpaperConfiguration;
        if (!liveMap) return undefined;
        // Real Plasma maps expose dynamic properties. The value() fallback
        // also supports an injectable test map while keeping that interface
        // out of the editor and reconciliation logic.
        const direct = liveMap[key];
        if (direct !== undefined) return direct;
        return typeof liveMap.value === "function" ? liveMap.value(key) : undefined;
    }

    function refreshFollowExports() {
        if (!followDraftState) return;
        const values = FollowDraft.exports(followDraftState, followFields);
        syncingFollowExports = true;
        cfg_FollowWallpaperEngine = Boolean(values.FollowWallpaperEngine);
        cfg_WallpaperEngineAccount = String(values.WallpaperEngineAccount || "");
        cfg_FollowSourceMode = String(values.FollowSourceMode || "");
        cfg_FollowSourceAccountKey = String(values.FollowSourceAccountKey || "");
        cfg_FollowSourceKey = String(values.FollowSourceKey || "");
        syncingFollowExports = false;
    }

    function stageFollowField(key, value) {
        if (followFields.indexOf(key) < 0) return;
        if (!followDraftState) initializeFollowDraft();
        followDraftState = FollowDraft.stage(followDraftState, key,
            normalizeFollowValue(key, value));
        refreshFollowExports();
    }

    function acceptFollowLiveValue(key, value) {
        // KConfigPropertyMap.valueChanged is reliable here for local QML writes
        // to the same in-process wallpaperConfiguration object. External C++ or
        // disk edits do not emit this notification and are outside this bridge.
        if (!followDraftState || followFields.indexOf(key) < 0) return;
        const patch = {};
        patch[key] = normalizeFollowValue(key, value);
        followDraftState = FollowDraft.updateLive(followDraftState, patch);
        refreshFollowExports();
    }

    function resolveFollowConflict(key, choice) {
        if (!followDraftState) return;
        followDraftState = FollowDraft.resolveConflict(followDraftState, key, choice);
        refreshFollowExports();
    }

    function initializeFollowDraft() {
        if (followDraftState) return;
        // Plasma has injected the initial cfg_ values before this real page's
        // Component.onCompleted. Read the shared map once to close the small
        // creation window; all later in-process QML writes arrive via signal.
        followDraftState = FollowDraft.create(followValuesFromCfg());
        const patch = {};
        for (let i = 0; i < followFields.length; ++i) {
            const key = followFields[i];
            const value = readFollowLiveValue(key);
            if (value !== undefined)
                patch[key] = normalizeFollowValue(key, value);
        }
        followDraftState = FollowDraft.updateLive(followDraftState, patch);
        refreshFollowExports();
    }

    function makeFollowSourceCandidates(catalog, preview) {
        const result = [];
        const path = steamDetect.configFile || "config.json";
        const previewAccounts = preview && preview.accounts ? Array.from(preview.accounts) : [];
        const legacySections = previewAccounts.length ? previewAccounts : [legacyFollowSection()];
        const provenance = preview && preview.provenance ? preview.provenance : {};
        const previewStatus = [preview && preview.status, provenance.label]
            .filter(value => typeof value === "string" && value.length).join(" — ");
        const binding = configuredFollowSelection();
        legacySections.forEach(section => result.push({
            mode: "legacySession", accountKey: "", sourceKey: followSectionSourceKey(section),
            label: `Wallpaper Engine section ${section}`,
            // Keep the legacy fallback visible for migration, but only expose
            // sections actually read from the config as association targets.
            associable: previewAccounts.includes(section),
            status: binding.mode === "legacySession" && section === legacyFollowSection()
                ? previewStatus : "",
            available: true
        }));

        const accounts = catalog && Array.isArray(catalog.accounts) ? catalog.accounts : [];
        accounts.forEach(account => {
            if (!account || account.identityStatus !== "verified"
                || typeof account.steamId64 !== "string"
                || account.key !== `steam:${account.steamId64}`) return;
            const selected = binding.mode === "account" && binding.accountKey === account.key;
            result.push({
                mode: "account", accountKey: account.key,
                // Account mode resolves native sections by verified identity;
                // this key records which config file supplied the candidate.
                sourceKey: `config:${path}#account:${account.steamId64}`,
                label: `${account.accountId32 || account.steamId64} saved selection`,
                status: selected ? previewStatus : "",
                available: true
            });
        });
        // A saved manualMapping is shown only when its account remains verified
        // and its source still resolves to an explicitly discovered legacy
        // section. New mappings are created only by FollowSourcePicker's
        // confirmation action and then staged through the normal G3 path.
        if (binding.mode === "manualMapping" && binding.accountKey && binding.sourceKey) {
            const account = accounts.find(item => item && item.key === binding.accountKey
                && item.identityStatus === "verified"
                && typeof item.steamId64 === "string"
                && item.key === `steam:${item.steamId64}`);
            const section = result.find(item => item.mode === "legacySession"
                && item.sourceKey === binding.sourceKey && item.accountKey === ""
                && item.associable === true);
            if (account && section) result.push({
                mode: "manualMapping", accountKey: account.key, sourceKey: section.sourceKey,
                label: `User-associated — ${account.label || account.key} — ${section.label}`,
                status: "Explicitly associated by the user.", available: true, userConfirmed: true
            });
        }
        return result;
    }

    property bool catalogRequested: false
    readonly property var steamAccountsCatalogSnapshot:
        (steamCatalogProvider.ready || steamCatalogProvider.unavailable)
            ? steamCatalogProvider.snapshot() : null
    readonly property var browseScopeBinding: configuredBrowseScope()
    readonly property var followSelectionBinding: configuredFollowSelection()
    readonly property var followSourceCandidates:
        makeFollowSourceCandidates(steamAccountsCatalogSnapshot, followPreview)

    SteamCatalogProvider {
        id: steamCatalogProvider
        pyext: root.pyext
        // The normal config dialog is the catalog consumer. Temporary host
        // previews stay idle; a deliberate rescan can request it explicitly.
        enabled: root.catalogRequested || (Boolean(root.configDialog) && bar.currentIndex === 0)
    }

    WallpaperEngineFollower {
        id: followPreview
        enabled: Boolean(root.followEditorValues.FollowWallpaperEngine)
        configPath: steamDetect.configFile || ""
        projectDirs: steamDetect.allProjectDirs
        account: root.followEditorValues.WallpaperEngineAccount
        // Keep the follower input on the raw editor binding. The display
        // fallback can inspect followPreview.accounts without feeding that
        // output back into the follower and creating a binding cycle.
        followMode: root.followEditorValues.FollowSourceMode || "legacySession"
        followAccountKey: root.followEditorValues.FollowSourceAccountKey || ""
        followSourceKey: root.followEditorValues.FollowSourceKey || ""
        currentSteamId64: ""
        currentIdentityVerified: false
    }

    // Global Mode: on Apply, push the shared keys into ~/.config/wekde/global.json
    // and wake the running wallpaper(s) (the config dialog may be another process).
    GlobalConfig {
        id: globalCfg
    }
    WallpaperSyncBus {
        id: syncBus
        onGlobalConfigChanged: globalCfg.reload()
    }
    property int globalAudioRevision: 0
    readonly property bool globalAudioMuted: {
        globalAudioRevision;
        return Boolean(globalCfg.get("AudioDisabled", false));
    }
    property int globalSizingRevision: 0
    readonly property bool globalSizingEnabled: {
        globalSizingRevision;
        return Boolean(globalCfg.get("SizingEnabled", false));
    }
    readonly property int globalSizingMode: {
        globalSizingRevision;
        return Number(globalCfg.get("SizingMode", Common.DisplayMode.Crop)) === Common.DisplayMode.Scale
            ? Common.DisplayMode.Scale : Common.DisplayMode.Crop;
    }
    Connections {
        target: globalCfg
        function onChanged(key, value) {
            if (key === "AudioDisabled") root.globalAudioRevision++;
            if (key === "SizingEnabled" || key === "SizingMode") root.globalSizingRevision++;
            if (key === "WindowPauseMode") {
                const keepDraft = root.windowPauseModeUserDirty;
                root.windowPauseModeBaseline = value;
                if (!keepDraft)
                    root.draftWindowPauseMode = root.normalizeWindowPauseMode(value);
            }
            if (key === "WindowResumeDelay") {
                const keepDraft = root.windowResumeDelayUserDirty;
                root.windowResumeDelayBaseline = value;
                if (!keepDraft)
                    root.draftWindowResumeDelay = root.normalizeWindowResumeDelay(value);
            }
        }
    }
    Connections {
        target: root.wallpaperConfiguration
        ignoreUnknownSignals: true
        function onValueChanged(key, value) {
            root.acceptFollowLiveValue(key, value);
        }
    }
    function setGlobalAudioMuted(value) {
        globalCfg.set("AudioDisabled", value);
        syncBus.broadcastGlobalChanged();
    }
    function setGlobalSizingEnabled(value) {
        globalCfg.set("SizingEnabled", Boolean(value));
        syncBus.broadcastGlobalChanged();
    }
    function setGlobalSizingMode(value) {
        globalCfg.set("SizingMode", value === Common.DisplayMode.Scale
            ? Common.DisplayMode.Scale : Common.DisplayMode.Crop);
        syncBus.broadcastGlobalChanged();
    }
    function normalizeWindowPauseMode(value) {
        const mode = Number(value);
        if (mode === 5) return 0; // Legacy FullScreen enum value.
        return [0, 1, 2, 3, 4, 6].includes(mode) ? mode : 6;
    }
    function normalizeWindowResumeDelay(value) {
        const delay = Number(value);
        if (!Number.isFinite(delay)) return 300;
        return Math.max(1, Math.min(60000, Math.round(delay)));
    }
    function initializeWindowPolicyDraft() {
        windowPauseModeBaseline = globalCfg.get("WindowPauseMode", 6);
        draftWindowPauseMode = normalizeWindowPauseMode(windowPauseModeBaseline);
        windowResumeDelayBaseline = globalCfg.get("WindowResumeDelay", 300);
        draftWindowResumeDelay = normalizeWindowResumeDelay(windowResumeDelayBaseline);
    }
    function stageWindowPauseMode(value) {
        draftWindowPauseMode = normalizeWindowPauseMode(value);
    }
    function stageWindowResumeDelay(value) {
        draftWindowResumeDelay = normalizeWindowResumeDelay(value);
    }
    // Wallpaper selection as it was when the dialog opened; used so that pressing
    // Apply for an unrelated setting in Global Mode doesn't push this screen's
    // (possibly stale) per-screen wallpaper back into the shared global config.
    property string initialWallpaperSource
    property string initialWallpaperWorkShopId
    Component.onCompleted: {
        initializeFollowDraft();
        initializeWindowPolicyDraft();
        initialWallpaperSource = cfg_WallpaperSource;
        initialWallpaperWorkShopId = cfg_WallpaperWorkShopId;
        if(globalCfg.enabled)
            cfg_SpanScreens = globalCfg.get("SpanScreens", cfg_SpanScreens);
    }

    function saveConfig() {
        // Plasma copies every cfg_ key as soon as this synchronous hook returns.
        refreshFollowExports();
        wallpaperPage.saveConfig();
        globalCfg.enabled = cfg_GlobalMode;
        const batch = {};
        if (windowPauseModeNeedsCommit)
            batch["WindowPauseMode"] = draftWindowPauseMode;
        if (windowResumeDelayNeedsCommit)
            batch["WindowResumeDelay"] = draftWindowResumeDelay;
        if(cfg_GlobalMode) {
            batch["RandomizeWallpaper"] = cfg_RandomizeWallpaper;
            batch["SwitchTimer"] = cfg_SwitchTimer;
            batch["NoRandomWhilePaused"] = cfg_NoRandomWhilePaused;
            batch["FilterStr"] = cfg_FilterStr;
            batch["SortMode"] = cfg_SortMode;
            batch["SpanScreens"] = cfg_SpanScreens;
            // Only sync the wallpaper itself if the user actually changed the
            // selection this session, so we never overwrite the shared wallpaper
            // with a stale per-screen value.
            if(cfg_WallpaperSource !== initialWallpaperSource
                || cfg_WallpaperWorkShopId !== initialWallpaperWorkShopId) {
                batch["WallpaperSource"] = cfg_WallpaperSource;
                batch["WallpaperWorkShopId"] = cfg_WallpaperWorkShopId;
            }
        }
        if (Object.keys(batch).length)
            globalCfg.setBatch(batch);
        syncBus.broadcastGlobalChanged();
    }

    WallpaperListModel {
        id: wpListModel
        accountCatalogProvider: steamCatalogProvider
        directoryWatcher: ProjectDirectoryWatcher {
            enabled: wpListModel.enabled
            projectDirs: wpListModel.projectDirectories
        }
        // Detection already resolved and deduplicated every library; the
        // string-built dirs are the fallback for a tree it does not know.
        workshopDirs: steamDetect.projectDirs.length
            ? steamDetect.projectDirs
            : Common.getProjectDirs(cfg_SteamLibraryPath)
        folderList: (path, options) => pyext.get_folder_list(path, options)
        globalConfigPath: steamDetect.globalConfig || Common.getGlobalConfigPath(cfg_SteamLibraryPath)
        filterStr: cfg_FilterStr
        sortMode: cfg_SortMode
        initItemOp: (item) => {
            if(!root.customConf) return;
            item.favor = root.customConf.favor.has(item.workshopid);
        }
        enabled: Boolean(cfg_SteamLibraryPath)
        readfile: pyext.readfile
    }

    Component.onDestruction: {
        if(this.pyext) this.pyext.destroy();
    }

    function saveCustomConf() {
        cfg_CustomConf = Common.prepareCustomConf(this.customConf);
    }


    Loader {
        id: followConflictLoader
        objectName: "followConflictPresentationLoader"
        Layout.fillWidth: true
        Layout.preferredHeight: item && item.visible ? item.implicitHeight : 0
        visible: item && item.visible
        source: Qt.resolvedUrl("components/ConfigConflictPresentation.qml")

        Binding {
            target: followConflictLoader.item
            property: "reconcilerState"
            value: root.followDraftState
            when: followConflictLoader.status === Loader.Ready
        }
        Binding {
            target: followConflictLoader.item
            property: "fieldLabels"
            value: ({
                FollowWallpaperEngine: qsTr("Follow Wallpaper Engine"),
                WallpaperEngineAccount: qsTr("Wallpaper Engine account"),
                FollowSourceMode: qsTr("Follow source mode"),
                FollowSourceAccountKey: qsTr("Follow account"),
                FollowSourceKey: qsTr("Follow source")
            })
            when: followConflictLoader.status === Loader.Ready
        }
    }
    Connections {
        target: followConflictLoader.item
        ignoreUnknownSignals: true
        function onChoiceRequested(key, choice) {
            root.resolveFollowConflict(key, choice);
        }
    }

    // Content
    PlasmaComponents.TabBar {
        id: bar
        implicitWidth: font.pixelSize*8 * 3
        PlasmaComponents.TabButton {
            text: "Wallpapers"
        }
        PlasmaComponents.TabButton {
            text: "Settings"
        }
        PlasmaComponents.TabButton {
            text: "About"
        }
    }

    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: bar.currentIndex

        WallpaperPage {
            id: wallpaperPage
            wallpaperListModel: wpListModel
            accountCatalogSnapshot: root.steamAccountsCatalogSnapshot
            browseScope: root.browseScopeBinding
            followSources: root.followSourceCandidates
            followSelection: root.followSelectionBinding
            followEnabled: root.followEditorValues.FollowWallpaperEngine
            followAccount: root.followEditorValues.WallpaperEngineAccount
            useFollowSourcePicker: Boolean(root.steamAccountsCatalogSnapshot)
            freshAccountSetup: false
            allowIdentifiedCurrent: false
            onBrowseScopeSelected: scope => {
                root.cfg_BrowseScope = JSON.stringify({
                    mode: scope.mode === "selectedAccounts" ? "selectedAccounts" : "allDownloaded",
                    accountKeys: root.normalizedStringList(scope.accountKeys),
                    includeSharedProjects: scope.includeSharedProjects !== false
                });
            }
            onFollowSourceSelected: binding => {
                root.stageFollowField("FollowSourceMode", binding.mode || "legacySession");
                root.stageFollowField("FollowSourceAccountKey", binding.accountKey || "");
                root.stageFollowField("FollowSourceKey", binding.sourceKey || "");
                if (binding.mode === "legacySession") {
                    const match = String(binding.sourceKey || "").match(/#section:(.*)$/);
                    if (match) {
                        try { root.stageFollowField("WallpaperEngineAccount", decodeURIComponent(match[1])); }
                        catch (reason) { /* Keep the existing legacy section on invalid input. */ }
                    }
                }
            }
            onFollowEnabledSelected: enabled =>
                root.stageFollowField("FollowWallpaperEngine", enabled)
            onFollowAccountSelected: account =>
                root.stageFollowField("WallpaperEngineAccount", account)
        }

        SettingPage {
            id: settingPage
            draftWindowPauseMode: root.draftWindowPauseMode
            draftWindowResumeDelay: root.draftWindowResumeDelay
            onWindowPauseModeSelected: mode => root.stageWindowPauseMode(mode)
            onWindowResumeDelaySelected: milliseconds => root.stageWindowResumeDelay(milliseconds)
        }

        AboutPage {}
    }


}
