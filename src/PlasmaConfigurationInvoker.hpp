#pragma once

#include <QDBusConnection>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QUuid>

namespace wekde {

struct PlasmaConfigurationTarget {
    QUuid instanceId;
    QString containmentId;
    QString activityId;
    QString connectorName;
    QString pluginId;
};

class PlasmaScriptTransport : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~PlasmaScriptTransport() override = default;
    virtual bool available() const = 0;
    virtual void evaluate(const QString& requestId, const QString& script) = 0;

signals:
    void evaluated(const QString& requestId, bool transportOk, const QString& output,
        const QString& error);
};

class DBusPlasmaScriptTransport final : public PlasmaScriptTransport {
    Q_OBJECT
public:
    explicit DBusPlasmaScriptTransport(const QDBusConnection& connection,
        QObject* parent = nullptr);
    bool available() const override;
    void evaluate(const QString& requestId, const QString& script) override;

private:
    QDBusConnection m_connection;
};

class PlasmaConfigurationInvoker final : public QObject {
    Q_OBJECT
public:
    explicit PlasmaConfigurationInvoker(PlasmaScriptTransport* transport,
        QObject* parent = nullptr);

    bool available() const;
    bool open(const QString& requestId, const PlasmaConfigurationTarget& target);

signals:
    void finished(const QString& requestId, bool opened,
        const QString& errorCode, const QString& message);

private:
    static bool validTarget(const PlasmaConfigurationTarget& target);
    static QString scriptFor(const PlasmaConfigurationTarget& target);
    void transportFinished(const QString& requestId, bool transportOk,
        const QString& output, const QString& error);

    QPointer<PlasmaScriptTransport> m_transport;
    QHash<QString, PlasmaConfigurationTarget> m_pending;
};

} // namespace wekde
