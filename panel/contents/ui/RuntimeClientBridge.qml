import QtQuick
import com.github.catsout.wallpaperEngineKde 1.2

Item {
    id: root
    visible: false
    width: 0
    height: 0

    // Tests may provide a facade implementing execute() and the optional
    // ownerChanged/stateChanged/commandFinished signals. Production defaults
    // to the native D-Bus client.
    property var injectedClient: null
    readonly property var activeClient: injectedClient !== null ? injectedClient : nativeClient
    readonly property bool nativeAvailable: nativeClient.available
    readonly property var nativeSnapshot: nativeClient.snapshot

    WallpaperRuntimeClient {
        id: nativeClient
    }
}
