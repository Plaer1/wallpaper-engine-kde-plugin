#pragma once

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>

namespace wekde
{

// Event-driven view of KWin's public Show Desktop state. Unknown means the
// compositor state could not be authoritatively read from the current owner.
class DesktopVisibilityMonitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool known READ known NOTIFY knownChanged)
    Q_PROPERTY(bool showingDesktop READ showingDesktop NOTIFY showingDesktopChanged)

public:
    explicit DesktopVisibilityMonitor(
        const QDBusConnection& connection = QDBusConnection::sessionBus(),
        QObject*               parent     = nullptr);
    ~DesktopVisibilityMonitor() override;

    bool known() const;
    bool showingDesktop() const;

signals:
    void knownChanged();
    void showingDesktopChanged();

private slots:
    void ownerChanged(const QString& service, const QString& oldOwner, const QString& newOwner);
    void handleShowingDesktopChanged(bool showing, const QDBusMessage& message);

private:
    void setState(bool known, bool showing);
    void readInitialState();
    void invalidateOwner();

    QDBusConnection      m_connection;
    QDBusServiceWatcher* m_serviceWatcher = nullptr;
    QString              m_owner;
    quint64              m_ownerGeneration  = 0;
    quint64              m_signalGeneration = 0;
    bool                 m_known            = false;
    bool                 m_showingDesktop   = false;
};

} // namespace wekde
