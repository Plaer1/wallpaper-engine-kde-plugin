pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import com.github.catsout.wallpaperEngineKde 1.2

PlasmoidItem {
    id: root

    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false

    // Optional test/facade override. Production uses the native Runtime1 client.
    property string runtimeStatus: "absent"
    property var runtimeSnapshot: null
    property string runtimeErrorMessage: ""
    property var runtimeCommandClient: null
    // Tests/embedders may inject a choice list. Production derives choices
    // from the selected runtime targets' explicit follow.choices snapshots.
    property var followSourceChoices: null
    readonly property var runtimeTargets: commandController.targets
    readonly property string runtimeStatusSummary: {
        const status = commandController.effectiveRuntimeStatus;
        const targets = runtimeTargets;
        if (status === "loading") return qsTr("Connecting to wallpaper runtime…");
        if (status === "error") return qsTr("Wallpaper runtime unavailable");
        if (status === "absent" || !commandController.effectiveSnapshot)
            return qsTr("No active wallpaper runtime");
        if (!targets.length) return qsTr("No active wallpaper targets");
        const states = targets.map(target => target.effectivePlayback || "unknown");
        if (states.every(state => state === "paused")) return qsTr("Paused");
        if (states.some(state => state === "playing")) return qsTr("Wallpaper active");
        if (states.some(state => state === "error")) return qsTr("Wallpaper error");
        if (states.some(state => state === "loading")) return qsTr("Wallpaper loading");
        return qsTr("Wallpaper state unknown");
    }
    readonly property string runtimeTargetSummary: runtimeTargets.length
        ? runtimeTargets.map(target => target.screenLabel || target.screenName || qsTr("Unknown screen")).join(", ")
        : runtimeStatusSummary
    readonly property var desktopSettingsInvoker: desktopSettingsLauncher
    readonly property var activeRuntimeCommandClient: runtimeClientBridge.activeClient
    readonly property color globalWindowColor: globalPalette.window
    readonly property string statusIconPath: {
        const luminance = 0.2126 * globalWindowColor.r + 0.7152 * globalWindowColor.g + 0.0722 * globalWindowColor.b
        return luminance >= 0.5 ? "../icons/eye-gear-3-light.svg" : "../icons/eye-gear-3-dark.svg"
    }
    toolTipMainText: runtimeStatusSummary
    toolTipSubText: globalPlaybackShortcut.errorCode
        ? globalPlaybackShortcut.message : runtimeTargetSummary

    Plasmoid.icon: Qt.resolvedUrl(statusIconPath)
    Plasmoid.status: commandController.effectiveRuntimeStatus === "absent"
        ? PlasmaCore.Types.PassiveStatus : PlasmaCore.Types.ActiveStatus
    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    Plasmoid.contextualActions: commandActionSet.contextualActions

    TrayActionsMenu {
        id: trayActionsMenu
        actions: commandActionSet.popupActions
    }

    SystemPalette {
        id: globalPalette
        colorGroup: SystemPalette.Active
    }

    RuntimeClientBridge {
        id: runtimeClientBridge
        injectedClient: root.runtimeCommandClient
    }

    RuntimeCommandController {
        id: commandController
        objectName: "runtimeCommandController"
        runtimeStatus: root.runtimeStatus
        snapshot: root.runtimeSnapshot
        commandClient: root.activeRuntimeCommandClient
    }

    DesktopSettingsInvoker {
        id: desktopSettingsLauncher
    }
    GlobalPlaybackShortcut {
        id: globalPlaybackShortcut
        shortcut: String(Plasmoid.configuration.GlobalPauseShortcut || "")
        onToggleRequested: commandActionSet.runAllPlaybackAction()
    }

    RuntimeCommandActionSet {
        id: commandActionSet
        controller: commandController
        appletConfigureAction: Plasmoid.internalAction("configure")
        desktopSettingsInvoker: root.desktopSettingsInvoker
    }

    compactRepresentation: TrayStatusButton {
        id: compactButton
        statusText: root.runtimeStatusSummary
        targetText: root.runtimeTargetSummary
        iconSource: Qt.resolvedUrl(root.statusIconPath)
        onActivated: trayActionsMenu.popup(compactButton, 0, compactButton.height)
    }

    fullRepresentation: TrayPopup {
        actions: commandActionSet.popupActions
    }
}
