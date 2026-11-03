import QtQuick
import QtTest
import "../../plugin/contents/ui" as Wallpaper

TestCase {
    id: testCase
    name: "PlaylistNamespacing"

    function fileUrl(directory) {
        return Wallpaper.Common.localFileUrl(directory + "/project.json");
    }

    function account(key, id, accountId32, label, identityStatus) {
        return {key, steamId64: id, accountId32, label, identityStatus, cacheKeys: []};
    }

    function catalog(accounts) {
        return {schemaVersion: 1, generation: 1, installations: [], accounts, caches: []};
    }

    QtObject {
        id: accountCatalog
        property var value: null
        function snapshot() { return value; }
    }

    QtObject {
        id: playlistSources
        property var value: null
        function snapshot() { return value; }
    }

    Component {
        id: modelComponent
        Wallpaper.WallpaperListModel {
            enabled: false
            globalConfigPath: "/tmp/session/global.json"
            accountCatalogProvider: accountCatalog
            playlistSourceProvider: playlistSources
            readfile: path => {
                if (path === "/tmp/session/global.json")
                    return Promise.resolve(JSON.stringify({steamuser: {general: {playlists: [
                        {name: "Favorites", items: [testCase.fileUrl("/tmp/session/project")]}
                    ]}}}));
                return Promise.resolve('{"file":"scene.json","type":"scene"}');
            }
        }
    }

    function playlistRows() {
        const rows = [];
        const filters = Wallpaper.Common.filterModel;
        for (let i = 0; i < filters.count; ++i) {
            const row = filters.get(i);
            if (row.type === "playlist") rows.push({index: i, key: row.key, text: row.text, def: row.def});
        }
        return rows;
    }

    function rowWithText(text) {
        return playlistRows().find(row => row.text === text) || null;
    }

    function load(list) {
        let done = false;
        list.loadPlaylists().then(() => { done = true; });
        tryVerify(() => done);
    }

    function cleanup() {
        const filters = Wallpaper.Common.filterModel;
        for (let i = filters.count - 1; i >= 0; --i) {
            if (filters.get(i).type === "playlist") filters.remove(i);
        }
    }

    function test_sameDisplayNameUsesVerifiedAccountAndSessionNamespaces() {
        const aliceKey = "steam:76561190000000001";
        const bobKey = "steam:76561190000000002";
        accountCatalog.value = catalog([
            account(aliceKey, "76561190000000001", "00000001", "Shared persona", "verified"),
            account(bobKey, "76561190000000002", "00000002", "Shared persona", "verified")
        ]);
        playlistSources.value = {
            schemaVersion: 1,
            generation: 7,
            sources: [
                {sourceKey: "config:/tmp/alice/config.json#steamuser.general.playlists",
                    accountKey: aliceKey, generation: 3,
                    playlists: [{id: "favorites", name: "Favorites", items: [fileUrl("/tmp/alice/project")]}]},
                {sourceKey: "config:/tmp/bob/config.json#steamuser.general.playlists",
                    accountKey: bobKey, generation: 4,
                    playlists: [{id: "favorites", name: "Favorites", items: [fileUrl("/tmp/bob/project")]}]}
            ]
        };

        const list = createTemporaryObject(modelComponent, testCase);
        verify(list);
        load(list);

        const alice = rowWithText("Shared persona (…0001) — Favorites");
        const bob = rowWithText("Shared persona (…0002) — Favorites");
        const session = rowWithText("Session / unassociated — Favorites");
        verify(alice && bob && session);
        verify(alice.key !== bob.key && alice.key !== session.key && bob.key !== session.key);
        verify(alice.key.includes(encodeURIComponent(aliceKey)));
        verify(bob.key.includes(encodeURIComponent(bobKey)));
        verify(alice.key.includes(encodeURIComponent("favorites")));
        verify(bob.key.includes(encodeURIComponent("favorites")));
        verify(list.playlists[alice.key].has(fileUrl("/tmp/alice/project").replace(/\/project\.json$/, "")));
        verify(list.playlists[bob.key].has(fileUrl("/tmp/bob/project").replace(/\/project\.json$/, "")));
        verify(list.playlists[session.key].has(fileUrl("/tmp/session/project").replace(/\/project\.json$/, "")));

        list.loadFolderLists([
            {folder: "/tmp/alice", items: [{name: "project", mtime: 1}]},
            {folder: "/tmp/bob", items: [{name: "project", mtime: 1}]},
            {folder: "/tmp/session", items: [{name: "project", mtime: 1}]}
        ]);
        tryCompare(list.model, "count", 3);
        for (let i = 0; i < list.model.count; ++i) {
            const item = list.model.get(i);
            compare(item.playlists.count, 1);
            if (item.path.includes("/alice/project")) {
                compare(item.playlists.get(0).key, alice.key);
                compare(item.playlists.get(0).text, "Shared persona (…0001) — Favorites");
            } else if (item.path.includes("/bob/project")) {
                compare(item.playlists.get(0).key, bob.key);
                compare(item.playlists.get(0).text, "Shared persona (…0002) — Favorites");
            } else {
                verify(item.path.includes("/session/project"));
                compare(item.playlists.get(0).key, session.key);
                compare(item.playlists.get(0).text, "Session / unassociated — Favorites");
            }
        }

        const aliceRow = playlistRows().find(row => row.key === alice.key);
        Wallpaper.Common.filterModel.get(aliceRow.index).def = 1;
        load(list);
        compare(rowWithText("Shared persona (…0001) — Favorites").def, 1);
        verify(rowWithText("Shared persona (…0002) — Favorites").def !== 1);

        list.filterStr = Wallpaper.Common.filterModel.map(filter =>
            filter.type === "playlist" ? Number(filter.key === alice.key) : filter.def).join("");
        list.loadFolderLists([
            {folder: "/tmp/alice", items: [{name: "project", mtime: 1}]},
            {folder: "/tmp/bob", items: [{name: "project", mtime: 1}]},
            {folder: "/tmp/session", items: [{name: "project", mtime: 1}]}
        ]);
        tryCompare(list.model, "count", 1);
        verify(list.model.get(0).path.includes("/alice/project"));
    }

    function test_unverifiedAccountRemainsUnassociated() {
        const key = "steam:76561190000000003";
        accountCatalog.value = catalog([
            account(key, "76561190000000003", "00000003", "Not verified", "unresolved")
        ]);
        playlistSources.value = {
            schemaVersion: 1,
            generation: 1,
            sources: [{sourceKey: "config:/tmp/unresolved/config.json#playlists",
                accountKey: key, generation: 1,
                playlists: [{id: "saved", name: "Saved", items: []}]}]
        };
        const list = createTemporaryObject(modelComponent, testCase);
        verify(list);
        load(list);
        const row = rowWithText("Session / unassociated — Saved");
        verify(row);
        verify(row.key.startsWith("playlist:legacySession:"));
        verify(!row.key.includes(encodeURIComponent(key)));
    }

    function test_indexFallbackIncludesSourceGenerationAndInvalidatesSelection() {
        const key = "steam:76561190000000001";
        accountCatalog.value = catalog([
            account(key, "76561190000000001", "00000001", "Alice", "verified")
        ]);
        playlistSources.value = {
            schemaVersion: 1,
            generation: 1,
            sources: [{sourceKey: "config:/tmp/alice/config.json#playlists", accountKey: key,
                generation: 10, playlists: [{name: "Same name", items: []}]}]
        };
        const list = createTemporaryObject(modelComponent, testCase);
        verify(list);
        load(list);
        const first = rowWithText("Alice — Same name");
        verify(first && first.key.includes("index%3A0%40generation%3A10"));
        const firstIndex = playlistRows().find(row => row.key === first.key).index;
        Wallpaper.Common.filterModel.get(firstIndex).def = 1;

        playlistSources.value = {
            schemaVersion: 1,
            generation: 1,
            sources: [{sourceKey: "config:/tmp/alice/config.json#playlists", accountKey: key,
                generation: 11, playlists: [{name: "Same name", items: []}]}]
        };
        load(list);
        const second = rowWithText("Alice — Same name");
        verify(second && second.key !== first.key);
        verify(second.key.includes("index%3A0%40generation%3A11"));
        verify(second.def !== 1, "generation invalidation must not carry selection by display name");
    }
}
