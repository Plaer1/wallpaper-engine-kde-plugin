import QtQuick
import com.github.catsout.wallpaperEngineKde
import QtQuick.Window
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import "js/follow-source-state.mjs" as FollowSourceState
import "js/follow-choice-builder.mjs" as FollowChoiceBuilder
import "components" as Components

WallpaperItem {
    id: wallpaperRoot
    property string lastRuntimeContainmentContextError: ""

    function runtimeContainmentContext() {
        try {
            const containment = wallpaperRoot.parent
                ? wallpaperRoot.parent.Containment : null;
            if (!containment) throw new Error("WallpaperItem has no parent Containment attachment");
            const context = {
                wallpaperObject: wallpaperRoot,
                activeWallpaperObject: containment.wallpaperGraphicsObject,
                containmentId: String(containment.id),
                activityId: containment.activity,
                containmentType: containment.containmentType,
                screen: containment.screen,
                screenGeometry: containment.screenGeometry,
                availableScreenRegion: containment.availableScreenRegion,
                wallpaperPlugin: containment.wallpaperPlugin,
                corona: containment.corona
            };
            lastRuntimeContainmentContextError = "";
            return context;
        } catch (error) {
            // Missing Plasma containment context (including configuration previews) is
            // deliberately represented as incomplete data and cannot register.
            const message = String(error);
            if (lastRuntimeContainmentContextError !== message) {
                lastRuntimeContainmentContextError = message;
                console.warn("Wallpaper runtime containment context unavailable:", message);
            }
            return {};
        }
    }

    readonly property var runtimeContainmentContextValue: runtimeContainmentContext()

Rectangle {
    id: background
    anchors.fill: parent
    clip: true
    color: wallpaper.configuration.BackgroundColor
    
    // Steam libraries found on this machine, see FileHelper::detectSteam
    property var steamDetect: pyextObject.detect_steam(wallpaper.configuration.SteamLibraryPath)
    property string steamlibrary: {
        const path = wallpaper.configuration.SteamLibraryPath || steamDetect.library;
        return Common.localFileUrl(path);
    }
    // SceneBackend reads assets as a QUrl and QUrl::toLocalFile() is empty for
    // a scheme-less one, so the detected native path has to be resolved too.
    property string steamAssets: steamDetect.assets
        ? Common.localFileUrl(steamDetect.assets)
        : Common.getAssetsPath(steamlibrary)
    // Global Mode: shared config (wallpaper choice + randomization) lives in
    // ~/.config/wekde/global.json instead of per-screen KConfig. globalRev is
    // bumped on every global change to re-evaluate the resolver bindings below.
    property int    globalRev: 0
    function effCfg(key, fallback) {
        background.globalRev;
        return globalCfg.enabled ? globalCfg.get(key, fallback) : fallback;
    }

    function configuredBrowseScope() {
        const defaults = {mode: "allDownloaded", accountKeys: [], includeSharedProjects: true};
        const raw = wallpaper.configuration.BrowseScope;
        if (typeof raw !== "string" || !raw.trim()) return defaults;
        try {
            const saved = JSON.parse(raw);
            if (!saved || typeof saved !== "object") return defaults;
            return {
                mode: saved.mode === "selectedAccounts" ? "selectedAccounts" : "allDownloaded",
                accountKeys: Array.isArray(saved.accountKeys)
                    ? Array.from(new Set(saved.accountKeys.filter(key => typeof key === "string"))) : [],
                includeSharedProjects: saved.includeSharedProjects !== false
            };
        } catch (reason) { return defaults; }
    }

    function selectedLegacySection() {
        const configured = wallpaper.configuration.WallpaperEngineAccount;
        if (typeof configured === "string" && configured) return configured;
        const sections = engineFollower.accounts ? Array.from(engineFollower.accounts) : [];
        if (sections.includes("steamuser")) return "steamuser";
        return sections.length === 1 ? sections[0] : "steamuser";
    }

    function configuredFollowBinding() {
        const validModes = ["identifiedCurrent", "account", "legacySession", "manualMapping"];
        const savedMode = wallpaper.configuration.FollowSourceMode;
        const mode = validModes.includes(savedMode) ? savedMode : "legacySession";
        const accountKey = typeof wallpaper.configuration.FollowSourceAccountKey === "string"
            ? wallpaper.configuration.FollowSourceAccountKey : "";
        let sourceKey = typeof wallpaper.configuration.FollowSourceKey === "string"
            ? wallpaper.configuration.FollowSourceKey : "";
        if (!sourceKey && mode === "legacySession") {
            const path = background.steamDetect.configFile || "config.json";
            sourceKey = `config:${path}#section:${encodeURIComponent(selectedLegacySection())}`;
        } else if (!sourceKey && mode === "account" && accountKey.startsWith("steam:")) {
            const path = background.steamDetect.configFile || "config.json";
            sourceKey = `config:${path}#account:${accountKey.slice(6)}`;
        }
        return {mode, accountKey, sourceKey};
    }

    readonly property var browseScopeBinding: configuredBrowseScope()
    readonly property var followBinding: configuredFollowBinding()
    readonly property var manualWallpaperSelection: ({
        source: Qt.resolvedUrl(effCfg("WallpaperSource", wallpaper.configuration.WallpaperSource)).toString(),
        workshopid: String(effCfg("WallpaperWorkShopId", wallpaper.configuration.WallpaperWorkShopId) || "")
    })
    property var followDisplayState: FollowSourceState.create(
        manualWallpaperSelection, false, followBinding)
    readonly property var followDisplay: FollowSourceState.view(followDisplayState)
    readonly property var followStateInputs: ({
        manual: manualWallpaperSelection,
        enabled: engineFollower.enabled,
        binding: followBinding,
        selection: engineFollower.selection,
        provenance: engineFollower.provenance,
        status: engineFollower.status
    })

    function synchronizeFollowDisplay() {
        let state = FollowSourceState.setManualSelection(followDisplayState, manualWallpaperSelection);
        state = FollowSourceState.setEnabled(state, engineFollower.enabled);
        state = FollowSourceState.setRequestedBinding(state, followBinding);
        state = FollowSourceState.acceptFollowerResult(state, {
            selection: engineFollower.selection,
            provenance: engineFollower.provenance,
            status: engineFollower.status
        }, followBinding);
        followDisplayState = state;
    }
    onFollowStateInputsChanged: synchronizeFollowDisplay()

    WallpaperEngineFollower {
        id: engineFollower
        enabled: wallpaper.configuration.FollowWallpaperEngine
        observeAccounts: runtimeRegistration.registered && runtimeEndpoint.registered
        configPath: background.steamDetect.configFile || ""
        projectDirs: background.steamDetect.allProjectDirs
        account: wallpaper.configuration.WallpaperEngineAccount
        followMode: background.followBinding.mode
        followAccountKey: background.followBinding.accountKey
        followSourceKey: wallpaper.configuration.FollowSourceKey || ""
        currentSteamId64: ""
        currentIdentityVerified: false
    }
    property string source: followDisplay.source

    property string filterStr: effCfg("FilterStr", wallpaper.configuration.FilterStr)

    property int    videoBackend: wallpaper.configuration.VideoBackend
    property int    switchTimer: effCfg("SwitchTimer", wallpaper.configuration.SwitchTimer)
    property int    fps: wallpaper.configuration.Fps
    property bool   cacheScenePasses: wallpaper.configuration.SceneCachePasses
    property bool   shareGpuContext: wallpaper.configuration.ShareGpuContext
    property bool   mirrorScene: wallpaper.configuration.MirrorScene
    property bool   spanScreens: effCfg("SpanScreens", wallpaper.configuration.SpanScreens)

    property bool   randomizeWallpaper: !engineFollower.enabled && effCfg("RandomizeWallpaper", wallpaper.configuration.RandomizeWallpaper)
    property bool   noRandomWhilePaused: effCfg("NoRandomWhilePaused", wallpaper.configuration.NoRandomWhilePaused)
    property bool   mouseInput: wallpaper.configuration.MouseInput
    property bool   mpvStats: wallpaper.configuration.MpvStats
    property string mpvHwdec: wallpaper.configuration.MpvHwdec

    property bool   pauseOnBatPower: wallpaper.configuration.PauseOnBatPower
    property int    pauseBatPercent: wallpaper.configuration.PauseBatPercent

    
    property var curOpt: ({})
    property string workshopid: {
        const isFollowedSource = followDisplay.enabled
            && ["followed", "previouslyFollowed"].includes(followDisplay.origin);
        const wid = isFollowedSource
            ? followDisplay.workshopid
            : effCfg("WallpaperWorkShopId", wallpaper.configuration.WallpaperWorkShopId);
        pyextObject.read_wallpaper_config(wid).then((res) => this.curOpt = res);
        return wid;
    }
    function get_opt_value(key, def) {
        if(curOpt.hasOwnProperty(key))
            return curOpt[key];
        return def;
    }

    // Update all derived properties when curOpt changes
    onCurOptChanged: {
        console.log("main.qml onCurOptChanged, curOpt:", JSON.stringify(curOpt));
        wallpaperDisplayMode = get_opt_value('display_mode', wallpaper.configuration.DisplayMode);
        wallpaperMute = get_opt_value('mute_audio', wallpaper.configuration.MuteAudio);
        volume = get_opt_value('volume', wallpaper.configuration.Volume);
        speed = get_opt_value('speed', wallpaper.configuration.Speed);
        const userProps = curOpt['user_props'];
        userPropsJson = userProps ? JSON.stringify(userProps) : "";
        console.log("main.qml updated: displayMode=", displayMode, "speed=", speed, "userPropsJson=", userPropsJson);
    }

    property int wallpaperDisplayMode: get_opt_value('display_mode', wallpaper.configuration.DisplayMode)
    readonly property bool globalSizingEnabled: {
        globalRev;
        return Boolean(globalCfg.get("SizingEnabled", false));
    }
    readonly property int globalSizingMode: {
        globalRev;
        return Number(globalCfg.get("SizingMode", Common.DisplayMode.Crop)) === Common.DisplayMode.Scale
            ? Common.DisplayMode.Scale : Common.DisplayMode.Crop;
    }
    readonly property int displayMode: globalSizingEnabled ? globalSizingMode : wallpaperDisplayMode
    property bool wallpaperMute: get_opt_value('mute_audio', wallpaper.configuration.MuteAudio)
    readonly property bool globalAudioMuted: {
        globalRev;
        return Boolean(globalCfg.get("AudioDisabled", false));
    }
    readonly property bool mute: globalAudioMuted || wallpaperMute
    property int    volume: get_opt_value('volume', wallpaper.configuration.Volume)
    property real   speed: get_opt_value('speed', wallpaper.configuration.Speed)
    // User properties for scene wallpapers (JSON string format)
    property string userPropsJson: ""

    // Reactive bindings for configuration changes
    Connections {
        target: wallpaper.configuration
        function onDisplayModeChanged() {
            background.wallpaperDisplayMode = background.get_opt_value('display_mode', wallpaper.configuration.DisplayMode);
        }
        function onMuteAudioChanged() {
            background.wallpaperMute = background.get_opt_value('mute_audio', wallpaper.configuration.MuteAudio);
        }
        function onVolumeChanged() {
            background.volume = background.get_opt_value('volume', wallpaper.configuration.Volume);
        }
        function onSpeedChanged() {
            background.speed = background.get_opt_value('speed', wallpaper.configuration.Speed);
        }
    }

    property int    perOptChanged: wallpaper.configuration.PerOptChanged
    onPerOptChangedChanged: {
        console.log("main.qml onPerOptChangedChanged, perOptChanged:", perOptChanged, "workshopid:", workshopid);
        pyextObject.read_wallpaper_config(workshopid).then((res) => {
            console.log("main.qml read_wallpaper_config result:", JSON.stringify(res));
            this.curOpt = res;
        });
    }

    property bool   pauseOnGamemode: wallpaper.configuration.PauseOnGamemode

    // auto pause
    readonly property bool ok: automaticPauseResolver.canPlay

    function setRuntimePlaybackMode(mode) {
        return automaticPauseResolver.setPlaybackMode(mode);
    }

    Components.AutomaticPauseResolver {
        id: automaticPauseResolver
        windowPause: windowModel.reqPause
        fullscreenPause: windowModel.globalFullscreenPause
        batteryPause: powerSource.reqPause
        gameModePause: gamemodeLoader.item ? gamemodeLoader.item.reqPause : false
    }

    // detect TTY switch and pause wallpaper(s)
    TTYSwitchMonitor {
        id: ttyMonitor
        onTtySwitch: {
            if (sleep) {
                console.log("Preparing for sleep (possibly a VT switch)");
                this.pause();
            } else {
                console.log("Waking up (VT switch back)");
                this.play();
            }
        }
    }

    Loader {
        id: gamemodeLoader
        active: background.pauseOnGamemode

        sourceComponent: Component {
            GamemodeMonitor {}
        }
    }

    property string nowBackend: ""
    property int backendLoadGeneration: 0

    property var mouseHooker
    // The root imports this native module, so a loaded wallpaper already has it.
    readonly property bool hasLib: true

    property var customConf: Common.loadCustomConf(wallpaper.configuration.CustomConf)

    property string wallpaperPath
    property string wallpaperType

    signal sig_backendFirstFrame(string backname, int generation)
    onSig_backendFirstFrame: function(backname, generation) {
        background.onBackendFirstFrame(backname, generation);
    }
    function onBackendFirstFrame(backname, generation) {
        console.error(`backend ${backname} first frame`);
        if (wallpaper.hasOwnProperty('accentColor'))
            wallpaper.accentColorChanged();
        runtimeReloadController.observeFirstFrame(generation, backname);
    }

    function beginRuntimeReload(requestId, targetId) {
        if (!runtimeEndpoint.registered || !requestId || targetId !== runtimeEndpoint.instanceId)
            return runtimeEndpoint.completeReloadRequest(requestId, targetId, false,
                "unverifiedTarget", "Wallpaper target is no longer active");
        if (!source || !wallpaperPath || !["scene", "video", "web"].includes(wallpaperType))
            return runtimeEndpoint.completeReloadRequest(requestId, targetId, false,
                "reloadUnavailable", "The current wallpaper source cannot be reloaded");
        return runtimeReloadController.request(requestId, targetId);
    }

    function beginRuntimeNext(requestId, targetId) {
        if (!runtimeEndpoint.registered || !requestId || targetId !== runtimeEndpoint.instanceId)
            return runtimeEndpoint.completeNextRequest(requestId, targetId, false,
                "unverifiedTarget", "Wallpaper target is no longer active");
        if (engineFollower.enabled)
            return runtimeEndpoint.completeNextRequest(requestId, targetId, false,
                "followActive", "Disable Follow before selecting another wallpaper");
        // Global Mode has one shared selection, so only its existing primary
        // writer may change it.
        if (globalCfg.enabled && !syncBus.isPrimary)
            return runtimeEndpoint.completeNextRequest(requestId, targetId, false,
                "globalModeNonPrimary", "Another wallpaper screen owns the shared selection");
        return runtimeNextController.request(requestId, targetId);
    }

    function refreshRuntimeNextModel(requestId, generation) {
        const startingGeneration = wpListModel.refreshGeneration;
        wpListModel.refresh(true).then(function() {
            if (wpListModel.refreshGeneration <= startingGeneration) {
                runtimeNextController.reject(requestId, runtimeEndpoint.instanceId,
                    "catalogRefreshFailed", "The eligible wallpaper list could not be refreshed");
                return;
            }
            const candidates = [];
            for (let i = 0; i < wpListModel.model.count; ++i) {
                const item = wpListModel.model.get(i);
                candidates.push({
                    source: Common.packWallpaperSource(item),
                    workshopId: String(item.workshopid || "")
                });
            }
            runtimeNextController.modelRefreshed(requestId, generation, candidates);
        }).catch(function(reason) {
            runtimeNextController.reject(requestId, runtimeEndpoint.instanceId,
                "catalogRefreshFailed", String(reason));
        });
    }

    function applyRuntimeNextSelection(requestId, targetId, source, workshopId, globalMode) {
        if (!runtimeEndpoint.registered || targetId !== runtimeEndpoint.instanceId
                || engineFollower.enabled) {
            runtimeNextController.reject(requestId, targetId,
                engineFollower.enabled ? "followActive" : "unverifiedTarget",
                engineFollower.enabled ? "Follow became active before selection" : "Wallpaper target is no longer active");
            return;
        }
        try {
            if (globalMode) {
                if (!globalCfg.enabled || !syncBus.isPrimary) {
                    runtimeNextController.reject(requestId, targetId, "globalModeChanged",
                        "Shared wallpaper mode changed before selection");
                    return;
                }
                globalCfg.setBatch({WallpaperSource: source, WallpaperWorkShopId: workshopId});
                if (String(globalCfg.get("WallpaperSource", "")) !== source
                        || String(globalCfg.get("WallpaperWorkShopId", "")) !== workshopId) {
                    runtimeNextController.reject(requestId, targetId, "selectionWriteFailed",
                        "Shared wallpaper selection was not saved");
                    return;
                }
            } else {
                if (globalCfg.enabled) {
                    runtimeNextController.reject(requestId, targetId, "globalModeChanged",
                        "Shared wallpaper mode changed before selection");
                    return;
                }
                wallpaper.configuration.WallpaperSource = source;
                wallpaper.configuration.WallpaperWorkShopId = workshopId;
                if (String(wallpaper.configuration.WallpaperSource) !== source
                        || String(wallpaper.configuration.WallpaperWorkShopId) !== workshopId) {
                    runtimeNextController.reject(requestId, targetId, "selectionWriteFailed",
                        "Wallpaper selection was not confirmed by the configuration map");
                    return;
                }
            }
            Qt.callLater(confirmRuntimeNextSelection);
        } catch (error) {
            runtimeNextController.reject(requestId, targetId, "selectionWriteFailed", String(error));
        }
    }

    function confirmRuntimeNextSelection() {
        const globalMode = globalCfg.enabled;
        const storedSource = globalMode
            ? String(globalCfg.get("WallpaperSource", ""))
            : String(wallpaper.configuration.WallpaperSource || "");
        const storedWorkshopId = globalMode
            ? String(globalCfg.get("WallpaperWorkShopId", ""))
            : String(wallpaper.configuration.WallpaperWorkShopId || "");
        runtimeNextController.confirmSelection(background.source,
            String(background.workshopid || ""), storedSource, storedWorkshopId, globalMode);
    }

    // QtObject.destroy() deletes at the end of the event loop. Clear the target
    // before destroy(), then drop the property, or the grabber keeps forwarding.
    // Turning mouse input off also recenters. Teardown uses this drop and does not.
    function dropMouseHook() {
        if(!mouseHooker)
            return;
        mouseHooker.target = null;
        mouseHooker.destroy();
        mouseHooker = null;
    }

    Component.onDestruction: {
        dropMouseHook();
    }

    function applySource() {
        if (runtimeReloadController.pendingRequestId)
            runtimeReloadController.supersede();
        const { path, type } = Common.unpackWallpaperSource(source);
        const path_changed = background.wallpaperPath !== path;
        const type_changed = background.wallpaperType !== type;
        const is_infobackend = background.nowBackend === "InfoShow";

        if(type_changed) wallpaperType = type;
        if(path_changed) wallpaperPath = path;

        if(type_changed || is_infobackend || !source) {
            loadBackend();
        } else if(path_changed) {
            backendLoader.item.source = path;
        }

        sourceCallback();
    }

    function getWorkshopIDPath() {
        if(followDisplay.enabled && ["followed", "previouslyFollowed"].includes(followDisplay.origin)
            && followDisplay.selection.projectDir)
            return Common.localFileUrl(followDisplay.selection.projectDir);
        return Common.getWorkshopDir(this.steamlibrary) + `/${this.workshopid}`;
    }

    onMouseInputChanged: {
        if(this.mouseInput) {
            hookTimer.start();
        } else {
            dropMouseHook();
            // The scene keeps the last pointer. Centre it so camera parallax
            // eases back instead of staying at the offset where input was cut.
            if(backendLoader.item && backendLoader.item.resetPointer)
                backendLoader.item.resetPointer();
        }
    }

    Timer {
        id: hookTimer
        running: true
        repeat: false
        interval: 2000
        property int tryTimes: 0
        onTriggered: {
            tryTimes++;
            if(tryTimes >= 10 || !background.hasLib || !background.mouseInput) return;
            if(background.mouseHooker) return;
            background.hookMouse();
        }
        Component.onCompleted: {
            background.hookMouse.connect(background.hookMouseSlot);
        }
    }
    signal hookMouse
    function hookMouseSlot() {
        if(!background.doHookMouse()) {
            hookTimer.start();
        } else {
            hookTimer.tryTimes = 0;
        }
    }
    function doHookMouse() {
        if(background.Window) {
            const screenArea = Common.findItem(Window.contentItem, "MouseEventListener");
            if(screenArea === null)
                return false;
            const screenGrid = Common.findItem(screenArea, "QQuickGridView");
            if(screenGrid === null)
                return false;
            console.error(screenGrid);
            dropMouseHook();
            background.mouseHooker = Qt.createQmlObject(`import QtQuick 2.12;
                    import com.github.catsout.wallpaperEngineKde 1.2
                    MouseGrabber {
                        z: -1
                        anchors.fill: parent
                    }
            `, screenGrid);
            return true;
       }
       return false;
    }

    WindowModel {
        id: windowModel
        containmentContext: wallpaperRoot.runtimeContainmentContextValue
    }

    PowerSource {
        id: powerSource
        readonly property bool reqPause: {
            (background.pauseOnBatPower && (st_battery_state == 'NoCharge' || st_battery_state == 'Discharging')) ||
            (background.pauseBatPercent !== 0 && st_battery_has && st_battery_percent < background.pauseBatPercent)
        }
    }

    Pyext {
        id: pyextObject
    }
    SteamCatalogProvider {
        id: steamCatalogProvider
        pyext: pyextObject
        // Real wallpaper targets need the verified catalog for tray Follow
        // choices; previews stay idle because their registration is rejected.
        enabled: runtimeEndpoint.registered
            || (runtimeRegistration.registered && background.randomizeWallpaper)
    }
    WallpaperListModel {
        id: wpListModel
        accountCatalogProvider: steamCatalogProvider
        browseScope: background.browseScopeBinding
        directoryWatcher: ProjectDirectoryWatcher {
            enabled: background.randomizeWallpaper
            projectDirs: wpListModel.projectDirectories
        }
        enabled: background.randomizeWallpaper
        // Detection already resolved and deduplicated every library; the
        // string-built dirs are the fallback for a tree it does not know.
        workshopDirs: background.steamDetect.projectDirs.length
            ? background.steamDetect.projectDirs
            : Common.getProjectDirs(background.steamlibrary)
        folderList: (path, options) => pyextObject.get_folder_list(path, options)
        globalConfigPath: background.steamDetect.globalConfig
            || Common.getGlobalConfigPath(background.steamlibrary)
        filterStr: background.filterStr
        initItemOp: (item) => {
            if(!background.customConf) return;
            item.favor = background.customConf.favor.has(item.workshopid);
        }
        readfile: pyextObject.readfile

        function applyWallpaperBySource(source, workshopid) {
            wallpaper.configuration.WallpaperWorkShopId = workshopid;
            wallpaper.configuration.WallpaperSource = source;
        }
        function changeWallpaper(index) {
            if(this.model.count === 0) return;
            const model = this.model.get(index);
            this.applyWallpaperBySource(Common.packWallpaperSource(model), model.workshopid);
        }
    }

    GlobalConfig {
        id: globalCfg
    }
    Connections {
        target: globalCfg
        function onChanged(key, value) { background.globalRev++; }
        function onEnabledChanged()    { background.globalRev++; }
    }
    // wakes this renderer when the config dialog (possibly another process)
    // writes the global config
    WallpaperSyncBus {
        id: syncBus
        onGlobalConfigChanged: globalCfg.reload()
    }
    Timer {
        id: randomizeTimer
        running: background.randomizeWallpaper
        interval: background.switchTimer * 1000 * 60
        repeat: true
        onTriggered: {
            if(background.noRandomWhilePaused && !background.ok) return;
            // in Global Mode only the primary screen picks; the choice is
            // shared via the global config so all screens apply the same one
            if(globalCfg.enabled && !syncBus.isPrimary) return;
            if(wpListModel.model.count === 0) return;

            const i = Math.floor(Math.random() * wpListModel.model.count);
            const model = wpListModel.model.get(i);
            const source = Common.packWallpaperSource(model);
            if(globalCfg.enabled)
                globalCfg.setBatch({ "WallpaperSource": source, "WallpaperWorkShopId": model.workshopid });
            else
                wpListModel.applyWallpaperBySource(source, model.workshopid);
        }
    }

    // lauch pause time to avoid freezing
    Timer {
        id: lauchPauseTimer
        running: false
        repeat: false
        interval: 300
        onTriggered: {
            background.autoPause();
            playTimer.start();
        }
    }
    Timer{
        id: playTimer
        running: false
        repeat: false
        interval: 5000
        onTriggered: { background.autoPause(); }
    }
    // lauch pause end

    // As always autoplay for refresh lastframe, sourceChange need autoPause
    // need a time for delay, which is needed for refresh
    function sourceCallback() {
        sourcePauseTimer.start();   
    }
    Timer {
        id: sourcePauseTimer
        running: false
        repeat: false
        interval: 200
        onTriggered: background.autoPause();
    }
    // main  
    Item { 
        id: backendLoader
        // Qt screen geometry and item geometry both use device-independent pixels.
        readonly property rect desktop: Common.desktopGeometry(Qt.application.screens)
        readonly property bool spanning: background.spanScreens && desktop.width > 0 && desktop.height > 0
        x: spanning ? desktop.x - Screen.virtualX : 0
        y: spanning ? desktop.y - Screen.virtualY : 0
        width: spanning ? desktop.width : parent.width
        height: spanning ? desktop.height : parent.height
        property var item: null
        onItemChanged: background.autoPause()

        // Scene framebuffers take their size at creation; wait for geometry to settle.
        onWidthChanged: sceneResizeTimer.restart()
        onHeightChanged: sceneResizeTimer.restart()
        Timer {
            id: sceneResizeTimer
            interval: 100
            onTriggered: {
                if(backendLoader.item && background.wallpaperType === 'scene')
                    background.loadBackend();
            }
        }

        signal loaded

        Component.onCompleted: {
            if(background.hasLib) {
                this.loaded.connect(this.changeMouseTarget);
                background.mouseHookerChanged.connect(this.changeMouseTarget);
            }
        }
        Component.onDestruction: {
            if(this.item) this.item.destroy();
        }
        function load(url, properties) {
            const com = Qt.createComponent(url);
            if(com.status === Component.Ready) {
                if(this.item) this.item.destroy(100);
                this.item = null;
                try {
                    this.item = com.createObject(this, properties);
                } catch(e) {
                    this.loadInfoShow(e);
                    return;
                }
                this.loaded();
            } else if(com.status == Component.Error) {
                this.loadInfoShow(com.errorString());
            }
        }
        function loadInfoShow(info) {
            this.load("backend/InfoShow.qml", {
                wid: background.workshopid,
                type: background.wallpaperType,
                info: info
            });
        }
        function changeMouseTarget() {
           if(backendLoader.item && background.mouseHooker) {
                let re = backendLoader.item.getMouseTarget();
                if(!re)
                    re = null;
                background.mouseHooker.target = re;
           }
        }
    }

    function loadBackend(forRuntimeReload, requestedGeneration) {
        if (runtimeReloadController.pendingRequestId && forRuntimeReload !== true)
            runtimeReloadController.supersede();
        if (forRuntimeReload === true) {
            background.backendLoadGeneration = requestedGeneration;
        } else {
            background.backendLoadGeneration++;
            runtimeReloadController.generation = background.backendLoadGeneration;
        }
        let qmlsource = "";
        let properties = {};

    
        // check source
        if(!background.source) {
            backendLoader.loadInfoShow("Source is empty. The config may be broken.");
            return;
        }
        // choose backend
        switch (background.wallpaperType) {
            case 'video':
                if(background.videoBackend == Common.VideoBackend.Mpv && background.hasLib)
                    qmlsource = "backend/Mpv.qml";
                else qmlsource = "backend/QtMultimedia.qml";
                properties = {};
                break;
            case 'web':
                qmlsource = "backend/QtWebView.qml";
                properties = {readfile: pyextObject.readfile};
                break;
            case 'scene':
                if(background.hasLib) {
                    qmlsource = "backend/Scene.qml";
                    properties = {"assets": background.steamAssets};
                } else {
                    backendLoader.loadInfoShow("Plugin lib not found. To support scene, please compile and install it.");
                    return; 
                }
                break;
            default:
                backendLoader.loadInfoShow("Not supported wallpaper type");
                return; 
        }
        properties['source'] = background.wallpaperPath;
        properties['reloadGeneration'] = background.backendLoadGeneration;
        console.error("load backend: "+qmlsource);
        backendLoader.load(qmlsource, properties);
        sourceCallback();
    }
   
    function autoPause() {
        const backend = backendLoader.item;
        const reasons = automaticPauseResolver.pauseReasons;
        if (!backend) {
            if (runtimeEndpoint.registered)
                runtimeEndpoint.reportPlaybackState("loading", reasons);
            return;
        }

        if (background.ok) {
            if (typeof backend.play === "function") {
                backend.play();
                if (runtimeEndpoint.registered)
                    runtimeEndpoint.reportPlaybackState("playing", []);
            } else if (runtimeEndpoint.registered) {
                runtimeEndpoint.reportPlaybackState("unknown", reasons);
            }
        } else if (typeof backend.pause === "function") {
            backend.pause();
            if (runtimeEndpoint.registered)
                runtimeEndpoint.reportPlaybackState("paused", reasons);
        } else if (runtimeEndpoint.registered) {
            runtimeEndpoint.reportPlaybackState("unknown", reasons);
        }
    }

    Component.onCompleted: {
        synchronizeFollowDisplay();
        // load first backend
        applySource();

        // background signal connect
        background.videoBackendChanged.connect(loadBackend);
        background.okChanged.connect(autoPause);
        background.sourceChanged.connect(applySource);

        lauchPauseTimer.start();
    }
}

WallpaperRuntimeRegistration {
    id: runtimeRegistration
    targetItem: wallpaperRoot
    containmentContext: wallpaperRoot.runtimeContainmentContextValue
    property string lastRejectedReason: ""

    onRegistrationRejected: function(reason) {
        if (lastRejectedReason === reason) return;
        lastRejectedReason = reason;

        const context = wallpaperRoot.runtimeContainmentContextValue || ({});
        const region = context.availableScreenRegion;
        console.warn("Wallpaper runtime registration rejected:", reason,
            JSON.stringify({
                wallpaperMatchesRoot: context.wallpaperObject === wallpaperRoot,
                activeWallpaperMatchesRoot: context.activeWallpaperObject === wallpaperRoot,
                wallpaperPlugin: String(context.wallpaperPlugin),
                containmentType: String(context.containmentType),
                screen: String(context.screen),
                activityId: String(context.activityId),
                containmentId: String(context.containmentId),
                screenGeometry: String(context.screenGeometry),
                availableScreenRegionType: typeof region,
                availableScreenRegionIsArray: Array.isArray(region),
                availableScreenRegionLength: region && typeof region.length === "number"
                    ? region.length : -1
            }));
    }

    onRegistrationChanged: {
        if (runtimeRegistration.registered) {
            lastRejectedReason = "";
            console.info("Wallpaper runtime registration accepted:",
                runtimeRegistration.containmentId, runtimeRegistration.activityId,
                runtimeRegistration.outputName);
        }
    }

    Component.onCompleted: {
        const context = wallpaperRoot.runtimeContainmentContextValue || ({});
        const region = context.availableScreenRegion;
        console.info("Wallpaper runtime registration startup context:",
            JSON.stringify({
                wallpaperMatchesRoot: context.wallpaperObject === wallpaperRoot,
                activeWallpaperMatchesRoot: context.activeWallpaperObject === wallpaperRoot,
                wallpaperPlugin: String(context.wallpaperPlugin),
                containmentType: String(context.containmentType),
                screen: String(context.screen),
                activityId: String(context.activityId),
                containmentId: String(context.containmentId),
                screenGeometry: String(context.screenGeometry),
                availableScreenRegionType: typeof region,
                availableScreenRegionIsArray: Array.isArray(region),
                availableScreenRegionLength: region && typeof region.length === "number"
                    ? region.length : -1
            }));
        Qt.callLater(function() {
            runtimeRegistration.refresh();
            console.info("Wallpaper runtime registration deferred state:",
                runtimeRegistration.registered, runtimeRegistration.containmentId,
                runtimeRegistration.activityId, runtimeRegistration.outputName);
        });
    }
}

WallpaperRuntimeEndpoint {
    id: runtimeEndpoint
    registration: runtimeRegistration
    wallpaperSource: background.source
    workshopId: background.workshopid
    backend: background.wallpaperType
    onEndpointRejected: function(reason) {
        console.warn("Wallpaper runtime endpoint rejected:", reason,
            JSON.stringify({
                registrationActive: runtimeRegistration.registered,
                containmentId: runtimeRegistration.containmentId,
                activityId: runtimeRegistration.activityId,
                outputName: runtimeRegistration.outputName
            }));
    }
    onRegisteredChanged: {
        if (runtimeEndpoint.registered) {
            console.info("Wallpaper runtime endpoint registered:",
                runtimeEndpoint.instanceId, runtimeRegistration.containmentId,
                runtimeRegistration.outputName);
        }
    }
    followState: ({
        enabled: Boolean(wallpaper.configuration.FollowWallpaperEngine),
        mode: background.followBinding.mode,
        accountKey: background.followBinding.accountKey,
        sourceKey: background.followBinding.sourceKey,
        status: wallpaper.configuration.FollowWallpaperEngine
            ? (engineFollower.status || "Waiting for Wallpaper Engine source resolution.")
            : "disabled",
        provenance: JSON.stringify(Object.assign({}, background.followDisplay.provenance || ({}), {
            origin: background.followDisplay.origin
        })),
        choices: FollowChoiceBuilder.buildFollowChoices(
            steamCatalogProvider.catalogSnapshot,
            engineFollower.accounts,
            background.steamDetect.configFile || "",
            background.followBinding)
    })
}

Components.RuntimeFollowAssignment {
    id: runtimeFollowAssignment
    configuration: wallpaper.configuration
    targetVerified: runtimeRegistration.registered && runtimeEndpoint.registered
    configurationPreview: false
    instanceId: runtimeEndpoint.instanceId
    configPath: background.steamDetect.configFile || ""
    currentIdentityVerified: false
    currentSteamId64: ""
}

Components.RuntimeReloadController {
    id: runtimeReloadController
    targetActive: runtimeRegistration.registered && runtimeEndpoint.registered
    instanceId: runtimeEndpoint.instanceId
    generation: background.backendLoadGeneration
}

Components.RuntimeNextController {
    id: runtimeNextController
    targetActive: runtimeRegistration.registered && runtimeEndpoint.registered
    instanceId: runtimeEndpoint.instanceId
    followEnabled: engineFollower.enabled
    globalMode: globalCfg.enabled
    currentSource: background.source
    currentWorkshopId: String(background.workshopid || "")
}

Connections {
    target: runtimeEndpoint
    function onEndpointRejected(reason) {
        console.warn("Wallpaper runtime endpoint rejected:", reason,
            JSON.stringify({
                registrationActive: runtimeRegistration.registered,
                containmentId: runtimeRegistration.containmentId,
                activityId: runtimeRegistration.activityId,
                outputName: runtimeRegistration.outputName
            }));
    }
    function onRegisteredChanged() {
        if (runtimeEndpoint.registered) {
            console.info("Wallpaper runtime endpoint registered:",
                runtimeEndpoint.instanceId, runtimeRegistration.containmentId,
                runtimeRegistration.outputName);
            background.autoPause();
        }
    }
    function onPlaybackRequested(mode) {
        background.setRuntimePlaybackMode(mode)
        background.autoPause()
    }
    function onFollowRequested(requestId, instanceId, args) {
        runtimeFollowAssignment.apply(requestId, instanceId, args)
    }
    function onReloadRequested(requestId, instanceId) {
        background.beginRuntimeReload(requestId, instanceId)
    }
    function onNextRequested(requestId, instanceId) {
        background.beginRuntimeNext(requestId, instanceId)
    }
    Component.onCompleted: {
        runtimeEndpoint.playbackRequestConnected = true
        runtimeEndpoint.followRequestConnected = true
        runtimeEndpoint.reloadRequestConnected = true
        runtimeEndpoint.nextRequestConnected = true
    }
    Component.onDestruction: {
        runtimeEndpoint.playbackRequestConnected = false
        runtimeEndpoint.followRequestConnected = false
        runtimeEndpoint.reloadRequestConnected = false
        runtimeEndpoint.nextRequestConnected = false
    }
}

Connections {
    target: automaticPauseResolver
    function onPauseReasonsChanged() { background.autoPause(); }
}

Connections {
    target: runtimeReloadController
    function onReloadBackendRequested(generation) {
        background.loadBackend(true, generation)
    }
}

Connections {
    target: runtimeFollowAssignment
    function onCompleted(requestId, instanceId, applied, enabled, mode, accountKey,
                         sourceKey, errorCode, message) {
        runtimeEndpoint.completeFollowRequest(requestId, instanceId, applied, enabled,
            mode, accountKey, sourceKey, errorCode, message)
    }
}

Connections {
    target: runtimeReloadController
    function onCompleted(requestId, instanceId, applied, errorCode, message) {
        runtimeEndpoint.completeReloadRequest(requestId, instanceId, applied, errorCode, message)
    }
}

Connections {
    target: runtimeNextController
    function onRefreshRequested(requestId, generation) {
        background.refreshRuntimeNextModel(requestId, generation)
    }
    function onSelectionRequested(requestId, targetId, source, workshopId, globalMode) {
        background.applyRuntimeNextSelection(requestId, targetId, source, workshopId, globalMode)
    }
    function onCompleted(requestId, targetId, applied, errorCode, message) {
        runtimeEndpoint.completeNextRequest(requestId, targetId, applied, errorCode, message)
    }
}

Connections {
    target: background
    function onSourceChanged() { background.confirmRuntimeNextSelection() }
    function onWorkshopidChanged() { background.confirmRuntimeNextSelection() }
}

Connections {
    target: globalCfg
    function onChanged(key) {
        if (key === "WallpaperSource" || key === "WallpaperWorkShopId")
            background.confirmRuntimeNextSelection()
    }
}

Connections {
    target: wallpaper.configuration
    ignoreUnknownSignals: true
    function onWallpaperSourceChanged() { background.confirmRuntimeNextSelection() }
    function onWallpaperWorkShopIdChanged() { background.confirmRuntimeNextSelection() }
}
}
