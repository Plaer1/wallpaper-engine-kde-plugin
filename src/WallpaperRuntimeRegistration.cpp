#include "WallpaperRuntimeRegistration.hpp"

#include <QGuiApplication>
#include <QMetaType>
#include <QQuickWindow>
#include <QScreen>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

namespace wekde
{
namespace
{

constexpr int DesktopContainmentType = 0;
constexpr quint32 NoContainmentScreen = std::numeric_limits<quint32>::max();
constexpr auto ExpectedPluginId = "com.github.catsout.wallpaperEngineKde";

bool isCanonicalUuid(const QString& text) {
    const QUuid id = QUuid::fromString(text);
    return !id.isNull() && id.toString(QUuid::WithoutBraces).compare(text, Qt::CaseInsensitive) == 0;
}

bool isDecimalId(const QString& text) {
    if (text.isEmpty() || text.size() > 20) {
        return false;
    }
    return std::all_of(text.cbegin(), text.cend(), [](QChar c) {
        return c >= QLatin1Char('0') && c <= QLatin1Char('9');
    });
}

QObject* objectFromVariant(const QVariant& value) {
    if (!value.isValid() || value.isNull()) {
        return nullptr;
    }
    if (value.metaType().id() == QMetaType::QObjectStar) {
        return value.value<QObject*>();
    }
    if (value.canConvert<QObject*>()) {
        return value.value<QObject*>();
    }
    return nullptr;
}

bool validRect(const QRectF& rect) {
    return std::isfinite(rect.x()) && std::isfinite(rect.y()) && std::isfinite(rect.width()) &&
           std::isfinite(rect.height()) && rect.width() > 0.0 && rect.height() > 0.0;
}

bool integralInRange(const QVariant& value, quint64 maximum, quint64* result) {
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number) || number < 0.0 || std::floor(number) != number ||
        number > static_cast<double>(maximum)) {
        return false;
    }
    *result = static_cast<quint64>(number);
    return true;
}

} // namespace

WallpaperRuntimeRegistration::WallpaperRuntimeRegistration(QObject* parent)
    : QObject(parent) {
    if (qGuiApp) {
        connect(qGuiApp, &QGuiApplication::screenAdded, this, [this] { refresh(); });
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] { refresh(); });
    }
}

QQuickItem* WallpaperRuntimeRegistration::targetItem() const { return m_targetItem; }

void WallpaperRuntimeRegistration::setTargetItem(QQuickItem* item) {
    if (m_targetItem == item) {
        return;
    }
    if (m_targetItem) {
        disconnect(m_targetItem, nullptr, this, nullptr);
    }
    m_targetItem = item;
    invalidate();
    if (item) {
        connect(item, &QQuickItem::windowChanged, this, &WallpaperRuntimeRegistration::handleWindowChanged);
        connect(item, &QObject::destroyed, this, [this] {
            m_targetItem = nullptr;
            m_window = nullptr;
            m_screen = nullptr;
            invalidate();
            emit targetItemChanged();
        });
        handleWindowChanged(item->window());
    } else {
        handleWindowChanged(nullptr);
    }
    emit targetItemChanged();
}

QVariantMap WallpaperRuntimeRegistration::containmentContext() const { return m_containmentContext; }

void WallpaperRuntimeRegistration::setContainmentContext(const QVariantMap& context) {
    if (m_containmentContext == context) {
        return;
    }
    m_containmentContext = context;
    invalidate();
    emit containmentContextChanged();
    refresh();
}

bool WallpaperRuntimeRegistration::isRegistered() const { return m_registered; }
QString WallpaperRuntimeRegistration::instanceId() const { return m_instanceId; }
QString WallpaperRuntimeRegistration::outputName() const { return m_outputName; }
QString WallpaperRuntimeRegistration::activityId() const { return m_activityId; }
QString WallpaperRuntimeRegistration::containmentId() const { return m_containmentId; }

QString WallpaperRuntimeRegistration::uniqueOutputName(const QString& screenName,
                                                        const QStringList& screenNames) {
    if (screenName.isEmpty() ||
        std::count(screenNames.cbegin(), screenNames.cend(), screenName) != 1) {
        return {};
    }
    return screenName;
}

void WallpaperRuntimeRegistration::refresh() {
    if (!m_targetItem) {
        invalidate(QStringLiteral("wallpaperUnavailable"));
        return;
    }
    const OutputIdentity output = currentOutput();
    if (!output.window || !output.screen || !qGuiApp) {
        invalidate(QStringLiteral("screenUnavailable"));
        return;
    }
    const QString connector = uniqueOutputName(output.name, output.availableNames);
    if (connector.isEmpty()) {
        invalidate(QStringLiteral("ambiguousOutput"));
        return;
    }
    QString reason;
    if (!contextIsEligible(output, &reason)) {
        invalidate(reason);
        return;
    }
    if (!outputIsCurrent(output, connector)) {
        invalidate(QStringLiteral("outputChanged"));
        return;
    }

    const QString activity = m_containmentContext.value(QStringLiteral("activityId")).toString();
    const QString containment = m_containmentContext.value(QStringLiteral("containmentId")).toString();
    const bool changed = !m_registered || m_outputName != connector || m_activityId != activity ||
                         m_containmentId != containment;
    if (m_instanceId.isEmpty()) {
        m_instanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    m_registered = true;
    m_outputName = connector;
    m_activityId = activity;
    m_containmentId = containment;
    if (changed) {
        emit registrationChanged();
    }
}

void WallpaperRuntimeRegistration::handleWindowChanged(QQuickWindow* window) {
    if (m_window) {
        disconnect(m_window, nullptr, this, nullptr);
    }
    m_window = window;
    if (window) {
        connect(window, &QWindow::screenChanged, this, &WallpaperRuntimeRegistration::handleScreenChanged);
        handleScreenChanged(window->screen());
    } else {
        m_screen = nullptr;
        invalidate(QStringLiteral("windowUnavailable"));
    }
}

void WallpaperRuntimeRegistration::handleScreenChanged(QScreen* screen) {
    if (m_screen) {
        disconnect(m_screen, nullptr, this, nullptr);
    }
    m_screen = screen;
    if (screen) {
        connect(screen, &QScreen::geometryChanged, this, [this] { refresh(); });
    }
    invalidate();
    refresh();
}

void WallpaperRuntimeRegistration::invalidate(const QString& reason) {
    const bool changed = m_registered || !m_instanceId.isEmpty() || !m_outputName.isEmpty() ||
                         !m_activityId.isEmpty() || !m_containmentId.isEmpty();
    m_registered = false;
    m_instanceId.clear();
    m_outputName.clear();
    m_activityId.clear();
    m_containmentId.clear();
    if (changed) {
        emit registrationChanged();
    }
    if (!reason.isEmpty()) {
        emit registrationRejected(reason);
    }
}

bool WallpaperRuntimeRegistration::contextIsEligible(const OutputIdentity& output,
                                                       QString* reason) const {
    const auto reject = [reason](const QString& value) {
        if (reason) {
            *reason = value;
        }
        return false;
    };

    // This QObject equality is the preview guard: only the root that Plasma placed in
    // the containment's wallpaperGraphicsObject slot can register.
    QObject* const wallpaper = objectFromVariant(m_containmentContext.value(QStringLiteral("wallpaperObject")));
    QObject* const activeWallpaper = objectFromVariant(m_containmentContext.value(QStringLiteral("activeWallpaperObject")));
    if (!wallpaper || wallpaper != m_targetItem || activeWallpaper != wallpaper) {
        return reject(QStringLiteral("notActiveContainmentWallpaper"));
    }

    if (m_containmentContext.value(QStringLiteral("wallpaperPlugin")).metaType().id() != QMetaType::QString ||
        m_containmentContext.value(QStringLiteral("wallpaperPlugin")).toString() != QString::fromLatin1(ExpectedPluginId)) {
        return reject(QStringLiteral("wallpaperPluginMismatch"));
    }
    quint64 containmentType = 0;
    if (!integralInRange(m_containmentContext.value(QStringLiteral("containmentType")),
                         std::numeric_limits<quint32>::max(), &containmentType) ||
        containmentType != DesktopContainmentType) {
        return reject(QStringLiteral("notDesktopContainment"));
    }
    quint64 screen = 0;
    if (!integralInRange(m_containmentContext.value(QStringLiteral("screen")),
                         std::numeric_limits<quint32>::max(), &screen) ||
        screen == NoContainmentScreen) {
        return reject(QStringLiteral("containmentScreenUnavailable"));
    }
    const QString activity = m_containmentContext.value(QStringLiteral("activityId")).toString();
    if (!isCanonicalUuid(activity)) {
        return reject(QStringLiteral("containmentActivityUnavailable"));
    }
    const QString containment = m_containmentContext.value(QStringLiteral("containmentId")).toString();
    if (!isDecimalId(containment)) {
        return reject(QStringLiteral("containmentIdUnavailable"));
    }
    const QVariant screenGeometryValue = m_containmentContext.value(QStringLiteral("screenGeometry"));
    if (!screenGeometryValue.canConvert<QRectF>()) {
        return reject(QStringLiteral("containmentGeometryUnavailable(type=%1)")
                           .arg(QString::fromLatin1(screenGeometryValue.metaType().name())));
    }
    const QRectF containmentGeometry = screenGeometryValue.toRectF();
    if (!validRect(containmentGeometry) || !validRect(output.geometry) ||
        containmentGeometry != output.geometry) {
        return reject(QStringLiteral("containmentGeometryMismatch(type=%1,value=%2,output=%3)")
                           .arg(QString::fromLatin1(screenGeometryValue.metaType().name()),
                                QString::number(containmentGeometry.x()) + QLatin1Char(',') +
                                    QString::number(containmentGeometry.y()) + QLatin1Char(',') +
                                    QString::number(containmentGeometry.width()) + QLatin1Char('x') +
                                    QString::number(containmentGeometry.height()),
                                QString::number(output.geometry.x()) + QLatin1Char(',') +
                                    QString::number(output.geometry.y()) + QLatin1Char(',') +
                                    QString::number(output.geometry.width()) + QLatin1Char('x') +
                                    QString::number(output.geometry.height())));
    }
    const QVariant regionValue = m_containmentContext.value(QStringLiteral("availableScreenRegion"));
    QList<QRectF> availableRects;
    if (regionValue.metaType().id() == QMetaType::QVariantList) {
        const QVariantList values = regionValue.toList();
        availableRects.reserve(values.size());
        for (const QVariant& rectValue : values) {
            if (!rectValue.canConvert<QRectF>()) {
                return reject(QStringLiteral("invalidAvailableRegion(elementType=%1)")
                                  .arg(QString::fromLatin1(rectValue.metaType().name())));
            }
            availableRects.append(rectValue.toRectF());
        }
    } else if (regionValue.metaType().id() == qMetaTypeId<QList<QRectF>>()) {
        availableRects = regionValue.value<QList<QRectF>>();
    } else {
        return reject(QStringLiteral("availableRegionUnavailable(type=%1,valid=%2,null=%3)")
                           .arg(QString::fromLatin1(regionValue.metaType().name()))
                           .arg(regionValue.isValid())
                           .arg(regionValue.isNull()));
    }
    if (availableRects.isEmpty()) {
        return reject(QStringLiteral("availableRegionUnavailable(type=%1,empty=true)")
                           .arg(QString::fromLatin1(regionValue.metaType().name())));
    }
    for (const QRectF& rect : availableRects) {
        if (!validRect(rect)) {
            const QString rectDescription =
                QStringLiteral("%1,%2,%3x%4")
                    .arg(QString::number(rect.x()), QString::number(rect.y()),
                         QString::number(rect.width()), QString::number(rect.height()));
            return reject(QStringLiteral("invalidAvailableRegion(rect=%1)").arg(rectDescription));
        }
    }
    return true;
}

bool WallpaperRuntimeRegistration::outputIsCurrent(const OutputIdentity& output,
                                                    const QString& connector) const {
    if (!m_targetItem || !m_window || !m_screen || !qGuiApp || output.window != m_window ||
        output.screen != m_screen || output.name != connector ||
        uniqueOutputName(connector, output.availableNames) != connector) {
        return false;
    }
    return output.geometry == QRectF(output.screen->geometry());
}

WallpaperRuntimeRegistration::OutputIdentity WallpaperRuntimeRegistration::currentOutput() const {
    OutputIdentity output;
    output.window = m_targetItem ? m_targetItem->window() : nullptr;
    output.screen = output.window ? output.window->screen() : nullptr;
    if (!output.screen || !qGuiApp) {
        return output;
    }
    output.name = output.screen->name();
    output.geometry = QRectF(output.screen->geometry());
    for (QScreen* screen : qGuiApp->screens()) {
        output.availableNames.append(screen ? screen->name() : QString());
    }
    return output;
}

} // namespace wekde
