#!/usr/bin/env python3
"""Install/remove the opt-in WE browser selection bridge (no root required)."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile

TAG = '<script src="scripts/we-kde-selection-bridge.js"></script>'
ANCHOR = '<script src="scripts/scripts.js"></script>'
STATE_NAME = "we-kde-selection-bridge.wekde-state.json"
BACKUP_NAME = "we-kde-selection-bridge.js.wekde-original"
STATE_VERSION = 2


def atomic_write(path, content, mode=None):
    path = Path(path)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(content)
    if mode is None:
        mode = path.stat().st_mode & 0o777 if path.exists() else 0o644
    temporary.chmod(mode)
    temporary.replace(path)


def _paths(dist):
    dist = Path(dist)
    return dist / "index.html", dist / "scripts" / "we-kde-selection-bridge.js", \
        dist / STATE_NAME, dist / "scripts" / BACKUP_NAME


def _read_state(state_path):
    try:
        value = json.loads(state_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise SystemExit("Bridge state is unreadable; refusing to change installed files.") from error
    if (not isinstance(value, dict) or value.get("version") != STATE_VERSION
            or not isinstance(value.get("originalBridgeExists"), bool)
            or not isinstance(value.get("originalTagPresent"), bool)):
        raise SystemExit("Bridge state is unsupported; refusing to change installed files.")
    mode = value.get("originalBridgeMode", 0o644)
    if not isinstance(mode, int) or mode < 0 or mode > 0o777:
        raise SystemExit("Bridge state has an invalid original mode; refusing to change installed files.")
    return value


def _save_initial_state(target, state_path, backup_path, original_tag_present):
    original_exists = target.exists()
    original_mode = target.stat().st_mode & 0o777 if original_exists else 0o644
    if original_exists:
        atomic_write(backup_path, target.read_bytes(), original_mode)
    state = {
        "version": STATE_VERSION,
        "originalBridgeExists": original_exists,
        "originalBridgeMode": original_mode,
        "originalTagPresent": original_tag_present,
    }
    atomic_write(state_path, json.dumps(state, sort_keys=True, indent=2).encode("utf-8") + b"\n")


def install(dist, bridge_source):
    index, target, state_path, backup_path = _paths(dist)
    data = index.read_bytes()
    text = data.decode("utf-8")
    if TAG not in text and text.count(ANCHOR) != 1:
        raise SystemExit("Unsupported Wallpaper Engine UI layout; no files changed.")

    if not state_path.exists():
        _save_initial_state(target, state_path, backup_path, TAG in text)
    else:
        _read_state(state_path)  # Validate before replacing any installed file.

    bridge_source = bytes(bridge_source)
    atomic_write(target, bridge_source)
    if TAG not in text:
        backup = index.with_name("index.html.wekde-" + hashlib.sha256(data).hexdigest()[:12] + ".bak")
        if not backup.exists():
            atomic_write(backup, data, index.stat().st_mode & 0o777)
        text = text.replace(ANCHOR, ANCHOR + TAG)
        atomic_write(index, text.encode("utf-8"))


def remove(dist):
    index, target, state_path, backup_path = _paths(dist)
    state = _read_state(state_path) if state_path.exists() else None
    if state and state["originalBridgeExists"] and not backup_path.is_file():
        raise SystemExit("Original bridge backup is missing; refusing to remove the installed bridge.")

    restored_index = None
    if index.exists():
        data = index.read_bytes()
        text = data.decode("utf-8")
        if state and state["originalTagPresent"]:
            if TAG in text:
                restored_index = text
            elif text.count(ANCHOR) == 1:
                restored_index = text.replace(ANCHOR, ANCHOR + TAG)
            else:
                raise SystemExit("Cannot restore the original bridge tag in the edited UI layout.")
        else:
            restored_index = text.replace(TAG, "")

    if state and state["originalBridgeExists"]:
        atomic_write(target, backup_path.read_bytes(), state["originalBridgeMode"])
    elif target.exists():
        target.unlink()

    if restored_index is not None and restored_index.encode("utf-8") != data:
        atomic_write(index, restored_index.encode("utf-8"))

    if state_path.exists():
        state_path.unlink()
    if backup_path.exists():
        backup_path.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("install_dir", type=Path, help="Wallpaper Engine installation directory")
    parser.add_argument("--remove", action="store_true")
    args = parser.parse_args()
    dist = args.install_dir.expanduser().resolve() / "ui/dist"
    if args.remove:
        remove(dist)
        print("Bridge removed. Reopen Wallpaper Engine's browser.")
        return

    source = Path(__file__).with_name("we-kde-selection-bridge.js").read_bytes()
    try:
        install(dist, source)
    except OSError as error:
        raise SystemExit("Bridge installation failed: " + str(error)) from error
    print("Bridge installed. Reopen Wallpaper Engine's browser and choose a downloaded wallpaper.")
    print("Repair after Steam updates: run this command again. Remove: add --remove.")


if __name__ == "__main__":
    main()
