#pragma once

#include "WallpaperRuntime.hpp"

#include <QHash>
#include <QJSValue>
#include <QQmlEngine>
#include <QVariantMap>

namespace wekde {

class WallpaperRuntimeQmlClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY stateChanged)

public:
    explicit WallpaperRuntimeQmlClient(QObject *parent = nullptr);

    bool available() const;
    QVariantMap snapshot() const;

    Q_INVOKABLE void refreshSnapshot();
    Q_INVOKABLE QJSValue execute(const QVariantMap &request);

signals:
    void availableChanged();
    void ownerChanged(bool available);
    void stateChanged(const QVariantMap &snapshot);
    void commandAcknowledgement(const QVariantMap &acknowledgement);
    void commandFinished(const QVariantMap &result);
    void requestFailed(const QString &message);

private:
    struct PendingPromise {
        QPointer<QQmlEngine> engine;
        QJSValue resolve;
        QJSValue reject;
    };

    bool setSnapshot(const QVariantMap &snapshot);
    void rejectPending(const QString &message,
        const QString &errorCode = QStringLiteral("serviceUnavailable"));
    void handleAcknowledgement(const QString &json);
    void handleCompletion(const QString &json);
    void rejectRequest(const QString &requestId, const QString &message,
        const QString &errorCode = QStringLiteral("transportError"));

    WallpaperRuntime::RuntimeClient m_client;
    bool m_available = false;
    bool m_initialOwnerPublished = false;
    QVariantMap m_snapshot;
    QHash<QString, PendingPromise> m_pending;
};

} // namespace wekde
