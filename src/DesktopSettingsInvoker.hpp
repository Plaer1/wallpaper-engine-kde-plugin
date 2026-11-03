#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

namespace wekde {

class PlasmaScriptTransport;

class DesktopSettingsInvoker : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY statusChanged)
    Q_PROPERTY(QString message READ message NOTIFY statusChanged)

public:
    explicit DesktopSettingsInvoker(QObject* parent = nullptr);
    DesktopSettingsInvoker(PlasmaScriptTransport* transport, QObject* parent = nullptr);

    bool pending() const;
    QString errorCode() const;
    QString message() const;

    Q_INVOKABLE bool open();

signals:
    void pendingChanged();
    void statusChanged();
    void finished(bool opened, const QString& errorCode, const QString& message);

private:
    static QString scriptForCurrentDesktop();
    void setStatus(const QString& errorCode, const QString& message);
    void handleEvaluation(const QString& requestId, bool transportOk,
        const QString& output, const QString& error);

    QPointer<PlasmaScriptTransport> m_transport;
    QString m_requestId;
    QString m_errorCode;
    QString m_message;
};

} // namespace wekde
