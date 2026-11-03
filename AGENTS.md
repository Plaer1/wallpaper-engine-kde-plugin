# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Wallpaper Engine KDE Plugin — a plugin for KDE Plasma 6 that integrates Wallpaper Engine (Steam) wallpapers into the Linux desktop. Supports Scene (2D), Web, and Video wallpapers.

## Build

```bash
# Initialize submodules (required)
git submodule update --init --force --recursive

# Build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Install
sudo cmake --install build

# Restart plasmashell
systemctl --user restart plasma-plasmashell.service
```

### Standalone viewer for debugging

```bash
# In the src/backend_scene/standalone_view/ directory
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run with Vulkan validation layers
./sceneviewer --valid-layer <steamapps>/common/wallpaper_engine/assets <steamapps>/workshop/content/431960/<id>/scene.pkg
```

## Architecture

### Main components

- **src/** — C++ code for the QML plugin
  - `plugin.cpp` — QML plugin entry point
  - `FileHelper.cpp` — native C++ helper for file operations (replaces Python)
  - `MouseGrabber` — mouse event capture
  - `TTYSwitchMonitor` — TTY switch monitoring via D-Bus

- **src/backend_mpv/** — MPV backend for video wallpapers
  - Uses libmpv with SW render API (`MPV_RENDER_API_TYPE_SW`) + Qt RHI (`QQuickRhiItem`)
  - Hardware decode (VAAPI/nvdec) with software render output — no OpenGL dependency
  - Static library `mpvbackend`

- **src/backend_scene/** — Vulkan renderer for Scene wallpapers
  - `wescene-renderer` — main rendering library
  - `wescene-renderer-qml` — Qt/QML wrapper
  - Requires Vulkan 1.1+

### backend_scene subsystems

- **VulkanRender/** — Vulkan rendering and resource management
- **RenderGraph/** — automatic pass dependency resolution
- **Scene/** — scene parsing and representation
- **Particle/** — particle system
- **Audio/** — audio via miniaudio
- **Vulkan/** — base Vulkan abstraction
- **WP*Parser.cpp** — Wallpaper Engine format parsers (JSON, MDL, shaders, textures)

### QML UI

- **plugin/contents/ui/** — QML settings interface
  - `main.qml` — main plugin window
  - `config.qml` — configuration page
  - `WallpaperListModel.qml` — wallpaper list model

### Third-party libraries (in src/backend_scene/third_party/)

- Eigen — linear algebra
- glslang — GLSL to SPIR-V compilation
- SPIRV-Reflect — SPIR-V shader reflection
- nlohmann/json — JSON parsing
- miniaudio — cross-platform audio

## Dependencies

### Arch Linux
```bash
sudo pacman -S extra-cmake-modules libplasma gst-libav ninja \
base-devel mpv qt6-declarative qt6-websockets qt6-webchannel vulkan-headers cmake lz4
```

### Fedora
```bash
# Add RPM Fusion repos (required for ffmpeg/mpv)
sudo dnf install -y \
    https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm \
    https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm

sudo dnf swap -y ffmpeg-free ffmpeg --allowerasing
sudo dnf install -y ffmpeg-devel --allowerasing

sudo dnf install vulkan-headers plasma-workspace-devel kf6-plasma-devel \
    kf6-kcoreaddons-devel kf6-kpackage-devel gstreamer1-libav \
    lz4-devel mpv-libs-devel qt6-qtbase-private-devel libplasma-devel \
    qt6-qtwebchannel-devel qt6-qtwebsockets-devel cmake extra-cmake-modules
```

## Debugging

plasmashell logs:
```bash
journalctl /usr/bin/plasmashell -f
# or
plasmashell --replace
```

Install `vulkan-validation-layers` for Vulkan debugging.

## Code Style

- C++20
- Formatting: `.clang-format` (4 spaces, 100 character line width)
- Qt 6.7+ / KF6 / Plasma 6 (requires `QQuickRhiItem`)

# My Clanker workflow for Codex

Adopt these working agreements in this repository. Merge them into its `AGENTS.md` without overwriting unrelated instructions. Use repository-relative paths, not paths from another computer. This is a portable workflow, not a requirement to install Open Clank.

## Audit, plan, then implement

1. The parent researches the existing code and relevant references, makes the judgment calls, and records findings as it goes.
2. Write a short **metaplan** linking implementation slices. Organize slices by concern and dependency, not chronology. Each slice gives its outcome, scope/files, dependencies, owner/status, and meaningful acceptance criteria.
3. When I request planning first, finish the audit, notes and concrete plan, summarize it, and ask permission to release the implementation workers. Do not implement during that pass. Reuse approval already given; don't repeatedly ask for it.
4. Workers implement their assigned slices. The parent coordinates dependencies, reviews the results, integrates the work, and reports what changed, what was actually checked, and anything unfinished. Deployment/publication follows the authorization I gave; permission to implement is not automatically permission to deploy.

## Solbots and model choice

- “Solbots” means implementation subagents. When I request **6.1 Solbots**, explicitly select **`gpt-6.1-sol`** for them.
- A more expensive parent can supervise cheaper workers, but **I choose that arrangement**. Do not autonomously switch models or introduce cheaper workers.
- Without an explicit model choice, workers inherit the parent's model. Report an unavailable requested model instead of silently substituting.
- Read-only Luna workers are permitted only when I explicitly authorize them; the supervising agent keeps the judgment calls.
- Give each worker a bounded assignment, file ownership, relevant slice paths, and the required return evidence. All workers follow these same note rules. Prefer internal subagents over creating separate user-facing chats.

## Folder layout

```text
AGENTS.md                         durable working agreements
.clanker/                         private; Git-ignored
  futures/
    <topic>.md                    short metaplan / status / links
    <topic>/
      <concern>.md                individual plan slices
  robonotes/
    <topic-or-mirrored-path>/
      00-index.md                 current status, decisions, links
      <concern>.md                focused findings / implementation evidence
  references/                    temporary external study material
  artifacts/                     screenshots, receipts, raw logs
  archive/                       deliberately retired material
  hexes/                         optional Open Clank policy configuration
```

Use singular `.clanker/`. Keep all of it private and Git-ignored. Never force-add references or make application/build code depend on reference copies. Put durable conclusions in notes with provenance. Archive deliberately, preserving relative paths and links; don't automatically delete old material.

## Slice notes by meaning

- Use small, coherent files with **one concern per slice**. Boundaries are conceptual and judgment-based, **not hard byte or line limits**.
- Mirror source paths for local concerns; use topic folders for cross-cutting work. Keep folder roots navigable.
- Indexes contain current status, decisions, next steps and links—not the entire investigation. Read only the index and relevant slices when resuming.
- Record concise conclusions, evidence pointers, uncertainties and decisions. Keep raw logs in artifacts. Avoid copied transcripts, repeated inventories and chronological status piles.
- Update the existing relevant slice; don't duplicate worker detail in the parent's notes. Keep plans and notes connected.

## Continuity and discipline

Resume existing tasks and the **same workers** after interruptions; resend their unfinished assignments when needed. Don't replace them, redo finished work, or silently switch models. Report a specific recovery failure. Never create automatic worker-restart loops. An explicit pause persists until I authorize resumption.

Use one agreed integration branch. Give workers disjoint ownership; create extra branches/worktrees only when necessary and track their integration/retirement. Preserve recoverable preimages of existing work. Don't claim everything is consolidated without evidence.

Run focused checks appropriate to actual changes. Don't create a sprawling test campaign, repeatedly rerun passing checks, or turn debugging the test machinery into the project. Clearly distinguish implemented, checked, built, deployed and published.

If this repository already uses Open Clank Hexes, run `openclank hex explain <path>` before project writes. Edit canonical `.clanker/hexes/contract.yaml` and regenerate the digest with `openclank hex sync`; rule changes require owner authorization and executable hooks/checks need separate trust. Without Hexes, use `AGENTS.md` directly—don't invent a new policy stack.
