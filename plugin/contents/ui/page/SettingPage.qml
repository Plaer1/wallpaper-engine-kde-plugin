import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.5 as QSMat
import QtQuick.Layouts 1.5

import ".."
import "../components"
import "../js/utils.mjs" as Utils

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.kirigami 2.6 as Kirigami
import com.github.catsout.wallpaperEngineKde

Flickable {
    id: settingTab

    // Наследуем тему от родителя
    Kirigami.Theme.inherit: true

    GlobalConfig {
        id: globalCfg
    }

    property alias cfg_Fps: sliderFps.value
    property alias cfg_Volume: sliderVol.value
    property alias cfg_MpvStats: ckbox_mpvStats.checked
    property alias cfg_SceneCachePasses: ckbox_cacheScenePasses.checked
    property alias cfg_ShareGpuContext: ckbox_shareGpuContext.checked
    property alias cfg_MirrorScene: ckbox_mirrorScene.checked
    property alias cfg_SpanScreens: ckbox_spanScreens.checked
    property string cfg_MpvHwdec
    property alias cfg_Speed: spin_speed.dValue
    property alias cfg_MuteAudio: ckbox_muteAudio.checked
    property alias cfg_MouseInput: ckbox_mouseInput.checked
    property alias cfg_SwitchTimer: randomSpin.value
    property alias cfg_RandomizeWallpaper: ckbox_randomizeWallpaper.checked
    property alias cfg_NoRandomWhilePaused: ckbox_noRandomWhilePaused.checked
    property alias cfg_GlobalMode: ckbox_globalMode.checked
    property int draftWindowPauseMode: 6
    property int draftWindowResumeDelay: 300
    signal windowPauseModeSelected(int mode)
    signal windowResumeDelaySelected(int milliseconds)

    property alias cfg_PauseOnBatPower: chkbox_pauseOnBatPower.checked
    property alias cfg_PauseBatPercent: spin_pauseBatPercent.value

    property alias cfg_PauseOnGamemode: chkbox_pauseOnGameMode.checked

    Layout.fillWidth: true
    ScrollBar.vertical: ScrollBar { id: scrollbar }

    contentWidth: width - (scrollbar.visible ? scrollbar.width : 0)
    contentHeight: contentItem.childrenRect.height
    clip: true
    boundsBehavior: Flickable.OvershootBounds

    OptionGroup {
        header.visible: false
        anchors.left: parent.left
        anchors.right: parent.right


        OptionGroup {
            Layout.fillWidth: true
            header.text: "Playback"
            header.text_color: Kirigami.Theme.textColor
            header.icon: '../../images/cheveron-down.svg'
            header.color: Kirigami.Theme.activeBackgroundColor

            OptionItem {
                text: 'Pause all wallpaper screens when'
                text_color: Kirigami.Theme.textColor
                icon: '../../images/pause.svg'
                actor: ComboBox {
                    objectName: "windowPauseModeCombo"
                    model: [
                        {
                            text: "Primary monitor is mostly covered",
                            value: 6
                        },
                        {
                            text: "Focused window",
                            value: 3
                        },
                        {
                            text: "Maximized window",
                            value: 2
                        },
                        {
                            text: "Focused or maximized window",
                            value: 4
                        },
                        {
                            text: "Any window",
                            value: 1
                        },
                        {
                            text: "Fullscreen only",
                            value: 0
                        }
                    ]
                    textRole: "text"
                    currentIndex: model.findIndex(item => item.value === settingTab.draftWindowPauseMode)
                    onActivated: index => settingTab.windowPauseModeSelected(model[index].value)
                }
                contentBottom: ColumnLayout {
                    Text {
                        objectName: "pausePolicyDescription"
                        Layout.fillWidth: true
                        color: Kirigami.Theme.disabledTextColor
                        text: "The selected window rule is checked on the primary monitor and pauses all wallpaper screens. Fullscreen apps always pause every screen."
                        wrapMode: Text.Wrap
                    }
               }
            }
            OptionItem {
                text: 'Resume delay'
                text_color: Kirigami.Theme.textColor
                icon: '../../images/timer.svg'
                actor: RowLayout {
                    SpinBox {
                        objectName: "windowResumeDelaySpin"
                        from: 1
                        to: 60 * 1000
                        stepSize: 50
                        value: settingTab.draftWindowResumeDelay
                        enabled: settingTab.draftWindowPauseMode !== 0
                        Accessible.name: qsTr("Wait before resuming wallpaper playback, in milliseconds")
                        onValueModified: settingTab.windowResumeDelaySelected(value)
                    }
                    Label {
                        text: "ms"
                        color: Kirigami.Theme.textColor
                    }
                }
                contentBottom: ColumnLayout {
                    Text {
                        objectName: "windowResumeDelayDescription"
                        Layout.fillWidth: true
                        color: Kirigami.Theme.disabledTextColor
                        text: "Wait before resuming after the primary-monitor window rule clears."
                        wrapMode: Text.Wrap
                    }
                }
            }
            OptionItem {
                text: 'Pause if PC is on battery power'
                text_color: Kirigami.Theme.textColor
                actor: Switch {
                    id: chkbox_pauseOnBatPower
                }
            }
            OptionItem {
                // Independent of "pause on battery power": the runtime applies this
                // threshold on its own (0 = off), so keep it reachable to reset.
                text: 'Pause if battery level is below'
                text_color: Kirigami.Theme.textColor
                actor: RowLayout {
                    SpinBox {
                        id: spin_pauseBatPercent
                        from: 0
                        to: 100
                        stepSize: 1
                    }
                    Label { text: " %"; color: Kirigami.Theme.textColor }
                }
            }
            OptionItem {
                text: 'Pause if gamemode is started'
                text_color: Kirigami.Theme.textColor
                actor: Switch {
                    id: chkbox_pauseOnGameMode
                }
            }
            OptionItem {
                text: 'Randomize Timer'
                text_color: Kirigami.Theme.textColor
                icon: '../../images/time.svg'
                actor: Switch {
                    id: ckbox_randomizeWallpaper
                }
                contentBottom: ColumnLayout {
                    Text {
                        Layout.fillWidth: true
                        color: Kirigami.Theme.disabledTextColor
                        text: "Randomize wallpapers filtered in the 'Wallpapers' page"
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: ckbox_randomizeWallpaper.checked
                        Label {
                            id:heightpicker
                            text: "Randomize every "
                            color: Kirigami.Theme.textColor
                        }
                        SpinBox {
                            id: randomSpin
                            width: font.pixelSize * 4
                            from: 1
                            to: 60*24*30
                            stepSize: 1
                        }
                        Label { text: " min"; color: Kirigami.Theme.textColor }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: ckbox_randomizeWallpaper.checked
                        Label {
                            id: randomWhilePausedSetter
                            text: "Skip randomizing while wallpaper is paused  "
                            color: Kirigami.Theme.textColor
                        }
                        Switch {
                            id: ckbox_noRandomWhilePaused
                        }
                    }
                }
            }
            OptionItem {
                text: "Playback Speed"
                text_color: Kirigami.Theme.textColor
                icon: '../../images/fast-forward.svg'
                actor: RowLayout {
                    DoubleSpinBox {
                        id: spin_speed
                        dFrom: 0.1
                        dTo: 16.0
                        dStepSize: 0.1
                    }
                }
            }
        }
        OptionGroup {
            Layout.fillWidth: true
            header.text: "Sources & accounts"
            header.text_color: Kirigami.Theme.textColor
            header.icon: '../../images/cheveron-down.svg'
            header.color: Kirigami.Theme.activeBackgroundColor

            OptionItem {
                text: 'Same wallpaper on all screens'
                text_color: Kirigami.Theme.textColor
                icon: '../../images/window.svg'
                actor: Switch {
                    id: ckbox_globalMode
                    // reflect the real shared state, not this screen's stale copy
                    Component.onCompleted: checked = globalCfg.enabled
                }
                contentBottom: Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.disabledTextColor
                    text: "Share the wallpaper choice and randomization across all screens "
                        + "(one configuration for the whole system). Press Apply to take effect."
                }
            }
        }
        OptionGroup {
            Layout.fillWidth: true
            header.text: "Appearance"
            header.text_color: Kirigami.Theme.textColor
            header.icon: '../../images/cheveron-down.svg'
            header.color: Kirigami.Theme.activeBackgroundColor

            OptionItem {
                text: 'Display'
                text_color: Kirigami.Theme.textColor
                icon: '../../images/window.svg'
                actor: ComboBox {
                    id: displayMode
                    model: [
                        {
                            text: "Keep Aspect Ratio",
                            value: Common.DisplayMode.Aspect
                        },
                        {
                            text: "Scale and Crop",
                            value: Common.DisplayMode.Crop
                        },
                        {
                            text: "Scale to Fill",
                            value: Common.DisplayMode.Scale
                        },
                    ]
                    textRole: "text"
                    onActivated: cfg_DisplayMode = Common.cbCurrentValue(this)
                    Component.onCompleted: currentIndex = Common.cbIndexOfValue(this, cfg_DisplayMode)
                }
            }
            OptionItem {
                text: 'Global sizing'
                text_color: Kirigami.Theme.textColor
                icon: '../../images/window.svg'
                actor: Switch {
                    objectName: "globalSizingEnabled"
                    checked: root.globalSizingEnabled
                    onToggled: root.setGlobalSizingEnabled(checked)
                }
                contentBottom: ColumnLayout {
                    ComboBox {
                        id: globalSizingModeSelector
                        objectName: "globalSizingMode"
                        Layout.fillWidth: true
                        enabled: root.globalSizingEnabled
                        model: [
                            {text: "Fill (crop)", value: Common.DisplayMode.Crop},
                            {text: "Stretch", value: Common.DisplayMode.Scale}
                        ]
                        textRole: "text"
                        currentIndex: root.globalSizingMode === Common.DisplayMode.Scale ? 1 : 0
                        onActivated: index => root.setGlobalSizingMode(
                            Common.cbValueOfIndex(globalSizingModeSelector, index))
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Kirigami.Theme.disabledTextColor
                        text: "Applies immediately to scene and video wallpapers on every screen. "
                            + "Turn off to restore each wallpaper's Display setting."
                    }
                }
            }
            OptionItem {
                text: 'Span Across Screens'
                text_color: Kirigami.Theme.textColor
                icon: '../../images/window.svg'
                actor: Switch {
                    id: ckbox_spanScreens
                }
                contentBottom: Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.disabledTextColor
                    text: "Show each screen's slice of the whole desktop. Enable 'Same wallpaper "
                        + "on all screens' for a continuous image. Each screen renders the full desktop."
                }
            }
        }
        OptionGroup {
            Layout.fillWidth: true
            header.text: "Audio"
            header.text_color: Kirigami.Theme.textColor
            header.icon: '../../images/cheveron-down.svg'
            header.color: Kirigami.Theme.activeBackgroundColor

            OptionItem {
                text: "Mute all wallpapers"
                text_color: Kirigami.Theme.textColor
                icon: '../../images/volume-off.svg'
                actor: Switch {
                    objectName: "globalAudioMute"
                    checked: root.globalAudioMuted
                    onToggled: root.setGlobalAudioMuted(checked)
                }
                contentBottom: Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.disabledTextColor
                    text: "Applies immediately to all screens. Stops wallpaper audio while animations keep running."
                }
            }
            OptionItem {
                text: "Mute Audio"
                text_color: Kirigami.Theme.textColor
                icon: ckbox_muteAudio.checked
                    ? '../../images/volume-off.svg'
                    : '../../images/volume-up.svg'
                actor: Switch {
                    id: ckbox_muteAudio
                }
            }
            OptionItem {
                text: "Volume"
                text_color: Kirigami.Theme.textColor
                visible: !cfg_MuteAudio
                actor: RowLayout {
                    Layout.preferredWidth: displayMode.width
                    Label {
                        Layout.preferredWidth: font.pixelSize * 2
                        text: sliderVol.value.toString()
                        color: Kirigami.Theme.textColor
                    }
                    Slider {
                        id: sliderVol
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        stepSize: 5.0
                        snapMode: Slider.SnapOnRelease
                    }
                }
            }
        }
        OptionGroup {
            Layout.fillWidth: true
            header.text: "Advanced"
            header.text_color: Kirigami.Theme.textColor
            header.icon: '../../images/cheveron-down.svg'
            header.color: Kirigami.Theme.activeBackgroundColor

            OptionItem {
                visible: libcheck.wallpaper
                text_color: Kirigami.Theme.textColor
                text: "Mouse Input"
                icon: '../../images/mouse.svg'
                actor: Switch {
                    id: ckbox_mouseInput
                }
            }
            OptionItem {
                text: 'Video Backend'
                text_color: Kirigami.Theme.textColor
                icon: '../../images/plugin.svg'
                actor: ComboBox {
                    model: [
                        {
                            text: "QtMultimedia",
                            value: Common.VideoBackend.QtMultimedia,
                            enabled: true
                        },
                        {
                            text: "Mpv",
                            value: Common.VideoBackend.Mpv,
                            enabled: libcheck.wallpaper
                        }
                    ].filter(el => el.enabled)
                    textRole: "text"
                    onActivated: cfg_VideoBackend = Common.cbCurrentValue(this)
                    Component.onCompleted: currentIndex = Common.cbIndexOfValue(this, cfg_VideoBackend)
                }
            }
            OptionItem {
                text: 'Show Mpv Stats'
                text_color: Kirigami.Theme.textColor
                icon: '../../images/information-outline.svg'
                visible: cfg_VideoBackend == Common.VideoBackend.Mpv
                actor: Switch {
                    id: ckbox_mpvStats
                }
            }
            OptionItem {
                text: 'Hardware Decode'
                text_color: Kirigami.Theme.textColor
                icon: '../../images/plugin.svg'
                visible: cfg_VideoBackend == Common.VideoBackend.Mpv
                actor: ComboBox {
                    id: cbox_mpvHwdec
                    model: [
                        { text: "Auto",     value: "auto" },
                        { text: "Software", value: "no"   }
                    ]
                    textRole: "text"
                    onActivated: cfg_MpvHwdec = model[currentIndex].value
                    Component.onCompleted: {
                        for (let i = 0; i < model.length; i++) {
                            if (model[i].value === cfg_MpvHwdec) {
                                currentIndex = i;
                                break;
                            }
                        }
                    }
                }
            }
            OptionItem {
                text: 'Fps'
                visible: libcheck.wallpaper
                text_color: Kirigami.Theme.textColor
                icon: '../../images/tuning.svg'
                actor: RowLayout {
                    Label {
                        Layout.preferredWidth: font.pixelSize * 2
                        text: sliderFps.value.toString()
                        color: Kirigami.Theme.textColor
                    }
                    Slider {
                        id: sliderFps
                        Layout.fillWidth: true
                        from: 5
                        to: 60
                        stepSize: 1.0
                        snapMode: Slider.SnapOnRelease
                    }
                }
                contentBottom: ColumnLayout {
                    Text {
                        Layout.fillWidth: true
                        color: Kirigami.Theme.disabledTextColor
                        text: "Low: 10, Medium: 15, High: 25, Ultra High: 30"
                    }
                }

            }
            OptionItem {
                text: 'Cache static layers'
                visible: libcheck.wallpaper
                text_color: Kirigami.Theme.textColor
                icon: '../../images/tuning.svg'
                actor: Switch {
                    id: ckbox_cacheScenePasses
                }
                contentBottom: ColumnLayout {
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Kirigami.Theme.disabledTextColor
                        text: "Render unchanging layers (e.g. blurred backgrounds) once instead of "
                            + "every frame. Saves CPU/GPU. Disable if a wallpaper looks wrong."
                    }
                }
            }
            OptionItem {
                text: 'Share GPU across screens'
                visible: libcheck.wallpaper
                text_color: Kirigami.Theme.textColor
                icon: '../../images/tuning.svg'
                actor: Switch {
                    id: ckbox_shareGpuContext
                }
                contentBottom: ColumnLayout {
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Kirigami.Theme.disabledTextColor
                        text: "On multi-monitor setups, share one GPU device so identical "
                            + "textures load once instead of per screen. Saves VRAM. "
                            + "Takes effect after the wallpaper reloads."
                    }
                }
            }
            OptionItem {
                text: 'Mirror scene across screens'
                visible: libcheck.wallpaper
                text_color: Kirigami.Theme.textColor
                icon: '../../images/tuning.svg'
                actor: Switch {
                    id: ckbox_mirrorScene
                }
                contentBottom: ColumnLayout {
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Kirigami.Theme.disabledTextColor
                        text: "On multi-monitor setups showing the same scene wallpaper, render "
                            + "it once and mirror it to the other screens. Saves the per-screen "
                            + "render-target VRAM. Skipped for wallpapers that react to the cursor. "
                            + "Takes effect after the wallpaper reloads."
                    }
                }
            }
            OptionItem {
                text: 'Shader cache'
                visible: libcheck.wallpaper
                text_color: Kirigami.Theme.textColor
                icon: '../../images/information-outline.svg'
                actor: Kirigami.ActionToolBar {
                    Layout.fillWidth: true
                    alignment: Qt.AlignRight
                    flat: false
                    actions: [
                        Kirigami.Action {
                            text: 'Show'
                            tooltip: 'Show in file manager'
                            onTriggered: {
                                if(plugin_info.cache_path)
                                    Qt.openUrlExternally(plugin_info.cache_path);
                            }
                        }
                    ]
                }
                contentBottom: ColumnLayout {
                    Text {
                        Layout.fillWidth: true
                        property string cache_path: Common.urlNative(plugin_info.cache_path)

                        color: Kirigami.Theme.disabledTextColor
                        text: plugin_info.cache_path
                        ? `${cache_path} - ${cache_size}`
                        : `Not available`

                        property string cache_size: {
                            if(pyext) {
                                pyext.get_dir_size(this.cache_path).then(res => {
                                    this.cache_size = Utils.prettyBytes(res);
                                }).catch(reason => console.error(reason));
                            }
                            return "? MB";
                        }
                    }
                }
            }
        }
    }


}

