import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kquickcontrols

Kirigami.FormLayout {
    id: root

    property string cfg_GlobalPauseShortcut

    KeySequenceItem {
        id: shortcutEditor
        Kirigami.FormData.label: qsTr("Pause or resume wallpapers:")
        keySequence: root.cfg_GlobalPauseShortcut
        showClearButton: true
        onKeySequenceModified: root.cfg_GlobalPauseShortcut = keySequence.toString()
    }

    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("The shortcut applies to all active Wallpaper Engine screens. Clear it to disable the global shortcut.")
        color: Kirigami.Theme.disabledTextColor
    }
}
