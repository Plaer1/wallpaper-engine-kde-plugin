import QtQuick
import QtQuick.Controls
import QtQuick.Window

import "../js/utils.mjs" as Utils
import "../js/bbcode.mjs" as BBCode

Item {
    id: root

    // The caller passes the native filesystem path expected by readFile.
    property string projectUrl
    property var readFile
    property alias textControl: textArea
    readonly property string status: loading ? "loading" : error ? "error" : description.length ? "ready" : "empty"
    readonly property string description: textArea.text
    readonly property bool truncated: _truncated
    property bool _truncated: false
    property bool loading: false
    property bool error: false
    property bool componentReady: false
    property var lifetimeToken: ({alive: true, generation: 0})

    implicitHeight: textArea.implicitHeight
    visible: status === "ready" || status === "loading"

    function loadDescription() {
        const life = lifetimeToken;
        const generation = ++life.generation;
        const path = projectUrl;
        textArea.text = "";
        _truncated = false;
        error = false;
        loading = Boolean(path && readFile);
        if (!loading)
            return;

        let read;
        try {
            read = readFile(path);
        } catch (reason) {
            if (life.alive && generation === life.generation) {
                loading = false;
                error = true;
                console.error(`Unable to read wallpaper description '${path}':`, reason);
            }
            return;
        }
        Promise.resolve(read).then(value => {
            if (!life.alive || generation !== life.generation)
                return;
            const project = Utils.parseJson(value);
            const description = project && project.description ? project.description : "";
            const parsed = BBCode.parser.parseWithLimit(description);
            textArea.text = description ? parsed.html : "";
            _truncated = Boolean(description && parsed.truncated);
            loading = false;
        }).catch(reason => {
            if (!life.alive || generation !== life.generation)
                return;
            loading = false;
            error = true;
            textArea.text = "";
            console.error(`Unable to read wallpaper description '${path}':`, reason);
        });
    }

    onProjectUrlChanged: if (componentReady) loadDescription()
    Component.onCompleted: {
        componentReady = true;
        loadDescription();
    }
    Component.onDestruction: lifetimeToken.alive = false

    SystemPalette { id: desktopPalette }

    TextArea {
        id: textArea
        anchors.fill: parent
        readOnly: true
        selectByMouse: true
        wrapMode: TextEdit.Wrap
        textFormat: TextEdit.RichText
        color: desktopPalette.text
        palette.text: desktopPalette.text
        palette.base: "transparent"
        palette.window: "transparent"
        palette.highlight: desktopPalette.highlight
        palette.highlightedText: desktopPalette.highlightedText
        palette.link: desktopPalette.link
        padding: 0
        background: Rectangle { color: "transparent" }
        onLinkActivated: Qt.openUrlExternally(link)
        MouseArea {
            anchors.fill: parent
            cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
            acceptedButtons: Qt.NoButton
        }
    }
}
