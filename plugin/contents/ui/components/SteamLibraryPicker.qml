import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: picker
    property var libraries: []
    property string selectedPath: ""
    signal librarySelected(string path)
    signal rescanRequested()
    signal browseRequested()

    readonly property var choices: {
        const result = libraries.map(lib => ({ path: lib.path, text: lib.path }));
        if (selectedPath && !result.some(lib => lib.path === selectedPath))
            result.push({ path: selectedPath, text: selectedPath + " (unavailable or custom)" });
        return result;
    }
    function ensureSelection() {
        if (!selectedPath && libraries.length)
            librarySelected(libraries[0].path);
    }
    Timer {
        id: autoSelect
        interval: 0
        repeat: false
        onTriggered: picker.ensureSelection()
    }
    Component.onCompleted: autoSelect.start()
    onLibrariesChanged: autoSelect.restart()

    Label { text: "Steam library:" }
    ComboBox {
        objectName: "libraryCombo"
        Layout.fillWidth: true
        Layout.minimumWidth: 100
        visible: picker.choices.length > 1
        model: picker.choices
        textRole: "text"
        currentIndex: picker.choices.findIndex(lib => lib.path === picker.selectedPath)
        onActivated: index => picker.librarySelected(picker.choices[index].path)
        ToolTip.visible: hovered
        ToolTip.text: picker.selectedPath
    }
    Label {
        objectName: "singleLibrary"
        Layout.fillWidth: true
        Layout.minimumWidth: 100
        visible: picker.choices.length <= 1
        text: picker.selectedPath || "No Steam library found"
        elide: Text.ElideMiddle
        ToolTip.visible: hover.hovered
        ToolTip.text: text
        HoverHandler { id: hover }
    }
    ToolButton {
        text: "Rescan"
        icon.name: "view-refresh-symbolic"
        onClicked: picker.rescanRequested()
    }
    ToolButton {
        text: "Browse…"
        icon.name: "folder-open-symbolic"
        onClicked: picker.browseRequested()
    }
}
