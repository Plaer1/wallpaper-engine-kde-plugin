import QtQuick
import QtTest
import "../../plugin/contents/ui/components" as Components

TestCase {
    name: "SteamLibraryPicker"
    when: windowShown
    visible: true
    width: 800
    height: 100
    Component {
        id: pickerComponent
        Components.SteamLibraryPicker {
            width: 800
            onLibrarySelected: path => selectedPath = path
        }
    }
    function test_singleLibraryAutoselects() {
        const picker = createTemporaryObject(pickerComponent, this, {
            libraries: [{path: "/home/user/.local/share/Steam"}]
        });
        tryCompare(picker, "selectedPath", "/home/user/.local/share/Steam");
        verify(!findChild(picker, "libraryCombo").visible);
        verify(findChild(picker, "singleLibrary").visible);
    }
    function test_multipleLibrariesAndSavedChoice() {
        const picker = createTemporaryObject(pickerComponent, this, {
            libraries: [{path: "/Steam"}, {path: "/mnt/Games"}], selectedPath: "/mnt/Games"
        });
        wait(1);
        compare(picker.selectedPath, "/mnt/Games");
        const combo = findChild(picker, "libraryCombo");
        verify(combo.visible);
        compare(combo.currentIndex, 1);
        combo.activated(0);
        compare(picker.selectedPath, "/Steam");
        picker.libraries = [{path: "/Steam"}, {path: "/mnt/Games"}, {path: "/Another"}];
        wait(1);
        compare(picker.selectedPath, "/Steam");
    }
    function test_noLibraryThenRescan() {
        const picker = createTemporaryObject(pickerComponent, this);
        wait(1);
        compare(picker.selectedPath, "");
        picker.libraries = [{path: "/New/Steam"}];
        tryCompare(picker, "selectedPath", "/New/Steam");
    }
    function test_unavailableManualChoiceIsPreserved() {
        const picker = createTemporaryObject(pickerComponent, this, {
            libraries: [{path: "/Steam"}], selectedPath: "/Offline/Library"
        });
        wait(1);
        compare(picker.selectedPath, "/Offline/Library");
        compare(picker.choices.length, 2);
        compare(findChild(picker, "libraryCombo").currentIndex, 1);
    }
}
