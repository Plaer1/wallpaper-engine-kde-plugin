#pragma once

#include <QVector>

namespace WallpaperPlaybackPolicy {

enum class Override {
    Automatic,
    Paused,
    PlayDespiteWindows,
};

enum class Reason {
    Sleep,
    InactiveSession,
    Manual,
    Battery,
    GameMode,
    WindowCoverage,
    FullscreenApp,
};

enum class EffectivePlayback {
    Playing,
    Paused,
};

struct Conditions {
    bool sleep = false;
    bool inactiveSession = false;
    bool battery = false;
    bool gameMode = false;
    bool windowCoverage = false;
    // Global across wallpaper outputs for the current activity/virtual desktop.
    bool fullscreenApp = false;
};

struct Resolution {
    Override desired = Override::Automatic;
    EffectivePlayback effective = EffectivePlayback::Playing;
    QVector<Reason> pauseReasons;
};

// Resolves session intent against the current automatic pause conditions.
// This function has no side effects; callers retain `desired` across reloads.
Resolution resolve(Override desired, const Conditions &conditions);

} // namespace WallpaperPlaybackPolicy
