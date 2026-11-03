#include "WallpaperRuntimeQmlClient.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QTimer>

namespace wekde {
namespace {

QVariantMap parseObject(const QString &json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    return document.isObject() ? document.object().toVariantMap() : QVariantMap{};
}

QJSValue errorValue(QQmlEngine *engine, const QString &message, const QString &errorCode)
{
    QJSValue error = engine->newErrorObject(QJSValue::GenericError, message);
    error.setProperty(QStringLiteral("errorCode"), errorCode);
    return error;
}

} // namespace

WallpaperRuntimeQmlClient::WallpaperRuntimeQmlClient(QObject *parent)
    : QObject(parent)
    , m_client(QDBusConnection::sessionBus(), this)
    , m_available(m_client.available())
    , m_snapshot(parseObject(m_client.snapshot()))
{
    connect(&m_client, &WallpaperRuntime::RuntimeClient::availabilityChanged,
        this, [this](bool available) {
            if (m_available == available) return;
            m_available = available;
            emit availableChanged();
        });
    connect(&m_client, &WallpaperRuntime::RuntimeClient::runtimeOwnerChanged,
        this, [this](bool available) {
            m_initialOwnerPublished = true;
            // A replacement owner is still available=true but has a new epoch.
            // Clear the old snapshot before forwarding owner availability.
            setSnapshot({});
            rejectPending(QStringLiteral("Wallpaper runtime owner changed during command"),
                available ? QStringLiteral("serviceChanged")
                          : QStringLiteral("serviceUnavailable"));
            emit ownerChanged(available);
        });
    connect(&m_client, &WallpaperRuntime::RuntimeClient::snapshotChanged,
        this, [this](const QString &json) { setSnapshot(parseObject(json)); });
    connect(&m_client, &WallpaperRuntime::RuntimeClient::commandAcknowledgement,
        this, &WallpaperRuntimeQmlClient::handleAcknowledgement);
    connect(&m_client, &WallpaperRuntime::RuntimeClient::commandFinished,
        this, &WallpaperRuntimeQmlClient::handleCompletion);
    connect(&m_client, &WallpaperRuntime::RuntimeClient::commandRequestFailed,
        this, [this](const QString &requestId, const QString &message) {
            rejectRequest(requestId, message);
        });
    connect(&m_client, &WallpaperRuntime::RuntimeClient::requestFailed,
        this, &WallpaperRuntimeQmlClient::requestFailed);
    QTimer::singleShot(0, this, [this] {
        if (m_initialOwnerPublished) return;
        m_initialOwnerPublished = true;
        emit ownerChanged(m_available);
        if (m_available) m_client.refreshSnapshot();
    });
}

bool WallpaperRuntimeQmlClient::available() const
{
    return m_available;
}

QVariantMap WallpaperRuntimeQmlClient::snapshot() const
{
    return m_snapshot;
}

void WallpaperRuntimeQmlClient::refreshSnapshot()
{
    m_client.refreshSnapshot();
}

QJSValue WallpaperRuntimeQmlClient::execute(const QVariantMap &request)
{
    QQmlEngine *engine = qmlEngine(this);
    const QString requestId = request.value(QStringLiteral("requestId")).toString();
    if (!engine) {
        emit requestFailed(QStringLiteral("Runtime client requires a QML engine"));
        return {};
    }

    const QJSValue factory = engine->evaluate(QStringLiteral(
        "(function() {"
        "  let resolvePromise, rejectPromise;"
        "  const promise = new Promise((resolve, reject) => {"
        "    resolvePromise = resolve; rejectPromise = reject;"
        "  });"
        "  return {promise: promise, resolve: resolvePromise, reject: rejectPromise};"
        "})"));
    const QJSValue promiseParts = factory.call();
    if (factory.isError() || promiseParts.isError()) {
        emit requestFailed(QStringLiteral("Could not create runtime command promise"));
        return {};
    }
    const QJSValue promise = promiseParts.property(QStringLiteral("promise"));
    const QJSValue reject = promiseParts.property(QStringLiteral("reject"));

    if (requestId.isEmpty() || m_pending.contains(requestId)) {
        reject.call({errorValue(engine,
            QStringLiteral("Command requestId is missing or already pending"),
            QStringLiteral("invalidRequestId"))});
        return promise;
    }
    if (!m_available) {
        reject.call({errorValue(engine, QStringLiteral("Runtime service is unavailable"),
            QStringLiteral("serviceUnavailable"))});
        return promise;
    }

    m_pending.insert(requestId, PendingPromise{engine,
        promiseParts.property(QStringLiteral("resolve")), reject});
    const QString json = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(request))
                                               .toJson(QJsonDocument::Compact));
    m_client.submitCommand(json);
    return promise;
}

bool WallpaperRuntimeQmlClient::setSnapshot(const QVariantMap &snapshot)
{
    if (m_snapshot == snapshot) {
        return false;
    }
    m_snapshot = snapshot;
    emit stateChanged(m_snapshot);
    return true;
}

void WallpaperRuntimeQmlClient::rejectPending(const QString &message, const QString &errorCode)
{
    QHash<QString, PendingPromise> pending;
    m_pending.swap(pending);
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        if (!it->engine) continue;
        it->reject.call({errorValue(it->engine, message, errorCode)});
    }
}

void WallpaperRuntimeQmlClient::handleAcknowledgement(const QString &json)
{
    const QVariantMap acknowledgement = parseObject(json);
    emit commandAcknowledgement(acknowledgement);
    const QString requestId = acknowledgement.value(QStringLiteral("requestId")).toString();
    auto it = m_pending.find(requestId);
    if (it == m_pending.end()) {
        return;
    }

    const PendingPromise pending = it.value();
    m_pending.erase(it);
    if (!pending.engine) return;
    const QJSValue value = pending.engine->toScriptValue(acknowledgement);
    if (acknowledgement.value(QStringLiteral("status")).toString()
        == QStringLiteral("accepted")) {
        pending.resolve.call({value});
    } else {
        QString message = acknowledgement.value(QStringLiteral("message")).toString();
        if (message.isEmpty()) message = QStringLiteral("Runtime command was rejected");
        const QString errorCode = acknowledgement.value(QStringLiteral("errorCode")).toString();
        pending.reject.call({errorValue(pending.engine, message, errorCode)});
    }
}

void WallpaperRuntimeQmlClient::handleCompletion(const QString &json)
{
    const QVariantMap result = parseObject(json);
    if (!result.isEmpty()) {
        emit commandFinished(result);
    }
}

void WallpaperRuntimeQmlClient::rejectRequest(const QString &requestId, const QString &message,
    const QString &errorCode)
{
    auto it = m_pending.find(requestId);
    if (it == m_pending.end()) return;
    const PendingPromise pending = it.value();
    m_pending.erase(it);
    if (pending.engine) {
        pending.reject.call({errorValue(pending.engine, message, errorCode)});
    }
}

} // namespace wekde
