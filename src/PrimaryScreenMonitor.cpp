#include "PrimaryScreenMonitor.hpp"

#include <Plasma/Corona>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPointer>
#include <QRegion>
#include <QRectF>
#include <QScreen>
#include <QVariant>

#include <utility>

namespace wekde
{

PrimaryScreenMonitor::PrimaryScreenMonitor(QObject *parent)
    : QObject(parent)
{
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::primaryScreenChanged, this, [this](QScreen *) {
            reconnectScreenSignals();
            refresh();
        });
        connect(app, &QGuiApplication::screenAdded, this, [this](QScreen *) {
            reconnectScreenSignals();
            refresh();
        });
        connect(app, &QGuiApplication::screenRemoved, this, [this](QScreen *) {
            reconnectScreenSignals();
            refresh();
        });
    }
    reconnectScreenSignals();
}

QObject *PrimaryScreenMonitor::corona() const
{
    return m_corona.data();
}

void PrimaryScreenMonitor::setCorona(QObject *object)
{
    auto *next = qobject_cast<Plasma::Corona *>(object);
    if (next == m_corona) return;

    m_corona = next;
    reconnectCoronaSignals();
    emit coronaChanged();
    refresh();
}

bool PrimaryScreenMonitor::valid() const { return m_valid; }
QString PrimaryScreenMonitor::screenName() const { return m_screenName; }
QRect PrimaryScreenMonitor::screenGeometry() const { return m_screenGeometry; }
QVariantList PrimaryScreenMonitor::usableRegion() const { return m_usableRegion; }

QVariantList PrimaryScreenMonitor::clipUsableRegion(const QRegion &region,
                                                    const QRect &screenGeometry)
{
    QVariantList usableRects;
    for (const QRect &rect : region.intersected(QRegion(screenGeometry))) {
        if (rect.isValid() && !rect.isEmpty())
            usableRects.append(QVariant::fromValue(QRectF(rect)));
    }
    return usableRects;
}

void PrimaryScreenMonitor::reconnectScreenSignals()
{
    for (const auto &connection : std::as_const(m_screenConnections))
        disconnect(connection);
    m_screenConnections.clear();

    auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    m_primaryScreen = app ? app->primaryScreen() : nullptr;
    if (!m_primaryScreen) return;

    m_screenConnections.append(connect(m_primaryScreen, &QScreen::geometryChanged,
                                       this, [this](const QRect &) { refresh(); }));
    m_screenConnections.append(connect(m_primaryScreen, &QScreen::availableGeometryChanged,
                                       this, [this](const QRect &) { refresh(); }));
    m_screenConnections.append(connect(m_primaryScreen, &QObject::destroyed,
                                       this, [this] { refresh(); }));
}

void PrimaryScreenMonitor::reconnectCoronaSignals()
{
    for (const auto &connection : std::as_const(m_coronaConnections))
        disconnect(connection);
    m_coronaConnections.clear();
    if (!m_corona) return;

    m_coronaConnections.append(connect(m_corona, &Plasma::Corona::availableScreenRegionChanged,
                                       this, [this](uint) { refresh(); }));
    m_coronaConnections.append(connect(m_corona, &Plasma::Corona::screenGeometryChanged,
                                       this, [this](uint) { refresh(); }));
    m_coronaConnections.append(connect(m_corona, &Plasma::Corona::screenAdded,
                                       this, [this](uint) { refresh(); }));
    m_coronaConnections.append(connect(m_corona, &Plasma::Corona::screenRemoved,
                                       this, [this](uint) { refresh(); }));
    m_coronaConnections.append(connect(m_corona, &QObject::destroyed,
                                       this, [this] { refresh(); }));
}

void PrimaryScreenMonitor::setInvalid()
{
    if (!m_valid && m_screenName.isEmpty() && m_screenGeometry.isNull()
        && m_usableRegion.isEmpty()) {
        return;
    }
    m_valid = false;
    m_screenId = 0;
    m_screenName.clear();
    m_screenGeometry = {};
    m_usableRegion.clear();
    emit changed();
}

void PrimaryScreenMonitor::refresh()
{
    if (!m_corona || !m_primaryScreen) {
        setInvalid();
        return;
    }

    const QRect primaryGeometry = m_primaryScreen->geometry();
    uint matchedScreen = 0;
    int matchCount = 0;
    for (uint id = 0; id < m_corona->numScreens(); ++id) {
        if (m_corona->screenGeometry(id) == primaryGeometry) {
            matchedScreen = id;
            ++matchCount;
        }
    }
    if (matchCount != 1) {
        setInvalid();
        return;
    }

    const QVariantList usableRects = clipUsableRegion(
        m_corona->availableScreenRegion(matchedScreen), primaryGeometry);
    if (usableRects.isEmpty()) {
        setInvalid();
        return;
    }

    const QString name = m_primaryScreen->name();
    if (m_valid && m_screenId == matchedScreen && m_screenName == name
        && m_screenGeometry == primaryGeometry && m_usableRegion == usableRects) {
        return;
    }

    m_valid = true;
    m_screenId = matchedScreen;
    m_screenName = name;
    m_screenGeometry = primaryGeometry;
    m_usableRegion = std::move(usableRects);
    emit changed();
}

} // namespace wekde
