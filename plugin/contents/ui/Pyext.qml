import QtQuick 2.0
import com.github.catsout.wallpaperEngineKde 1.2

// FileHelper wrapper with Promise-like API for backwards compatibility
Item {
    id: root

    // The catalog remains lazy: constructing Pyext does not ask FileHelper to
    // create its SteamAccountsWatcher. The override keeps relay behavior
    // testable without reading the user's Steam tree.
    property var catalogApi: fileHelper
    signal steamAccountsCatalogChanged(var catalog)

    // Always ready (no Python startup delay)
    readonly property bool ok: true
    readonly property string log: ""
    readonly property string version: "native"

    // Internal C++ FileHelper instance
    FileHelper {
        id: fileHelper
    }

    // Native promises keep chaining/assimilation inside Qt's engine. Custom
    // thenables captured this QML object's context and could run after the
    // settings page had been destroyed.
    function _makePromise(value) {
        return Promise.resolve(value);
    }

    function readfile(path) {
        const data = fileHelper.readFile(path);
        // Return as string (QML handles QByteArray to string conversion)
        return _makePromise(data);
    }

    function get_dir_size(path, depth) {
        if (depth === undefined) depth = 3;
        const size = fileHelper.getDirSize(path, depth);
        return _makePromise(size);
    }

    function get_folder_list(path, opt) {
        if (opt === undefined) opt = {};
        const result = fileHelper.getFolderList(path, opt);
        return _makePromise(result);
    }

    function read_wallpaper_config(id) {
        const config = fileHelper.readWallpaperConfig(id);
        return _makePromise(config);
    }

    function write_wallpaper_config(id, changed) {
        fileHelper.writeWallpaperConfig(id, changed);
        return _makePromise(null);
    }

    function reset_wallpaper_config(id) {
        fileHelper.resetWallpaperConfig(id);
        return _makePromise(null);
    }

    // No promise here, the callers bind the map straight into properties
    function detect_steam(configuredLibrary, refresh) {
        return fileHelper.detectSteam(configuredLibrary || '', Boolean(refresh));
    }

    function steam_accounts_catalog() {
        return catalogApi.steamAccountsCatalog();
    }

    function rescan_steam_accounts() {
        catalogApi.rescanSteamAccounts();
    }

    Connections {
        target: root.catalogApi
        ignoreUnknownSignals: true
        function onSteamAccountsCatalogChanged(catalog) {
            root.steamAccountsCatalogChanged(catalog);
        }
    }
}
