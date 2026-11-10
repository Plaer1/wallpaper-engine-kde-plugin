import QtQml 2.0

// Centralizes the existing automatic play gate so the renderer and its fake
// context tests exercise the same arbitration.
QtObject {
    property bool windowPause: false
    property bool fullscreenPause: false
    property bool batteryPause: false
    property bool gameModePause: false
    property bool sleepPause: false
    property bool inactiveSessionPause: false
    // Session intent survives renderer replacement because this resolver is
    // owned by the wallpaper root, not by the replaceable backend Loader.
    property string playbackMode: "automatic"

    readonly property var pauseReasons: {
        const reasons = [];
        if (playbackMode === "paused") reasons.push("manual");
        if (sleepPause) reasons.push("sleep");
        if (inactiveSessionPause) reasons.push("inactiveSession");
        if (batteryPause) reasons.push("battery");
        if (gameModePause) reasons.push("gameMode");
        if (fullscreenPause && playbackMode !== "playDespiteWindows") reasons.push("fullscreen");
        if (windowPause && playbackMode !== "playDespiteWindows") reasons.push("window");
        return reasons;
    }

    readonly property bool canPlay: playbackMode !== "paused"
        && !batteryPause
        && !gameModePause
        && !sleepPause
        && !inactiveSessionPause
        && (playbackMode === "playDespiteWindows" || !fullscreenPause)
        && (playbackMode === "playDespiteWindows" || !windowPause)

    function setPlaybackMode(mode) {
        if (!["automatic", "paused", "playDespiteWindows"].includes(mode))
            return false;
        playbackMode = mode;
        return true;
    }
}
