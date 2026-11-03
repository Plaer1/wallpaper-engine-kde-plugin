#include "DesktopCoverageCalculator.hpp"

#include "DesktopCoverage.hpp"

#include <QMetaType>
#include <QRect>
#include <QRectF>
#include <QVariant>
#include <QVector>

namespace wekde
{
namespace
{

bool actualBool(const QVariant& value, bool& result) {
    if (! value.isValid() || value.isNull() || value.metaType() != QMetaType::fromType<bool>()) {
        return false;
    }
    result = value.toBool();
    return true;
}

bool actualRect(const QVariant& value, QRectF& result) {
    if (! value.isValid() || value.isNull()) {
        return false;
    }
    if (value.metaType() == QMetaType::fromType<QRectF>()) {
        result = value.value<QRectF>();
        return true;
    }
    if (value.metaType() == QMetaType::fromType<QRect>()) {
        result = value.value<QRect>();
        return true;
    }
    return false;
}

QVariantMap resultMap(DesktopCoverage::Decision decision, quint64 coveredArea = 0,
                      quint64 targetArea = 0, double fraction = 0.0,
                      const QString& overrideReason = {}) {
    QString decisionName;
    switch (decision) {
    case DesktopCoverage::Decision::Visible: decisionName = QStringLiteral("visible"); break;
    case DesktopCoverage::Decision::Paused: decisionName = QStringLiteral("paused"); break;
    case DesktopCoverage::Decision::Unknown:
    default: decisionName = QStringLiteral("unknown"); break;
    }

    QVariantMap result {
        { QStringLiteral("decision"), decisionName },
        { QStringLiteral("known"), decision != DesktopCoverage::Decision::Unknown },
        // Keep 64-bit counts exact when crossing into JavaScript.
        { QStringLiteral("coveredArea"), QString::number(coveredArea) },
        { QStringLiteral("targetArea"), QString::number(targetArea) },
        { QStringLiteral("fraction"), fraction },
    };
    if (! overrideReason.isEmpty()) {
        result.insert(QStringLiteral("override"), overrideReason);
    }
    return result;
}

} // namespace

DesktopCoverageCalculator::DesktopCoverageCalculator(QObject* parent): QObject(parent) {}

QVariantMap DesktopCoverageCalculator::evaluate(const QVariantList& usableRects,
                                                const QVariantList& windowRects,
                                                const QVariant&     wasPausedValue,
                                                const QVariant&     showingDesktopValue,
                                                const QVariant& usableRegionVerifiedValue) const {
    bool showingDesktop = false;
    if (! actualBool(showingDesktopValue, showingDesktop)) {
        return resultMap(DesktopCoverage::Decision::Unknown);
    }
    if (showingDesktop) {
        return resultMap(
            DesktopCoverage::Decision::Visible, 0, 0, 0.0, QStringLiteral("showDesktop"));
    }

    bool usableRegionVerified = false;
    bool wasPaused            = false;
    if (! actualBool(usableRegionVerifiedValue, usableRegionVerified) || ! usableRegionVerified ||
        ! actualBool(wasPausedValue, wasPaused) || usableRects.isEmpty()) {
        return resultMap(DesktopCoverage::Decision::Unknown);
    }

    QVector<QRectF> targets;
    targets.reserve(usableRects.size());
    for (const QVariant& value : usableRects) {
        QRectF rect;
        if (! actualRect(value, rect)) {
            return resultMap(DesktopCoverage::Decision::Unknown);
        }
        targets.push_back(rect);
    }

    QVector<QRectF> windows;
    windows.reserve(windowRects.size());
    for (const QVariant& value : windowRects) {
        QRectF rect;
        if (! actualRect(value, rect)) {
            return resultMap(DesktopCoverage::Decision::Unknown);
        }
        windows.push_back(rect);
    }

    const DesktopCoverage::Result coverage = DesktopCoverage::evaluate(targets, windows, wasPaused);
    return resultMap(
        coverage.decision, coverage.coveredArea, coverage.targetArea, coverage.fraction);
}

} // namespace wekde
