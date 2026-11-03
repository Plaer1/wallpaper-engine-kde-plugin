import QtQuick
import QtTest
import "../../plugin/contents/ui" as Wallpaper

TestCase {
    id: testCase
    name: "SteamCatalogProvider"

    readonly property string workshopRoot: "/tmp/provider-workshop/content/431960"
    property var projectEntries: []
    property var metadata: ({})
    property int folderReads: 0
    property int metadataReads: 0
    property var fakeSource: pyext

    QtObject {
        id: pyext
        property int snapshotRequests: 0
        property int rescanRequests: 0
        property var initialSnapshot: ({
            schemaVersion: 1, generation: 0, installations: [], accounts: [], caches: []
        })
        signal steamAccountsCatalogChanged(var catalog)
        function get_folder_list(path, options) {
            testCase.folderReads++;
            return Promise.resolve({
                folder: path,
                items: path === testCase.workshopRoot
                    ? testCase.projectEntries.map(name => ({name, mtime: 1})) : []
            });
        }
        function steam_accounts_catalog() {
            snapshotRequests++;
            return initialSnapshot;
        }
        function rescan_steam_accounts() { rescanRequests++; }
        function publish(catalog) { steamAccountsCatalogChanged(catalog); }
    }

    Component {
        id: providerComponent
        Wallpaper.SteamCatalogProvider { pyext: testCase.fakeSource }
    }
    Component {
        id: emptyProviderComponent
        Wallpaper.SteamCatalogProvider { }
    }
    Component {
        id: watcherComponent
        QtObject { signal changed() }
    }
    Component {
        id: modelComponent
        Wallpaper.WallpaperListModel {
            enabled: false
            workshopDirs: [testCase.workshopRoot]
            globalConfigPath: "/tmp/provider-global.json"
            folderList: (path, options) => pyext.get_folder_list(path, options)
            readfile: path => {
                testCase.metadataReads++;
                if (path === "/tmp/provider-global.json") return Promise.resolve("{}");
                const id = path.split("/").slice(-2)[0];
                return Promise.resolve(JSON.stringify(testCase.metadata[id] || {}));
            }
        }
    }
    Component {
        id: spyComponent
        SignalSpy { }
    }

    function init() {
        projectEntries = ["1", "2"];
        metadata = {
            1: {title: "One", type: "scene", file: "one.json", contentrating: "Everyone", tags: []},
            2: {title: "Two", type: "scene", file: "two.json", contentrating: "Everyone", tags: []}
        };
        folderReads = 0;
        metadataReads = 0;
        pyext.snapshotRequests = 0;
        pyext.rescanRequests = 0;
        pyext.initialSnapshot = {
            schemaVersion: 1, generation: 0, installations: [], accounts: [], caches: []
        };
    }

    function catalogWithOneSubscribedProject(generation) {
        return {
            schemaVersion: 1,
            generation,
            installations: [],
            accounts: [{key: "steam:test", steamId64: "76561197960265729", label: "Test", identityStatus: "verified"}],
            caches: [{key: "cache:test", accountKey: "steam:test", state: "valid",
                subscribedIds: ["1"], disabledIds: []}]
        };
    }

    function makeLoadedModel(provider) {
        const watcher = createTemporaryObject(watcherComponent, testCase);
        const list = createTemporaryObject(modelComponent, testCase, {
            directoryWatcher: watcher,
            accountCatalogProvider: provider
        });
        verify(list);
        list.enabled = true;
        tryCompare(list.model, "count", 2);
        return list;
    }

    function test_lazyInitialLoadSignalAndCachedProjection() {
        const provider = createTemporaryObject(providerComponent, testCase);
        verify(provider);
        const changed = createTemporaryObject(spyComponent, testCase, {
            target: provider, signalName: "catalogChanged"
        });
        verify(changed);
        wait(40);
        compare(pyext.snapshotRequests, 0);
        compare(pyext.rescanRequests, 0);
        compare(provider.status, "disabled");
        compare(provider.rescan(), false); // disabled explicit refresh does not start a scan
        compare(pyext.snapshotRequests, 0);

        const list = makeLoadedModel(provider);
        const readsBeforeCatalog = metadataReads;
        const foldersBeforeCatalog = folderReads;
        const revisionBeforeCatalog = list.membershipRevision;
        list.browseScope = {mode: "selectedAccounts", accountKeys: ["steam:test"], includeSharedProjects: true};

        provider.enabled = true;
        compare(pyext.snapshotRequests, 1);
        compare(provider.loading, true);
        compare(provider.ready, false);
        compare(provider.unavailable, false);
        compare(provider.snapshot().generation, 0);

        compare(provider.rescan(), true);
        compare(pyext.snapshotRequests, 1); // request is only made once per Pyext instance
        compare(pyext.rescanRequests, 1);
        pyext.publish(catalogWithOneSubscribedProject(2));
        tryCompare(provider, "status", "ready");
        tryCompare(changed, "count", 2); // initial generation and published snapshot
        verify(list.membershipRevision >= revisionBeforeCatalog + 2);
        compare(provider.snapshot().generation, 2);
        compare(folderReads, foldersBeforeCatalog);
        compare(metadataReads, readsBeforeCatalog); // membership refresh reused loaded metadata
    }

    function test_invalidSnapshotReportsUnavailableWithoutDiscardingLastGood() {
        const provider = createTemporaryObject(providerComponent, testCase);
        provider.enabled = true;
        compare(provider.status, "loading");
        pyext.publish(catalogWithOneSubscribedProject(1));
        tryCompare(provider, "status", "ready");
        const lastGood = provider.snapshot();
        pyext.publish({schemaVersion: 2, generation: 3, installations: [], accounts: [], caches: []});
        compare(provider.status, "unavailable");
        compare(provider.ready, false);
        compare(provider.loading, false);
        compare(provider.snapshot().generation, lastGood.generation);
    }

    function test_missingPyextIsUnavailable() {
        const provider = createTemporaryObject(emptyProviderComponent, testCase);
        verify(provider);
        provider.enabled = true;
        compare(provider.unavailable, true);
        compare(provider.snapshot(), null);
    }
}
