import QtQuick
import QtTest
import com.github.catsout.wallpaperEngineKde
import "../../plugin/contents/ui" as Wallpaper

TestCase {
    name: "GlobalSizing"
    when: windowShown
    visible: true
    width: 1100
    height: 750
    GlobalConfig { id: shared }

    function openConfig(component, displayMode) {
        // Sizing does not depend on this machine's Steam downloads or previews.
        return component.createObject(this, {
            cfg_DisplayMode: displayMode || Wallpaper.Common.DisplayMode.Aspect,
            cfg_CustomConf: "eyJmYXZvciI6W119",
            steamDetect: {libraries: [], projectDirs: [], allProjectDirs: [], configFile: "", globalConfig: ""}
        });
    }
    // Run with an isolated XDG_CONFIG_HOME. Preserve values for interactive use.
    function test_sharedControlsAndRestore() {
        const originalEnabled = shared.get("SizingEnabled", false);
        const originalSizing = shared.get("SizingMode", Wallpaper.Common.DisplayMode.Crop);
        const originalGlobal = shared.enabled;
        const component = Qt.createComponent("../../plugin/contents/ui/config.qml");
        compare(component.status, Component.Ready, component.errorString());
        shared.set("SizingEnabled", false);
        shared.set("SizingMode", Wallpaper.Common.DisplayMode.Crop);
        shared.enabled = false;
        const first = openConfig(component, Wallpaper.Common.DisplayMode.Aspect);
        const second = openConfig(component, Wallpaper.Common.DisplayMode.Scale);
        verify(first && second);
        let reopened = null;
        try {
            compare(first.globalSizingEnabled, false);
            compare(second.globalSizingMode, Wallpaper.Common.DisplayMode.Crop);
            const firstToggle = findChild(first, "globalSizingEnabled");
            const secondToggle = findChild(second, "globalSizingEnabled");
            const selector = findChild(second, "globalSizingMode");
            verify(firstToggle && secondToggle && selector);
            compare(selector.enabled, false);
            first.setGlobalSizingEnabled(true);
            compare(second.globalSizingEnabled, true);
            compare(firstToggle.checked, true);
            compare(secondToggle.checked, true);
            compare(selector.enabled, true);
            compare(selector.currentIndex, 0);
            compare(shared.enabled, false);

            selector.activated(1);
            compare(first.globalSizingMode, Wallpaper.Common.DisplayMode.Scale);
            compare(second.globalSizingMode, Wallpaper.Common.DisplayMode.Scale);
            compare(selector.currentIndex, 1);
            selector.activated(0);
            compare(first.globalSizingMode, Wallpaper.Common.DisplayMode.Crop);
            compare(selector.currentIndex, 0);

            // Shared sizing never overwrites each screen's saved Display value.
            compare(first.cfg_DisplayMode, Wallpaper.Common.DisplayMode.Aspect);
            compare(second.cfg_DisplayMode, Wallpaper.Common.DisplayMode.Scale);
            second.cfg_DisplayMode = Wallpaper.Common.DisplayMode.Aspect;
            first.setGlobalSizingEnabled(false);
            compare(second.globalSizingEnabled, false);
            compare(selector.enabled, false);
            compare(first.cfg_DisplayMode, Wallpaper.Common.DisplayMode.Aspect);
            compare(second.cfg_DisplayMode, Wallpaper.Common.DisplayMode.Aspect);

            shared.enabled = true;
            first.setGlobalSizingEnabled(true);
            first.setGlobalSizingMode(Wallpaper.Common.DisplayMode.Scale);
            compare(second.globalSizingEnabled, true);
            compare(second.globalSizingMode, Wallpaper.Common.DisplayMode.Scale);
            compare(shared.enabled, true);
            reopened = openConfig(component, Wallpaper.Common.DisplayMode.Aspect);
            verify(reopened);
            compare(reopened.globalSizingEnabled, true);
            compare(reopened.globalSizingMode, Wallpaper.Common.DisplayMode.Scale);
            // Exercise the Switch's immediate write handler as well as methods.
            secondToggle.checked = false;
            secondToggle.toggled();
            compare(first.globalSizingEnabled, false);
            compare(reopened.globalSizingEnabled, false);
        } finally {
            shared.set("SizingEnabled", originalEnabled);
            shared.set("SizingMode", originalSizing);
            shared.enabled = originalGlobal;
            if(reopened) reopened.destroy();
            first.destroy();
            second.destroy();
        }
        wait(100);
    }
}
