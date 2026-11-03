import QtQuick
import QtTest

TestCase {
    name: "WebWallpaperChannel"
    when: windowShown
    visible: true
    width: 640
    height: 360

    QtObject {
        id: background
        property bool mute: false
        property bool hasLib: true
        property int fps: 36
        property string nowBackend
        signal sig_backendFirstFrame(string backend)
        function getWorkshopIDPath() { return "file:///tmp"; }
    }

    function js(web, source) {
        let complete = false;
        let value;
        web.runJavaScript(source, result => { value = result; complete = true; });
        tryVerify(() => complete, 5000);
        return value;
    }

    function test_handshakePropertiesAndReload() {
        background.fps = 36;
        let reads = 0;
        const component = Qt.createComponent("../../plugin/contents/ui/backend/QtWebView.qml");
        compare(component.status, Component.Ready, component.errorString());
        const backend = component.createObject(this, {
            source: Qt.resolvedUrl("web-channel-page.html"),
            readfile: () => {
                ++reads;
                return Promise.resolve('{"general":{"properties":{"accent":{"type":"color","value":"1 0 0"}}}}');
            }
        });
        verify(backend, component.errorString());
        const web = findChild(backend, "wallpaperWebView");
        verify(web);
        try {
            tryVerify(() => js(web, 'typeof QWebChannel === "function" && !!window.wpeQml'), 10000);
            tryVerify(() => js(web, 'generalHistory.length > 0 && userHistory.length > 0'), 5000);
            compare(js(web, 'generalHistory[0].fps'), 36);
            compare(js(web, 'userHistory[0].accent.value'), '1 0 0');
            compare(reads, 1);

            background.fps = 48;
            tryVerify(() => js(web, 'generalHistory[generalHistory.length - 1].fps === 48'), 5000);
            compare(reads, 1);

            // A fresh document reconnects to the same QML channel object and
            // must receive its initial properties even after a previous load.
            web.reload();
            tryVerify(() => reads === 2, 10000);
            tryVerify(() => js(web, 'generalHistory.length > 0 && userHistory.length > 0'), 5000);
            compare(js(web, 'generalHistory[0].fps'), 48);
            compare(js(web, 'userHistory[0].accent.value'), '1 0 0');
        } finally {
            backend.destroy();
            wait(100);
        }
    }
}
