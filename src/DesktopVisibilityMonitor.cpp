#include "DesktopVisibilityMonitor.hpp"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

namespace wekde
{
namespace
{
constexpr auto KWinService         = "org.kde.KWin";
constexpr auto KWinPath            = "/KWin";
constexpr auto KWinInterface       = "org.kde.KWin";
constexpr auto PropertiesInterface = "org.freedesktop.DBus.Properties";
} // namespace

DesktopVisibilityMonitor::DesktopVisibilityMonitor(const QDBusConnection& connection,
                                                   QObject*               parent)
    : QObject(parent),
      m_connection(connection),
      m_serviceWatcher(new QDBusServiceWatcher(QString::fromLatin1(KWinService), m_connection,
                                               QDBusServiceWatcher::WatchForRegistration |
                                                   QDBusServiceWatcher::WatchForUnregistration |
                                                   QDBusServiceWatcher::WatchForOwnerChange,
                                               this)) {
    // Install the signal match before asking for the initial property. If a
    // state change races that reply, signal generation prevents stale data
    // from overwriting the event.
    m_connection.connect(QString::fromLatin1(KWinService),
                         QString::fromLatin1(KWinPath),
                         QString::fromLatin1(KWinInterface),
                         QStringLiteral("showingDesktopChanged"),
                         this,
                         SLOT(handleShowingDesktopChanged(bool, QDBusMessage)));
    connect(m_serviceWatcher,
            &QDBusServiceWatcher::serviceOwnerChanged,
            this,
            &DesktopVisibilityMonitor::ownerChanged);

    if (QDBusConnectionInterface* busInterface = m_connection.interface()) {
        const QDBusReply<QString> owner =
            busInterface->serviceOwner(QString::fromLatin1(KWinService));
        if (owner.isValid() && ! owner.value().isEmpty()) {
            m_owner = owner.value();
            ++m_ownerGeneration;
            readInitialState();
        }
    }
}

DesktopVisibilityMonitor::~DesktopVisibilityMonitor() {
    m_connection.disconnect(QString::fromLatin1(KWinService),
                            QString::fromLatin1(KWinPath),
                            QString::fromLatin1(KWinInterface),
                            QStringLiteral("showingDesktopChanged"),
                            this,
                            SLOT(handleShowingDesktopChanged(bool, QDBusMessage)));
}

bool DesktopVisibilityMonitor::known() const { return m_known; }

bool DesktopVisibilityMonitor::showingDesktop() const { return m_showingDesktop; }

void DesktopVisibilityMonitor::setState(bool known, bool showing) {
    showing                        = known && showing;
    const bool knownChangedValue   = m_known != known;
    const bool showingChangedValue = m_showingDesktop != showing;
    if (! knownChangedValue && ! showingChangedValue) return;

    m_known          = known;
    m_showingDesktop = showing;
    if (knownChangedValue) emit knownChanged();
    if (showingChangedValue) emit showingDesktopChanged();
}

void DesktopVisibilityMonitor::invalidateOwner() {
    ++m_ownerGeneration;
    ++m_signalGeneration;
    setState(false, false);
}

void DesktopVisibilityMonitor::ownerChanged(const QString& service, const QString& oldOwner,
                                            const QString& newOwner) {
    Q_UNUSED(oldOwner)
    if (service != QLatin1String(KWinService) || newOwner == m_owner) return;

    invalidateOwner();
    m_owner = newOwner;
    if (! m_owner.isEmpty()) readInitialState();
}

void DesktopVisibilityMonitor::readInitialState() {
    if (m_owner.isEmpty()) return;

    const quint64 ownerGeneration  = m_ownerGeneration;
    const quint64 signalGeneration = m_signalGeneration;
    const QString owner            = m_owner;
    QDBusMessage  message = QDBusMessage::createMethodCall(owner,
                                                           QString::fromLatin1(KWinPath),
                                                           QString::fromLatin1(PropertiesInterface),
                                                           QStringLiteral("Get"));
    message << QString::fromLatin1(KWinInterface) << QStringLiteral("showingDesktop");

    auto* watcher = new QDBusPendingCallWatcher(m_connection.asyncCall(message), this);
    connect(watcher,
            &QDBusPendingCallWatcher::finished,
            this,
            [this, watcher, ownerGeneration, signalGeneration, owner] {
                QDBusPendingReply<QDBusVariant> reply = *watcher;
                watcher->deleteLater();
                if (ownerGeneration != m_ownerGeneration || owner != m_owner) return;
                if (signalGeneration != m_signalGeneration) return;
                QDBusConnectionInterface* busInterface = m_connection.interface();
                const QDBusReply<QString> currentOwner =
                    busInterface ? busInterface->serviceOwner(QString::fromLatin1(KWinService))
                                 : QDBusReply<QString>();
                if (! currentOwner.isValid() || currentOwner.value() != owner) {
                    ownerChanged(QString::fromLatin1(KWinService),
                                 owner,
                                 currentOwner.isValid() ? currentOwner.value() : QString());
                    return;
                }
                if (! reply.isValid()) {
                    setState(false, false);
                    return;
                }
                const QVariant value = reply.value().variant();
                if (value.metaType().id() != QMetaType::Bool) {
                    setState(false, false);
                    return;
                }
                setState(true, value.toBool());
            });
}

void DesktopVisibilityMonitor::handleShowingDesktopChanged(bool                showing,
                                                           const QDBusMessage& message) {
    // Well-known-name matches can span an owner replacement. Ignore signals
    // from the prior unique owner after the service watcher reports takeover.
    if (m_owner.isEmpty() || message.service() != m_owner) return;
    ++m_signalGeneration;
    setState(true, showing);
}

} // namespace wekde
