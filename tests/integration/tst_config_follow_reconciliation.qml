import QtQuick
import QtTest
import "../../plugin/contents/ui/js/config-draft-reconciler.mjs" as Draft
import com.github.catsout.wallpaperEngineKde

TestCase {
    id: testCase
    name: "ConfigFollowReconciliation"
    when: windowShown
    visible: true
    width: 1180
    height: 820

    readonly property var followFields: [
        "FollowWallpaperEngine", "WallpaperEngineAccount", "FollowSourceMode",
        "FollowSourceAccountKey", "FollowSourceKey"
    ]
    property var fakeCatalog: ({
        schemaVersion: 1,
        generation: 1,
        installations: [],
        accounts: [
            {key: "steam:76561198000000001", steamId64: "76561198000000001",
             accountId32: "400000001", label: "Account A", identityStatus: "verified", cacheKeys: []},
            {key: "steam:76561198000000002", steamId64: "76561198000000002",
             accountId32: "400000002", label: "Account B", identityStatus: "verified", cacheKeys: []}
        ],
        caches: []
    })
    property var fakeMap: null
    GlobalConfig { id: sharedGlobalConfig }

    Component {
        id: mapComponent
        QtObject {
            property var mapValues: ({
                FollowWallpaperEngine: false,
                WallpaperEngineAccount: "steamuser",
                BrowseScope: "{\"mode\":\"allDownloaded\",\"accountKeys\":[],\"includeSharedProjects\":true}",
                FollowSourceMode: "legacySession",
                FollowSourceAccountKey: "",
                FollowSourceKey: "config:/tmp/steam-fixture/loginusers.vdf#section:steamuser",
                Fps: 30
            })

            // Mirrors the same-process QML assignment signal of the shared
            // QQmlPropertyMap. The real map event has a separate probe recorded
            // in config-notifications.md.
            signal valueChanged(string key, var value)
            function value(key) { return mapValues[key]; }
            function qmlWrite(key, value) {
                const next = Object.assign({}, mapValues);
                next[key] = value;
                mapValues = next;
                valueChanged(key, value);
            }
            function snapshot() { return Object.assign({}, mapValues); }
        }
    }
    Component {
        id: fakePyextComponent
        QtObject {
            function readfile(path) { return Promise.resolve(""); }
            function get_dir_size(path, depth) { return Promise.resolve(0); }
            function get_folder_list(path, options) { return Promise.resolve([]); }
            function read_wallpaper_config(id) { return Promise.resolve({}); }
            function write_wallpaper_config(id, changed) { return Promise.resolve(null); }
            function reset_wallpaper_config(id) { return Promise.resolve(null); }
            function steam_accounts_catalog() { return testCase.fakeCatalog; }
            function rescan_steam_accounts() { return true; }
            signal steamAccountsCatalogChanged(var catalog)
        }
    }
    Component {
        id: signalSpyComponent
        SignalSpy { }
    }
    Component {
        id: activationHostComponent
        QtObject {
            property var wallpaperConfiguration: ({
                WallpaperSource: "host-old",
                WallpaperWorkShopId: "host-old-id",
                FollowWallpaperEngine: true
            })
            property int applyCount: 0
            property var appliedSnapshot: null
            function applyWallpaper() {
                applyCount++;
                appliedSnapshot = Object.assign({}, wallpaperConfiguration);
            }
        }
    }

    function init() {
        fakeMap = createTemporaryObject(mapComponent, testCase);
        verify(fakeMap);
        sharedGlobalConfig.enabled = false;
        sharedGlobalConfig.setBatch({
            WallpaperSource: "", WallpaperWorkShopId: "",
            WindowPauseMode: 6, WindowResumeDelay: 300
        });
    }

    function findMethodTarget(object, methodName, visited) {
        if (!object || visited.has(object)) return null;
        visited.add(object);
        if (typeof object[methodName] === "function") return object;
        const collections = [object.data, object.children];
        for (let c = 0; c < collections.length; ++c) {
            const children = collections[c];
            if (!children) continue;
            for (let i = 0; i < children.length; ++i) {
                const match = findMethodTarget(children[i], methodName, visited);
                if (match) return match;
            }
        }
        return null;
    }

    function waitForConflictLoader(config) {
        const loader = findChild(config, "followConflictPresentationLoader");
        verify(loader);
        tryCompare(loader, "status", Loader.Ready);
    }

    function createActualConfig(initialOverrides) {
        const component = Qt.createComponent("../../plugin/contents/ui/config.qml");
        compare(component.status, Component.Ready, component.errorString());
        const properties = {
            width: testCase.width,
            height: testCase.height,
            configDialog: null,
            wallpaperConfiguration: fakeMap,
            steamDetect: {
                libraries: [], selectedLibrary: "", projectDirs: [], allProjectDirs: [],
                configFile: "/tmp/steam-fixture/loginusers.vdf", globalConfig: ""
            },
            cfg_FollowWallpaperEngine: fakeMap.value("FollowWallpaperEngine"),
            cfg_WallpaperEngineAccount: fakeMap.value("WallpaperEngineAccount"),
            cfg_BrowseScope: fakeMap.value("BrowseScope"),
            cfg_FollowSourceMode: fakeMap.value("FollowSourceMode"),
            cfg_FollowSourceAccountKey: fakeMap.value("FollowSourceAccountKey"),
            cfg_FollowSourceKey: fakeMap.value("FollowSourceKey"),
            cfg_Fps: fakeMap.value("Fps"),
            cfg_GlobalMode: false,
            cfg_SteamLibraryPath: "",
            cfg_WallpaperWorkShopId: "",
            cfg_WallpaperSource: "",
            cfg_FilterStr: "",
            cfg_SortMode: 0,
            cfg_CustomConf: "eyJmYXZvciI6W119"
        };
        Object.assign(properties, initialOverrides || {});
        const config = component.createObject(testCase, properties);
        verify(config);
        config.pyext = fakePyextComponent.createObject(testCase);
        waitForConflictLoader(config);
        return config;
    }

    function hostApply(config) {
        config.saveConfig();
        for (const key of followFields)
            fakeMap.qmlWrite(key, config["cfg_" + key]);
        fakeMap.qmlWrite("Fps", config.cfg_Fps);
    }

    function hostCopyValues() {
        return fakeMap.snapshot();
    }

    function test_immediateBrowserActivationUsesHostMapAndLiveSharedMode() {
        sharedGlobalConfig.enabled = true;
        sharedGlobalConfig.setBatch({WallpaperSource: "shared-old", WallpaperWorkShopId: "old-id"});

        const host = createTemporaryObject(activationHostComponent, testCase);
        verify(host && host.wallpaperConfiguration);
        const config = createActualConfig({
            configDialog: host,
            wallpaperConfiguration: fakeMap,
            cfg_GlobalMode: false, // Pending checkbox differs from the live shared mode.
            cfg_FollowWallpaperEngine: true,
            cfg_WallpaperEngineAccount: "steamuser",
            cfg_WallpaperSource: "draft-old",
            cfg_WallpaperWorkShopId: "draft-old-id",
            cfg_Fps: 47
        });
        try {
            config.cfg_GlobalMode = false;
            config.stageFollowField("WallpaperEngineAccount", "unsaved-account-draft");
            config.stageWindowPauseMode(3);
            const result = config.applyWallpaperImmediately({
                path: "/tmp/browser-picked", file: "selected.pkg", type: "scene", workshopid: "765"
            });

            verify(result.applied, result.message || "Immediate Apply returned no success");
            compare(host.applyCount, 1);
            compare(host.appliedSnapshot.WallpaperSource, "/tmp/browser-picked/selected.pkg+scene");
            compare(host.appliedSnapshot.WallpaperWorkShopId, "765");
            compare(host.appliedSnapshot.FollowWallpaperEngine, false);
            compare(host.wallpaperConfiguration.WallpaperSource, "/tmp/browser-picked/selected.pkg+scene");
            compare(fakeMap.value("WallpaperSource"), undefined); // Host map is not the root map.
            compare(sharedGlobalConfig.get("WallpaperSource", ""), "/tmp/browser-picked/selected.pkg+scene");
            compare(sharedGlobalConfig.get("WallpaperWorkShopId", ""), "765");
            compare(config.cfg_GlobalMode, false);
            compare(config.cfg_Fps, 47);
            compare(config.draftWindowPauseMode, 3);
            compare(config.followEditorValues.WallpaperEngineAccount, "unsaved-account-draft");
            compare(config.followEditorValues.FollowWallpaperEngine, false);
            compare(config.initialWallpaperSource, "/tmp/browser-picked/selected.pkg+scene");
            compare(config.initialWallpaperWorkShopId, "765");
        } finally {
            config.destroy();
            host.destroy();
        }
    }

    function test_immediateActivationUsesLiveLocalModeAndUnavailableHostIsNoOp() {
        sharedGlobalConfig.enabled = false;
        sharedGlobalConfig.setBatch({WallpaperSource: "shared-keep", WallpaperWorkShopId: "keep-id"});
        const host = createTemporaryObject(activationHostComponent, testCase);
        verify(host && host.wallpaperConfiguration);
        let config = createActualConfig({
            configDialog: host,
            cfg_GlobalMode: true, // Pending checkbox differs from live local mode.
            cfg_WallpaperSource: "draft-old",
            cfg_WallpaperWorkShopId: "draft-id"
        });
        try {
            config.cfg_GlobalMode = true;
            const result = config.applyWallpaperImmediately({
                path: "/tmp/local-picked", file: "local.pkg", type: "scene", workshopid: "900"
            });
            verify(result.applied, result.message || "Immediate Apply returned no success");
            compare(host.applyCount, 1);
            compare(host.wallpaperConfiguration.WallpaperSource, "/tmp/local-picked/local.pkg+scene");
            compare(sharedGlobalConfig.get("WallpaperSource", ""), "shared-keep");
            compare(sharedGlobalConfig.get("WallpaperWorkShopId", ""), "keep-id");
        } finally {
            config.destroy();
            host.destroy();
        }

        sharedGlobalConfig.enabled = true;
        const unavailable = createActualConfig({
            configDialog: null, cfg_GlobalMode: false,
            cfg_WallpaperSource: "draft-before", cfg_WallpaperWorkShopId: "draft-before-id"
        });
        try {
            const beforeSource = sharedGlobalConfig.get("WallpaperSource", "");
            const beforeId = sharedGlobalConfig.get("WallpaperWorkShopId", "");
            const result = unavailable.applyWallpaperImmediately({
                path: "/tmp/no-host", file: "no.pkg", type: "scene", workshopid: "901"
            });
            verify(!result.applied);
            compare(sharedGlobalConfig.get("WallpaperSource", ""), beforeSource);
            compare(sharedGlobalConfig.get("WallpaperWorkShopId", ""), beforeId);
            compare(unavailable.cfg_WallpaperSource, "draft-before");
            compare(unavailable.cfg_WallpaperWorkShopId, "draft-before-id");
        } finally {
            unavailable.destroy();
        }
    }

    function test_windowPolicyDraftUsesSharedValuesAndWaitsForApply() {
        const originalMode = sharedGlobalConfig.get("WindowPauseMode", 6);
        const originalDelay = sharedGlobalConfig.get("WindowResumeDelay", 300);
        const originalGlobalMode = sharedGlobalConfig.enabled;
        const changedSpy = createTemporaryObject(signalSpyComponent, testCase, {
            target: sharedGlobalConfig, signalName: "changed"
        });
        verify(changedSpy);
        let cancelled = null;
        let config = null;
        try {
            sharedGlobalConfig.setBatch({WindowPauseMode: 6, WindowResumeDelay: 300});
            const legacyLocalSettings = {
                cfg_PauseMode: 0, cfg_ResumeTime: 9000,
                cfg_PauseFilterByScreen: false, cfg_GlobalMode: false
            };
            cancelled = createActualConfig(legacyLocalSettings);
            compare(cancelled.draftWindowPauseMode, 6);
            compare(cancelled.draftWindowResumeDelay, 300);
            compare(cancelled.cfg_PauseMode, 0);
            compare(cancelled.cfg_ResumeTime, 9000);
            compare(cancelled.cfg_PauseFilterByScreen, false);
            verify(findChild(cancelled, "windowPauseModeCombo"));
            verify(findChild(cancelled, "windowResumeDelaySpin"));
            compare(findChild(cancelled, "pauseFilterDescription"), null);

            const cancelMode = findChild(cancelled, "windowPauseModeCombo");
            cancelMode.activated(1); // Focused window.
            const cancelDelay = findChild(cancelled, "windowResumeDelaySpin");
            cancelDelay.value = 950;
            cancelDelay.valueModified();
            compare(cancelled.draftWindowPauseMode, 3);
            compare(cancelled.draftWindowResumeDelay, 950);
            compare(sharedGlobalConfig.get("WindowPauseMode", 6), 6);
            compare(sharedGlobalConfig.get("WindowResumeDelay", 300), 300);
            cancelled.destroy();
            cancelled = null;
            wait(50);
            compare(sharedGlobalConfig.get("WindowPauseMode", 6), 6);
            compare(sharedGlobalConfig.get("WindowResumeDelay", 300), 300);

            config = createActualConfig(legacyLocalSettings);
            compare(config.draftWindowPauseMode, 6);
            compare(config.draftWindowResumeDelay, 300);
            const pauseCombo = findChild(config, "windowPauseModeCombo");
            const resumeDelay = findChild(config, "windowResumeDelaySpin");
            verify(pauseCombo && resumeDelay);
            changedSpy.clear();
            pauseCombo.activated(1);
            resumeDelay.value = 750;
            resumeDelay.valueModified();
            compare(sharedGlobalConfig.get("WindowPauseMode", 6), 6);
            compare(sharedGlobalConfig.get("WindowResumeDelay", 300), 300);

            config.saveConfig();

            compare(sharedGlobalConfig.get("WindowPauseMode", 6), 3);
            compare(sharedGlobalConfig.get("WindowResumeDelay", 300), 750);
            compare(changedSpy.count, 2);
            const changedKeys = changedSpy.signalArguments.map(args => args[0]).sort();
            compare(changedKeys, ["WindowPauseMode", "WindowResumeDelay"]);
            compare(config.cfg_PauseMode, 0); // Legacy local value did not seed or override shared policy.
            compare(config.cfg_ResumeTime, 9000);
            compare(config.cfg_PauseFilterByScreen, false);
            config.destroy();
            config = null;

            const reopened = createActualConfig(legacyLocalSettings);
            try {
                compare(reopened.draftWindowPauseMode, 3);
                compare(reopened.draftWindowResumeDelay, 750);
            } finally {
                reopened.destroy();
                wait(50);
            }
        } finally {
            if (cancelled) cancelled.destroy();
            if (config) config.destroy();
            sharedGlobalConfig.setBatch({
                WindowPauseMode: originalMode,
                WindowResumeDelay: originalDelay
            });
            sharedGlobalConfig.enabled = originalGlobalMode;
        }
    }

    function test_legacyFullscreenModeNormalizesOnSaveNotOnOpen() {
        const originalMode = sharedGlobalConfig.get("WindowPauseMode", 6);
        const originalDelay = sharedGlobalConfig.get("WindowResumeDelay", 300);
        const originalGlobalMode = sharedGlobalConfig.enabled;
        const legacyLocalSettings = {
            cfg_PauseMode: 0, cfg_ResumeTime: 9000,
            cfg_PauseFilterByScreen: false, cfg_GlobalMode: false
        };
        let config = null;
        try {
            sharedGlobalConfig.setBatch({WindowPauseMode: 5, WindowResumeDelay: 300});
            config = createActualConfig(legacyLocalSettings);
            compare(config.draftWindowPauseMode, 0);
            compare(findChild(config, "windowPauseModeCombo").currentIndex, 5);
            compare(sharedGlobalConfig.get("WindowPauseMode", 6), 5);
            config.destroy(); // Opening and cancelling never writes shared state.
            config = null;
            wait(50);
            compare(sharedGlobalConfig.get("WindowPauseMode", 6), 5);

            config = createActualConfig(legacyLocalSettings);
            compare(config.draftWindowPauseMode, 0);
            config.saveConfig(); // Legacy FullScreen=5 is saved as Fullscreen only=0.
            compare(sharedGlobalConfig.get("WindowPauseMode", 6), 0);
        } finally {
            if (config) config.destroy();
            sharedGlobalConfig.setBatch({
                WindowPauseMode: originalMode,
                WindowResumeDelay: originalDelay
            });
            sharedGlobalConfig.enabled = originalGlobalMode;
        }
    }

    function test_invalidSharedPauseValuesUseSafeDraftUntilApply() {
        const originalMode = sharedGlobalConfig.get("WindowPauseMode", 6);
        const originalDelay = sharedGlobalConfig.get("WindowResumeDelay", 300);
        const originalGlobalMode = sharedGlobalConfig.enabled;
        let config = null;
        try {
            sharedGlobalConfig.setBatch({WindowPauseMode: "not-a-mode", WindowResumeDelay: "not-a-delay"});
            config = createActualConfig({cfg_PauseMode: 0, cfg_ResumeTime: 9000, cfg_GlobalMode: false});
            compare(config.draftWindowPauseMode, 6);
            compare(config.draftWindowResumeDelay, 300);
            compare(sharedGlobalConfig.get("WindowPauseMode", 6), "not-a-mode");
            compare(sharedGlobalConfig.get("WindowResumeDelay", 300), "not-a-delay");

            config.saveConfig();

            compare(sharedGlobalConfig.get("WindowPauseMode", 6), 6);
            compare(sharedGlobalConfig.get("WindowResumeDelay", 300), 300);
            config.destroy();
            config = null;

            sharedGlobalConfig.setBatch({WindowPauseMode: 6, WindowResumeDelay: 90000});
            config = createActualConfig({cfg_PauseMode: 0, cfg_ResumeTime: 9000, cfg_GlobalMode: false});
            compare(config.draftWindowResumeDelay, 60000);
            config.saveConfig();
            compare(sharedGlobalConfig.get("WindowResumeDelay", 300), 60000);
        } finally {
            if (config) config.destroy();
            sharedGlobalConfig.setBatch({
                WindowPauseMode: originalMode,
                WindowResumeDelay: originalDelay
            });
            sharedGlobalConfig.enabled = originalGlobalMode;
        }
    }

    function test_sameProcessFollowToggleSurvivesUnrelatedFpsApply() {
        const config = createActualConfig();
        const before = hostCopyValues();
        try {
            fakeMap.qmlWrite("FollowWallpaperEngine", true);
            tryVerify(() => config.followDraftState.live.FollowWallpaperEngine === true);
            compare(config.followDraftState.draft.FollowWallpaperEngine, true);
            compare(config.cfg_FollowWallpaperEngine, true);

            config.cfg_Fps = 60;
            hostApply(config);

            compare(fakeMap.value("FollowWallpaperEngine"), true);
            compare(fakeMap.value("Fps"), 60);
            compare(fakeMap.value("WallpaperEngineAccount"), before.WallpaperEngineAccount);
        } finally {
            config.destroy();
            wait(50);
        }
    }

    function test_emptyLegacyFollowFieldsInitializeWithoutBindingLoop() {
        fakeMap.qmlWrite("WallpaperEngineAccount", "");
        fakeMap.qmlWrite("FollowSourceMode", "");
        fakeMap.qmlWrite("FollowSourceAccountKey", "");
        fakeMap.qmlWrite("FollowSourceKey", "");
        const config = createActualConfig();
        try {
            compare(config.followSelectionBinding.mode, "legacySession");
            compare(config.followSelectionBinding.sourceKey,
                "config:/tmp/steam-fixture/loginusers.vdf#section:steamuser");
        } finally {
            config.destroy();
            wait(50);
        }
    }

    function test_sameKeyConflictApplyUseMyEditSecondConflictAndKeepLive() {
        const config = createActualConfig();
        try {
            const page = findMethodTarget(config, "acceptFollowSource", new Set());
            verify(page);
            page.acceptFollowSource({
                mode: "legacySession", accountKey: "",
                sourceKey: "config:/tmp/steam-fixture/loginusers.vdf#section:steamB"
            });
            compare(config.followEditorValues.WallpaperEngineAccount, "steamB");
            compare(config.followSelectionBinding.sourceKey,
                "config:/tmp/steam-fixture/loginusers.vdf#section:steamB");

            fakeMap.qmlWrite("WallpaperEngineAccount", "steamC");
            const conflict = Draft.conflict(config.followDraftState, "WallpaperEngineAccount");
            compare(conflict, {baseline: "steamuser", live: "steamC", draft: "steamB"});
            compare(config.cfg_WallpaperEngineAccount, "steamC");
            tryVerify(() => findChild(config, "use-my-edit-WallpaperEngineAccount") !== null);

            hostApply(config); // Plasma blindly copies cfg_, which is the live value here.
            compare(fakeMap.value("WallpaperEngineAccount"), "steamC");
            compare(Draft.conflict(config.followDraftState, "WallpaperEngineAccount").live, "steamC");

            const useMine = findChild(config, "use-my-edit-WallpaperEngineAccount");
            verify(useMine);
            useMine.forceActiveFocus();
            keyClick(Qt.Key_Space);
            compare(Draft.conflict(config.followDraftState, "WallpaperEngineAccount"), null);
            compare(config.followDraftState.baseline.WallpaperEngineAccount, "steamC");
            compare(config.followDraftState.draft.WallpaperEngineAccount, "steamB");
            compare(config.cfg_WallpaperEngineAccount, "steamB");
            compare(fakeMap.value("WallpaperEngineAccount"), "steamC");

            fakeMap.qmlWrite("WallpaperEngineAccount", "steamD");
            compare(Draft.conflict(config.followDraftState, "WallpaperEngineAccount"),
                {baseline: "steamC", live: "steamD", draft: "steamB"});
            compare(config.cfg_WallpaperEngineAccount, "steamD");

            const keepLive = findChild(config, "keep-live-WallpaperEngineAccount");
            verify(keepLive);
            keepLive.forceActiveFocus();
            keyClick(Qt.Key_Space);
            compare(Draft.conflict(config.followDraftState, "WallpaperEngineAccount"), null);
            compare(config.followDraftState.draft.WallpaperEngineAccount, "steamD");
            compare(config.cfg_WallpaperEngineAccount, "steamD");
        } finally {
            config.destroy();
            wait(50);
        }
    }

    function test_temporaryConstructionAndCancelDoNotWriteFollowFields() {
        const before = JSON.stringify(hostCopyValues());
        const component = Qt.createComponent("../../plugin/contents/ui/config.qml");
        compare(component.status, Component.Ready, component.errorString());
        const temporary = component.createObject(testCase, {
            configDialog: null,
            wallpaperConfiguration: fakeMap,
            steamDetect: {
                libraries: [], selectedLibrary: "", projectDirs: [], allProjectDirs: [],
                configFile: "/tmp/steam-fixture/loginusers.vdf", globalConfig: ""
            }
        });
        verify(temporary);
        waitForConflictLoader(temporary);
        compare(JSON.stringify(hostCopyValues()), before);
        temporary.destroy();
        wait(50);

        const config = createActualConfig();
        try {
            const page = findMethodTarget(config, "acceptFollowSource", new Set());
            verify(page);
            page.acceptFollowSource({
                mode: "legacySession", accountKey: "",
                sourceKey: "config:/tmp/steam-fixture/loginusers.vdf#section:steamB"
            });
            compare(config.cfg_WallpaperEngineAccount, "steamB");
            compare(JSON.stringify(hostCopyValues()), before);
        } finally {
            config.destroy(); // Cancel: no host save/copy occurs.
            wait(50);
        }
        compare(JSON.stringify(hostCopyValues()), before);
    }

    function test_confirmedManualMappingStagesDraftAndCancelDoesNotWriteLiveMap() {
        const before = JSON.stringify(hostCopyValues());
        const fixtureUrl = Qt.resolvedUrl("../fixtures/we_follow_manual_mapping_sections.json").toString();
        const fixturePath = decodeURIComponent(fixtureUrl.slice(7));
        const config = createActualConfig({steamDetect: {
            libraries: [], selectedLibrary: "", projectDirs: [], allProjectDirs: [],
            configFile: fixturePath, globalConfig: ""
        }});
        try {
            config.catalogRequested = true;
            tryVerify(() => config.steamAccountsCatalogSnapshot
                && config.steamAccountsCatalogSnapshot.accounts.length === 2);
            config.stageFollowField("FollowWallpaperEngine", true);
            tryVerify(() => config.followSourceCandidates.some(candidate =>
                candidate.mode === "legacySession" && candidate.associable === true
                && candidate.sourceKey.endsWith("#section:steamB")));
            const browserSettingsButton = findChild(config, "browserSettingsButton");
            verify(browserSettingsButton);
            browserSettingsButton.clicked();
            const picker = findChild(config, "followSourcePicker");
            verify(picker && picker.visible);
            verify(picker.choices.some(choice => choice.binding.mode === "legacySession"));
            verify(!picker.choices.some(choice => choice.binding.mode === "manualMapping"));

            const accountCombo = findChild(picker, "manualMappingAccountCombo");
            const sectionCombo = findChild(picker, "manualMappingSectionCombo");
            const accountIndex = picker.associationAccounts.findIndex(
                account => account.key === "steam:76561198000000002");
            verify(accountIndex >= 0);
            const sectionIndex = picker.associationSections.findIndex(
                section => section.sourceKey.endsWith("#section:steamB"));
            verify(sectionIndex >= 0);
            accountCombo.activated(accountIndex);
            sectionCombo.activated(sectionIndex);
            compare(config.followDraftState.draft.FollowSourceMode, "legacySession");
            const confirmButton = findChild(picker, "manualMappingConfirmButton");
            verify(confirmButton.enabled);
            mouseClick(confirmButton);
            const dialog = findChild(picker, "manualMappingConfirmationDialog");
            verify(dialog && dialog.visible);
            verify(dialog.contentItem.text.includes("does not change Steam"));
            dialog.accept();

            tryVerify(() => config.followDraftState.draft.FollowSourceMode === "manualMapping");
            compare(config.followDraftState.draft.FollowSourceAccountKey, "steam:76561198000000002");
            compare(config.followDraftState.draft.FollowSourceKey,
                `config:${fixturePath}#section:steamB`);
            tryVerify(() => config.followSourceCandidates.some(candidate =>
                candidate.mode === "manualMapping" && candidate.userConfirmed === true
                && candidate.accountKey === "steam:76561198000000002"));
            compare(config.cfg_FollowSourceMode, "manualMapping");
            compare(fakeMap.value("FollowSourceMode"), "legacySession");
            compare(fakeMap.value("FollowSourceAccountKey"), "");
            compare(fakeMap.value("FollowSourceKey"),
                "config:/tmp/steam-fixture/loginusers.vdf#section:steamuser");
        } finally {
            config.destroy(); // Cancel: host never calls saveConfig()/copies cfg_ exports.
            wait(50);
        }
        compare(JSON.stringify(hostCopyValues()), before);
    }
}
