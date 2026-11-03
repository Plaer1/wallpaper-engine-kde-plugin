import QtQuick
import QtTest

TestCase {
    name: "AudioBackends"
    when: windowShown
    visible: true
    width: 640
    height: 360
    QtObject {
        id: background
        property bool mute: false
        property int displayMode: 0
        property int volume: 0
        property real speed: 1
        property int fps: 30
        property bool hasLib: true
        property string nowBackend
        signal sig_backendFirstFrame(string backend)
        function getWorkshopIDPath() { return "file:///tmp"; }
    }
    SignalSpy {
        id: firstFrame
        target: background
        signalName: "sig_backendFirstFrame"
    }
    function js(web, source) {
        let complete = false;
        let value;
        web.runJavaScript(source, result => { value = result; complete = true; });
        tryVerify(() => complete, 5000);
        return value;
    }
    function test_webAudioDisconnectAndRestore() {
        background.mute = false;
        const component = Qt.createComponent("../../plugin/contents/ui/backend/QtWebView.qml");
        compare(component.status, Component.Ready, component.errorString());
        const backend = component.createObject(this, {
            readfile: () => Promise.resolve('{"general":{"properties":{}}}')
        });
        verify(backend, component.errorString());
        const web = findChild(backend, "wallpaperWebView");
        verify(web);
        try {
            firstFrame.clear();
            backend.source = Qt.resolvedUrl("audio-page.html");
            firstFrame.wait(10000);
            tryVerify(() => js(web, 'window.ctx && ctx.state') === 'running', 5000);
            tryVerify(() => js(web, 'window.audio && !audio.paused'), 5000);
            background.mute = true;
            tryVerify(() => js(web, 'ctx.state') === 'suspended', 5000);
            compare(js(web, 'ctx.resume(); ctx.state'), 'suspended');
            compare(js(web, 'audio.paused'), true);
            compare(js(web, 'audio.play(); audio.paused'), true);
            const frames = js(web, 'framesRendered');
            wait(100);
            verify(js(web, 'framesRendered') > frames, 'animations continue while audio is suspended');
            background.mute = false;
            tryVerify(() => js(web, 'ctx.state') === 'running', 5000);
            tryVerify(() => js(web, '!audio.paused'), 5000);
            // A page that pauses its own audio while muted stays paused afterward.
            background.mute = true;
            tryVerify(() => js(web, 'ctx.state') === 'suspended', 5000);
            js(web, 'ctx.suspend()');
            background.mute = false;
            wait(100);
            compare(js(web, 'ctx.state'), 'suspended');
            // New documents inherit the latest mute state before their scripts execute.
            js(web, 'window.previousAudioDocument = true');
            background.mute = true;
            firstFrame.clear();
            web.reload();
            firstFrame.wait(10000);
            compare(js(web, 'window.previousAudioDocument'), undefined);
            tryVerify(() => js(web, 'window.ctx && ctx.state') === 'suspended', 5000);
            compare(web.audioMuted, true);
            background.mute = false;
            tryVerify(() => js(web, 'ctx.state') === 'running', 5000);
            tryVerify(() => js(web, '!audio.paused'), 5000);
        } finally {
            backend.destroy();
            wait(100);
            background.mute = false;
        }
    }
    function test_qtAudioOutputDisconnect() {
        background.mute = false;
        const component = Qt.createComponent("../../plugin/contents/ui/backend/QtMultimedia.qml");
        compare(component.status, Component.Ready, component.errorString());
        const backend = component.createObject(this, {reloadGeneration: 17});
        verify(backend, component.errorString());
        compare(backend.reloadGeneration, 17);
        const player = findChild(backend, "wallpaperMediaPlayer");
        verify(player);
        try {
            verify(player.audioOutput !== null);
            background.mute = true;
            compare(player.audioOutput, null);
            compare(player.activeAudioTrack, -1);
            background.mute = false;
            verify(player.audioOutput !== null);
            verify(player.videoOutput !== null);
        } finally {
            backend.destroy();
            wait(100);
        }
    }
}
