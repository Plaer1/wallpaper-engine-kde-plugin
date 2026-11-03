import QtQuick
import QtTest

TestCase {
    id: testCase
    name: "ConfigBrowseScopeBridge"
    when: windowShown
    visible: true
    width: 1180
    height: 820

    property var fakeCatalog: ({
        schemaVersion: 1,
        generation: 1,
        installations: [],
        accounts: [{
            key: "steam:76561198000000001",
            steamId64: "76561198000000001",
            accountId32: "400000001",
            label: "Fixture account",
            identityStatus: "verified",
            cacheKeys: []
        }],
        caches: []
    })
    property var wallpaperConfiguration: ({
        BrowseScope: JSON.stringify({
            mode: "allDownloaded", accountKeys: [], includeSharedProjects: true
        }),
        FollowWallpaperEngine: false,
        WallpaperEngineAccount: "steamuser",
        FollowSourceMode: "legacySession",
        FollowSourceAccountKey: "",
        FollowSourceKey: "config:fixture#section:steamuser"
    })

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

    function createActualConfig() {
        const component = Qt.createComponent("../../plugin/contents/ui/config.qml");
        compare(component.status, Component.Ready, component.errorString());
        const config = component.createObject(testCase, {
            width: testCase.width,
            height: testCase.height,
            configDialog: null,
            wallpaperConfiguration: testCase.wallpaperConfiguration,
            steamDetect: {
                libraries: [], selectedLibrary: "", projectDirs: [], allProjectDirs: [],
                configFile: "/tmp/steam-fixture/config/loginusers.vdf", globalConfig: ""
            },
            cfg_FollowWallpaperEngine: false,
            cfg_WallpaperEngineAccount: "steamuser",
            cfg_BrowseScope: wallpaperConfiguration.BrowseScope,
            cfg_FollowSourceMode: "legacySession",
            cfg_FollowSourceAccountKey: "",
            cfg_FollowSourceKey: "config:/tmp/steam-fixture/config/loginusers.vdf#section:steamuser",
            cfg_SteamLibraryPath: "",
            cfg_WallpaperWorkShopId: "",
            cfg_WallpaperSource: "",
            cfg_FilterStr: "",
            cfg_SortMode: 0,
            cfg_CustomConf: "eyJmYXZvciI6W119"
        });
        verify(config);
        config.pyext = fakePyextComponent.createObject(testCase);
        config.configDialog = testCase;
        config.catalogRequested = true;
        return config;
    }

    function hostApply(config) {
        config.saveConfig();
        wallpaperConfiguration.BrowseScope = config.cfg_BrowseScope;
    }

    function test_keyboardBrowseSelectionSurvivesPageSignalAndApply() {
        const config = createActualConfig();
        verify(config.pyext);
        try {
            tryVerify(() => config.steamAccountsCatalogSnapshot
                && config.steamAccountsCatalogSnapshot.accounts.length === 1);
            const picker = findChild(config, "browseAccountPicker");
            const modeOption = findChild(config, "selectedAccountsOption");
            const account = findChild(config, "account-steam:76561198000000001");
            verify(picker && modeOption && account);

            const pickerSpy = createTemporaryObject(signalSpyComponent, testCase, {
                target: picker, signalName: "browseScopeSelected"
            });
            const wallpaperPage = findMethodTarget(config, "acceptBrowseScope", new Set());
            verify(wallpaperPage);
            const pageSpy = createTemporaryObject(signalSpyComponent, testCase, {
                target: wallpaperPage, signalName: "browseScopeSelected"
            });
            verify(pickerSpy && pageSpy);

            modeOption.forceActiveFocus();
            keyClick(Qt.Key_Space);
            tryVerify(() => JSON.parse(config.cfg_BrowseScope).mode === "selectedAccounts");
            pickerSpy.clear();
            pageSpy.clear();

            account.forceActiveFocus();
            keyClick(Qt.Key_Space);
            tryCompare(pickerSpy, "count", 1);
            tryCompare(pageSpy, "count", 1);
            compare(pickerSpy.signalArguments[0][1], ["steam:76561198000000001"]);
            compare(pageSpy.signalArguments[0][0].accountKeys, ["steam:76561198000000001"]);
            tryVerify(() => JSON.parse(config.cfg_BrowseScope).accountKeys.length === 1);

            pickerSpy.clear();
            account.forceActiveFocus();
            keyClick(Qt.Key_Space);
            tryCompare(pickerSpy, "count", 1);
            tryVerify(() => JSON.parse(config.cfg_BrowseScope).accountKeys.length === 0);
            compare(JSON.parse(config.cfg_BrowseScope).mode, "selectedAccounts");

            account.forceActiveFocus();
            keyClick(Qt.Key_Space);
            tryVerify(() => JSON.parse(config.cfg_BrowseScope).accountKeys.length === 1);
            hostApply(config);
            compare(JSON.parse(wallpaperConfiguration.BrowseScope).accountKeys,
                ["steam:76561198000000001"]);
        } finally {
            config.destroy();
        }
    }
}
