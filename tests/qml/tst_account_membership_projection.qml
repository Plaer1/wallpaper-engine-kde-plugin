import QtQuick
import QtTest
import "../../plugin/contents/ui" as Wallpaper

TestCase {
    id: testCase
    name: "AccountMembershipProjection"

    readonly property string workshopRoot: "/tmp/steamapps/workshop/content/431960"
    readonly property string localRoot: "/tmp/steamapps/common/wallpaper_engine/projects/defaultprojects"
    property var projectEntries: []
    property var projectMetadata: ({})
    property int folderReads: 0
    property int metadataReads: 0

    QtObject {
        id: pyext
        function get_folder_list(path, options) {
            testCase.folderReads++;
            const items = path === testCase.workshopRoot ? testCase.projectEntries
                : path === testCase.localRoot ? ["42"] : [];
            return Promise.resolve({folder: path, items: items.map(name => ({name, mtime: 1}))});
        }
    }
    Component {
        id: watcherComponent
        QtObject { signal changed() }
    }
    Component {
        id: providerComponent
        QtObject {
            property var value: null
            signal catalogChanged()
            function snapshot() { return value; }
        }
    }
    Component {
        id: modelComponent
        Wallpaper.WallpaperListModel {
            enabled: false
            workshopDirs: [testCase.workshopRoot, testCase.localRoot]
            globalConfigPath: "/tmp/account-projection-global.json"
            folderList: (path, options) => pyext.get_folder_list(path, options)
            readfile: path => {
                testCase.metadataReads++;
                if (path === "/tmp/account-projection-global.json") return Promise.resolve("{}");
                const id = path.split("/").slice(-2)[0];
                return Promise.resolve(JSON.stringify(testCase.projectMetadata[id] || {}));
            }
        }
    }

    function project(title) {
        return {title, type: "scene", file: "scene.json", contentrating: "Everyone", tags: []};
    }
    function cache(key, accountKey, subscribedIds, disabledIds, state) {
        return {key, accountKey, subscribedIds, disabledIds, state};
    }
    function catalog(caches, accountKeys) {
        return {
            schemaVersion: 1,
            generation: 1,
            installations: [],
            accounts: accountKeys.map(key => ({key, label: key, cacheKeys: []})),
            caches
        };
    }
    function fixtureCatalog() {
        return catalog([
            cache("A", "steam:A", ["1", "2"], [], "valid"),
        cache("A-second", "steam:A", ["2"], [], "valid"),
            cache("B", "steam:B", ["2", "3"], ["2"], "valid")
        ], ["steam:A", "steam:B"]);
    }
    function init() {
        projectEntries = ["1", "2", "3", "4"];
        projectMetadata = {1: project("One"), 2: project("Two"), 3: project("Three"),
            4: project("Unknown owner"), 42: project("Local numeric folder")};
        folderReads = 0;
        metadataReads = 0;
    }
    function makeModel(snapshot) {
        const watcher = createTemporaryObject(watcherComponent, this);
        const provider = createTemporaryObject(providerComponent, this, {value: snapshot || fixtureCatalog()});
        const list = createTemporaryObject(modelComponent, this, {
            directoryWatcher: watcher,
            accountCatalogProvider: provider
        });
        verify(list);
        list.enabled = true;
        tryCompare(list.model, "count", 5);
        return {list, watcher, provider};
    }
    function ids(list) {
        const result = [];
        for (let i = 0; i < list.model.count; i++) result.push(list.model.get(i).workshopid);
        return result.sort();
    }
    function setScope(list, mode, accountKeys, includeSharedProjects) {
        list.browseScope = {mode, accountKeys, includeSharedProjects};
    }
    function publish(provider, value) {
        provider.value = value;
        provider.catalogChanged();
    }

    function test_individualUnionAndCachedProjection() {
        const fixture = makeModel();
        const list = fixture.list;
        const reads = folderReads;
        const metadata = metadataReads;
        compare(ids(list), ["1", "2", "3", "4", "42"]); // All downloaded default
        compare(list.browseProjectionStatus, "allDownloaded");

        setScope(list, "selectedAccounts", ["steam:A"], true);
        tryCompare(list.model, "count", 3);
        compare(ids(list), ["1", "2", "42"]);
        compare(list.model.get(1).subscriptionAccountKeys, "steam:A");
        compare(list.model.get(2).sourceKind, "shared");
        compare(list.model.get(2).projectIdentity, "local:/tmp/steamapps/common/wallpaper_engine/projects/defaultprojects/42");
        compare(list.model.get(2).publishedFileId, "");
        compare(list.browseProjectionStatus, "available");

        setScope(list, "selectedAccounts", ["steam:B"], true);
        tryCompare(list.model, "count", 2);
        compare(ids(list), ["3", "42"]); // B disables 2

        setScope(list, "selectedAccounts", ["steam:A", "steam:B"], true);
        tryCompare(list.model, "count", 4);
        compare(ids(list), ["1", "2", "3", "42"]); // A keeps 2 enabled
        const idTwo = list.model.get(1);
        compare(idTwo.workshopid, "2");
        compare(idTwo.subscriptionAccountKeys, "steam:A");
        compare(idTwo.subscriptionCacheKeys, "A,A-second");

        setScope(list, "selectedAccounts", ["steam:A", "steam:B"], false);
        tryCompare(list.model, "count", 3);
        compare(ids(list), ["1", "2", "3"]);
        compare(folderReads, reads);
        compare(metadataReads, metadata); // Scope changes reuse loaded project metadata.
        compare(list.filterStr, ""); // Existing content filters are not rewritten.
    }

    function test_emptyUnavailableStaleAndValidEmptyNeverBroaden() {
        const fixture = makeModel();
        const list = fixture.list;
        const reads = folderReads;
        const metadata = metadataReads;

        setScope(list, "selectedAccounts", [], true);
        tryCompare(list.model, "count", 1);
        compare(ids(list), ["42"]); // Only the independently enabled shared/local source.
        compare(list.browseProjectionStatus, "emptySelection");
        setScope(list, "selectedAccounts", [], false);
        tryCompare(list.model, "count", 0);
        compare(list.browseProjectionStatus, "emptySelection");

        setScope(list, "selectedAccounts", ["steam:missing"], true);
        tryCompare(list.model, "count", 1);
        compare(ids(list), ["42"]);
        compare(list.browseProjectionStatus, "selectedAccountsUnavailable");

        publish(fixture.provider, catalog([
            cache("A-offline", "steam:A", ["1"], [], "missing")
        ], ["steam:A"]));
        setScope(list, "selectedAccounts", ["steam:A"], true);
        tryCompare(list.model, "count", 2);
        compare(ids(list), ["1", "42"]); // Last-good membership remains usable and visibly stale.
        compare(list.browseProjectionStatus, "stale");

        publish(fixture.provider, catalog([
            cache("A-empty", "steam:A", [], [], "empty")
        ], ["steam:A"]));
        tryCompare(list.model, "count", 1);
        compare(ids(list), ["42"]); // Valid empty replaces prior membership.
        compare(list.browseProjectionStatus, "available");

        publish(fixture.provider, catalog([
            cache("A-unavailable", "steam:A", [], [], "unavailable")
        ], ["steam:A"]));
        tryCompare(list.model, "count", 1);
        compare(ids(list), ["42"]);
        compare(list.browseProjectionStatus, "subscriptionDataUnavailable");

        list.accountCatalogProvider = null;
        setScope(list, "selectedAccounts", ["steam:A"], true);
        tryCompare(list.model, "count", 1);
        compare(ids(list), ["42"]);
        compare(list.browseProjectionStatus, "catalogUnavailable");
        compare(folderReads, reads);
        compare(metadataReads, metadata);
    }

    function test_catalogSignalReprojectsWithoutReplacingFilters() {
        const fixture = makeModel();
        const list = fixture.list;
        const contentFilter = Wallpaper.Common.filterModel.map(filter =>
            filter.type === "type" && filter.key === "scene" ? 0 : filter.def).join("");
        list.filterStr = contentFilter;
        tryCompare(list.model, "count", 0);
        const reads = folderReads;
        const metadata = metadataReads;
        publish(fixture.provider, catalog([
            cache("A", "steam:A", ["4"], [], "valid")
        ], ["steam:A"]));
        compare(list.filterStr, contentFilter);
        compare(list.model.count, 0); // Catalog changes do not reset intentional content filters.
        compare(folderReads, reads);
        compare(metadataReads, metadata);
    }

    function test_localNumericFolderDoesNotImplyWorkshopOwnership() {
        const fixture = makeModel();
        const local = fixture.list.model.get(4);
        compare(local.workshopid, "42");
        compare(local.sourceKind, "shared");
        compare(local.publishedFileId, "");
        compare(local.projectIdentity,
            "local:/tmp/steamapps/common/wallpaper_engine/projects/defaultprojects/42");
        setScope(fixture.list, "selectedAccounts", ["steam:A"], false);
        tryCompare(fixture.list.model, "count", 2);
        verify(!ids(fixture.list).includes("42"));
    }
}
