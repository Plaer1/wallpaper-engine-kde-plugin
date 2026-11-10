import QtQuick
import QtTest
import "../../plugin/contents/ui" as Wallpaper

TestCase {
    id: testCase
    name: "WallpaperListRefresh"
    property var entries: []
    property var metadata: ({})
    property int folderReads: 0
    property var holdFolder: null

    QtObject {
        id: pyext
        function get_folder_list(path, options) {
            testCase.folderReads++;
            const snapshot = { folder: path, items: testCase.entries.map(name => ({name: name, mtime: 1})) };
            if(testCase.holdFolder) return testCase.holdFolder(snapshot);
            return Promise.resolve(snapshot);
        }
    }
    Component {
        id: watcherComponent
        QtObject { signal changed() }
    }
    Component {
        id: modelComponent
        Wallpaper.WallpaperListModel {
            enabled: false
            workshopDirs: ["/tmp/workshop"]
            globalConfigPath: "/tmp/config.json"
            folderList: (path, options) => pyext.get_folder_list(path, options)
            readfile: path => Promise.resolve(path === "/tmp/config.json" ? "{}"
                : JSON.stringify(testCase.metadata[path.split('/').slice(-2)[0]] || {}))
        }
    }

    function scene(title) {
        return {title: title, type: "scene", file: "scene.json", contentrating: "Everyone", tags: ["Landscape"]};
    }
    function init() {
        entries = ["old"];
        metadata = {old: scene("Existing wallpaper")};
        folderReads = 0;
        holdFolder = null;
    }
    function makeModel(workshopDirs) {
        const watcher = createTemporaryObject(watcherComponent, this);
        const list = createTemporaryObject(modelComponent, this, {
            directoryWatcher: watcher,
            workshopDirs: workshopDirs || ["/tmp/workshop"]
        });
        verify(list);
        list.enabled = true;
        return {list: list, watcher: watcher};
    }
    function test_downloadBeforeMetadata() {
        const fixture = makeModel();
        const list = fixture.list;
        tryCompare(list.model, "count", 1);
        entries = ["old", "3269762902", "3793620838"];
        // Existing populated lists used to remain unchanged after Steam downloads.
        wait(30);
        compare(list.model.count, 1);
        fixture.watcher.changed();
        tryCompare(list, "countNoFilter", 3);
        compare(list.model.count, 1); // directories arrived before project.json
        metadata["3269762902"] = scene("Alpine Village 21:9 w lofi");
        metadata["3793620838"] = scene("In the Mountains 21:9 w lofi");
        fixture.watcher.changed();
        tryCompare(list.model, "count", 3);
        compare(list.model.get(0).title, "Alpine Village 21:9 w lofi");
        compare(list.model.get(1).title, "In the Mountains 21:9 w lofi");
        list.filterStr = Wallpaper.Common.filterModel.map(filter =>
            filter.type === "type" && filter.key === "scene" ? 0 : filter.def).join('');
        tryCompare(list.model, "count", 0);
        fixture.watcher.changed();
        wait(30);
        compare(list.model.count, 0); // automatic refresh preserves intentional filters
    }
    function test_changesDuringScanCoalesce() {
        let release = null;
        holdFolder = snapshot => new Promise(resolve => {
            release = () => { holdFolder = null; resolve(snapshot); };
        });
        const fixture = makeModel();
        tryVerify(() => release !== null);
        compare(folderReads, 1);
        entries = ["old", "new"];
        metadata.new = scene("New wallpaper");
        fixture.watcher.changed();
        fixture.watcher.changed();
        fixture.list.refresh();
        compare(folderReads, 1);
        release();
        tryCompare(fixture.list.model, "count", 2);
        compare(folderReads, 2); // current scan plus a single queued rescan
        compare(fixture.list.model.get(0).title, "New wallpaper");
        wait(30);
        compare(folderReads, 2); // no recurring rescans
    }
    function test_projectDirectoryChangesRefresh() {
        const fixture = makeModel();
        tryCompare(fixture.list.model, "count", 1);
        fixture.list.workshopDirs = ["file:///tmp/another%20library"];
        tryVerify(() => fixture.list.model.count === 1
            && fixture.list.model.get(0).path === "file:///tmp/another%20library/old");
        compare(folderReads, 2);
    }
    function test_browserFiltersUseCachedMetadataAndKeepTagCatalogUnfiltered() {
        entries = ["100", "200", "300", "400"];
        metadata = {
            "100": {title: "Aurora Galaxy", type: "scene", contentrating: "Everyone", tags: ["Landscape", "Sci-Fi"]},
            "200": {title: "Forest Dawn", type: "scene", contentrating: "Everyone", tags: ["Landscape", "Nature"]},
            "300": {title: "Meme Garden", type: "scene", contentrating: "Everyone", tags: ["Meme", "Nature"]},
            "400": {type: "scene", contentrating: "Everyone"}
        };
        const fixture = makeModel(["/tmp/steamapps/workshop/content/431960"]);
        const list = fixture.list;
        tryCompare(list.model, "count", 4);
        compare(list.browserTagCatalog, ["Landscape", "Meme", "Nature", "Sci-Fi"]);
        const reads = folderReads;
        const legacyFilter = list.filterStr;

        list.browserFilters = {
            query: "aurora 100",
            includedTags: ["landscape"],
            excludedTags: ["Meme"],
            sourceKind: "workshop"
        };
        tryCompare(list.model, "count", 1);
        compare(list.model.get(0).publishedFileId, "100");
        compare(list.filterStr, legacyFilter);
        compare(list.browserTagCatalog, ["Landscape", "Meme", "Nature", "Sci-Fi"]);
        compare(folderReads, reads);

        list.browserFilters = {includedTags: ["Landscape", "Nature"]};
        tryCompare(list.model, "count", 1);
        compare(list.model.get(0).publishedFileId, "200");
        compare(folderReads, reads);

        list.browserFilters = {sourceKind: "shared"};
        tryCompare(list.model, "count", 0);
        compare(folderReads, reads);

        list.browserFilters = {includedTags: ["not in catalog"]};
        tryCompare(list.model, "count", 0);
        list.browserFilters = {};
        tryCompare(list.model, "count", 4); // Missing metadata stays visible without an active filter.

        list.browserFilters = {query: "   ", includedTags: [42], sourceKind: "unknown"};
        tryCompare(list.model, "count", 4); // malformed/unknown values safely normalize to no extra filter
        compare(folderReads, reads);
    }
    function expectResolved(promise) {
        let result = "pending";
        promise.then(() => { result = "resolved"; }, reason => { result = "rejected: " + reason; });
        tryVerify(() => result !== "pending");
        compare(result, "resolved");
    }
    function test_destroyBeforeDeferredScan() {
        const list = createTemporaryObject(modelComponent, this);
        list.enabled = true;
        const pending = list.refresh();
        list.destroy();
        expectResolved(pending);
        compare(folderReads, 0);
    }
    function test_destroyDuringFolderScan() {
        let release = null;
        holdFolder = snapshot => new Promise(resolve => {
            release = () => resolve(snapshot);
        });
        const fixture = makeModel();
        const pending = fixture.list.refresh();
        tryVerify(() => release !== null);
        fixture.list.destroy();
        wait(0);
        release();
        expectResolved(pending);
        compare(folderReads, 1); // no queued rescan after owner destruction
    }
    function test_destroyDuringMetadataRead() {
        const list = createTemporaryObject(modelComponent, this);
        let release = null;
        list.readfile = path => path === "/tmp/config.json" ? Promise.resolve("{}")
            : new Promise(resolve => { release = () => resolve(JSON.stringify(scene("Late metadata"))); });
        list.enabled = true;
        const pending = list.refresh();
        tryVerify(() => release !== null);
        list.destroy();
        wait(0);
        release();
        expectResolved(pending);
        compare(folderReads, 1);
    }
    function test_destroyWhileWaitingForPlaylistLock() {
        const filters = Wallpaper.Common.filterModel;
        const lock = filters.lock;
        const protectedName = "other-model-playlist";
        filters.append({type: "playlist", key: protectedName, text: protectedName, def: 0});
        const list = createTemporaryObject(modelComponent, this);
        list.readfile = () => Promise.resolve(JSON.stringify({steamuser: {general: {playlists: [
            {name: "late-model-playlist", items: []}
        ]}}}));
        let ownedLock = false;
        lock.lock().then(() => { ownedLock = true; });
        tryVerify(() => ownedLock);
        const pending = list.loadPlaylists();
        tryVerify(() => lock.lock_resolvers.length === 1);
        list.destroy();
        wait(0);
        lock.release();
        expectResolved(pending);
        compare(lock.locked, false);
        compare(lock.lock_resolvers.length, 0);
        let preserved = false;
        for(let i = filters.count - 1; i >= 0; --i) {
            if(filters.get(i).key === protectedName) {
                preserved = true;
                filters.remove(i);
            }
            else if(filters.get(i).key === "late-model-playlist") filters.remove(i);
        }
        verify(preserved, "deleted model must not mutate another model's playlist filters");
    }
    function test_disabledAndNormalizedPaths() {
        const fixture = makeModel();
        tryCompare(fixture.list.model, "count", 1);
        fixture.list.enabled = false;
        const reads = folderReads;
        fixture.watcher.changed();
        wait(30);
        compare(folderReads, reads);
        fixture.list.workshopDirs = [
            ["file:///tmp/workshop%20space", "file:///tmp/Workshop%20space"],
            "/tmp/defaultprojects"
        ];
        compare(fixture.list.projectDirectories, ["/tmp/workshop space", "/tmp/Workshop space", "/tmp/defaultprojects"]);
        compare(folderReads, reads);
    }
    function test_forcedOneShotRefreshWorksWithoutEnablingWatcher() {
        entries = ["old"];
        metadata = {old: scene("On demand")};
        const watcher = createTemporaryObject(watcherComponent, this);
        const list = createTemporaryObject(modelComponent, this, {directoryWatcher: watcher});
        verify(list);
        compare(list.enabled, false);
        compare(folderReads, 0);
        const generation = list.refreshGeneration;
        list.refresh(true);
        tryCompare(list.model, "count", 1);
        tryVerify(() => list.refreshGeneration > generation);
        compare(folderReads, 1);
        const reads = folderReads;
        watcher.changed();
        wait(30);
        compare(folderReads, reads);
        compare(list.enabled, false);
    }
}
