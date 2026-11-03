import QtQuick 2.5
import QtWebEngine 1.10
import QtWebChannel 1.10
import com.github.catsout.wallpaperEngineKde 1.2
import ".."
import "../js/utils.mjs" as Utils
import "../js/audio-control.mjs" as AudioControl

Item {
    id: webItem
    anchors.fill: parent
    property alias source: web.url
    property int reloadGeneration: 0
    property bool hasLib: background.hasLib
    property int fps: background.fps
    property var readfile
    readonly property bool audioDisabled: background.mute
    onAudioDisabledChanged: {
        if (!audioControllerReady) return;
        updateAudioController();
        applyAudioController();
    }
    property bool audioControllerReady: false
    function applyAudioController() {
        web.runJavaScript("window.__wekdeSetAudioDisabled && window.__wekdeSetAudioDisabled(" + audioDisabled + ");");
    }
    function updateAudioController() {
        for (const oldScript of web.userScripts.find("wallpaper-audio-control"))
            web.userScripts.remove(oldScript);
        web.userScripts.insert([{
            name: "wallpaper-audio-control",
            injectionPoint: WebEngineScript.DocumentCreation,
            worldId: WebEngineScript.MainWorld,
            runsOnSubFrames: true,
            sourceCode: AudioControl.script(audioDisabled)
        }]);
    }

    FileHelper { id: fileHelper }

    onFpsChanged: {
        if(webobj.loaded) {
            webobj.generalProperties.fps = webItem.fps;
            webobj.sigGeneralProperties(webobj.generalProperties);
        }
    }

    Image {
        id: pauseImage
        anchors.fill: parent
        visible: true
        enabled: false
    }
    QtObject {
        id: webobj
        WebChannel.id: "wpeQml"
        signal sigGeneralProperties(var properties)
        signal sigUserProperties(var properties)
        signal sigAudio(var audioArray)
        property bool loaded: false
        property var userProperties 
        property var generalProperties
        onLoadedChanged: {
            if (!loaded) return;
            webobj.generalProperties = {fps: webItem.fps};
            webobj.sigGeneralProperties(webobj.generalProperties);
            readfile(Common.urlNative(background.getWorkshopIDPath()) + "/project.json").then(function(text) {
                const json = Utils.parseJson(text);
                webobj.userProperties = json?.general?.properties || {};
                webobj.sigUserProperties(webobj.userProperties);
            });
        }
    }
    WebChannel {
        id: channel
        registeredObjects: [webobj]
    }

    WebEngineView {
    //WebView {
        id: web
        anchors.fill: parent
        enabled: true
        audioMuted: background.mute
        objectName: "wallpaperWebView"
        activeFocusOnPress: false
        webChannel: channel

        property bool paused: false
        property bool _init: {
            settings.fullscreenSupportEnabled = true;
            settings.autoLoadIconsForPage = false;
            settings.printElementBackgrounds = false;
            settings.playbackRequiresUserGesture = false;
            settings.pdfViewerEnabled = false;
            settings.showScrollBars = false;

            settings.localContentCanAccessRemoteUrls = true;
            settings.allowGeolocationOnInsecureOrigins = true;
            _init = true;
        }


        //onContextMenuRequested: function(request) {
        //    request.accepted = true;
        //}
        onLoadingChanged: (loadingInfo) => {
            if (loadingInfo.status == WebEngineView.LoadStartedStatus)
                webobj.loaded = false;
            if(loadingInfo.status == WebEngineView.LoadSucceededStatus) {
                // Script updates and navigation reach Chromium asynchronously.
                // Reapply the current policy if this document used an older
                // DocumentCreation snapshot during a simultaneous mute change.
                webItem.applyAudioController();
                // check pause after load
                if(paused) {
                    webItem.play();
                    webItem.pause();
                }
                background.sig_backendFirstFrame('QtWebEngine', webItem.reloadGeneration);
            }
        }

        onPausedChanged: {
            if(paused) {
                pauseTimer.start();
            }
            else {
                web.visible = true;
                web.lifecycleState = WebEngineView.LifecycleState.Active;
                pauseImage.visible = false;
            }
        }

        Component.onCompleted: {
            webItem.updateAudioController();
            webItem.audioControllerReady = true;
            userScripts.insert([
                {
                    injectionPoint: WebEngineScript.DocumentCreation,
                    worldId: WebEngineScript.MainWorld,
                    name: "Audio",
                    sourceCode: `
                        window.wallpaperRegisterAudioListener = function(listener) {
                            if(window.wpeQml)
                                window.wpeQml.sigAudio.connect(listener);
                            else
                                window.wallpaperRAed = listener;
                        }
                    `
                },
                {
                    worldId: WebEngineScript.MainWorld,
                    injectionPoint: WebEngineScript.Deferred,
                    name: "ObjectInjector",
                    // sourceUrl only supports local files. Read Qt's embedded
                    // client without a distro-specific filesystem path, and
                    // initialize the channel in the same injected script.
                    sourceCode: String(fileHelper.readFile(":/qtwebchannel/qwebchannel.js")) + `
                        new QWebChannel(qt.webChannelTransport, function(channel) {
                            window.wpeQml = channel.objects.wpeQml;
                            const wpeQml = window.wpeQml;
                            const propertyListener = window.wallpaperPropertyListener;
                            if(window.wallpaperRAed)
                                wpeQml.sigAudio.connect(window.wallpaperRAed);
                            if(propertyListener) {
                                if(propertyListener.applyGeneralProperties)
                                    wpeQml.sigGeneralProperties.connect(propertyListener.applyGeneralProperties);
                                if(propertyListener.applyUserProperties)
                                    wpeQml.sigUserProperties.connect(propertyListener.applyUserProperties);
                            }
                            wpeQml.loaded = true;
                        });
                        document.getElementsByTagName('body')[0].ondragstart = function() { return false; }
                        `
                }
            ])
            background.nowBackend = 'QtWebEngine';
        }

    }
    // There is no signal for frame complete, so use timer to make sure not black result
    Timer{
        id: pauseTimer
        running: false
        repeat: false
        interval: 300 
        onTriggered: {
            // only check paused status on timer, not set
            // this is async
            web.grabToImage(function(result) {
                // check for paused again, make sure web is visible
                if(web.paused == false || web.visible == false) return;
                pauseImage.source = result.url;
                pauseImage.visible = true;
                web.visible = false;
                web.lifecycleState = WebEngineView.LifecycleState.Frozen;
            });
        }   
    }
    Component.onCompleted: {
    //target: web.children[0] ? web.children[0] : null
    }

    function play(){
        web.paused = false;
    }
    function pause(){
        // Set status first
        web.paused = true;
    }
    function getMouseTarget() {
        web.activeFocusOnPress = true;
        return Qt.binding(function() { return web.children[0]; })
    }
}
