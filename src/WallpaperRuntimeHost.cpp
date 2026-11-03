#include "WallpaperRuntimeHost.hpp"

#include <QMetaType>
#include <QDBusMessage>
#include <QUuid>

namespace wekde
{
namespace
{

class GlobalConfigPatchAdapter final : public WallpaperRuntime::GlobalPatchBackend {
public:
    GlobalConfigPatchAdapter(GlobalConfigBackend* backend, GlobalConfigRuntimeBridge* syncBridge)
        : m_backend(backend) {
        m_syncBridge = syncBridge;
        if (m_backend) {
            connect(m_backend, &GlobalConfigBackend::changed, this,
                    [this] { emit valuesChanged(); });
        }
    }

    QVariantMap currentValues(const QStringList& keys) const override {
        QVariantMap current;
        if (!m_backend) {
            return current;
        }
        const QVariantMap all = m_backend->all();
        for (const QString& key : keys) {
            if (all.contains(key)) {
                current.insert(key, all.value(key));
            }
        }
        return current;
    }

    void submitPatch(const QString& requestId, const QVariantMap& patch,
                     const QVariantMap& baseline) override {
        if (!m_backend) {
            emit patchFinished(requestId, QStringLiteral("rejected"),
                               QStringLiteral("serviceUnavailable"),
                               QStringLiteral("Global configuration store is unavailable"));
            return;
        }
        const GlobalConfigPatchResult result = m_backend->patchValues(patch, baseline);
        switch (result.status) {
        case GlobalConfigPatchStatus::Applied:
        case GlobalConfigPatchStatus::NoChange:
            if (result.status == GlobalConfigPatchStatus::Applied && m_syncBridge) {
                m_syncBridge->broadcastGlobalChanged();
            }
            emit patchFinished(requestId, QStringLiteral("applied"), {}, result.message);
            return;
        case GlobalConfigPatchStatus::Busy:
            emit patchFinished(requestId, QStringLiteral("rejected"),
                               QStringLiteral("busy"), result.message);
            return;
        case GlobalConfigPatchStatus::Conflict:
            emit patchFinished(requestId, QStringLiteral("rejected"),
                               QStringLiteral("conflict"), result.message);
            return;
        case GlobalConfigPatchStatus::InvalidJson:
            emit patchFinished(requestId, QStringLiteral("rejected"),
                               QStringLiteral("invalidGlobalConfig"), result.message);
            return;
        case GlobalConfigPatchStatus::IoError:
            emit patchFinished(requestId, QStringLiteral("rejected"),
                               QStringLiteral("globalConfigIoError"), result.message);
            return;
        }
    }

private:
    GlobalConfigBackend* m_backend = nullptr;
    QPointer<GlobalConfigRuntimeBridge> m_syncBridge;
};

bool isCanonicalUuid(const QString& value, QUuid* id = nullptr) {
    const QUuid parsed = QUuid::fromString(value);
    if (parsed.isNull() ||
        parsed.toString(QUuid::WithoutBraces).compare(value, Qt::CaseInsensitive) != 0) {
        return false;
    }
    if (id) {
        *id = parsed;
    }
    return true;
}

bool isDecimalId(const QString& value) {
    if (value.isEmpty() || value.size() > 20) {
        return false;
    }
    for (const QChar character : value) {
        if (character < QLatin1Char('0') || character > QLatin1Char('9')) {
            return false;
        }
    }
    return true;
}

QString desiredName(WallpaperPlaybackPolicy::Override desired) {
    switch (desired) {
    case WallpaperPlaybackPolicy::Override::Automatic:
        return QStringLiteral("automatic");
    case WallpaperPlaybackPolicy::Override::Paused:
        return QStringLiteral("paused");
    case WallpaperPlaybackPolicy::Override::PlayDespiteWindows:
        return QStringLiteral("playDespiteWindows");
    }
    return {};
}

} // namespace

GlobalConfigRuntimeBridge::GlobalConfigRuntimeBridge(GlobalConfigBackend* backend,
    const QDBusConnection& connection,
    QObject* parent)
    : QObject(parent)
    , m_backend(backend)
    , m_connection(connection)
    , m_senderId(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    if (m_connection.isConnected() &&
        !m_connection.connect(QString(), QStringLiteral("/WallpaperSync"),
            QStringLiteral("com.github.catsout.wallpaperEngineKde.Sync"),
            QStringLiteral("GlobalConfigChanged"), this,
            SLOT(handleGlobalConfigChanged(QString)))) {
        qWarning("GlobalConfigRuntimeBridge: failed to subscribe to global config sync");
    }
}

void GlobalConfigRuntimeBridge::broadcastGlobalChanged() {
    if (!m_connection.isConnected()) {
        return;
    }
    QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/WallpaperSync"),
        QStringLiteral("com.github.catsout.wallpaperEngineKde.Sync"),
        QStringLiteral("GlobalConfigChanged"));
    message << m_senderId;
    m_connection.send(message);
}

void GlobalConfigRuntimeBridge::handleGlobalConfigChanged(const QString& senderId) {
    if (senderId == m_senderId || !m_backend) {
        return;
    }
    m_backend->reloadFromDisk();
}

WallpaperRuntimeHost::WallpaperRuntimeHost(
    WallpaperRuntime::GlobalPatchBackend* patchBackend,
    const QDBusConnection& connection,
    PlasmaConfigurationInvoker* configurationInvoker)
    : m_syncBridge(patchBackend ? nullptr : std::make_unique<GlobalConfigRuntimeBridge>(
              GlobalConfigBackend::instance(), connection))
    , m_ownedPatchBackend(patchBackend ? nullptr
                                      : std::make_unique<GlobalConfigPatchAdapter>(
                                            GlobalConfigBackend::instance(), m_syncBridge.get()))
    , m_patchBackend(patchBackend ? patchBackend : m_ownedPatchBackend.get())
    , m_ownedScriptTransport(std::make_unique<DBusPlasmaScriptTransport>(connection))
    , m_ownedConfigurationInvoker(configurationInvoker ? nullptr
        : std::make_unique<PlasmaConfigurationInvoker>(m_ownedScriptTransport.get()))
    , m_configurationInvoker(configurationInvoker ? configurationInvoker : m_ownedConfigurationInvoker.get())
    , m_registry()
    , m_service(m_registry, m_patchBackend, connection) {
    if (m_configurationInvoker) {
        QObject::connect(m_configurationInvoker, &PlasmaConfigurationInvoker::finished, &m_service,
            [this](const QString& requestId, bool opened, const QString& errorCode,
                   const QString& message) {
                QPointer<WallpaperRuntimeQmlEndpoint> endpoint = m_pendingConfigurations.take(requestId);
                if (endpoint) endpoint->completeConfigurationRequest(requestId, endpoint->instanceId(),
                    opened, errorCode, message);
            });
        QObject::connect(m_configurationInvoker, &QObject::destroyed, &m_service, [this] {
            m_configurationInvoker = nullptr;
            const auto pending = m_pendingConfigurations.keys();
            for (const QString& requestId : pending) {
                QPointer<WallpaperRuntimeQmlEndpoint> endpoint = m_pendingConfigurations.take(requestId);
                if (endpoint) endpoint->completeConfigurationRequest(requestId, endpoint->instanceId(),
                    false, QStringLiteral("serviceUnavailable"),
                    QStringLiteral("Plasma configuration service was destroyed"));
            }
        });
    }
    if (m_patchBackend) {
        QObject::connect(m_patchBackend, &QObject::destroyed, &m_registry, [this] {
            const auto targets = m_registry.snapshot().targets;
            for (const auto& target : targets) {
                QStringList remaining;
                if (target.capabilities.contains(QStringLiteral("SetPlayback"))) {
                    remaining.append(QStringLiteral("SetPlayback"));
                }
                if (target.capabilities.contains(QStringLiteral("OpenConfiguration"))
                    && m_configurationInvoker && m_configurationInvoker->available()) {
                    remaining.append(QStringLiteral("OpenConfiguration"));
                }
                if (target.capabilities.contains(QStringLiteral("NextWallpaper")))
                    remaining.append(QStringLiteral("NextWallpaper"));
                m_registry.updateCapabilities(target.instanceId, remaining);
            }
        });
    }
}

WallpaperRuntimeHost& WallpaperRuntimeHost::instance() {
    static WallpaperRuntimeHost* host = new WallpaperRuntimeHost;
    return *host;
}

WallpaperRuntime::Registry& WallpaperRuntimeHost::registry() { return m_registry; }
const WallpaperRuntime::Registry& WallpaperRuntimeHost::registry() const { return m_registry; }
WallpaperRuntime::RuntimeService& WallpaperRuntimeHost::service() { return m_service; }
const WallpaperRuntime::RuntimeService& WallpaperRuntimeHost::service() const { return m_service; }

WallpaperRuntime::Registration WallpaperRuntimeHost::registerEndpoint(
    const WallpaperRuntime::TargetDescriptor& descriptor,
    WallpaperRuntime::PlaybackEndpoint* endpoint,
    bool configurationPreview) {
    return m_registry.registerTarget(descriptor, endpoint, configurationPreview);
}

bool WallpaperRuntimeHost::unregisterEndpoint(const QUuid& instanceId) {
    return m_registry.unregisterTarget(instanceId);
}

bool WallpaperRuntimeHost::supportsGlobalPatches() const {
    return !m_patchBackend.isNull();
}

QStringList WallpaperRuntimeHost::capabilities(bool playbackConnected, bool followConnected,
    bool reloadConnected, bool configurationConnected, bool nextConnected) const {
    QStringList result;
    if (playbackConnected) {
        result.append(QStringLiteral("SetPlayback"));
    }
    if (followConnected) result.append(QStringLiteral("SetFollow"));
    if (reloadConnected) result.append(QStringLiteral("ReloadWallpaper"));
    if (configurationConnected && m_configurationInvoker && m_configurationInvoker->available())
        result.append(QStringLiteral("OpenConfiguration"));
    if (nextConnected) result.append(QStringLiteral("NextWallpaper"));
    if (supportsGlobalPatches()) {
        result.append(QStringLiteral("SetGlobalAudioMuted"));
        result.append(QStringLiteral("SetGlobalSizing"));
    }
    return result;
}

bool WallpaperRuntimeHost::requestConfiguration(WallpaperRuntimeQmlEndpoint* endpoint,
    const QString& requestId, const PlasmaConfigurationTarget& target) {
    if (!endpoint || !m_configurationInvoker || !m_configurationInvoker->available()
        || m_pendingConfigurations.contains(requestId)) return false;
    m_pendingConfigurations.insert(requestId, endpoint);
    if (!m_configurationInvoker->open(requestId, target)) {
        m_pendingConfigurations.remove(requestId);
        return false;
    }
    return true;
}

WallpaperRuntimeQmlEndpoint::WallpaperRuntimeQmlEndpoint(QObject* parent)
    : WallpaperRuntimeQmlEndpoint(&WallpaperRuntimeHost::instance(), parent) {
}

WallpaperRuntimeQmlEndpoint::WallpaperRuntimeQmlEndpoint(WallpaperRuntimeHost* host,
                                                         QObject* parent)
    : WallpaperRuntime::PlaybackEndpoint(parent)
    , m_host(host) {
}

WallpaperRuntimeQmlEndpoint::~WallpaperRuntimeQmlEndpoint() {
    unregisterCurrentTarget();
}

WallpaperRuntimeRegistration* WallpaperRuntimeQmlEndpoint::registration() const {
    return m_registration;
}

void WallpaperRuntimeQmlEndpoint::setRegistration(WallpaperRuntimeRegistration* registration) {
    if (m_registration == registration) {
        return;
    }
    if (m_registration) {
        disconnect(m_registration, nullptr, this, nullptr);
    }
    unregisterCurrentTarget();
    m_registration = registration;
    if (registration) {
        connect(registration, &WallpaperRuntimeRegistration::registrationChanged,
                this, &WallpaperRuntimeQmlEndpoint::synchronizeRegistration);
        connect(registration, &QObject::destroyed, this, [this] {
            m_registration = nullptr;
            unregisterCurrentTarget();
            emit registrationChanged();
        });
    }
    emit registrationChanged();
    synchronizeRegistration();
}

bool WallpaperRuntimeQmlEndpoint::isRegistered() const { return !m_registeredInstance.isNull(); }
bool WallpaperRuntimeQmlEndpoint::playbackRequestConnected() const {
    return m_playbackRequestConnected;
}
void WallpaperRuntimeQmlEndpoint::setPlaybackRequestConnected(bool connected) {
    if (m_playbackRequestConnected == connected) {
        return;
    }
    m_playbackRequestConnected = connected;
    emit playbackRequestConnectedChanged();
    if (isRegistered() && m_host) {
        m_host->registry().updateCapabilities(m_registeredInstance,
            m_host->capabilities(m_playbackRequestConnected, m_followRequestConnected,
                m_reloadRequestConnected, true, m_nextRequestConnected));
    }
}
bool WallpaperRuntimeQmlEndpoint::followRequestConnected() const { return m_followRequestConnected; }
void WallpaperRuntimeQmlEndpoint::setFollowRequestConnected(bool connected) {
    if (m_followRequestConnected == connected) return;
    m_followRequestConnected = connected;
    emit followRequestConnectedChanged();
    if (isRegistered() && m_host)
        m_host->registry().updateCapabilities(m_registeredInstance,
            m_host->capabilities(m_playbackRequestConnected, m_followRequestConnected,
                m_reloadRequestConnected, true, m_nextRequestConnected));
}
bool WallpaperRuntimeQmlEndpoint::reloadRequestConnected() const { return m_reloadRequestConnected; }
void WallpaperRuntimeQmlEndpoint::setReloadRequestConnected(bool connected) {
    if (m_reloadRequestConnected == connected) return;
    m_reloadRequestConnected = connected;
    emit reloadRequestConnectedChanged();
    if (isRegistered() && m_host)
        m_host->registry().updateCapabilities(m_registeredInstance,
            m_host->capabilities(m_playbackRequestConnected, m_followRequestConnected,
                m_reloadRequestConnected, true, m_nextRequestConnected));
}
bool WallpaperRuntimeQmlEndpoint::nextRequestConnected() const { return m_nextRequestConnected; }
void WallpaperRuntimeQmlEndpoint::setNextRequestConnected(bool connected) {
    if (m_nextRequestConnected == connected) return;
    m_nextRequestConnected = connected;
    emit nextRequestConnectedChanged();
    if (isRegistered() && m_host)
        m_host->registry().updateCapabilities(m_registeredInstance,
            m_host->capabilities(m_playbackRequestConnected, m_followRequestConnected,
                m_reloadRequestConnected, true, m_nextRequestConnected));
}
QString WallpaperRuntimeQmlEndpoint::instanceId() const {
    return m_registeredInstance.isNull()
        ? QString()
        : m_registeredInstance.toString(QUuid::WithoutBraces);
}

QString WallpaperRuntimeQmlEndpoint::wallpaperSource() const { return m_wallpaperSource; }
void WallpaperRuntimeQmlEndpoint::setWallpaperSource(const QString& source) {
    if (m_wallpaperSource == source) return;
    m_wallpaperSource = source;
    emit metadataChanged();
    updateRegisteredMetadata();
}
QString WallpaperRuntimeQmlEndpoint::title() const { return m_title; }
void WallpaperRuntimeQmlEndpoint::setTitle(const QString& title) {
    if (m_title == title) return;
    m_title = title;
    emit metadataChanged();
    updateRegisteredMetadata();
}
QString WallpaperRuntimeQmlEndpoint::workshopId() const { return m_workshopId; }
void WallpaperRuntimeQmlEndpoint::setWorkshopId(const QString& workshopId) {
    if (m_workshopId == workshopId) return;
    m_workshopId = workshopId;
    emit metadataChanged();
    updateRegisteredMetadata();
}
QString WallpaperRuntimeQmlEndpoint::backend() const { return m_backend; }
void WallpaperRuntimeQmlEndpoint::setBackend(const QString& backend) {
    if (m_backend == backend) return;
    m_backend = backend;
    emit metadataChanged();
    updateRegisteredMetadata();
}
QVariantMap WallpaperRuntimeQmlEndpoint::followState() const { return m_followState; }
void WallpaperRuntimeQmlEndpoint::setFollowState(const QVariantMap& state) {
    const QVariantMap normalized{
        {QStringLiteral("enabled"), state.value(QStringLiteral("enabled")).toBool()},
        {QStringLiteral("mode"), state.value(QStringLiteral("mode")).toString()},
        {QStringLiteral("accountKey"), state.value(QStringLiteral("accountKey")).toString()},
        {QStringLiteral("sourceKey"), state.value(QStringLiteral("sourceKey")).toString()},
        {QStringLiteral("choices"), state.value(QStringLiteral("choices")).toList()},
        {QStringLiteral("status"), state.value(QStringLiteral("status")).toString()},
        {QStringLiteral("provenance"), state.value(QStringLiteral("provenance")).toString()}};
    if (m_followState == normalized) return;
    m_followState = normalized;
    emit followStateChanged();
    updateRegisteredFollowState();
}

bool WallpaperRuntimeQmlEndpoint::reportPlaybackState(const QString& state,
                                                        const QStringList& pauseReasons) {
    if (!isRegistered() || !m_host) {
        return false;
    }
    WallpaperRuntime::EffectivePlayback effective;
    if (state == QStringLiteral("unknown")) {
        effective = WallpaperRuntime::EffectivePlayback::Unknown;
    } else if (state == QStringLiteral("loading")) {
        effective = WallpaperRuntime::EffectivePlayback::Loading;
    } else if (state == QStringLiteral("playing")) {
        effective = WallpaperRuntime::EffectivePlayback::Playing;
    } else if (state == QStringLiteral("paused")) {
        effective = WallpaperRuntime::EffectivePlayback::Paused;
    } else if (state == QStringLiteral("error")) {
        effective = WallpaperRuntime::EffectivePlayback::Error;
    } else {
        return false;
    }

    QVector<WallpaperPlaybackPolicy::Reason> reasons;
    for (const QString& reason : pauseReasons) {
        if (reason == QStringLiteral("sleep")) {
            reasons.append(WallpaperPlaybackPolicy::Reason::Sleep);
        } else if (reason == QStringLiteral("inactiveSession")) {
            reasons.append(WallpaperPlaybackPolicy::Reason::InactiveSession);
        } else if (reason == QStringLiteral("manual")) {
            reasons.append(WallpaperPlaybackPolicy::Reason::Manual);
        } else if (reason == QStringLiteral("battery")) {
            reasons.append(WallpaperPlaybackPolicy::Reason::Battery);
        } else if (reason == QStringLiteral("gameMode")) {
            reasons.append(WallpaperPlaybackPolicy::Reason::GameMode);
        } else if (reason == QStringLiteral("window")) {
            reasons.append(WallpaperPlaybackPolicy::Reason::WindowCoverage);
        } else if (reason == QStringLiteral("fullscreen")) {
            reasons.append(WallpaperPlaybackPolicy::Reason::FullscreenApp);
        } else {
            return false;
        }
    }
    return m_host->registry().confirmPlayback(m_registeredInstance, effective, reasons);
}

void WallpaperRuntimeQmlEndpoint::requestPlayback(WallpaperPlaybackPolicy::Override desired) {
    if (isRegistered()) {
        emit playbackRequested(desiredName(desired));
    }
}

bool WallpaperRuntimeQmlEndpoint::supportsFollow() const {
    return isRegistered() && m_followRequestConnected;
}

void WallpaperRuntimeQmlEndpoint::requestFollow(const QString& requestId, const QVariantMap& args) {
    if (!supportsFollow() || requestId.isEmpty() || m_pendingFollowRequests.contains(requestId)) return;
    m_pendingFollowRequests.insert(requestId);
    emit followRequested(requestId, instanceId(), args);
}

bool WallpaperRuntimeQmlEndpoint::supportsReload() const {
    return isRegistered() && m_reloadRequestConnected;
}

void WallpaperRuntimeQmlEndpoint::requestReload(const QString& requestId) {
    if (!supportsReload() || requestId.isEmpty() || m_pendingReloadRequests.contains(requestId)) return;
    m_pendingReloadRequests.insert(requestId);
    emit reloadRequested(requestId, instanceId());
}

bool WallpaperRuntimeQmlEndpoint::supportsOpenConfiguration() const {
    return isRegistered() && m_host
        && m_host->capabilities(false, false, false, true).contains(QStringLiteral("OpenConfiguration"));
}

void WallpaperRuntimeQmlEndpoint::requestOpenConfiguration(const QString& requestId) {
    if (!supportsOpenConfiguration() || !m_registration || requestId.isEmpty()
        || m_pendingConfigurationRequests.contains(requestId)) return;
    PlasmaConfigurationTarget target;
    target.instanceId = QUuid::fromString(m_registration->instanceId());
    target.containmentId = m_registration->containmentId();
    target.activityId = m_registration->activityId();
    target.connectorName = m_registration->outputName();
    target.pluginId = QStringLiteral("com.github.catsout.wallpaperEngineKde");
    m_pendingConfigurationRequests.insert(requestId);
    if (!m_host->requestConfiguration(this, requestId, target)) {
        m_pendingConfigurationRequests.remove(requestId);
        emit configurationFinished(requestId, false, QStringLiteral("serviceUnavailable"),
            QStringLiteral("Plasma configuration service is unavailable"));
    }
}

bool WallpaperRuntimeQmlEndpoint::supportsNextWallpaper() const {
    return isRegistered() && m_nextRequestConnected;
}

void WallpaperRuntimeQmlEndpoint::requestNextWallpaper(const QString& requestId) {
    if (!supportsNextWallpaper() || requestId.isEmpty() || m_pendingNextRequests.contains(requestId)) return;
    m_pendingNextRequests.insert(requestId);
    emit nextRequested(requestId, instanceId());
}

bool WallpaperRuntimeQmlEndpoint::completeFollowRequest(const QString& requestId,
    const QString& instanceIdValue, bool applied, bool enabled, const QString& mode,
    const QString& accountKey, const QString& sourceKey, const QString& errorCode,
    const QString& message) {
    if (!isRegistered() || instanceIdValue != instanceId()
        || !m_pendingFollowRequests.contains(requestId)) return false;
    if (applied && (m_followState.value(QStringLiteral("enabled")).toBool() != enabled
        || (enabled && (m_followState.value(QStringLiteral("mode")).toString() != mode
            || m_followState.value(QStringLiteral("accountKey")).toString() != accountKey
            || m_followState.value(QStringLiteral("sourceKey")).toString() != sourceKey)))) {
        applied = false;
    }
    m_pendingFollowRequests.remove(requestId);
    updateRegisteredFollowState();
    emit followFinished(requestId, applied, enabled, mode, accountKey, sourceKey,
        applied ? errorCode : QStringLiteral("assignmentMismatch"),
        applied ? message : QStringLiteral("Live Follow settings did not match the confirmed assignment"));
    return true;
}

bool WallpaperRuntimeQmlEndpoint::completeReloadRequest(const QString& requestId,
    const QString& instanceIdValue, bool applied, const QString& errorCode,
    const QString& message) {
    if (!isRegistered() || instanceIdValue != instanceId()
        || !m_pendingReloadRequests.contains(requestId)) return false;
    m_pendingReloadRequests.remove(requestId);
    emit reloadFinished(requestId, applied, errorCode, message);
    return true;
}

bool WallpaperRuntimeQmlEndpoint::completeConfigurationRequest(const QString& requestId,
    const QString& instanceIdValue, bool opened, const QString& errorCode,
    const QString& message) {
    if (!isRegistered() || instanceIdValue != instanceId()
        || !m_pendingConfigurationRequests.remove(requestId)) return false;
    emit configurationFinished(requestId, opened, errorCode, message);
    return true;
}

bool WallpaperRuntimeQmlEndpoint::completeNextRequest(const QString& requestId,
    const QString& instanceIdValue, bool applied, const QString& errorCode,
    const QString& message) {
    if (!isRegistered() || instanceIdValue != instanceId()
        || !m_pendingNextRequests.remove(requestId)) return false;
    emit nextWallpaperFinished(requestId, applied, errorCode, message);
    return true;
}

void WallpaperRuntimeQmlEndpoint::synchronizeRegistration() {
    if (!m_host || !m_registration || !m_registration->isRegistered()) {
        unregisterCurrentTarget();
        return;
    }
    QUuid requestedInstance;
    if (!isCanonicalUuid(m_registration->instanceId(), &requestedInstance) ||
        !isDecimalId(m_registration->containmentId()) ||
        m_registration->activityId().isEmpty() || m_registration->outputName().isEmpty()) {
        emit endpointRejected(QStringLiteral("invalidActiveWallpaperIdentity"));
        unregisterCurrentTarget();
        return;
    }
    if (!m_registeredInstance.isNull() && m_registeredInstance != requestedInstance) {
        unregisterCurrentTarget();
    }
    if (m_registeredInstance.isNull()) {
        const WallpaperRuntime::Registration result =
            m_host->registerEndpoint(descriptor(), this, false);
        if (!result.accepted || result.instanceId != requestedInstance) {
            if (result.accepted) {
                m_host->unregisterEndpoint(result.instanceId);
            }
            emit endpointRejected(QStringLiteral("runtimeTargetRegistrationFailed"));
            return;
        }
        m_registeredInstance = result.instanceId;
        emit registeredChanged();
        return;
    }
    m_host->registry().updateOutput(m_registeredInstance, m_registration->outputName(),
                                    m_registration->outputName());
    m_host->registry().updateWallpaper(m_registeredInstance, m_wallpaperSource, m_title,
                                       m_workshopId, m_backend);
}

void WallpaperRuntimeQmlEndpoint::unregisterCurrentTarget() {
    if (m_registeredInstance.isNull()) {
        return;
    }
    const QUuid oldInstance = m_registeredInstance;
    m_registeredInstance = {};
    if (m_host) {
        m_host->unregisterEndpoint(oldInstance);
    }
    emit registeredChanged();
}

void WallpaperRuntimeQmlEndpoint::updateRegisteredMetadata() {
    if (!m_registeredInstance.isNull() && m_host) {
        m_host->registry().updateWallpaper(m_registeredInstance, m_wallpaperSource, m_title,
                                           m_workshopId, m_backend);
    }
}

void WallpaperRuntimeQmlEndpoint::updateRegisteredFollowState() {
    if (!m_registeredInstance.isNull() && m_host) {
        m_host->registry().updateFollow(m_registeredInstance,
            m_followState.value(QStringLiteral("enabled")).toBool(),
            m_followState.value(QStringLiteral("mode")).toString(),
            m_followState.value(QStringLiteral("accountKey")).toString(),
            m_followState.value(QStringLiteral("sourceKey")).toString(),
            m_followState.value(QStringLiteral("choices")).toList(),
            m_followState.value(QStringLiteral("status")).toString(),
            m_followState.value(QStringLiteral("provenance")).toString());
    }
}

WallpaperRuntime::TargetDescriptor WallpaperRuntimeQmlEndpoint::descriptor() const {
    WallpaperRuntime::TargetDescriptor result;
    result.instanceId = QUuid::fromString(m_registration->instanceId());
    result.containmentId = m_registration->containmentId();
    result.activityId = m_registration->activityId();
    result.screenName = m_registration->outputName();
    result.screenLabel = m_registration->outputName();
    result.wallpaperSource = m_wallpaperSource;
    result.title = m_title;
    result.workshopId = m_workshopId;
    result.backend = m_backend;
    result.followEnabled = m_followState.value(QStringLiteral("enabled")).toBool();
    result.followMode = m_followState.value(QStringLiteral("mode")).toString();
    result.followAccountKey = m_followState.value(QStringLiteral("accountKey")).toString();
    result.followSourceKey = m_followState.value(QStringLiteral("sourceKey")).toString();
    result.followChoices = m_followState.value(QStringLiteral("choices")).toList();
    result.followStatus = m_followState.value(QStringLiteral("status")).toString();
    result.followProvenance = m_followState.value(QStringLiteral("provenance")).toString();
    if (m_host) {
        result.capabilities = m_host->capabilities(m_playbackRequestConnected, m_followRequestConnected,
            m_reloadRequestConnected, true, m_nextRequestConnected);
    }
    return result;
}

} // namespace wekde
