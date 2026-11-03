import QtQml
import wekde.coverage 1.0

QtObject {
    property var calculator: DesktopCoverageCalculator {}

    function evaluate(targetRects, windowRects) {
        return calculator.evaluate(targetRects, windowRects, false, false, true);
    }
}
