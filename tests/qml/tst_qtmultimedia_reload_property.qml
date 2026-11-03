import QtQuick
import QtTest

TestCase {
    name: "QtMultimediaReloadProperty"
    when: windowShown
    visible: true
    width: 320
    height: 240

    QtObject {
        id: background
        property bool mute: false
        property int displayMode: 0
        property int volume: 30
        property real speed: 1
        property int fps: 30
        property bool hasLib: true
        property string nowBackend: ""
    }

    function test_realMultimediaBackendAcceptsLoaderGeneration() {
        const component = Qt.createComponent("../../plugin/contents/ui/backend/QtMultimedia.qml");
        compare(component.status, Component.Ready, component.errorString());
        const backend = component.createObject(this, {reloadGeneration: 42});
        verify(backend, component.errorString());
        try {
            compare(backend.reloadGeneration, 42);
            compare(background.nowBackend, "QtMultimedia");
            verify(findChild(backend, "wallpaperMediaPlayer"));
        } finally {
            backend.destroy();
            wait(50);
        }
    }
}
