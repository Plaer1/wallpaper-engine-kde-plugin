#include "WallpaperPlaybackPolicy.hpp"

namespace WallpaperPlaybackPolicy {

Resolution resolve(Override desired, const Conditions &conditions)
{
    Resolution result;
    result.desired = desired;

    if (conditions.sleep) {
        result.pauseReasons.append(Reason::Sleep);
    }
    if (conditions.inactiveSession) {
        result.pauseReasons.append(Reason::InactiveSession);
    }
    if (desired == Override::Paused) {
        result.pauseReasons.append(Reason::Manual);
    }
    if (conditions.battery) {
        result.pauseReasons.append(Reason::Battery);
    }
    if (conditions.gameMode) {
        result.pauseReasons.append(Reason::GameMode);
    }
    if (conditions.fullscreenApp && desired != Override::PlayDespiteWindows) {
        result.pauseReasons.append(Reason::FullscreenApp);
    }
    if (conditions.windowCoverage && desired != Override::PlayDespiteWindows) {
        result.pauseReasons.append(Reason::WindowCoverage);
    }

    const bool hardPause = conditions.sleep || conditions.inactiveSession;
    const bool manualPause = desired == Override::Paused;
    const bool automaticPause = conditions.battery || conditions.gameMode;
    const bool windowPause = conditions.windowCoverage && desired == Override::Automatic;

    const bool fullscreenPause = conditions.fullscreenApp
        && desired != Override::PlayDespiteWindows;

    result.effective = hardPause || manualPause || automaticPause || windowPause
            || fullscreenPause
        ? EffectivePlayback::Paused
        : EffectivePlayback::Playing;
    return result;
}

} // namespace WallpaperPlaybackPolicy
