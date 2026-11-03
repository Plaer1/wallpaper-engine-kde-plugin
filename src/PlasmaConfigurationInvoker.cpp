#include "PlasmaConfigurationInvoker.hpp"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace wekde {
namespace {
constexpr auto PlasmaService = "org.kde.plasmashell";
constexpr auto PlasmaPath = "/PlasmaShell";
constexpr auto PlasmaInterface = "org.kde.PlasmaShell";
constexpr auto ExpectedPluginId = "com.github.catsout.wallpaperEngineKde";
constexpr quint64 MaxSafeInteger = 9007199254740991ULL;

QString jsString(const QString& input) {
    const QString encoded = QString::fromUtf8(
        QJsonDocument(QJsonArray{input}).toJson(QJsonDocument::Compact));
    return encoded.mid(1, encoded.size() - 2);
}
}

DBusPlasmaScriptTransport::DBusPlasmaScriptTransport(const QDBusConnection& connection,
    QObject* parent)
    : PlasmaScriptTransport(parent), m_connection(connection) {}

bool DBusPlasmaScriptTransport::available() const {
    return m_connection.isConnected();
}

void DBusPlasmaScriptTransport::evaluate(const QString& requestId, const QString& script) {
    if (!available()) {
        emit evaluated(requestId, false, {}, QStringLiteral("Session bus is unavailable"));
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(PlasmaService),
        QString::fromLatin1(PlasmaPath), QString::fromLatin1(PlasmaInterface),
        QStringLiteral("evaluateScript"));
    message << script;
    auto* watcher = new QDBusPendingCallWatcher(m_connection.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
        [this, watcher, requestId] {
            QDBusPendingReply<QString> reply = *watcher;
            watcher->deleteLater();
            if (reply.isError()) {
                emit evaluated(requestId, false, {}, reply.error().message());
                return;
            }
            emit evaluated(requestId, true, reply.value(), {});
        });
}

PlasmaConfigurationInvoker::PlasmaConfigurationInvoker(PlasmaScriptTransport* transport,
    QObject* parent)
    : QObject(parent), m_transport(transport) {
    if (transport) {
        connect(transport, &PlasmaScriptTransport::evaluated,
            this, &PlasmaConfigurationInvoker::transportFinished);
        connect(transport, &QObject::destroyed, this, [this] {
            m_transport = nullptr;
            const auto pending = m_pending;
            m_pending.clear();
            for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
                const QString& id = it.key();
                emit finished(id, false, QStringLiteral("serviceUnavailable"),
                    QStringLiteral("Plasma scripting transport was destroyed"));
            }
        });
    }
}

bool PlasmaConfigurationInvoker::available() const {
    return m_transport && m_transport->available();
}

bool PlasmaConfigurationInvoker::validTarget(const PlasmaConfigurationTarget& target) {
    if (target.instanceId.isNull() || target.connectorName.isEmpty()
        || target.connectorName.size() > 256 || target.activityId.isEmpty()
        || target.activityId.size() > 64 || target.pluginId != QString::fromLatin1(ExpectedPluginId)
        || target.containmentId.isEmpty() || target.containmentId.size() > 16) return false;
    const QUuid activity = QUuid::fromString(target.activityId);
    if (activity.isNull()
        || activity.toString(QUuid::WithoutBraces).compare(target.activityId, Qt::CaseInsensitive) != 0)
        return false;
    quint64 containment = 0;
    for (const QChar ch : target.containmentId) {
        if (ch < QLatin1Char('0') || ch > QLatin1Char('9')) return false;
        containment = containment * 10 + ch.digitValue();
        if (containment > MaxSafeInteger) return false;
    }
    for (const QChar ch : target.connectorName) {
        if (ch.isNull() || ch.category() == QChar::Other_Control) return false;
    }
    return containment > 0;
}

QString PlasmaConfigurationInvoker::scriptFor(const PlasmaConfigurationTarget& target) {
    const QString connector = jsString(target.connectorName);
    const QString activity = jsString(target.activityId);
    const QString containment = jsString(target.containmentId);
    const QString plugin = jsString(target.pluginId);
    return QStringLiteral("(function(){var r={ok:false,reason:'targetMismatch'};try{"
        "if(typeof screenForConnector!=='function'||typeof desktopForScreen!=='function')"
        "{r.reason='mappingUnavailable';}"
        "else if(typeof currentActivity!=='function'||currentActivity()!==%1)"
        "{r.reason='activityMismatch';}"
        "else{var s=screenForConnector(%2);if(!Number.isInteger(s)||s<0)"
        "{r.reason='screenUnavailable';}"
        "else{var d=desktopForScreen(s);if(!d||String(d.id)!==%3"
        "||String(d.wallpaperPlugin)!==%4)"
        "{r.reason='targetMismatch';}"
        "else if(typeof d.showConfigurationInterface!=='function')"
        "{r.reason='configureUnavailable';}"
        "else{d.showConfigurationInterface();r={ok:true,screen:s,id:String(d.id),"
        "activity:%1,plugin:String(d.wallpaperPlugin)};}}}"
        "}catch(e){r={ok:false,reason:'scriptError'};}print(JSON.stringify(r));})()")
        .arg(activity, connector, containment, plugin);
}

bool PlasmaConfigurationInvoker::open(const QString& requestId,
    const PlasmaConfigurationTarget& target) {
    if (requestId.isEmpty() || m_pending.contains(requestId) || !validTarget(target)
        || !available()) return false;
    m_pending.insert(requestId, target);
    if (!m_transport) {
        m_pending.remove(requestId);
        return false;
    }
    m_transport->evaluate(requestId, scriptFor(target));
    return true;
}

void PlasmaConfigurationInvoker::transportFinished(const QString& requestId,
    bool transportOk, const QString& output, const QString& error) {
    const auto pending = m_pending.find(requestId);
    if (pending == m_pending.end()) return;
    const PlasmaConfigurationTarget target = pending.value();
    m_pending.erase(pending);
    if (!transportOk) {
        emit finished(requestId, false, QStringLiteral("plasmaUnavailable"), error);
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8());
    if (!document.isObject()) {
        emit finished(requestId, false, QStringLiteral("invalidPlasmaResponse"),
            QStringLiteral("Plasma returned an invalid configuration result"));
        return;
    }
    const QJsonObject result = document.object();
    if (result.value(QStringLiteral("ok")).isBool() && result.value(QStringLiteral("ok")).toBool()
        && result.value(QStringLiteral("id")).isString()
        && result.value(QStringLiteral("activity")).isString()
        && result.value(QStringLiteral("plugin")).isString()
        && result.value(QStringLiteral("screen")).isDouble()
        && result.value(QStringLiteral("screen")).toDouble(-1) >= 0
        && result.value(QStringLiteral("id")).toString() == target.containmentId
        && result.value(QStringLiteral("activity")).toString() == target.activityId
        && result.value(QStringLiteral("plugin")).toString() == target.pluginId) {
        emit finished(requestId, true, {}, QStringLiteral("Plasma confirmed the target configuration action"));
        return;
    }
    const QString reason = result.value(QStringLiteral("reason")).toString();
    const QString code = reason == QStringLiteral("mappingUnavailable")
        ? QStringLiteral("mappingUnavailable")
        : reason == QStringLiteral("configureUnavailable")
            ? QStringLiteral("configurationUnavailable") : QStringLiteral("targetMismatch");
    emit finished(requestId, false, code,
        QStringLiteral("Plasma did not confirm the requested wallpaper target"));
}

} // namespace wekde
