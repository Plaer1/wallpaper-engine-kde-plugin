import QtQuick
import QtTest
import "../../plugin/contents/ui/components" as Components

TestCase {
    name: "WindowModelFullscreen"

    QtObject {
        id: fakeTasks
        property var tasks: []
        property int currentDesktop: 1
        readonly property var visibleTasks: tasks.filter(function(task) {
            return task.onAllDesktops === true
                || !task.virtualDesktops
                || task.virtualDesktops.length === 0
                || task.virtualDesktops.indexOf(currentDesktop) >= 0;
        })
        property int count: visibleTasks.length
        property int propertyReads: 0
        signal dataChanged()

        function makeModelIndex(row) { return row; }
        function getProperty(row, name) {
            propertyReads++;
            return visibleTasks[row][name];
        }
        function replaceTasks(value) {
            tasks = value;
            dataChanged();
        }
    }

    Component {
        id: conditionComponent
        Components.FullscreenTaskCondition {
            taskModel: fakeTasks
            activity: "current-activity"
        }
    }

    function init() {
        fakeTasks.currentDesktop = 1;
        fakeTasks.replaceTasks([]);
        fakeTasks.propertyReads = 0;
    }

    function task(overrides) {
        return Object.assign({
            IsWindow: true,
            IsMinimized: false,
            IsFullScreen: true,
            Activities: ["current-activity"],
            virtualDesktops: [1],
            screenName: "another-output"
        }, overrides || {});
    }

    function test_fullscreenOnAnotherOutputIsGlobalAndExitClears() {
        const condition = createTemporaryObject(conditionComponent, this);
        verify(condition);

        fakeTasks.replaceTasks([task()]);
        tryCompare(condition, "active", true);

        fakeTasks.replaceTasks([task({IsFullScreen: false})]);
        tryCompare(condition, "active", false);

        fakeTasks.replaceTasks([task()]);
        tryCompare(condition, "active", true);
        fakeTasks.replaceTasks([]);
        tryCompare(condition, "active", false);
    }

    function test_minimized_otherActivityAndOtherDesktopAreIgnored() {
        const condition = createTemporaryObject(conditionComponent, this);
        verify(condition);

        fakeTasks.replaceTasks([task({IsMinimized: true})]);
        tryCompare(condition, "active", false);
        fakeTasks.replaceTasks([task({Activities: ["different-activity"]})]);
        tryCompare(condition, "active", false);
        fakeTasks.replaceTasks([task({virtualDesktops: [2]})]);
        tryCompare(condition, "active", false);
        fakeTasks.replaceTasks([task({IsWindow: false})]);
        tryCompare(condition, "active", false);
    }

    function test_updatesAreEventDrivenWithoutIdlePolling() {
        const condition = createTemporaryObject(conditionComponent, this);
        verify(condition);
        fakeTasks.replaceTasks([task()]);
        tryCompare(condition, "active", true);
        const readsAfterUpdate = fakeTasks.propertyReads;
        wait(250);
        compare(fakeTasks.propertyReads, readsAfterUpdate);
    }
}
