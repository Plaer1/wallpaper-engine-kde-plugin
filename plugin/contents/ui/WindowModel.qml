import QtQuick

// Per-wallpaper adapter only. TaskManager scanning, primary-screen coverage,
// fullscreen detection and resume timing live once in WindowPausePolicy.
Item {
    id: root
    property var containmentContext: null

    readonly property bool reqPause: WindowPausePolicy.windowPause
    readonly property bool globalFullscreenPause: WindowPausePolicy.fullscreenPause
    // Compatibility aliases for callers/tests from the previous local model.
    readonly property bool globalPrimaryCoveragePause: false
    readonly property bool coverageModeSupported: WindowPausePolicy.coverageSupported
    readonly property string coverageModeUnavailableReason:
        WindowPausePolicy.coverageUnavailableReason

    onContainmentContextChanged: WindowPausePolicy.attachCorona(
        containmentContext ? containmentContext.corona : null)
    Component.onCompleted: WindowPausePolicy.attachCorona(
        containmentContext ? containmentContext.corona : null)
}
