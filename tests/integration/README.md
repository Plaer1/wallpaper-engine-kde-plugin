# Native QML integration checks

Build/install the native QML module, then run from the repository root:

```sh
XDG_CONFIG_HOME="$(mktemp -d)" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  /usr/lib/qt6/bin/qmltestrunner -input tests/integration
```

These checks exercise rapid settings destruction, shared audio state, Qt Multimedia output removal, and actual Chromium Web Audio/HTML audio suspend and resume while animation continues. Web fixtures use local files like real wallpapers: Chromium does not inject document-creation scripts into data URLs. Config reconciliation checks write shared settings, and the current GlobalConfig API cannot remove keys; always run these checks with an isolated `XDG_CONFIG_HOME`.
