import QtQml

// Adapter from Pyext's FileHelper relay to WallpaperListModel's snapshot API.
// It does not request the catalog until enabled.
QtObject {
    id: root

    property var pyext: null
    property bool enabled: false
    property var catalogSnapshot: null
    property string status: "disabled" // disabled, loading, ready, unavailable
    property var _requestedSource: null

    readonly property bool loading: status === "loading"
    readonly property bool ready: status === "ready"
    readonly property bool unavailable: status === "unavailable"

    signal catalogChanged()

    function snapshot() {
        return catalogSnapshot;
    }

    function acceptSnapshot(value) {
        let normalized = null;
        try { normalized = JSON.parse(JSON.stringify(value)); }
        catch (reason) { normalized = null; }
        const validShape = normalized && normalized.schemaVersion === 1
            && Array.isArray(normalized.installations)
            && Array.isArray(normalized.accounts)
            && Array.isArray(normalized.caches)
            && Number.isFinite(normalized.generation)
            && Number.isInteger(normalized.generation)
            && normalized.generation >= 0;
        if (!validShape) {
            status = "unavailable";
            catalogChanged();
            return false;
        }
        catalogSnapshot = normalized;
        status = normalized.generation === 0 ? "loading" : "ready";
        catalogChanged();
        return true;
    }

    function requestInitialSnapshot() {
        if (!enabled) return false;
        if (!pyext || typeof pyext.steam_accounts_catalog !== "function") {
            status = "unavailable";
            catalogChanged();
            return false;
        }
        if (_requestedSource === pyext) return true;
        _requestedSource = pyext;
        try {
            acceptSnapshot(pyext.steam_accounts_catalog());
        } catch (reason) {
            status = "unavailable";
            catalogChanged();
            return false;
        }
        return true;
    }

    function rescan() {
        if (!enabled || !requestInitialSnapshot()) return false;
        if (typeof pyext.rescan_steam_accounts !== "function") {
            status = "unavailable";
            catalogChanged();
            return false;
        }
        try {
            pyext.rescan_steam_accounts();
            return true;
        } catch (reason) {
            status = "unavailable";
            catalogChanged();
            return false;
        }
    }

    onEnabledChanged: if (enabled) requestInitialSnapshot()
    onPyextChanged: {
        if (_requestedSource !== pyext) {
            _requestedSource = null;
            catalogSnapshot = null;
            status = enabled ? "loading" : "disabled";
            if (enabled) requestInitialSnapshot();
        }
    }

    property var _catalogConnection: Connections {
        target: root.pyext
        enabled: root._requestedSource === root.pyext
        ignoreUnknownSignals: true
        function onSteamAccountsCatalogChanged(value) {
            root.acceptSnapshot(value);
        }
    }
}
