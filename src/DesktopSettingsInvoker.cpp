#include "DesktopSettingsInvoker.hpp"

#include "PlasmaConfigurationInvoker.hpp"

#include <QDBusConnection>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace wekde {

DesktopSettingsInvoker::DesktopSettingsInvoker(QObject* parent)
    : QObject(parent) {
    auto* transport = new DBusPlasmaScriptTransport(QDBusConnection::sessionBus(), this);
    m_transport = transport;
    connect(transport, &PlasmaScriptTransport::evaluated,
        this, &DesktopSettingsInvoker::handleEvaluation);
    connect(transport, &QObject::destroyed, this, [this] {
        m_transport = nullptr;
        if (!m_requestId.isEmpty()) {
            handleEvaluation(m_requestId, false, {},
                QStringLiteral("Plasma scripting transport was destroyed"));
        }
    });
}

DesktopSettingsInvoker::DesktopSettingsInvoker(PlasmaScriptTransport* transport, QObject* parent)
    : QObject(parent), m_transport(transport) {
    if (transport) {
        connect(transport, &PlasmaScriptTransport::evaluated,
            this, &DesktopSettingsInvoker::handleEvaluation);
        connect(transport, &QObject::destroyed, this, [this] {
            m_transport = nullptr;
            if (!m_requestId.isEmpty()) {
                handleEvaluation(m_requestId, false, {},
                    QStringLiteral("Plasma scripting transport was destroyed"));
            }
        });
    }
}

bool DesktopSettingsInvoker::pending() const { return !m_requestId.isEmpty(); }
QString DesktopSettingsInvoker::errorCode() const { return m_errorCode; }
QString DesktopSettingsInvoker::message() const { return m_message; }

bool DesktopSettingsInvoker::open() {
    if (pending()) {
        setStatus(QStringLiteral("requestPending"),
            QStringLiteral("A wallpaper settings request is already pending"));
        emit finished(false, m_errorCode, m_message);
        return false;
    }
    if (!m_transport || !m_transport->available()) {
        setStatus(QStringLiteral("plasmaUnavailable"),
            QStringLiteral("The Plasma scripting service is unavailable"));
        emit finished(false, m_errorCode, m_message);
        return false;
    }

    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setStatus({}, {});
    emit pendingChanged();
    m_transport->evaluate(m_requestId, scriptForCurrentDesktop());
    return true;
}

QString DesktopSettingsInvoker::scriptForCurrentDesktop() {
    return QStringLiteral(
        "(function(){var r={ok:false,reason:'wallpaperUnavailable'};try{"
        "var d=typeof desktopForScreen==='function'?desktopForScreen(0):null;"
        "if(!d||Number(d.screen)<0||typeof d.showConfigurationInterface!=='function'){d=null;"
        "if(typeof desktops==='function'){var ds=desktops();"
        "for(var i=0;i<ds.length;i++){var item=ds[i];"
        "if(Number(item.screen)>=0&&typeof item.showConfigurationInterface==='function')"
        "{d=item;break;}}}}"
        "if(d&&typeof d.showConfigurationInterface==='function'){"
        "d.showConfigurationInterface();r={ok:true,id:String(d.id),"
        "screen:Number(d.screen),plugin:String(d.wallpaperPlugin||'')};"
        "}else if(d){r.reason='configurationUnavailable';}}"
        "catch(e){r.reason='scriptError';}print(JSON.stringify(r));})()");
}

void DesktopSettingsInvoker::setStatus(const QString& errorCode, const QString& message) {
    if (m_errorCode == errorCode && m_message == message) {
        return;
    }
    m_errorCode = errorCode;
    m_message = message;
    emit statusChanged();
}

void DesktopSettingsInvoker::handleEvaluation(const QString& requestId, bool transportOk,
    const QString& output, const QString& error) {
    if (m_requestId.isEmpty() || requestId != m_requestId) {
        return;
    }

    QString code;
    QString text;
    bool opened = false;
    if (!transportOk) {
        code = QStringLiteral("plasmaUnavailable");
        text = error;
    } else {
        const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8());
        const QJsonObject result = document.object();
        if (document.isObject() && result.value(QStringLiteral("ok")).toBool()
            && result.value(QStringLiteral("id")).isString()
            && result.value(QStringLiteral("screen")).isDouble()
            && result.value(QStringLiteral("screen")).toDouble(-1) >= 0) {
            opened = true;
            text = QStringLiteral("Wallpaper settings opened");
        } else {
            code = result.value(QStringLiteral("reason")).toString();
            if (code.isEmpty()) {
                code = QStringLiteral("invalidPlasmaResponse");
            }
            text = code == QStringLiteral("wallpaperUnavailable")
                ? QStringLiteral("No Wallpaper Engine desktop is available")
                : code == QStringLiteral("configurationUnavailable")
                    ? QStringLiteral("Wallpaper configuration is unavailable")
                    : QStringLiteral("Plasma could not open wallpaper settings");
        }
    }

    m_requestId.clear();
    setStatus(opened ? QString() : code, text);
    emit pendingChanged();
    emit finished(opened, m_errorCode, m_message);
}

} // namespace wekde
