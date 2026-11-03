import QtQuick
import QtTest
import "../../plugin/contents/ui" as WallpaperUi

TestCase {
    name: "PauseModeValues"

    function test_coveredIsAppendedWithoutRenumberingExistingModes() {
        compare(WallpaperUi.Common.PauseMode.Never, 0);
        compare(WallpaperUi.Common.PauseMode.Any, 1);
        compare(WallpaperUi.Common.PauseMode.Max, 2);
        compare(WallpaperUi.Common.PauseMode.Focus, 3);
        compare(WallpaperUi.Common.PauseMode.FocusOrMax, 4);
        compare(WallpaperUi.Common.PauseMode.FullScreen, 5);
        compare(WallpaperUi.Common.PauseMode.Covered, 6);
    }
}
