import QtQuick

Item {
    id: root
    property string resolvedStatus: "Waiting for saved selection"
    property var resolvedProvenance: ({})
    property string resolvedOrigin: "manualFallback"
    property var resolvedChoices: [{mode: "account", accountKey: "steam:76561198000000001",
        sourceKey: "config:/fixture/loginusers.vdf#account:76561198000000001",
        label: "Saved selection", identityVerified: true}]

    function synchronize() {
        runtimeEndpoint.followState = ({
            enabled: Boolean(targetConfiguration.FollowWallpaperEngine),
            mode: targetConfiguration.FollowSourceMode,
            accountKey: targetConfiguration.FollowSourceAccountKey,
            sourceKey: targetConfiguration.FollowSourceKey,
            choices: resolvedChoices,
            status: targetConfiguration.FollowWallpaperEngine ? resolvedStatus : "disabled",
            provenance: JSON.stringify(Object.assign({}, resolvedProvenance || ({}), {
                origin: resolvedOrigin
            }))
        });
    }

    function externalQmlChange() {
        targetConfiguration.FollowWallpaperEngine = false;
        resolvedStatus = "disabled";
    }

    Connections {
        target: targetConfiguration
        function onValueChanged(key, value) { root.synchronize(); }
    }

    Component.onCompleted: synchronize()
}
