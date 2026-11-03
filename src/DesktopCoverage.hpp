#pragma once

#include <QRectF>
#include <QVector>

namespace DesktopCoverage {

enum class Decision {
    Unknown,
    Visible,
    Paused,
};

struct Result {
    Decision decision = Decision::Unknown;
    quint64 coveredArea = 0;
    quint64 targetArea = 0;
    double fraction = 0.0;

    bool known() const { return decision != Decision::Unknown; }
};

// Target rectangles are rounded outward and window rectangles inward in logical pixels.
// Invalid target geometry returns Unknown; invalid window rectangles contribute no area.
Result evaluate(const QVector<QRectF> &targetRegions,
    const QVector<QRectF> &windowBounds,
    bool wasPaused);

} // namespace DesktopCoverage
