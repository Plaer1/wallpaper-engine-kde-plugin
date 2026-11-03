# Wallpaper Engine for KDE (Plasma 6)

A wallpaper plugin integrating [Wallpaper Engine](https://store.steampowered.com/app/431960/Wallpaper_Engine) into KDE Plasma wallpaper settings.

- **Native file operations** — C++, no Python
- **KDE 6.5+ theme reactivity** — UI stays visible when switching between light and dark themes
- **Plasma 6 / Qt 6**
- **Span Across Screens** — show each screen's part of one desktop-sized wallpaper

## Hyprland and Omarchy

The [`hyprland/`](hyprland/README.md) directory contains a separate host that shows web wallpapers
as a Wayland background on Hyprland without Plasma. It has its own build
(`cmake -B build/hyprland -S hyprland`) and currently supports web wallpapers only; scene and
video wallpapers remain KDE-only for now. The KDE plugin build below is unaffected.

## Install

### Arch Linux (AUR)
```sh
yay -S wallpaper-engine-kde-plugin-git
# or
paru -S wallpaper-engine-kde-plugin-git
```

### Fedora / rpm-ostree / Bazzite (RPM)

A prebuilt RPM from February 2026 is available from
[CaptSilver's releases](https://github.com/CaptSilver/wallpaper-engine-kde-plugin/releases). It predates
the current code, so for the current version build the RPM yourself as described under
[Build RPM package](#build-rpm-package-fedora).

```sh
curl -LO https://github.com/CaptSilver/wallpaper-engine-kde-plugin/releases/download/v1.0/wallpaper-engine-kde-plugin-qt6-0-1.fc43.x86_64.rpm
```

Install:
```sh
# Standard Fedora
sudo dnf install ./wallpaper-engine-kde-plugin-qt6-0-1.fc43.x86_64.rpm

# rpm-ostree / Bazzite
rpm-ostree install ./wallpaper-engine-kde-plugin-qt6-0-1.fc43.x86_64.rpm
```

### Build from source

#### Dependencies

Arch:
```sh
sudo pacman -S extra-cmake-modules plasma-framework gst-libav ninja \
base-devel mpv qt6-declarative qt6-webchannel vulkan-headers cmake lz4 \
kglobalaccel kxmlgui
```

Fedora:
```sh
# Add RPM Fusion repos (required for ffmpeg/mpv)
sudo dnf install -y \
    https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm \
    https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm

# Replace ffmpeg-free with full ffmpeg
sudo dnf swap -y ffmpeg-free ffmpeg --allowerasing
sudo dnf install -y ffmpeg-devel --allowerasing

sudo dnf install vulkan-headers plasma-workspace-devel kf6-plasma-devel \
    kf6-kcoreaddons-devel kf6-kpackage-devel \
    kf6-kglobalaccel-devel kf6-kxmlgui-devel gstreamer1-libav \
    lz4-devel mpv-libs-devel qt6-qtbase-private-devel libplasma-devel \
    qt6-qtwebchannel-devel cmake extra-cmake-modules
```

#### Build and Install
```sh
# Download source
git clone https://github.com/RainyPixel/wallpaper-engine-kde-plugin.git
cd wallpaper-engine-kde-plugin

# Download submodules
git submodule update --init --force --recursive

# Configure and build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Install (system-wide)
sudo cmake --install build

# Restart plasmashell
systemctl --user restart plasma-plasmashell.service
```

To run native, scene parser, shader compiler and QML tests, configure with
`-DBUILD_TESTS=ON`, build, then run `ctest --test-dir build --output-on-failure`.
QML tests additionally need Qt 6.7+ Quick Test, Quick Controls/Layouts and Kirigami
runtime modules.

#### Build RPM package (Fedora)

Useful for rpm-ostree/Bazzite systems where layered packages survive updates.

On Bazzite and other rpm-ostree hosts `dnf` and `rpmbuild` do not run directly, so do the build
inside a Fedora toolbox or distrobox of the same release as the host (`rpm -E %fedora` prints it)
and skip the tmpfs mount there. Your home directory is shared with the container, so afterwards
layer the RPM from the host.

```sh
git clone https://github.com/RainyPixel/wallpaper-engine-kde-plugin.git
cd wallpaper-engine-kde-plugin

# Install build dependencies from spec
sudo dnf builddep ./rpm/wek.spec

# Initialise submodules
git submodule update --init --force --recursive

# Use tmpfs for the build directory to avoid slow disk writes
sudo mount -t tmpfs tmpfs ~/rpmbuild/BUILD

# Build the RPM
rpmbuild --define="commit $(git rev-parse HEAD)" \
    --define="reporoot $(pwd)" \
    --define="glslang_ver 11.8.0" \
    --undefine=_disable_source_fetch \
    -ba ./rpm/wek.spec

sudo umount ~/rpmbuild/BUILD
# Install (rpm-ostree example)
rpm-ostree install ~/rpmbuild/RPMS/x86_64/wallpaper-engine-kde-plugin-qt6-*.rpm
```

## Activate in Plasma

After installing via any method:

1. Right-click the desktop → **Configure Desktop and Wallpaper...**
2. Open the **Wallpaper Type** dropdown and select **Wallpaper Engine for KDE**
3. Your subscribed Workshop wallpapers appear in the list — select one and click **Apply**

Steam libraries are detected automatically, including native, Flatpak and Snap installs and libraries on other drives. A single library is selected immediately. In the Wallpapers tab, open **Settings** to reveal the Steam library dropdown, account scope, **Rescan**, and **Browse…** controls. Rescan discovers newly added libraries and downloaded content; Browse remains available for custom locations. Saved selections are preserved, including temporarily disconnected drives. With multiple Wallpaper Engine accounts, browsing and Follow choices are account-specific; select the account whose saved wallpaper you want to use.

Enable **Follow Wallpaper Engine’s current selection** on the Wallpapers tab and click Apply once to use Wallpaper Engine’s own UI for subsequent changes. Select an account and source explicitly; choices can be account-specific, an existing legacy session section, or a user-associated legacy mapping. A legacy mapping is offered only after you associate it with a verified account and the saved section/config still match. Account identity for Wallpaper Engine running under Proton has not been verified, so the plugin does not claim an identified-current-account choice. Following reads saved wallpaper selections; it does not recreate Wallpaper Engine’s unsaved playlist playback state or copy its wallpaper-property settings.

Under Proton, Wallpaper Engine may leave its applied wallpaper map empty even after a browser selection. Install the included version 2 compatibility bridge, then reopen Wallpaper Engine’s browser and choose a downloaded wallpaper:

```sh
python3 tools/install-we-selection-bridge.py "$HOME/.local/share/Steam/steamapps/common/wallpaper_engine"
```

Adjust the install directory if Steam uses another library. This adds a small script to Wallpaper Engine’s UI (with a backup of its HTML entry point); the script records selections by Steam account in the existing browser settings. It uses Angular UI change notifications, with no timer. Rerun the same command after Steam replaces the UI during updates/verification; append `--remove` to uninstall the bridge and restore the backed-up entry point. The follower uses bridge data only when no native applied selection is available.

The picker also watches Steam project directories and metadata, so downloads arriving while settings are open appear automatically, including when Steam creates the directory before writing its project file. These watches stop when the list is inactive. Follow uses filesystem notifications, a 180 ms debounce and background file parsing; account observation for tray source choices is event-driven and enabled only for a registered desktop wallpaper target. No recurring catalog polling is used.

The follower uses filesystem notifications, a 180 ms debounce and background file parsing. There is no recurring polling. Atomic config replacement is handled, repeated selections do not restart playback, and incomplete writes or unavailable wallpapers leave the last working background in place. KDE random wallpaper rotation is suspended while following. Disable the option to return to your manual KDE selection.

**Settings → Global sizing** overrides scene and video sizing on every screen immediately. Enable it and choose **Fill (crop)** to preserve proportions while filling the screen, or **Stretch** to fill without preserving proportions. Turn it off to restore each wallpaper’s own Display setting. This works independently of shared wallpaper mode; web wallpapers use their page layout.

**Settings → Mute all wallpapers** is a persistent master switch that takes effect immediately on all screens, independently of shared wallpaper mode and per-wallpaper audio overrides. MPV disables the audio track; scene wallpapers release their audio device; Qt Multimedia detaches its output and disables the audio track. Web wallpapers suspend Web Audio contexts and pause audio-only media where supported. Chromium still requires an output-mute fallback for embedded video and unsupported web audio APIs. Unmuting restores each wallpaper’s own mute setting.

### Playback and tray controls

Plasma also provides a **Wallpaper Engine Controls** panel applet with a compact menu: **Pause** or **Resume**, **Configure Controls**, and **Desktop and Wallpaper Settings**. Configure Controls includes the optional global pause/resume hotkey. Resume can temporarily override ordinary primary-monitor window coverage; fullscreen and other hard pause reasons still prevent playback. The applet is a client of the in-process wallpaper runtime; it does not start a separate daemon.

A shared **Settings → Playback** rule pauses all wallpaper screens when the selected condition is met on the primary monitor. The choices are primary monitor mostly covered (the default, including tiled windows), focused window, maximized window, focused or maximized window, any window, and fullscreen only. Fullscreen applications always pause every wallpaper screen. The resume delay applies after the selected primary-monitor window rule clears and is disabled for fullscreen-only mode. Resume can temporarily override ordinary window-based pauses; fullscreen and other hard pause conditions remain in force.

Runtime state and wallpaper/file changes are updated from notifications and model events rather than idle polling. This includes the desktop visibility monitor and account observation used by registered wallpaper targets. Runtime reports the playback policy dispatched to the backend; it does not independently confirm that rendered frames have stopped or resumed.

Enable **Span Across Screens** under Display to lay out a wallpaper over the bounding rectangle
of all screens. Use **Same wallpaper on all screens** and the same Display mode for a continuous
image. Each backend uses the full desktop size; compatible scenes can share rendering with
**Mirror Scene**. The option is off by default.

> **Note:** After an rpm-ostree/Bazzite install you may need to reboot before the plugin starts working. For cmake installs, restarting plasmashell is enough: `systemctl --user restart plasma-plasmashell.service`

If the list stays empty and you copied the plugin into
`~/.local/share/plasma/wallpapers/com.github.catsout.wallpaperEngineKde` at some point, remove that
copy. Plasma prefers it over the installed package, and it stops matching the plugin's native part
as soon as either one is updated.

### Uninstall
1. Remove files listed in `build/install_manifest.txt`
2. `kpackagetool6 -t Plasma/Wallpaper -r com.github.catsout.wallpaperEngineKde`

## Usage
1. *Wallpaper Engine* installed on Steam
2. Subscribe to some wallpapers on the Workshop
3. Open the Wallpapers tab — the Steam library is found automatically
   - Use **Rescan** for new libraries, the dropdown to switch libraries, or **Browse…** for a custom folder
   - The *steamlibrary* is the one containing the *steamapps* folder

## Requirements
- KDE Plasma 6
- Qt 6.7+ (requires `QQuickRhiItem`)
- Vulkan 1.1+
- C++20 (GCC 10+)
- [Vulkan driver](https://wiki.archlinux.org/title/Vulkan#Installation) installed (AMD users: use RADV)

## Known Issues
- Some scene wallpapers may **crash** KDE
  - Remove `WallpaperSource` line in `~/.config/plasma-org.kde.plasma.desktop-appletsrc` and restart KDE to fix
- Mouse long press (to enter panel edit mode) is broken on desktop
- Screen Locking is not supported
- Identifying the current Wallpaper Engine account under Proton is not verified; that choice remains unavailable.
- Next wallpaper in shared GlobalMode can currently advance only through the primary target and may report a partial result for other selected targets.
- Qt Multimedia reload cannot yet confirm that a replacement wallpaper reached its first frame.
- Shared playback modes were checked live in a configured Plasma 6/Wayland session. Preview exclusion, multi-output activity mapping, and mirrored/spanned edge cases still need additional host validation.
- Settings-dialog conflict handling observes same-object QML writes; arbitrary external C++ or on-disk edits are not guaranteed to refresh an open dialog.

## Support Status

### Scene (2D)
Supported by Vulkan 1.1. Requires *Wallpaper Engine* installed for assets.

### Web
Basic web APIs supported. WebGL may not work properly.

### Video
- **QtMultimedia** (default) — uses GStreamer
- **MPV** — requires plugin lib compilation

Video embedded in a scene's `.tex` file is not played yet. Such textures use a transparent
placeholder instead of uploading compressed video bytes to the GPU; the other scene layers
can still render. Ordinary video wallpapers are unaffected.

## Acknowledgments
- [RePKG](https://github.com/notscuffed/repkg)
- All open-source libraries used in this project
