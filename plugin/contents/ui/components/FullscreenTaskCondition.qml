import QtQuick 2.5

// Tracks fullscreen windows from a TaskManager model that is not screen-filtered.
// The model remains scoped to the current virtual desktop; activity membership is
// checked explicitly because TasksModel's activity filter is unavailable here.
Item {
    id: root

    property var taskModel: null
    property string activity
    readonly property bool active: _active
    property bool _active: false

    function update() {
        let hasFullscreen = false;
        if (taskModel && typeof taskModel.count === "number"
                && typeof taskModel.getProperty === "function") {
            for (let row = 0; row < taskModel.count; ++row) {
                const index = typeof taskModel.makeModelIndex === "function"
                    ? taskModel.makeModelIndex(row) : row;
                const get = (name) => taskModel.getProperty(index, name);
                if (get("IsWindow") !== true || get("IsMinimized") === true
                        || get("IsFullScreen") !== true) {
                    continue;
                }

                const activities = get("Activities");
                if (activities && activities.length > 0 && activity
                        && activities.indexOf(activity) < 0) {
                    continue;
                }
                hasFullscreen = true;
                break;
            }
        }
        _active = hasFullscreen;
    }

    onTaskModelChanged: update()
    onActivityChanged: update()

    Connections {
        target: root.taskModel
        ignoreUnknownSignals: true
        function onDataChanged() { root.update(); }
        function onCountChanged() { root.update(); }
        function onRowsInserted() { root.update(); }
        function onRowsRemoved() { root.update(); }
        function onModelReset() { root.update(); }
        function onActiveTaskChanged() { root.update(); }
    }
}
