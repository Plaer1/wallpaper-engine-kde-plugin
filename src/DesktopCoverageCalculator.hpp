#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace wekde
{

// Stateless QML adapter for the pure DesktopCoverage geometry helper.
class DesktopCoverageCalculator : public QObject {
    Q_OBJECT

public:
    explicit DesktopCoverageCalculator(QObject* parent = nullptr);

    // QML API: evaluate(usableRects, windowRects, wasPaused, showingDesktop,
    // usableRegionVerified). Rect entries must be QRect or QRectF variants;
    // status inputs must be actual booleans. Unknown input never pauses.
    Q_INVOKABLE QVariantMap evaluate(const QVariantList& usableRects,
                                     const QVariantList& windowRects, const QVariant& wasPaused,
                                     const QVariant& showingDesktop,
                                     const QVariant& usableRegionVerified) const;
};

} // namespace wekde
