import QtQuick

Item {
    id: root
    width: 0
    height: 0
    visible: false

    property bool targetActive: false
    property string instanceId: ""
    property int generation: 0
    property int timeoutInterval: 6500
    property string pendingRequestId: ""
    property string pendingTargetId: ""
    property int pendingGeneration: -1

    signal reloadBackendRequested(int generation)
    signal completed(string requestId, string targetId, bool applied,
                     string errorCode, string message)

    function request(requestId, targetId) {
        if (!targetActive || !requestId || targetId !== instanceId) {
            completed(requestId, targetId, false, "unverifiedTarget",
                      "Wallpaper target is no longer active");
            return false;
        }
        if (pendingRequestId) {
            completed(requestId, targetId, false, "reloadBusy",
                      "A reload is already pending for this wallpaper");
            return false;
        }
        pendingRequestId = requestId;
        pendingTargetId = targetId;
        pendingGeneration = ++generation;
        timeout.restart();
        reloadBackendRequested(pendingGeneration);
        return true;
    }

    function observeFirstFrame(frameGeneration, backend) {
        if (!pendingRequestId || frameGeneration !== pendingGeneration) return false;
        finish(true, "", `First frame observed from ${backend}`);
        return true;
    }

    function supersede() {
        if (pendingRequestId)
            finish(false, "reloadSuperseded", "Another source or geometry change replaced the reload");
    }

    function finish(applied, errorCode, message) {
        if (!pendingRequestId) return false;
        const requestId = pendingRequestId;
        const targetId = pendingTargetId;
        pendingRequestId = "";
        pendingTargetId = "";
        pendingGeneration = -1;
        timeout.stop();
        completed(requestId, targetId, applied, errorCode, message);
        return true;
    }

    onTargetActiveChanged: if (!targetActive) finish(false, "targetRemoved", "Wallpaper target became inactive during reload")

    Timer {
        id: timeout
        interval: root.timeoutInterval
        repeat: false
        onTriggered: root.finish(false, "reloadUnconfirmed",
            "The backend did not confirm a first frame after reload")
    }
}
