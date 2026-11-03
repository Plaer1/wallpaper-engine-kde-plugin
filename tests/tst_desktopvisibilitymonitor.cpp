#include "DesktopVisibilityMonitor.hpp"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QSignalSpy>
#include <QTest>

namespace
{
constexpr auto ServiceName   = "org.kde.KWin";
constexpr auto ObjectPath    = "/KWin";
constexpr auto InterfaceName = "org.kde.KWin";

class FakeKWin final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin")
    Q_PROPERTY(bool showingDesktop READ showingDesktop NOTIFY showingDesktopChanged)

public:
    explicit FakeKWin(bool initial, bool signalDuringFirstRead = false)
        : m_showingDesktop(initial), m_signalDuringFirstRead(signalDuringFirstRead) {}

    bool showingDesktop() const {
        if (m_signalDuringFirstRead) {
            auto* self                    = const_cast<FakeKWin*>(this);
            self->m_signalDuringFirstRead = false;
            self->m_showingDesktop        = true;
            emit self->showingDesktopChanged(true);
            // Return the stale value from before the signal; the monitor must
            // retain the newer event rather than accept this reply.
            return false;
        }
        return m_showingDesktop;
    }

    void setShowingDesktop(bool showing) {
        if (m_showingDesktop == showing) return;
        m_showingDesktop = showing;
        emit showingDesktopChanged(showing);
    }

signals:
    void showingDesktopChanged(bool showing);

private:
    bool         m_showingDesktop        = false;
    mutable bool m_signalDuringFirstRead = false;
};

class KWinFixture {
public:
    explicit KWinFixture(bool initial, bool signalDuringFirstRead = false)
        : service(QDBusConnection::sessionBus()), object(initial, signalDuringFirstRead) {
        const auto registration =
            service.interface()->registerService(QString::fromLatin1(ServiceName),
                                                 QDBusConnectionInterface::DontQueueService,
                                                 QDBusConnectionInterface::AllowReplacement);
        owner = registration.isValid() &&
                registration.value() == QDBusConnectionInterface::ServiceRegistered;
        if (owner) {
            registeredObject = service.registerObject(QString::fromLatin1(ObjectPath),
                                                      &object,
                                                      QDBusConnection::ExportAllProperties |
                                                          QDBusConnection::ExportAllSignals);
        }
    }

    ~KWinFixture() {
        if (registeredObject) service.unregisterObject(QString::fromLatin1(ObjectPath));
        if (owner) service.unregisterService(QString::fromLatin1(ServiceName));
    }

    bool valid() const { return owner && registeredObject; }

    QDBusConnection service;
    FakeKWin        object;
    bool            owner            = false;
    bool            registeredObject = false;
};

bool replaceOwner(QDBusConnection& connection, FakeKWin& object) {
    if (! connection.registerObject(QString::fromLatin1(ObjectPath),
                                    &object,
                                    QDBusConnection::ExportAllProperties |
                                        QDBusConnection::ExportAllSignals))
        return false;
    const auto registration =
        connection.interface()->registerService(QString::fromLatin1(ServiceName),
                                                QDBusConnectionInterface::ReplaceExistingService,
                                                QDBusConnectionInterface::AllowReplacement);
    if (! registration.isValid() ||
        registration.value() != QDBusConnectionInterface::ServiceRegistered) {
        connection.unregisterObject(QString::fromLatin1(ObjectPath));
        return false;
    }
    return true;
}
} // namespace

class DesktopVisibilityMonitorTest final : public QObject {
    Q_OBJECT

private slots:
    void initialReadAndSignalTransitions() {
        KWinFixture fixture(false);
        QVERIFY(fixture.valid());

        wekde::DesktopVisibilityMonitor monitor;
        QSignalSpy knownSpy(&monitor, &wekde::DesktopVisibilityMonitor::knownChanged);
        QSignalSpy showingSpy(&monitor, &wekde::DesktopVisibilityMonitor::showingDesktopChanged);
        QTRY_VERIFY_WITH_TIMEOUT(monitor.known(), 2000);
        QVERIFY(! monitor.showingDesktop());

        fixture.object.setShowingDesktop(true);
        QTRY_VERIFY_WITH_TIMEOUT(monitor.showingDesktop(), 2000);
        QVERIFY(monitor.known());
        QCOMPARE(showingSpy.count(), 1);

        fixture.object.setShowingDesktop(false);
        QTRY_VERIFY_WITH_TIMEOUT(! monitor.showingDesktop(), 2000);
        QCOMPARE(showingSpy.count(), 2);
        QCOMPARE(knownSpy.count(), 1);
    }

    void signalWinsAgainstStaleInitialReply() {
        KWinFixture fixture(false, true);
        QVERIFY(fixture.valid());

        wekde::DesktopVisibilityMonitor monitor;
        QTRY_VERIFY_WITH_TIMEOUT(monitor.known(), 2000);
        QVERIFY(monitor.showingDesktop());
    }

    void ownerLossFailsClosedAndReplacementReadsAgain() {
        KWinFixture original(true);
        QVERIFY(original.valid());
        wekde::DesktopVisibilityMonitor monitor;
        QTRY_VERIFY_WITH_TIMEOUT(monitor.known(), 2000);
        QVERIFY(monitor.showingDesktop());

        original.service.unregisterObject(QString::fromLatin1(ObjectPath));
        original.registeredObject = false;
        original.service.unregisterService(QString::fromLatin1(ServiceName));
        original.owner = false;
        QTRY_VERIFY_WITH_TIMEOUT(! monitor.known(), 2000);
        QVERIFY(! monitor.showingDesktop());

        QDBusConnection replacement = QDBusConnection::sessionBus();
        FakeKWin        replacementObject(false);
        QVERIFY(replaceOwner(replacement, replacementObject));
        QTRY_VERIFY_WITH_TIMEOUT(monitor.known(), 2000);
        QVERIFY(! monitor.showingDesktop());

        replacementObject.setShowingDesktop(true);
        QTRY_VERIFY_WITH_TIMEOUT(monitor.showingDesktop(), 2000);
        replacement.unregisterObject(QString::fromLatin1(ObjectPath));
        replacement.unregisterService(QString::fromLatin1(ServiceName));
    }

    void ownerReplacementInvalidatesOldState() {
        KWinFixture original(true);
        QVERIFY(original.valid());
        wekde::DesktopVisibilityMonitor monitor;
        QTRY_VERIFY_WITH_TIMEOUT(monitor.known(), 2000);
        QVERIFY(monitor.showingDesktop());
        QSignalSpy knownSpy(&monitor, &wekde::DesktopVisibilityMonitor::knownChanged);

        const QString connectionName =
            QStringLiteral("visibility-replacement-%1").arg(quintptr(this));
        QDBusConnection replacement =
            QDBusConnection::connectToBus(QDBusConnection::SessionBus, connectionName);
        QVERIFY(replacement.isConnected());
        FakeKWin replacementObject(false);
        QVERIFY(replaceOwner(replacement, replacementObject));
        QTRY_VERIFY_WITH_TIMEOUT(knownSpy.count() >= 2, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(monitor.known(), 2000);
        QVERIFY(! monitor.showingDesktop());
        QVERIFY(knownSpy.count() >= 2); // invalidated, then read from new owner

        replacement.unregisterObject(QString::fromLatin1(ObjectPath));
        replacement.unregisterService(QString::fromLatin1(ServiceName));
        QDBusConnection::disconnectFromBus(connectionName);
    }
};

QTEST_GUILESS_MAIN(DesktopVisibilityMonitorTest)
#include "tst_desktopvisibilitymonitor.moc"
