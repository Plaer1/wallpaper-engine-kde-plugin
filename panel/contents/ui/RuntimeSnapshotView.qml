pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Item {
    id: root

    property string runtimeStatus: "absent"
    property var snapshot: null
    property string errorMessage: ""
    property var commandController: null
    property var commandActions: []
    property var followChoices: []
    property color textColor: "#232629"
    property color secondaryTextColor: "#55595c"
    property color accentColor: "#3daee9"
    property color separatorColor: "#808080"
    // Snapshot values may be plain JavaScript arrays or Qt's QVariantList wrapper.
    readonly property var targets: snapshot && snapshot.schemaVersion === 1
        && snapshot.targets && typeof snapshot.targets.length === "number" ? snapshot.targets : []
    readonly property string statusSummary: statusText()
    readonly property string tooltipSummary: tooltipText()

    implicitWidth: 360
    implicitHeight: content.implicitHeight + 32

    function statusText() {
        if (runtimeStatus === "loading") return qsTr("Connecting to wallpaper runtime…")
        if (runtimeStatus === "error") return qsTr("Wallpaper runtime unavailable")
        if (runtimeStatus === "absent" || !snapshot) return qsTr("No active wallpaper runtime")
        if (snapshot.schemaVersion !== 1) return qsTr("Wallpaper runtime state is unsupported")
        if (!targets.length) return qsTr("No active wallpaper targets")
        const states = targets.map(target => target.effectivePlayback || "unknown")
        if (states.every(state => state === "paused")) return qsTr("Paused")
        if (states.some(state => state === "playing")) return qsTr("Wallpaper active")
        if (states.some(state => state === "error")) return qsTr("Wallpaper error")
        if (states.some(state => state === "loading")) return qsTr("Wallpaper loading")
        return qsTr("Wallpaper state unknown")
    }

    function tooltipText() {
        const labels = targets.map(target => target.screenLabel || target.screenName || qsTr("Unknown screen"))
        if (labels.length) return statusSummary + " · " + labels.join(", ")
        if (runtimeStatus === "error" && errorMessage.length) return statusSummary + " · " + errorMessage
        return statusSummary
    }

    function playbackLabel(state) {
        switch (state) {
        case "playing": return qsTr("Playing")
        case "paused": return qsTr("Paused")
        case "loading": return qsTr("Loading")
        case "error": return qsTr("Error")
        default: return qsTr("State unknown")
        }
    }

    function pauseReasonLabel(reason) {
        if (reason === "window")
            return qsTr("window coverage (fullscreen apps pause all wallpaper screens)");
        return reason;
    }

    function validatedWorkshopUrl(wallpaper) {
        const id = wallpaper && wallpaper.workshopId;
        if (typeof id !== "string" || !/^[1-9][0-9]{0,19}$/.test(id)) return "";
        const maxId = "18446744073709551615";
        if (id.length > maxId.length || (id.length === maxId.length && id > maxId)) return "";
        return "https://steamcommunity.com/sharedfiles/filedetails/?id=" + id;
    }

    function validatedFolderUrl(wallpaper) {
        const source = wallpaper && wallpaper.source;
        if (typeof source !== "string" || !source || source.length > 4096
                || /[\u0000-\u001f\u007f?#]/.test(source)) return "";

        let encodedPath = "";
        if (source.startsWith("file:///")) encodedPath = source.slice(7);
        else if (source.startsWith("/")) encodedPath = source;
        else return "";

        encodedPath = encodedPath.replace(/\+[a-z]+$/, "");
        let path = "";
        try { path = decodeURIComponent(encodedPath); } catch (e) { return ""; }
        if (!path.startsWith("/") || path.includes("\\")
                || path.split("/").some(part => part === "." || part === "..")) return "";

        const separator = path.lastIndexOf("/");
        if (separator <= 0) return "";
        const directory = path.slice(0, separator);
        return "file://" + directory.split("/").map(encodeURIComponent).join("/");
    }

    Column {
        id: content
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Label {
            objectName: "runtimeStatusLabel"
            text: root.statusSummary
            color: root.textColor
            font.pixelSize: 18
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
            width: parent.width
        }

        Label {
            objectName: "runtimeErrorMessage"
            visible: root.runtimeStatus === "error" && root.errorMessage.length > 0
            text: root.errorMessage
            color: root.secondaryTextColor
            wrapMode: Text.Wrap
            width: parent.width
        }

        Label {
            objectName: "globalScopeLabel"
            visible: root.targets.length > 0
            text: qsTr("Status for all wallpaper screens")
            color: root.secondaryTextColor
            font.pixelSize: 12
        }

        Rectangle {
            visible: root.targets.length > 0
            width: parent.width
            height: 1
            color: root.separatorColor
            opacity: 0.35
        }

        Repeater {
            model: root.targets

            delegate: Column {
                id: targetRow
                required property var modelData
                required property int index
                width: root.width - 32
                spacing: 4

                Label {
                    objectName: "targetLabel-" + (targetRow.modelData.containmentId || targetRow.index)
                    text: qsTr("Target: %1").arg(targetRow.modelData.screenLabel || targetRow.modelData.screenName || qsTr("Unknown screen"))
                    color: root.textColor
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                    width: parent.width
                }

                Label {
                    objectName: "targetPlayback-" + (targetRow.modelData.containmentId || targetRow.index)
                    text: root.playbackLabel(targetRow.modelData.effectivePlayback || "unknown")
                    color: targetRow.modelData.effectivePlayback === "error" ? "#c0392b" : root.accentColor
                    width: parent.width
                }

                Label {
                    objectName: "targetWallpaper-" + (targetRow.modelData.containmentId || targetRow.index)
                    visible: Boolean(targetRow.modelData.wallpaper && (targetRow.modelData.wallpaper.title || targetRow.modelData.wallpaper.source))
                    text: targetRow.modelData.wallpaper && targetRow.modelData.wallpaper.title
                        ? targetRow.modelData.wallpaper.title
                        : (targetRow.modelData.wallpaper ? targetRow.modelData.wallpaper.source : "")
                    color: root.secondaryTextColor
                    wrapMode: Text.Wrap
                    width: parent.width
                }

                Label {
                    objectName: "targetPauseReasons-" + (targetRow.modelData.containmentId || targetRow.index)
                    visible: targetRow.modelData.effectivePlayback === "paused"
                        && targetRow.modelData.pauseReasons && targetRow.modelData.pauseReasons.length > 0
                    text: qsTr("Paused: %1").arg((targetRow.modelData.pauseReasons || [])
                        .map(reason => root.pauseReasonLabel(reason)).join(", "))
                    color: root.secondaryTextColor
                    wrapMode: Text.Wrap
                    width: parent.width
                }

                Label {
                    objectName: "targetFollowStatus-" + (targetRow.modelData.containmentId || targetRow.index)
                    visible: Boolean(targetRow.modelData.follow && targetRow.modelData.follow.enabled)
                    text: targetRow.modelData.follow && targetRow.modelData.follow.enabled
                        ? qsTr("Following %1").arg(targetRow.modelData.follow.accountLabel || targetRow.modelData.follow.accountKey || qsTr("selected source"))
                        : ""
                    color: root.secondaryTextColor
                    wrapMode: Text.Wrap
                    width: parent.width
                }

                Row {
                    spacing: 8
                    visible: root.validatedWorkshopUrl(targetRow.modelData.wallpaper)
                        || root.validatedFolderUrl(targetRow.modelData.wallpaper)

                    Button {
                        objectName: "openWorkshop-" + (targetRow.modelData.containmentId || targetRow.index)
                        visible: Boolean(root.validatedWorkshopUrl(targetRow.modelData.wallpaper))
                        text: qsTr("Open Workshop page")
                        Accessible.name: qsTr("Open the current wallpaper's Workshop page")
                        onClicked: Qt.openUrlExternally(root.validatedWorkshopUrl(targetRow.modelData.wallpaper))
                    }
                    Button {
                        objectName: "openFolder-" + (targetRow.modelData.containmentId || targetRow.index)
                        visible: Boolean(root.validatedFolderUrl(targetRow.modelData.wallpaper))
                        text: qsTr("Open wallpaper folder")
                        Accessible.name: qsTr("Open the current wallpaper's containing folder")
                        onClicked: Qt.openUrlExternally(root.validatedFolderUrl(targetRow.modelData.wallpaper))
                    }
                }
            }
        }

        RuntimeCommandPanel {
            objectName: "runtimeCommandPanel"
            width: parent.width
            controller: root.commandController
            actions: root.commandActions
            followChoices: root.followChoices
            textColor: root.textColor
            secondaryTextColor: root.secondaryTextColor
        }
    }
}
