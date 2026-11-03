import QtQuick
import QtTest
import com.github.catsout.wallpaperEngineKde

TestCase {
    name: "ConfigLifetime"
    when: windowShown
    visible: true
    width: 1100
    height: 750
    GlobalConfig { id: shared }
    // Run with an isolated XDG_CONFIG_HOME; restores values for interactive runs too.
    function test_globalAudioAcrossInstances() {
        const originalMute = shared.get("AudioDisabled", false);
        const originalMode = shared.enabled;
        const component = Qt.createComponent("../../plugin/contents/ui/config.qml");
        compare(component.status, Component.Ready, component.errorString());
        const first = component.createObject(this);
        const second = component.createObject(this);
        verify(first && second);
        try {
            shared.enabled = false;
            first.setGlobalAudioMuted(true);
            compare(first.globalAudioMuted, true);
            compare(second.globalAudioMuted, true);
            compare(shared.enabled, false);
            const toggle = findChild(second, "globalAudioMute");
            verify(toggle);
            compare(toggle.checked, true);
            second.setGlobalAudioMuted(false);
            compare(first.globalAudioMuted, false);
            compare(toggle.checked, false);
            shared.enabled = true;
            first.setGlobalAudioMuted(true);
            compare(second.globalAudioMuted, true);
        } finally {
            shared.set("AudioDisabled", originalMute);
            shared.enabled = originalMode;
            first.destroy();
            second.destroy();
        }
        wait(50);
    }
    function test_rapidOpenClose() {
        const component = Qt.createComponent("../../plugin/contents/ui/config.qml");
        compare(component.status, Component.Ready, component.errorString());
        for (let i = 0; i < 12; i++) {
            const config = component.createObject(this, {width: 1100, height: 750});
            verify(config);
            config.destroy();
            wait(5);
        }
        wait(250);
    }
}
