#include "DesktopCoverage.hpp"

#include <QRegion>

#include <cmath>
#include <limits>

namespace DesktopCoverage {
namespace {

bool pixelRect(const QRectF &input, bool roundOutward, QRect &output)
{
    const double x = input.x();
    const double y = input.y();
    const double width = input.width();
    const double height = input.height();
    const double right = x + width;
    const double bottom = y + height;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width)
        || !std::isfinite(height) || !std::isfinite(right) || !std::isfinite(bottom)
        || width <= 0.0 || height <= 0.0) {
        return false;
    }

    const long double leftEdge = roundOutward ? std::floor(x) : std::ceil(x);
    const long double topEdge = roundOutward ? std::floor(y) : std::ceil(y);
    const long double rightEdge = roundOutward ? std::ceil(right) : std::floor(right);
    const long double bottomEdge = roundOutward ? std::ceil(bottom) : std::floor(bottom);
    const long double pixelWidth = rightEdge - leftEdge;
    const long double pixelHeight = bottomEdge - topEdge;
    const long double intMin = std::numeric_limits<int>::min();
    const long double intMax = std::numeric_limits<int>::max();
    if (leftEdge < intMin || topEdge < intMin || rightEdge > intMax || bottomEdge > intMax
        || pixelWidth <= 0.0L || pixelHeight <= 0.0L || pixelWidth > intMax
        || pixelHeight > intMax) {
        return false;
    }

    output = QRect(int(leftEdge), int(topEdge), int(pixelWidth), int(pixelHeight));
    return !output.isEmpty();
}

quint64 area(const QRegion &region)
{
    quint64 result = 0;
    for (const QRect &rect : region) {
        const quint64 width = quint64(rect.width());
        const quint64 height = quint64(rect.height());
        const quint64 rectangleArea = width * height;
        if (std::numeric_limits<quint64>::max() - result < rectangleArea) {
            return std::numeric_limits<quint64>::max();
        }
        result += rectangleArea;
    }
    return result;
}

quint64 thresholdArea(quint64 totalArea, quint64 percent)
{
    const quint64 whole = (totalArea / 100) * percent;
    const quint64 remainder = ((totalArea % 100) * percent + 99) / 100;
    return whole + remainder;
}

} // namespace

Result evaluate(const QVector<QRectF> &targetRegions,
    const QVector<QRectF> &windowBounds,
    bool wasPaused)
{
    if (targetRegions.isEmpty()) {
        return {};
    }

    QRegion target;
    for (const QRectF &input : targetRegions) {
        QRect rect;
        if (!pixelRect(input, true, rect)) {
            return {};
        }
        target += rect;
    }
    const quint64 targetArea = area(target);
    if (target.isEmpty() || targetArea == 0) {
        return {};
    }

    QRegion windows;
    for (const QRectF &input : windowBounds) {
        QRect rect;
        if (pixelRect(input, false, rect)) {
            windows += rect;
        }
    }
    const quint64 coveredArea = area(target & windows);
    const double fraction = double(coveredArea) / double(targetArea);

    const quint64 pauseThreshold = thresholdArea(targetArea, 98);
    const quint64 resumeThreshold = thresholdArea(targetArea, 95);
    const Decision decision = wasPaused
        ? (coveredArea < resumeThreshold ? Decision::Visible : Decision::Paused)
        : (coveredArea >= pauseThreshold ? Decision::Paused : Decision::Visible);
    return {decision, coveredArea, targetArea, fraction};
}

} // namespace DesktopCoverage
