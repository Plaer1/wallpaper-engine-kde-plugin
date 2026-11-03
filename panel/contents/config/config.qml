import QtQuick
import org.kde.plasma.configuration

ConfigModel {
    ConfigCategory {
        name: i18n("Shortcuts")
        icon: "input-keyboard"
        source: "config/ShortcutPage.qml"
    }
}
