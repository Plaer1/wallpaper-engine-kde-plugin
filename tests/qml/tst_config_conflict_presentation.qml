import QtQuick
import QtTest
import "../../plugin/contents/ui/components" as Components
import "../../plugin/contents/ui/js/config-draft-reconciler.mjs" as Draft

TestCase {
    id: testCase
    name: "ConfigConflictPresentation"
    when: windowShown
    visible: true
    width: 620
    height: 520

    property var state: null
    property var lastChoice: null

    Component {
        id: presentationComponent
        Components.ConfigConflictPresentation {
            anchors.fill: parent
            reconcilerState: testCase.state
            fieldLabels: ({WallpaperEngineAccount: "Follow account", Fps: "Frame rate"})
            onChoiceRequested: (key, choice) => testCase.lastChoice = {key, choice}
        }
    }

    function init() {
        state = Draft.create({WallpaperEngineAccount: "steam:A", Fps: 30});
        lastChoice = null;
    }

    function presentation() {
        const item = createTemporaryObject(presentationComponent, testCase);
        verify(item);
        return item;
    }

    function test_conflictValuesAccessibleAndKeyboardKeepLiveChoice() {
        state = Draft.stage(state, "WallpaperEngineAccount", "steam:B");
        state = Draft.updateLive(state, {WallpaperEngineAccount: "steam:C"});
        const view = presentation();
        verify(view.visible);
        compare(view.conflictKeys, ["WallpaperEngineAccount"]);
        compare(findChild(view, "conflict-field-WallpaperEngineAccount").text,
            "Conflicting setting: Follow account");
        compare(findChild(view, "conflict-live-WallpaperEngineAccount").text,
            "Newer live value: steam:C");
        compare(findChild(view, "conflict-draft-WallpaperEngineAccount").text,
            "Your edit: steam:B");

        const button = findChild(view, "keep-live-WallpaperEngineAccount");
        verify(button);
        compare(button.Accessible.name, "Keep the newer live value for Follow account");
        button.forceActiveFocus();
        keyClick(Qt.Key_Space);
        compare(lastChoice, {key: "WallpaperEngineAccount", choice: "keepLive"});
    }

    function test_useMyEditChoiceAndSecondConflictAfterNewLiveChange() {
        state = Draft.stage(state, "WallpaperEngineAccount", "steam:B");
        state = Draft.updateLive(state, {WallpaperEngineAccount: "steam:C"});
        const view = presentation();
        const button = findChild(view, "use-my-edit-WallpaperEngineAccount");
        verify(button);
        compare(button.Accessible.name, "Use your edit for Follow account");
        button.forceActiveFocus();
        keyClick(Qt.Key_Space);
        compare(lastChoice, {key: "WallpaperEngineAccount", choice: "useMyEdit"});

        state = Draft.resolveConflict(state, lastChoice.key, lastChoice.choice);
        compare(Draft.conflict(state, "WallpaperEngineAccount"), null);
        compare(Draft.exports(state, ["WallpaperEngineAccount"]).WallpaperEngineAccount, "steam:B");

        state = Draft.updateLive(state, {WallpaperEngineAccount: "steam:D"});
        view.reconcilerState = state;
        compare(view.conflictKeys, ["WallpaperEngineAccount"]);
        compare(findChild(view, "conflict-live-WallpaperEngineAccount").text,
            "Newer live value: steam:D");
        compare(findChild(view, "conflict-draft-WallpaperEngineAccount").text,
            "Your edit: steam:B");

        const keepLive = findChild(view, "keep-live-WallpaperEngineAccount");
        keepLive.forceActiveFocus();
        keyClick(Qt.Key_Space);
        compare(lastChoice, {key: "WallpaperEngineAccount", choice: "keepLive"});
    }

    function test_noConflictsHidePresentationAndMultipleConflictsHaveSeparateRows() {
        const view = presentation();
        compare(view.visible, false);
        state = Draft.stage(state, "WallpaperEngineAccount", "steam:B");
        state = Draft.stage(state, "Fps", 45);
        state = Draft.updateLive(state, {WallpaperEngineAccount: "steam:C", Fps: 60});
        view.reconcilerState = state;
        compare(view.conflictKeys, ["Fps", "WallpaperEngineAccount"]);
        verify(findChild(view, "keep-live-Fps"));
        verify(findChild(view, "use-my-edit-WallpaperEngineAccount"));
    }
}
