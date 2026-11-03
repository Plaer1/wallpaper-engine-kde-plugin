#include <QQmlExtensionPlugin>
#include <QQmlEngine>
#include <array>
#include "MpvBackend.hpp"
#include "SceneBackend.hpp"
#include "MouseGrabber.hpp"
#include "TTYSwitchMonitor.hpp"
#include "PluginInfo.hpp"
#include "FileHelper.hpp"
#include "WallpaperEngineFollower.hpp"
#include "ProjectDirectoryWatcher.hpp"
#include "GamemodeMonitor.hpp"
#include "WallpaperSyncBus.hpp"
#include "GlobalConfigStore.hpp"
#include "WallpaperRuntimeRegistration.hpp"
#include "WallpaperRuntimeQmlClient.hpp"
#include "WallpaperRuntimeHost.hpp"
#include "DesktopSettingsInvoker.hpp"
#include "GlobalPlaybackShortcut.hpp"
#include "DesktopCoverageCalculator.hpp"
#include "DesktopVisibilityMonitor.hpp"
#include "PrimaryScreenMonitor.hpp"

constexpr std::array<uint, 2> WPVer { 1, 2 };

class Port : public QQmlExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)

public:
    void registerTypes(const char* uri) override {
        if (strcmp(uri, "com.github.catsout.wallpaperEngineKde") != 0) return;
        qmlRegisterType<wekde::PluginInfo>(uri, WPVer[0], WPVer[1], "PluginInfo");
        qmlRegisterType<wekde::MouseGrabber>(uri, WPVer[0], WPVer[1], "MouseGrabber");
        qmlRegisterType<scenebackend::SceneObject>(uri, WPVer[0], WPVer[1], "SceneViewer");
        std::setlocale(LC_NUMERIC, "C");
        qmlRegisterType<mpv::MpvObject>(uri, WPVer[0], WPVer[1], "Mpv");
        qmlRegisterType<wekde::TTYSwitchMonitor>(uri, WPVer[0], WPVer[1], "TTYSwitchMonitor");
        qmlRegisterType<wekde::WallpaperEngineFollower>(uri, WPVer[0], WPVer[1], "WallpaperEngineFollower");
        qmlRegisterType<wekde::ProjectDirectoryWatcher>(uri, WPVer[0], WPVer[1], "ProjectDirectoryWatcher");
        qmlRegisterType<wekde::FileHelper>(uri, WPVer[0], WPVer[1], "FileHelper");
        qmlRegisterType<wekde::GamemodeMonitor>(uri, WPVer[0], WPVer[1], "GamemodeMonitor");
        qmlRegisterType<wekde::WallpaperSyncBus>(uri, WPVer[0], WPVer[1], "WallpaperSyncBus");
        qmlRegisterType<wekde::GlobalConfig>(uri, WPVer[0], WPVer[1], "GlobalConfig");
        qmlRegisterType<wekde::WallpaperRuntimeRegistration>(uri, WPVer[0], WPVer[1],
            "WallpaperRuntimeRegistration");
        qmlRegisterType<wekde::WallpaperRuntimeQmlEndpoint>(uri, WPVer[0], WPVer[1],
            "WallpaperRuntimeEndpoint");
        qmlRegisterType<wekde::DesktopSettingsInvoker>(uri, WPVer[0], WPVer[1],
            "DesktopSettingsInvoker");
        qmlRegisterType<wekde::GlobalPlaybackShortcut>(uri, WPVer[0], WPVer[1],
            "GlobalPlaybackShortcut");
        qmlRegisterType<wekde::WallpaperRuntimeQmlClient>(uri, WPVer[0], WPVer[1],
            "WallpaperRuntimeClient");
        qmlRegisterType<wekde::DesktopCoverageCalculator>(uri, WPVer[0], WPVer[1],
            "DesktopCoverageCalculator");
        qmlRegisterType<wekde::DesktopVisibilityMonitor>(uri, WPVer[0], WPVer[1],
            "DesktopVisibilityMonitor");
        qmlRegisterType<wekde::PrimaryScreenMonitor>(uri, WPVer[0], WPVer[1],
            "PrimaryScreenMonitor");
    }
};

#include "plugin.moc"
