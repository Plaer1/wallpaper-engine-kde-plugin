import QtQuick 2.5
import QtQuick.Controls 2.0
import QtQuick.Layouts 1.2

import "js/utils.mjs" as Utils

Item {
    id: root
    property var workshopDirs: []
    // The native watcher is supplied by config/main so source-only model tests
    // can exercise refreshes without loading the renderer plugin.
    property var directoryWatcher: null
    readonly property var projectDirectories: (workshopDirs || []).reduce((paths, entry) =>
        paths.concat(Array.isArray(entry) ? entry : [entry]), []).map(Common.urlNative)
    property var _refreshPromise: null
    property bool _refreshPending: false
    property bool _refreshPendingForced: false
    // Promise jobs can outlive their QML owner. Keep a plain JS cancellation
    // token that stays readable after root/folderWorker have been deleted.
    property var _lifetime: ({alive: true})
    Component.onDestruction: root._lifetime.alive = false
    property var globalConfigPath
    property string filterStr: ""
    property var browserFilters: ({query: "", includedTags: [], excludedTags: [], sourceKind: ""})
    property int sortMode: Common.SortMode.Id
    property bool enabled: true

    property var initItemOp: null
    property var _initItemOp: Boolean(initItemOp) ? initItemOp : function(){ }
    // Folder enumeration is supplied by the owning Pyext instance. Do not
    // depend on an id leaking into this component's creation context.
    property var folderList: null
    property var _folderList: Boolean(folderList) ? folderList : function(){
        return Promise.reject("folder list func not available");
    }
    property var readfile: null 
    property var _readfile: Boolean(readfile) ? readfile : function(){ return Promise.reject("read file func not available"); }

    // P1 consumes a small injectable catalog-provider interface rather than
    // coupling this model to the A3 native bridge or browse-settings storage.
    // Providers implement snapshot() and emit catalogChanged when it changes.
    property var accountCatalogProvider: null
    // Optional local adapter for account-scoped playlist config snapshots.
    // snapshot() returns {schemaVersion:1,generation,sources:[{sourceKey,
    // accountKey?,generation,playlists:[{id?,name,items}]}]}. The canonical
    // catalog above remains the source of truth for verified account labels.
    property var playlistSourceProvider: null
    property var browseScope: ({mode: "allDownloaded", accountKeys: [], includeSharedProjects: true})
    property var _subscriptionAccountsById: ({})
    property var _subscriptionCacheKeysById: ({})
    property int membershipRevision: 0
    property int refreshGeneration: 0
    property string browseProjectionStatus: "allDownloaded"
    property bool componentReady: false

    signal modelStartSync
    signal modelRefreshed
    signal membershipProjectionChanged

    readonly property ListModel model: ListModel {
        function assignModel(index, value) {
            Object.assign(this.get(index), value);
            const workshopid = this.get(index).workshopid;
            new Promise((resolve, reject) => {
                const model = folderWorker.model;
                for(let i=0;i<model.length;i++) {
                    if(model[i].workshopid === workshopid) {
                        Object.assign(model[i], value);
                        resolve();
                    }
                }
                reject();
            });
        }
    }

    property int countNoFilter: 0
    readonly property var browserTagCatalog: Common.getBrowserTagCatalog(folderWorker.model)

    property var playlists: {}
    property var playlistLabels: ({})
    property var folderModels: []

    function loadItemFromJson(text, el) {
        const project = Utils.parseJson(text);    
        if(project !== null) {
            if("title" in project)
                el.title = project.title;
            if("preview" in project && project.preview)
                el.preview = project.preview;
            if("file" in project)
                el.file = project.file;
            if("type" in project)
                el.type = project.type.toLowerCase();
            if("contentrating" in project)
                el.contentrating = project.contentrating;
            if("tags" in project) {
                el.tags = project.tags.map(el => Object({key: el}));
            }
        }
    }

    function isWorkshopDirectory(path) {
        const normalized = Common.urlNative(String(path || ""))
            .replace(/\\/g, "/").replace(/\/+$/, "").toLowerCase();
        return normalized.endsWith("/workshop/content/431960");
    }

    function refreshMembershipProjection(updateVisibleModel) {
        const scope = browseScope || {};
        const mode = scope.mode;
        let catalog = null;
        try {
            if (accountCatalogProvider && typeof accountCatalogProvider.snapshot === "function")
                catalog = JSON.parse(JSON.stringify(accountCatalogProvider.snapshot()));
        } catch (reason) {
            console.error("Unable to read account catalog snapshot:", reason);
        }

        const byId = Object.create(null);
        const cacheKeysById = Object.create(null);
        const caches = catalog && Array.isArray(catalog.caches) ? catalog.caches : [];
        caches.forEach(cache => {
            if (!cache || typeof cache.accountKey !== "string" || !Array.isArray(cache.subscribedIds))
                return;
            const disabled = new Set(Array.isArray(cache.disabledIds) ? cache.disabledIds : []);
            cache.subscribedIds.forEach(id => {
                // Keep published file IDs as strings; never route Steam IDs through JS Number.
                if (typeof id !== "string" || !/^\d+$/.test(id) || disabled.has(id))
                    return;
                if (!byId[id]) byId[id] = new Set();
                byId[id].add(cache.accountKey);
                if (typeof cache.key === "string" && cache.key) {
                    if (!cacheKeysById[id]) cacheKeysById[id] = new Set();
                    cacheKeysById[id].add(cache.key);
                }
            });
        });
        Object.keys(byId).forEach(id => { byId[id] = Array.from(byId[id]).sort(); });
        Object.keys(cacheKeysById).forEach(id => { cacheKeysById[id] = Array.from(cacheKeysById[id]).sort(); });
        _subscriptionAccountsById = byId;
        _subscriptionCacheKeysById = cacheKeysById;
        membershipRevision++;

        const selected = Array.isArray(scope.accountKeys)
            ? Array.from(new Set(scope.accountKeys.filter(key => typeof key === "string"))) : [];
        if (mode === "allDownloaded") {
            browseProjectionStatus = "allDownloaded";
        } else if (mode !== "selectedAccounts") {
            browseProjectionStatus = "invalidScope";
        } else if (selected.length === 0) {
            browseProjectionStatus = "emptySelection";
        } else if (!catalog) {
            browseProjectionStatus = "catalogUnavailable";
        } else {
            const known = new Set((Array.isArray(catalog.accounts) ? catalog.accounts : [])
                .map(account => account && account.key).filter(key => typeof key === "string"));
            const selectedCaches = caches.filter(cache => cache && selected.includes(cache.accountKey));
            const missingAccount = selected.some(key => !known.has(key));
            const unhealthy = selectedCaches.filter(cache => ["missing", "invalid", "unavailable"].includes(cache.state));
            const retainedLastGood = unhealthy.some(cache => Array.isArray(cache.subscribedIds)
                && cache.subscribedIds.length > 0);
            if (missingAccount || selectedCaches.length === 0)
                browseProjectionStatus = "selectedAccountsUnavailable";
            else if (unhealthy.length)
                browseProjectionStatus = retainedLastGood ? "stale" : "subscriptionDataUnavailable";
            else
                browseProjectionStatus = "available";
        }

        if (updateVisibleModel && componentReady && folderWorker)
            return folderWorker.filterToList(model, filterStr, folderWorker.model);
        membershipProjectionChanged();
    }

    function matchesBrowseScope(item, accountKeys) {
        const scope = browseScope || {};
        if (item.sourceKind === "shared")
            return scope.includeSharedProjects !== false;
        if (scope.mode === "allDownloaded")
            return true;
        if (scope.mode !== "selectedAccounts")
            return false;
        const selected = Array.isArray(scope.accountKeys) ? scope.accountKeys : [];
        return accountKeys.some(key => selected.includes(key));
    }

    function loadPlaylists() {
        const life = root._lifetime;
        // reset playlists property
        root.playlists = {};
        root.playlistLabels = ({});
    
        return root._readfile(Common.urlNative(globalConfigPath)).then(value => {
            if(!life.alive || !root) return;
            var jsonData = Utils.parseJson(value);
            if (jsonData === null) {
                console.warn("Failed to parse playlists config, skipping playlists");
                return Promise.resolve();
            }

            // refreshing entries in the filter model is not thread safe, so we need to lock it
            var filterModel = Common.filterModel;
            // A waiting model may be deleted before its turn. The shared lock
            // must still be released after acquisition, even when work is skipped.
            const lock = filterModel.lock;
            return lock.lock().then(() => {
                if(!life.alive || !root) return;
                // remove playlists from the filterModel
                var selectedPlaylists = new Set();
                for(var i =0; i < filterModel.count; i++) {
                    var el = filterModel.get(i);
                    if(el.type == "playlist") {
                        if(el.def) selectedPlaylists.add(el.key);
                        filterModel.remove(i);
                        i--;
                    }
                }

                const legacyPlaylists = jsonData?.steamuser?.general?.playlists || [];
                const sourceKey = "config:" + Common.urlNative(String(globalConfigPath || ""))
                    + "#steamuser.general.playlists";
                const sources = [{
                    sourceKey,
                    generation: playlistContentGeneration(String(value || "")),
                    playlists: legacyPlaylists,
                    legacySession: true
                }];
                try {
                    if (playlistSourceProvider && typeof playlistSourceProvider.snapshot === "function") {
                        const provided = JSON.parse(JSON.stringify(playlistSourceProvider.snapshot()));
                        if (provided && provided.schemaVersion === 1 && Array.isArray(provided.sources)) {
                            provided.sources.forEach(source => {
                                if (!source || typeof source !== "object")
                                    return;
                                const sourcePlaylists = Array.isArray(source.playlists) ? source.playlists : [];
                                const generation = source.generation !== undefined && source.generation !== null
                                    ? source.generation
                                    : (provided.generation !== undefined && provided.generation !== null
                                        ? provided.generation : playlistContentGeneration(JSON.stringify(sourcePlaylists)));
                                sources.push(Object.assign({}, source, {generation}));
                            });
                        }
                    }
                } catch (reason) {
                    console.error("Unable to read account playlist snapshot:", reason);
                }

                const accountCatalog = verifiedPlaylistAccounts();
                sources.forEach(source => {
                    if (!source || typeof source.sourceKey !== "string" || !source.sourceKey
                            || !Array.isArray(source.playlists))
                        return;

                    const account = typeof source.accountKey === "string"
                        ? accountCatalog.byKey[source.accountKey] : null;
                    const namespace = account ? account.key : "legacySession";
                    const sourceGeneration = String(source.generation);
                    source.playlists.forEach((playlist, index) => {
                        if (!playlist || typeof playlist.name !== "string" || !Array.isArray(playlist.items))
                            return;
                        const localId = typeof playlist.id === "string" && playlist.id.length
                            ? "id:" + playlist.id
                            : "index:" + index + "@generation:" + sourceGeneration;
                        const playlistKey = namespacedPlaylistKey(namespace, source.sourceKey, localId);
                        const ownerLabel = account
                            ? accountCatalog.labelsByKey[account.key]
                            : qsTr("Session / unassociated");
                        const displayText = ownerLabel + " — " + playlist.name;
                        const paths = new Set(playlist.items.filter(item => typeof item === "string").map(item => {
                            // Normalize native Windows paths and saved file URLs like the folder model.
                            const path = item.startsWith('file:') ? Common.urlNative(item) : item.substring(2);
                            return Common.localFileUrl(path.replace(/\/[^\/]*$/, "").substring(0,110));
                        }));
                        root.playlists[playlistKey] = paths;
                        root.playlistLabels[playlistKey] = displayText;
                        filterModel.append({
                            type: "playlist", key: playlistKey, text: displayText,
                            def: selectedPlaylists.has(playlistKey) ? 1 : 0
                        });
                    });
                });
            })
            .then(() => { lock.release(); }, reason => {
                lock.release();
                if(life.alive && root) console.error(reason);
            });
        }).catch(reason => {
            if(life.alive && root)
                console.error("PlaylistLoadError " + reason.lineNumber + " -- " + reason.type + reason.message);
        });
    }

    function playlistContentGeneration(text) {
        // Stable for an unchanged legacy config; a content edit invalidates
        // index-based IDs instead of attaching an old filter to a same-name row.
        let fnv = 2166136261;
        let djb = 5381;
        for (let i = 0; i < text.length; ++i) {
            const code = text.charCodeAt(i);
            fnv = Math.imul(fnv ^ code, 16777619);
            djb = Math.imul(djb, 33) ^ code;
        }
        return text.length + "-" + (fnv >>> 0).toString(16) + "-" + (djb >>> 0).toString(16);
    }

    function namespacedPlaylistKey(namespace, sourceKey, localId) {
        return "playlist:" + [namespace, sourceKey, localId].map(encodeURIComponent).join(":");
    }

    function verifiedPlaylistAccounts() {
        let catalog = null;
        try {
            if (accountCatalogProvider && typeof accountCatalogProvider.snapshot === "function")
                catalog = JSON.parse(JSON.stringify(accountCatalogProvider.snapshot()));
        } catch (reason) {
            console.error("Unable to verify playlist account identity:", reason);
        }
        const accounts = catalog && catalog.schemaVersion === 1 && Array.isArray(catalog.accounts)
            ? catalog.accounts : [];
        const verified = accounts.filter(account => account
            && typeof account.key === "string"
            && /^steam:[1-9]\d{16}$/.test(account.key)
            && account.identityStatus === "verified"
            && account.steamId64 === account.key.substring(6));
        const labelCounts = Object.create(null);
        verified.forEach(account => {
            const label = typeof account.label === "string" && account.label ? account.label : account.key;
            labelCounts[label] = (labelCounts[label] || 0) + 1;
        });
        const byKey = Object.create(null);
        const labelsByKey = Object.create(null);
        verified.forEach(account => {
            byKey[account.key] = account;
            const label = typeof account.label === "string" && account.label ? account.label : account.key;
            const suffix = String(account.accountId32 || account.steamId64).slice(-4);
            labelsByKey[account.key] = labelCounts[label] > 1 ? label + " (…" + suffix + ")" : label;
        });
        return {byKey, labelsByKey};
    }

    function genSortCmp(mode) {
        switch (mode) {
          case Common.SortMode.Modified:
            return function(a, b) {
                return -(a.modified - b.modified);
            }
          case Common.SortMode.Name:
            return function(a, b) {
                return a.title<b.title ? -1 : 1;
            }
          case Common.SortMode.Id:
          default:
            return function(a, b) {
                return a.workshopid<b.workshopid ? -1 : 1;
            };
        }
    }

    Item {
        id: folderWorker

        // array
        property var folderMapModel: new Map()
        property var model: []

        function loadModel(path, data) {
            this.folderMapModel.set(path, data);
            const allItems = [];
            this.folderMapModel.forEach((value, key) => {
                allItems.push(...value);
            });
            this.model = allItems;
            return filterToList(root.model, root.filterStr, this.model);
        }
        function filterToList(listModel, filterStr, data) {
            const life = root._lifetime;
            const filterValues = Common.filterModel.getValueArray(filterStr);
            const filterstr = Common.filterModel.map((el, index) => {
                    return {
                        type: el.type,
                        key: el.key,
                        value: filterValues[index]
                    };
                });
            root.modelStartSync();
            return new Promise((resolve, reject) => {
                const filter = Common.filterModel.genFilter(filterstr);
                const browserFilter = Common.createBrowserFilter(root.browserFilters);
                const model = listModel;
                data.sort(genSortCmp(sortMode));
                model.clear();
                data.forEach(function(el) {
                    const workshop = el.sourceKind === "workshop";
                    const id = workshop ? el.publishedFileId : "";
                    const accountKeys = id && _subscriptionAccountsById[id]
                        ? _subscriptionAccountsById[id].slice() : [];
                    const cacheKeys = id && _subscriptionCacheKeysById[id]
                        ? _subscriptionCacheKeysById[id].slice() : [];
                    // QML ListModel turns JavaScript arrays into nested list models;
                    // keep this role scalar and reversible for delegates.
                    el.subscriptionAccountKeys = accountKeys.join(",");
                    el.subscriptionCacheKeys = cacheKeys.join(",");
                    el.isSubscribed = accountKeys.length > 0;
                    if(matchesBrowseScope(el, accountKeys) && filter(el) && browserFilter(el))
                        model.append(el);
                });
                resolve();
            }).then(() => {
                if(!life.alive || !root || !folderWorker) return;
                root.countNoFilter = folderWorker.model.length;
                root.modelRefreshed();
            });
        }
    }

    Connections {
        target: root.directoryWatcher
        function onChanged() { root.refresh(); }
    }

    Connections {
        target: root.accountCatalogProvider
        ignoreUnknownSignals: true
        function onCatalogChanged() { root.refreshMembershipProjection(true); }
    }

    onAccountCatalogProviderChanged: if (componentReady) refreshMembershipProjection(true)
    onBrowseScopeChanged: if (componentReady) refreshMembershipProjection(true)

    function refresh(force) {
        const life = root._lifetime;
        if(!life.alive || (!root.enabled && force !== true)) return Promise.resolve(null);
        if(root._refreshPromise) {
            // A download can finish while a scan is reading its metadata.
            // Finish the current scan, then take one fresh snapshot.
            root._refreshPending = true;
            root._refreshPendingForced = root._refreshPendingForced || force === true;
            return root._refreshPromise;
        }
        root._refreshPromise = Promise.resolve().then(() => {
            if(!life.alive || !root) return;
            return root.loadPlaylists();
        }).then(() => {
            if(!life.alive || !root) return;
            const scans = (root.workshopDirs || []).map(entry => {
                const dirs = (Array.isArray(entry) ? entry : [entry]).map(Common.urlNative);
                return root._folderList(dirs[0], { only_dir: true, fallbacks: dirs.slice(1) })
                    .then(result => {
                        if(!life.alive || !root) return null;
                        if(!result) console.error(`folder not found: ${dirs[0]}`);
                        return result;
                    }).catch(reason => {
                        if(life.alive && root) console.error(reason);
                        return null;
                    });
            });
            return Promise.all(scans).then(folders => {
                if(!life.alive || !root) return;
                return Promise.resolve(root.loadFolderLists(folders)).then(() => {
                    if(life.alive && root) root.refreshGeneration++;
                });
            });
        }).catch(reason => {
            if(life.alive && root) console.error(reason);
        }).then(() => {
            if(!life.alive || !root) return;
            root._refreshPromise = null;
            if(root._refreshPending) {
                root._refreshPending = false;
                const forced = root._refreshPendingForced;
                root._refreshPendingForced = false;
                return root.refresh(forced);
            }
        });
        return root._refreshPromise;
    }

    function loadFolderLists(folders) {
        const life = root._lifetime;
        const proxyModel = [];
        folders.forEach(folder => {
            if(!folder || !folder.items) return;
            const folderDir = folder.folder;
            folder.items.forEach(el => {
                const item = Object.assign({}, Common.wpitem_template);
                item.workshopid = el.name;
                item.path = Common.localFileUrl(folderDir + '/' + el.name);
                item.sourceKind = isWorkshopDirectory(folderDir) ? "workshop" : "shared";
                item.publishedFileId = item.sourceKind === "workshop" && /^\d+$/.test(el.name)
                    ? String(el.name) : "";
                item.projectIdentity = item.publishedFileId
                    ? `workshop:${item.publishedFileId}` : `local:${Common.urlNative(item.path)}`;
                item.modified = el.mtime;
                root._initItemOp(item);
                proxyModel.push(item);
            });
        });
        const reads = proxyModel.map(item => {
            return root._readfile(Common.urlNative(Common.getWpModelProjectPath(item))).then(value => {
                if(!life.alive || !root) return;
                item.playlists = [];
                root.loadItemFromJson(value, item);
                Object.keys(root.playlists).forEach(key => {
                    if(root.playlists[key].has(item.path) && !item.playlists.includes(key))
                        item.playlists.push({key, text: root.playlistLabels[key] || key});
                });
            }).catch(reason => {
                if(life.alive && root) console.error(reason);
            });
        });
        const path = this.folder;
        return Promise.all(reads).then(() => {
            if(!life.alive || !root || !folderWorker) return;
            return folderWorker.loadModel(path, proxyModel);
        }).catch(reason => {
            if(life.alive && root) console.error(reason);
        });
    }

    Component.onCompleted: {
        const life = root._lifetime;
        componentReady = true;
        refreshMembershipProjection(false);
        this.filterStrChanged.connect(function() {
            if(root.enabled) {
                return folderWorker.filterToList(root.model, root.filterStr, folderWorker.model)
            }
            return Promise.resolve();
        });
        this.browserFiltersChanged.connect(function() {
            if(root.enabled) {
                return folderWorker.filterToList(root.model, root.filterStr, folderWorker.model)
            }
            return Promise.resolve();
        });
        this.sortModeChanged.connect(this.filterStrChanged);
        this.enabledChanged.connect(this.refresh.bind(this));
        this.workshopDirsChanged.connect(this.refresh.bind(this));
        this.globalConfigPathChanged.connect(this.refresh.bind(this));

        let fc = this.readfile;
        this.readfileChanged.connect(function() {
            if(fc === root.readfile) return Promise.resolve();
            return root.refresh().then(() => {
                if(life.alive && root) fc = root.readfile;
            });
        });
        return this.refresh();
    }

}
