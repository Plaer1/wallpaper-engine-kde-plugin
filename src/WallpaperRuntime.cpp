#include "WallpaperRuntime.hpp"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QScopedValueRollback>
#include <QSet>
#include <QTimer>

namespace WallpaperRuntime {

Registry::Registry(QObject *parent)
    : QObject(parent)
    , m_epoch(QUuid::createUuid())
{
}

QUuid Registry::epoch() const
{
    return m_epoch;
}

quint64 Registry::revision() const
{
    return m_revision;
}

void Registry::renewEpoch()
{
    m_epoch = QUuid::createUuid();
    m_revision = 0;
}

Registration Registry::registerTarget(const TargetDescriptor &descriptor,
    PlaybackEndpoint *endpoint,
    bool configurationPreview)
{
    if (configurationPreview || !endpoint || descriptor.containmentId.isEmpty()) {
        return {};
    }

    const QUuid instanceId = descriptor.instanceId.isNull()
        ? QUuid::createUuid()
        : descriptor.instanceId;
    if (find(instanceId)) {
        return {};
    }

    Entry entry;
    entry.snapshot.instanceId = instanceId;
    entry.snapshot.containmentId = descriptor.containmentId;
    entry.snapshot.activityId = descriptor.activityId;
    entry.snapshot.screenName = descriptor.screenName;
    entry.snapshot.screenLabel = descriptor.screenLabel;
    entry.snapshot.wallpaperSource = descriptor.wallpaperSource;
    entry.snapshot.title = descriptor.title;
    entry.snapshot.workshopId = descriptor.workshopId;
    entry.snapshot.backend = descriptor.backend;
    entry.snapshot.followEnabled = descriptor.followEnabled;
    entry.snapshot.followMode = descriptor.followMode;
    entry.snapshot.followAccountKey = descriptor.followAccountKey;
    entry.snapshot.followSourceKey = descriptor.followSourceKey;
    entry.snapshot.followChoices = descriptor.followChoices;
    entry.snapshot.followStatus = descriptor.followStatus;
    entry.snapshot.followProvenance = descriptor.followProvenance;
    entry.snapshot.capabilities = descriptor.capabilities;
    entry.endpoint = endpoint;

    m_entries.append(entry);
    connect(endpoint, &QObject::destroyed, this, [this, instanceId] { removeTarget(instanceId); });
    connect(endpoint, &PlaybackEndpoint::followFinished, this,
        [this, instanceId](const QString &requestId, bool applied, bool enabled, const QString &mode,
            const QString &accountKey, const QString &sourceKey, const QString &errorCode,
            const QString &message) {
            Entry *current = find(instanceId);
            if (!current || !current->endpoint || !current->endpoint->supportsFollow()) return;
            emit followFinished(requestId, instanceId, applied, enabled, mode, accountKey,
                sourceKey, errorCode, message);
        });
    connect(endpoint, &PlaybackEndpoint::reloadFinished, this,
        [this, instanceId](const QString &requestId, bool applied,
            const QString &errorCode, const QString &message) {
            const Entry *current = find(instanceId);
            if (!current || !current->endpoint || !current->endpoint->supportsReload()) return;
            emit reloadFinished(requestId, instanceId, applied, errorCode, message);
        });
    connect(endpoint, &PlaybackEndpoint::configurationFinished, this,
        [this, instanceId](const QString &requestId, bool applied,
            const QString &errorCode, const QString &message) {
            const Entry *current = find(instanceId);
            if (!current || !current->endpoint || !current->endpoint->supportsOpenConfiguration()) return;
            emit configurationFinished(requestId, instanceId, applied, errorCode, message);
        });
    connect(endpoint, &PlaybackEndpoint::nextWallpaperFinished, this,
        [this, instanceId](const QString &requestId, bool applied,
            const QString &errorCode, const QString &message) {
            const Entry *current = find(instanceId);
            if (!current || !current->endpoint || !current->endpoint->supportsNextWallpaper()) return;
            emit nextWallpaperFinished(requestId, instanceId, applied, errorCode, message);
        });
    advanceRevision();
    return {true, instanceId};
}

bool Registry::unregisterTarget(const QUuid &instanceId)
{
    const quint64 before = m_revision;
    removeTarget(instanceId);
    return m_revision != before;
}

bool Registry::updateOutput(const QUuid &instanceId,
    const QString &screenName,
    const QString &screenLabel)
{
    Entry *entry = find(instanceId);
    if (!entry || (entry->snapshot.screenName == screenName
                      && entry->snapshot.screenLabel == screenLabel)) {
        return false;
    }
    entry->snapshot.screenName = screenName;
    entry->snapshot.screenLabel = screenLabel;
    advanceRevision();
    return true;
}

bool Registry::updateWallpaper(const QUuid &instanceId,
    const QString &source,
    const QString &title,
    const QString &workshopId,
    const QString &backend)
{
    Entry *entry = find(instanceId);
    if (!entry || (entry->snapshot.wallpaperSource == source && entry->snapshot.title == title
                      && entry->snapshot.workshopId == workshopId
                      && entry->snapshot.backend == backend)) {
        return false;
    }
    entry->snapshot.wallpaperSource = source;
    entry->snapshot.title = title;
    entry->snapshot.workshopId = workshopId;
    entry->snapshot.backend = backend;
    advanceRevision();
    return true;
}

bool Registry::updateCapabilities(const QUuid &instanceId, const QStringList &capabilities)
{
    Entry *entry = find(instanceId);
    if (!entry || entry->snapshot.capabilities == capabilities) {
        return false;
    }
    entry->snapshot.capabilities = capabilities;
    advanceRevision();
    return true;
}

bool Registry::updateFollow(const QUuid &instanceId, bool enabled, const QString &mode,
    const QString &accountKey, const QString &sourceKey, const QVariantList &choices,
    const QString &status, const QString &provenance)
{
    Entry *entry = find(instanceId);
    if (!entry || (entry->snapshot.followEnabled == enabled
                      && entry->snapshot.followMode == mode
                      && entry->snapshot.followAccountKey == accountKey
                      && entry->snapshot.followSourceKey == sourceKey
                      && entry->snapshot.followChoices == choices
                      && entry->snapshot.followStatus == status
                      && entry->snapshot.followProvenance == provenance)) return false;
    entry->snapshot.followEnabled = enabled;
    entry->snapshot.followMode = mode;
    entry->snapshot.followAccountKey = accountKey;
    entry->snapshot.followSourceKey = sourceKey;
    entry->snapshot.followChoices = choices;
    entry->snapshot.followStatus = status;
    entry->snapshot.followProvenance = provenance;
    advanceRevision();
    return true;
}

bool Registry::updateGlobal(const QVariantMap &values)
{
    if (m_global == values) {
        return false;
    }
    m_global = values;
    advanceRevision();
    return true;
}

bool Registry::confirmPlayback(const QUuid &instanceId,
    EffectivePlayback effective,
    const QVector<WallpaperPlaybackPolicy::Reason> &pauseReasons)
{
    Entry *entry = find(instanceId);
    if (!entry || (entry->snapshot.effectivePlayback == effective
                      && entry->snapshot.pauseReasons == pauseReasons)) {
        return false;
    }
    entry->snapshot.effectivePlayback = effective;
    entry->snapshot.pauseReasons = pauseReasons;
    advanceRevision();
    return true;
}

RuntimeSnapshot Registry::snapshot() const
{
    RuntimeSnapshot result;
    result.epoch = m_epoch;
    result.revision = m_revision;
    result.global = m_global;
    result.targets.reserve(m_entries.size());
    for (const Entry &entry : m_entries) {
        if (entry.endpoint) {
            result.targets.append(entry.snapshot);
        }
    }
    return result;
}

ResolveResult Registry::resolveTargets(const QUuid &requestEpoch,
    const TargetSelection &selection) const
{
    if (requestEpoch != m_epoch) {
        return {ResolveError::StaleEpoch, {}};
    }

    QVector<QUuid> resolved;
    if (selection.allWallpaperTargets) {
        if (!selection.targetIds.isEmpty()) {
            return {ResolveError::InvalidSelection, {}};
        }
        for (const Entry &entry : m_entries) {
            if (entry.endpoint) {
                resolved.append(entry.snapshot.instanceId);
            }
        }
        return {ResolveError::None, resolved};
    }

    if (selection.targetIds.isEmpty()) {
        return {ResolveError::InvalidSelection, {}};
    }

    QSet<QUuid> seen;
    for (const QUuid &instanceId : selection.targetIds) {
        if (instanceId.isNull() || seen.contains(instanceId)) {
            return {ResolveError::InvalidSelection, {}};
        }
        seen.insert(instanceId);
        const Entry *entry = find(instanceId);
        if (!entry || !entry->endpoint) {
            return {ResolveError::UnknownTarget, {}};
        }
        resolved.append(instanceId);
    }
    return {ResolveError::None, resolved};
}

DispatchResult Registry::setPlayback(const QUuid &requestEpoch,
    const TargetSelection &selection,
    WallpaperPlaybackPolicy::Override desired)
{
    if (m_dispatchingPlayback) {
        return {ResolveError::ReentrantDispatch, {}};
    }
    const ResolveResult resolved = resolveTargets(requestEpoch, selection);
    if (resolved.error != ResolveError::None) {
        return {resolved.error, {}};
    }

    QScopedValueRollback<bool> dispatchGuard(m_dispatchingPlayback, true);

    QVector<QPointer<PlaybackEndpoint>> endpoints;
    endpoints.reserve(resolved.targetIds.size());
    bool desiredStateChanged = false;
    for (const QUuid &instanceId : resolved.targetIds) {
        Entry *entry = find(instanceId);
        if (!entry || !entry->endpoint) {
            endpoints.append(nullptr);
            continue;
        }
        endpoints.append(entry->endpoint);
        if (entry->snapshot.desiredPlayback != desired) {
            entry->snapshot.desiredPlayback = desired;
            desiredStateChanged = true;
        }
    }
    if (desiredStateChanged) {
        advanceRevision();
    }

    DispatchResult result;
    result.targets.reserve(resolved.targetIds.size());
    for (qsizetype i = 0; i < resolved.targetIds.size(); ++i) {
        const QUuid instanceId = resolved.targetIds.at(i);
        QPointer<PlaybackEndpoint> endpoint = endpoints.at(i);
        Entry *entry = find(instanceId);
        if (!endpoint || !entry || entry->endpoint != endpoint) {
            result.targets.append({instanceId, false});
            continue;
        }
        endpoint->requestPlayback(desired);
        result.targets.append({instanceId, true});
    }
    return result;
}

ResolveResult Registry::setFollow(const QUuid &requestEpoch, const TargetSelection &selection,
    const QString &requestId, const QVariantMap &args)
{
    const ResolveResult resolved = resolveTargets(requestEpoch, selection);
    if (resolved.error != ResolveError::None || resolved.targetIds.isEmpty()) return resolved;
    QVector<QPointer<PlaybackEndpoint>> endpoints;
    for (const QUuid &id : resolved.targetIds) {
        const Entry *entry = find(id);
        if (!entry || !entry->endpoint || !entry->snapshot.capabilities.contains(QStringLiteral("SetFollow"))
            || !entry->endpoint->supportsFollow()) return {ResolveError::UnknownTarget, {}};
        endpoints.append(entry->endpoint);
    }
    for (qsizetype i = 0; i < resolved.targetIds.size(); ++i) {
        const QUuid id = resolved.targetIds.at(i);
        QPointer<PlaybackEndpoint> endpoint = endpoints.at(i);
        const Entry *entry = find(id);
        if (endpoint && entry && entry->endpoint == endpoint) endpoint->requestFollow(requestId, args);
    }
    return resolved;
}

ResolveResult Registry::reloadWallpaper(const QUuid &requestEpoch,
    const TargetSelection &selection, const QString &requestId)
{
    const ResolveResult resolved = resolveTargets(requestEpoch, selection);
    if (resolved.error != ResolveError::None || resolved.targetIds.isEmpty()) return resolved;
    QVector<QPointer<PlaybackEndpoint>> endpoints;
    for (const QUuid &id : resolved.targetIds) {
        const Entry *entry = find(id);
        if (!entry || !entry->endpoint
            || !entry->snapshot.capabilities.contains(QStringLiteral("ReloadWallpaper"))
            || !entry->endpoint->supportsReload()) return {ResolveError::UnknownTarget, {}};
        endpoints.append(entry->endpoint);
    }
    for (qsizetype i = 0; i < resolved.targetIds.size(); ++i) {
        const QUuid id = resolved.targetIds.at(i);
        QPointer<PlaybackEndpoint> endpoint = endpoints.at(i);
        const Entry *entry = find(id);
        if (endpoint && entry && entry->endpoint == endpoint) endpoint->requestReload(requestId);
    }
    return resolved;
}

ResolveResult Registry::openConfiguration(const QUuid &requestEpoch,
    const TargetSelection &selection, const QString &requestId)
{
    const ResolveResult resolved = resolveTargets(requestEpoch, selection);
    if (resolved.error != ResolveError::None || resolved.targetIds.size() != 1)
        return {resolved.error == ResolveError::None ? ResolveError::InvalidSelection : resolved.error, {}};
    const QUuid id = resolved.targetIds.first();
    Entry *entry = find(id);
    if (!entry || !entry->endpoint
        || !entry->snapshot.capabilities.contains(QStringLiteral("OpenConfiguration"))
        || !entry->endpoint->supportsOpenConfiguration()) return {ResolveError::UnknownTarget, {}};
    QPointer<PlaybackEndpoint> endpoint = entry->endpoint;
    if (!endpoint || find(id) == nullptr || find(id)->endpoint != endpoint)
        return {ResolveError::UnknownTarget, {}};
    endpoint->requestOpenConfiguration(requestId);
    return resolved;
}

ResolveResult Registry::nextWallpaper(const QUuid &requestEpoch,
    const TargetSelection &selection, const QString &requestId)
{
    const ResolveResult resolved = resolveTargets(requestEpoch, selection);
    if (resolved.error != ResolveError::None || resolved.targetIds.isEmpty()) return resolved;
    QVector<QPointer<PlaybackEndpoint>> endpoints;
    for (const QUuid& id : resolved.targetIds) {
        const Entry *entry = find(id);
        if (!entry || !entry->endpoint
            || !entry->snapshot.capabilities.contains(QStringLiteral("NextWallpaper"))
            || !entry->endpoint->supportsNextWallpaper()) return {ResolveError::UnknownTarget, {}};
        endpoints.append(entry->endpoint);
    }
    for (qsizetype i = 0; i < resolved.targetIds.size(); ++i) {
        const QUuid id = resolved.targetIds.at(i);
        QPointer<PlaybackEndpoint> endpoint = endpoints.at(i);
        const Entry *entry = find(id);
        if (endpoint && entry && entry->endpoint == endpoint)
            endpoint->requestNextWallpaper(requestId);
    }
    return resolved;
}

void Registry::removeTarget(const QUuid &instanceId)
{
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).snapshot.instanceId == instanceId) {
            m_entries.removeAt(i);
            advanceRevision();
            return;
        }
    }
}

Registry::Entry *Registry::find(const QUuid &instanceId)
{
    for (Entry &entry : m_entries) {
        if (entry.snapshot.instanceId == instanceId) {
            return &entry;
        }
    }
    return nullptr;
}

const Registry::Entry *Registry::find(const QUuid &instanceId) const
{
    for (const Entry &entry : m_entries) {
        if (entry.snapshot.instanceId == instanceId) {
            return &entry;
        }
    }
    return nullptr;
}

void Registry::advanceRevision()
{
    ++m_revision;
    emit snapshotChanged(m_revision);
}

namespace {

QString desiredName(WallpaperPlaybackPolicy::Override desired)
{
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

QString effectiveName(EffectivePlayback effective)
{
    switch (effective) {
    case EffectivePlayback::Unknown:
        return QStringLiteral("unknown");
    case EffectivePlayback::Loading:
        return QStringLiteral("loading");
    case EffectivePlayback::Playing:
        return QStringLiteral("playing");
    case EffectivePlayback::Paused:
        return QStringLiteral("paused");
    case EffectivePlayback::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

QString reasonName(WallpaperPlaybackPolicy::Reason reason)
{
    switch (reason) {
    case WallpaperPlaybackPolicy::Reason::Sleep:
        return QStringLiteral("sleep");
    case WallpaperPlaybackPolicy::Reason::InactiveSession:
        return QStringLiteral("inactiveSession");
    case WallpaperPlaybackPolicy::Reason::Manual:
        return QStringLiteral("manual");
    case WallpaperPlaybackPolicy::Reason::Battery:
        return QStringLiteral("battery");
    case WallpaperPlaybackPolicy::Reason::GameMode:
        return QStringLiteral("gameMode");
    case WallpaperPlaybackPolicy::Reason::WindowCoverage:
        return QStringLiteral("window");
    case WallpaperPlaybackPolicy::Reason::FullscreenApp:
        return QStringLiteral("fullscreen");
    }
    return {};
}

QString uuidString(const QUuid &uuid)
{
    return uuid.toString(QUuid::WithoutBraces);
}

QJsonObject targetObject(const TargetSnapshot &target)
{
    QJsonArray reasons;
    for (const auto reason : target.pauseReasons) {
        reasons.append(reasonName(reason));
    }
    QJsonArray capabilities;
    for (const QString &capability : target.capabilities) {
        capabilities.append(capability);
    }
    QJsonObject wallpaper{{QStringLiteral("source"), target.wallpaperSource},
        {QStringLiteral("title"), target.title},
        {QStringLiteral("workshopId"), target.workshopId},
        {QStringLiteral("backend"), target.backend}};
    QJsonObject follow{{QStringLiteral("enabled"), target.followEnabled},
        {QStringLiteral("mode"), target.followMode},
        {QStringLiteral("accountKey"), target.followAccountKey},
        {QStringLiteral("sourceKey"), target.followSourceKey},
        {QStringLiteral("choices"), QJsonArray::fromVariantList(target.followChoices)},
        {QStringLiteral("status"), target.followStatus},
        {QStringLiteral("provenance"), target.followProvenance}};
    return {{QStringLiteral("instanceId"), uuidString(target.instanceId)},
        {QStringLiteral("containmentId"), target.containmentId},
        {QStringLiteral("activityId"), target.activityId},
        {QStringLiteral("screenName"), target.screenName},
        {QStringLiteral("screenLabel"), target.screenLabel},
        {QStringLiteral("desiredPlayback"), desiredName(target.desiredPlayback)},
        {QStringLiteral("effectivePlayback"), effectiveName(target.effectivePlayback)},
        {QStringLiteral("pauseReasons"), reasons},
        {QStringLiteral("wallpaper"), wallpaper},
        {QStringLiteral("follow"), follow},
        {QStringLiteral("capabilities"), capabilities}};
}

QJsonObject snapshotObject(const RuntimeSnapshot &snapshot)
{
    QJsonArray targets;
    for (const TargetSnapshot &target : snapshot.targets) {
        targets.append(targetObject(target));
    }
    return {{QStringLiteral("schemaVersion"), snapshot.schemaVersion},
        {QStringLiteral("epoch"), uuidString(snapshot.epoch)},
        {QStringLiteral("revision"), double(snapshot.revision)},
        {QStringLiteral("targets"), targets},
        {QStringLiteral("global"), QJsonObject::fromVariantMap(snapshot.global)}};
}

QString compactJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool validUuid(const QString &text, QUuid *out = nullptr)
{
    const QUuid parsed = QUuid::fromString(text);
    if (parsed.isNull() || parsed.toString(QUuid::WithoutBraces).compare(text, Qt::CaseInsensitive)) {
        return false;
    }
    if (out) {
        *out = parsed;
    }
    return true;
}

QString resolveErrorName(ResolveError error)
{
    switch (error) {
    case ResolveError::StaleEpoch:
        return QStringLiteral("staleEpoch");
    case ResolveError::InvalidSelection:
        return QStringLiteral("invalidTargetSelection");
    case ResolveError::UnknownTarget:
        return QStringLiteral("unknownTarget");
    case ResolveError::ReentrantDispatch:
        return QStringLiteral("busy");
    case ResolveError::None:
        return {};
    }
    return QStringLiteral("invalidRequest");
}

} // namespace

RuntimeService::RuntimeService(Registry &registry,
    const QDBusConnection &connection,
    QObject *parent)
    : RuntimeService(registry, nullptr, connection, parent)
{
}

RuntimeService::RuntimeService(Registry &registry,
    GlobalPatchBackend *globalPatchBackend,
    const QDBusConnection &connection,
    QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_connection(connection)
    , m_globalPatchBackend(globalPatchBackend)
{
    connect(&m_registry, &Registry::snapshotChanged, this, &RuntimeService::registryChanged);
    connect(&m_registry, &Registry::followFinished,
        this, &RuntimeService::followTargetFinished);
    connect(&m_registry, &Registry::reloadFinished,
        this, &RuntimeService::reloadTargetFinished);
    connect(&m_registry, &Registry::configurationFinished,
        this, &RuntimeService::configurationTargetFinished);
    connect(&m_registry, &Registry::nextWallpaperFinished,
        this, &RuntimeService::nextWallpaperTargetFinished);
    if (m_globalPatchBackend) {
        connect(m_globalPatchBackend, &GlobalPatchBackend::patchFinished,
            this, &RuntimeService::globalPatchFinished);
        connect(m_globalPatchBackend, &GlobalPatchBackend::valuesChanged,
            this, &RuntimeService::refreshGlobalSnapshot);
        connect(m_globalPatchBackend, &QObject::destroyed, this,
            &RuntimeService::refreshGlobalSnapshot);
    }
    refreshGlobalSnapshot();
    if (!m_registry.snapshot().targets.isEmpty()) {
        acquireService();
    }
}

RuntimeService::~RuntimeService()
{
    releaseService();
}

bool RuntimeService::isRegistered() const
{
    return m_registered;
}

bool RuntimeService::acquireService()
{
    if (m_registered) {
        return true;
    }
    if (m_registry.snapshot().targets.isEmpty()) {
        return false;
    }

    QScopedValueRollback<bool> transition(m_transitioning, true);
    m_registry.renewEpoch();
    if (!m_connection.registerObject(ObjectPath,
            this,
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
        return false;
    }
    if (!m_connection.registerService(ServiceName)) {
        m_connection.unregisterObject(ObjectPath);
        return false;
    }
    m_registered = true;
    return true;
}

void RuntimeService::releaseService()
{
    if (!m_registered) {
        return;
    }
    QScopedValueRollback<bool> transition(m_transitioning, true);
    m_connection.unregisterObject(ObjectPath);
    m_connection.unregisterService(ServiceName);
    m_registered = false;
    m_pendingCommands.clear();
    m_pendingFollows.clear();
    m_pendingReloads.clear();
    m_pendingConfigurations.clear();
    m_pendingNextWallpapers.clear();
}

void RuntimeService::registryChanged()
{
    if (m_transitioning) {
        return;
    }
    if (m_registry.snapshot().targets.isEmpty()) {
        releaseService();
        return;
    }
    if (!m_registered && !acquireService()) {
        return;
    }
    emit StateChanged(snapshotJson());
    QSet<QUuid> active;
    for (const TargetSnapshot &target : m_registry.snapshot().targets) active.insert(target.instanceId);
    QStringList completedRequests;
    for (auto it = m_pendingReloads.begin(); it != m_pendingReloads.end(); ++it) {
        for (const QUuid &target : it->targets) {
            bool alreadyReported = false;
            for (const QJsonValue &entry : it->results)
                if (entry.toObject().value(QStringLiteral("instanceId")).toString() == uuidString(target))
                    alreadyReported = true;
            if (!alreadyReported && !active.contains(target)) {
                it->results.append(QJsonObject{{QStringLiteral("instanceId"), uuidString(target)},
                    {QStringLiteral("status"), QStringLiteral("rejected")},
                    {QStringLiteral("errorCode"), QStringLiteral("targetRemoved")},
                    {QStringLiteral("message"), QStringLiteral("Target disappeared during reload")}});
                it->anyRejected = true;
                --it->remaining;
            }
        }
        if (it->remaining == 0) completedRequests.append(it.key());
    }
    for (const QString &requestId : completedRequests) {
        const PendingReload pending = m_pendingReloads.take(requestId);
        const QString status = pending.anyApplied
            ? QStringLiteral("partial") : QStringLiteral("rejected");
        completeCommand(requestId, status, QStringLiteral("targetRemoved"),
            QStringLiteral("One or more wallpaper targets disappeared during reload"), pending.results);
    }
    QStringList completedFollows;
    for (auto it = m_pendingFollows.begin(); it != m_pendingFollows.end(); ++it) {
        for (const QUuid &target : it->targets) {
            bool alreadyReported = false;
            for (const QJsonValue &entry : it->results)
                if (entry.toObject().value(QStringLiteral("instanceId")).toString() == uuidString(target))
                    alreadyReported = true;
            if (!alreadyReported && !active.contains(target)) {
                it->results.append(QJsonObject{{QStringLiteral("instanceId"), uuidString(target)},
                    {QStringLiteral("status"), QStringLiteral("rejected")},
                    {QStringLiteral("errorCode"), QStringLiteral("targetRemoved")},
                    {QStringLiteral("message"), QStringLiteral("Wallpaper target disappeared during Follow update")}});
                it->anyRejected = true;
                --it->remaining;
            }
        }
        if (it->remaining == 0) completedFollows.append(it.key());
    }
    for (const QString &requestId : completedFollows) {
        const PendingFollow pending = m_pendingFollows.take(requestId);
        const QString status = pending.anyApplied
            ? QStringLiteral("partial") : QStringLiteral("rejected");
        completeCommand(requestId, status, QStringLiteral("targetRemoved"),
            QStringLiteral("One or more wallpaper targets disappeared during Follow update"), pending.results);
    }
    QStringList removedConfigurations;
    for (auto it = m_pendingConfigurations.cbegin(); it != m_pendingConfigurations.cend(); ++it) {
        if (it.value().size() != 1 || !active.contains(it.value().first()))
            removedConfigurations.append(it.key());
    }
    for (const QString& requestId : removedConfigurations) {
        m_pendingConfigurations.remove(requestId);
        completeCommand(requestId, QStringLiteral("rejected"), QStringLiteral("targetRemoved"),
            QStringLiteral("Wallpaper target disappeared before Plasma confirmed configuration"));
    }
    QStringList completedNextRequests;
    for (auto it = m_pendingNextWallpapers.begin(); it != m_pendingNextWallpapers.end(); ++it) {
        for (const QUuid& target : it->targets) {
            bool alreadyReported = false;
            for (const QJsonValue& item : it->results)
                if (item.toObject().value(QStringLiteral("instanceId")).toString() == uuidString(target))
                    alreadyReported = true;
            if (!alreadyReported && !active.contains(target)) {
                it->results.append(QJsonObject{{QStringLiteral("instanceId"), uuidString(target)},
                    {QStringLiteral("status"), QStringLiteral("rejected")},
                    {QStringLiteral("errorCode"), QStringLiteral("targetRemoved")},
                    {QStringLiteral("message"), QStringLiteral("Wallpaper target disappeared during NextWallpaper")}});
                it->anyRejected = true;
                --it->remaining;
            }
        }
        if (it->remaining == 0) completedNextRequests.append(it.key());
    }
    for (const QString& requestId : completedNextRequests) {
        const PendingNext pending = m_pendingNextWallpapers.take(requestId);
        completeCommand(requestId, pending.anyApplied ? QStringLiteral("partial") : QStringLiteral("rejected"),
            QStringLiteral("targetRemoved"),
            QStringLiteral("One or more wallpaper targets disappeared during NextWallpaper"), pending.results);
    }
}

QString RuntimeService::snapshotJson() const
{
    const QString json = compactJson(snapshotObject(m_registry.snapshot()));
    if (json.toUtf8().size() <= MaximumMessageBytes) {
        return json;
    }
    return compactJson({{QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("errorCode"), QStringLiteral("snapshotTooLarge")},
        {QStringLiteral("message"), QStringLiteral("Runtime snapshot exceeds 1 MiB")}});
}

QString RuntimeService::GetSnapshot()
{
    return snapshotJson();
}

QString RuntimeService::rejection(const QString &requestId,
    const QString &errorCode,
    const QString &message) const
{
    return compactJson({{QStringLiteral("requestId"), requestId},
        {QStringLiteral("status"), QStringLiteral("rejected")},
        {QStringLiteral("errorCode"), errorCode},
        {QStringLiteral("message"), message}});
}

QString RuntimeService::Execute(const QString &requestJson)
{
    if (requestJson.toUtf8().size() > MaximumMessageBytes) {
        return rejection({}, QStringLiteral("messageTooLarge"), QStringLiteral("Request exceeds 1 MiB"));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(requestJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return rejection({}, QStringLiteral("invalidJson"), QStringLiteral("Request must be a JSON object"));
    }
    const QJsonObject request = document.object();
    const QSet<QString> allowedKeys{QStringLiteral("schemaVersion"),
        QStringLiteral("epoch"),
        QStringLiteral("requestId"),
        QStringLiteral("scope"),
        QStringLiteral("targetIds"),
        QStringLiteral("command"),
        QStringLiteral("args")};
    for (auto it = request.constBegin(); it != request.constEnd(); ++it) {
        if (!allowedKeys.contains(it.key())) {
            return rejection({}, QStringLiteral("invalidRequest"), QStringLiteral("Unknown request field"));
        }
    }
    if (request.size() != allowedKeys.size()
        || !request.value(QStringLiteral("schemaVersion")).isDouble()
        || request.value(QStringLiteral("schemaVersion")).toDouble(-1) != 1.0
        || !request.value(QStringLiteral("epoch")).isString()
        || !request.value(QStringLiteral("requestId")).isString()
        || !request.value(QStringLiteral("scope")).isString()
        || !request.value(QStringLiteral("targetIds")).isArray()
        || !request.value(QStringLiteral("command")).isString()
        || !request.value(QStringLiteral("args")).isObject()) {
        return rejection({}, QStringLiteral("invalidRequest"), QStringLiteral("Missing field or invalid field type"));
    }

    const QString requestId = request.value(QStringLiteral("requestId")).toString();
    if (requestId.isEmpty() || requestId.size() > 128) {
        return rejection({}, QStringLiteral("invalidRequest"), QStringLiteral("Invalid requestId"));
    }
    if (m_pendingCommands.contains(requestId)) {
        return rejection(requestId, QStringLiteral("duplicateRequestId"), QStringLiteral("Request is already pending"));
    }
    QUuid requestEpoch;
    if (!validUuid(request.value(QStringLiteral("epoch")).toString(), &requestEpoch)) {
        return rejection(requestId, QStringLiteral("invalidRequest"), QStringLiteral("Invalid service epoch"));
    }

    const QString command = request.value(QStringLiteral("command")).toString();
    const bool globalAudio = command == QStringLiteral("SetGlobalAudioMuted");
    const bool globalSizing = command == QStringLiteral("SetGlobalSizing");
    const bool followCommand = command == QStringLiteral("SetFollow");
    const bool reloadCommand = command == QStringLiteral("ReloadWallpaper");
    const bool configurationCommand = command == QStringLiteral("OpenConfiguration");
    const bool nextWallpaperCommand = command == QStringLiteral("NextWallpaper");
    if (command != QStringLiteral("SetPlayback") && !globalAudio && !globalSizing
        && !followCommand && !reloadCommand && !configurationCommand && !nextWallpaperCommand) {
        return rejection(requestId, QStringLiteral("unsupportedCommand"), QStringLiteral("Command is not supported"));
    }

    const QString scope = request.value(QStringLiteral("scope")).toString();
    TargetSelection selection;
    if (scope == QStringLiteral("targets")) {
        selection.allWallpaperTargets = false;
    } else if (scope == QStringLiteral("allWallpaperTargets")) {
        selection.allWallpaperTargets = true;
    } else {
        return rejection(requestId, QStringLiteral("invalidRequest"), QStringLiteral("Invalid command scope"));
    }
    const QJsonArray targetArray = request.value(QStringLiteral("targetIds")).toArray();
    for (const QJsonValue &value : targetArray) {
        QUuid targetId;
        if (!value.isString() || !validUuid(value.toString(), &targetId)) {
            return rejection(requestId, QStringLiteral("invalidRequest"), QStringLiteral("Invalid target ID"));
        }
        selection.targetIds.append(targetId);
    }

    const QJsonObject args = request.value(QStringLiteral("args")).toObject();
    if (nextWallpaperCommand) {
        if (scope != QStringLiteral("targets") || selection.allWallpaperTargets
            || selection.targetIds.isEmpty())
            return rejection(requestId, QStringLiteral("invalidScope"),
                QStringLiteral("NextWallpaper requires explicit target IDs"));
        if (!args.isEmpty())
            return rejection(requestId, QStringLiteral("invalidArgument"),
                QStringLiteral("NextWallpaper takes no arguments"));
        const ResolveResult resolution = m_registry.resolveTargets(requestEpoch, selection);
        if (resolution.error != ResolveError::None)
            return rejection(requestId, resolveErrorName(resolution.error),
                QStringLiteral("Target selection rejected"));
        if (resolution.targetIds.size() != selection.targetIds.size())
            return rejection(requestId, QStringLiteral("invalidTarget"),
                QStringLiteral("Target selection was not uniquely resolved"));
        for (const QUuid& id : resolution.targetIds) {
            bool supported = false;
            for (const TargetSnapshot& target : m_registry.snapshot().targets) {
                if (target.instanceId == id) {
                    supported = target.capabilities.contains(QStringLiteral("NextWallpaper"));
                    break;
                }
            }
            if (!supported)
                return rejection(requestId, QStringLiteral("unsupportedCommand"),
                    QStringLiteral("A selected target does not support NextWallpaper"));
        }
        PendingNext pending;
        pending.targets = resolution.targetIds;
        pending.remaining = resolution.targetIds.size();
        m_pendingNextWallpapers.insert(requestId, pending);
        m_pendingCommands.insert(requestId, resolution.targetIds);
        QMetaObject::invokeMethod(this, [this, requestId, requestEpoch, selection] {
            if (!m_pendingNextWallpapers.contains(requestId)) return;
            const ResolveResult dispatched = m_registry.nextWallpaper(requestEpoch, selection, requestId);
            if (dispatched.error != ResolveError::None) {
                completeCommand(requestId, QStringLiteral("rejected"),
                    resolveErrorName(dispatched.error), QStringLiteral("Next wallpaper request could not be dispatched"));
                m_pendingNextWallpapers.remove(requestId);
                return;
            }
            QTimer::singleShot(12000, this, [this, requestId] {
                if (m_pendingNextWallpapers.contains(requestId)) {
                    completeCommand(requestId, QStringLiteral("rejected"),
                        QStringLiteral("selectionUnconfirmed"),
                        QStringLiteral("Wallpaper selection was not confirmed"));
                    m_pendingNextWallpapers.remove(requestId);
                }
            });
        }, Qt::QueuedConnection);
        QJsonArray targetIds;
        for (const QUuid& id : resolution.targetIds)
            targetIds.append(uuidString(id));
        return compactJson({{QStringLiteral("requestId"), requestId},
            {QStringLiteral("status"), QStringLiteral("accepted")},
            {QStringLiteral("errorCode"), QString()},
            {QStringLiteral("message"), QStringLiteral("Next wallpaper selection queued")},
            {QStringLiteral("targetIds"), targetIds}});
    }
    if (configurationCommand) {
        if (scope != QStringLiteral("targets") || selection.allWallpaperTargets
            || selection.targetIds.size() != 1)
            return rejection(requestId, QStringLiteral("invalidScope"),
                QStringLiteral("OpenConfiguration requires exactly one explicit target"));
        if (!args.isEmpty())
            return rejection(requestId, QStringLiteral("invalidArgument"),
                QStringLiteral("OpenConfiguration takes no arguments"));
        const ResolveResult resolution = m_registry.resolveTargets(requestEpoch, selection);
        if (resolution.error != ResolveError::None)
            return rejection(requestId, resolveErrorName(resolution.error),
                QStringLiteral("Target selection rejected"));
        if (resolution.targetIds.size() != 1)
            return rejection(requestId, QStringLiteral("invalidTarget"),
                QStringLiteral("Target selection was not uniquely resolved"));
        bool supported = false;
        for (const TargetSnapshot& target : m_registry.snapshot().targets) {
            if (target.instanceId == resolution.targetIds.first()) {
                supported = target.capabilities.contains(QStringLiteral("OpenConfiguration"));
                break;
            }
        }
        if (!supported)
            return rejection(requestId, QStringLiteral("unsupportedCommand"),
                QStringLiteral("Target does not support OpenConfiguration"));
        m_pendingConfigurations.insert(requestId, resolution.targetIds);
        m_pendingCommands.insert(requestId, resolution.targetIds);
        QMetaObject::invokeMethod(this, [this, requestId, requestEpoch, selection] {
            if (!m_pendingConfigurations.contains(requestId)) return;
            const ResolveResult dispatched = m_registry.openConfiguration(requestEpoch, selection, requestId);
            if (dispatched.error != ResolveError::None) {
                completeCommand(requestId, QStringLiteral("rejected"),
                    resolveErrorName(dispatched.error), QStringLiteral("Configuration request could not be dispatched"));
                m_pendingConfigurations.remove(requestId);
                return;
            }
            QTimer::singleShot(5000, this, [this, requestId] {
                if (m_pendingConfigurations.contains(requestId)) {
                    completeCommand(requestId, QStringLiteral("rejected"),
                        QStringLiteral("configurationUnconfirmed"),
                        QStringLiteral("Plasma did not confirm the configuration action"));
                    m_pendingConfigurations.remove(requestId);
                }
            });
        }, Qt::QueuedConnection);
        return compactJson({{QStringLiteral("requestId"), requestId},
            {QStringLiteral("status"), QStringLiteral("accepted")},
            {QStringLiteral("errorCode"), QString()},
            {QStringLiteral("message"), QStringLiteral("Configuration request queued")},
            {QStringLiteral("targetIds"), QJsonArray{uuidString(resolution.targetIds.first())}}});
    }
    if (reloadCommand) {
        if (scope != QStringLiteral("targets") || selection.allWallpaperTargets
            || selection.targetIds.isEmpty())
            return rejection(requestId, QStringLiteral("invalidScope"), QStringLiteral("ReloadWallpaper requires explicit target IDs"));
        if (!args.isEmpty())
            return rejection(requestId, QStringLiteral("invalidArgument"), QStringLiteral("ReloadWallpaper takes no arguments"));
        const ResolveResult resolution = m_registry.resolveTargets(requestEpoch, selection);
        if (resolution.error != ResolveError::None)
            return rejection(requestId, resolveErrorName(resolution.error), QStringLiteral("Target selection rejected"));
        if (resolution.targetIds.size() != selection.targetIds.size())
            return rejection(requestId, QStringLiteral("invalidTarget"), QStringLiteral("Target selection was not uniquely resolved"));
        PendingReload pending;
        pending.targets = resolution.targetIds;
        pending.remaining = resolution.targetIds.size();
        m_pendingReloads.insert(requestId, pending);
        m_pendingCommands.insert(requestId, resolution.targetIds);
        QMetaObject::invokeMethod(this, [this, requestId, requestEpoch, selection] {
            if (!m_pendingReloads.contains(requestId)) return;
            const ResolveResult dispatched = m_registry.reloadWallpaper(requestEpoch, selection, requestId);
            if (dispatched.error != ResolveError::None) {
                completeCommand(requestId, QStringLiteral("rejected"),
                    resolveErrorName(dispatched.error), QStringLiteral("Reload request could not be dispatched"));
                m_pendingReloads.remove(requestId);
                return;
            }
            QTimer::singleShot(7000, this, [this, requestId] {
                if (m_pendingReloads.contains(requestId)) {
                    completeCommand(requestId, QStringLiteral("rejected"),
                        QStringLiteral("reloadUnconfirmed"),
                        QStringLiteral("Reload was requested but the backend did not confirm a first frame"));
                    m_pendingReloads.remove(requestId);
                }
            });
        }, Qt::QueuedConnection);
        QJsonArray targetIds;
        for (const QUuid &target : resolution.targetIds) targetIds.append(uuidString(target));
        return compactJson({{QStringLiteral("requestId"), requestId},
            {QStringLiteral("status"), QStringLiteral("accepted")},
            {QStringLiteral("errorCode"), QString()},
            {QStringLiteral("message"), QStringLiteral("Reload request queued")},
            {QStringLiteral("targetIds"), targetIds}});
    }
    if (followCommand) {
        if (scope != QStringLiteral("targets") || selection.allWallpaperTargets
            || selection.targetIds.isEmpty())
            return rejection(requestId, QStringLiteral("invalidScope"), QStringLiteral("SetFollow requires explicit target IDs"));
        if (args.size() != 4 || !args.contains(QStringLiteral("enabled"))
            || !args.value(QStringLiteral("enabled")).isBool()
            || !args.contains(QStringLiteral("mode")) || !args.value(QStringLiteral("mode")).isString()
            || !args.contains(QStringLiteral("accountKey")) || !args.value(QStringLiteral("accountKey")).isString()
            || !args.contains(QStringLiteral("sourceKey")) || !args.value(QStringLiteral("sourceKey")).isString())
            return rejection(requestId, QStringLiteral("invalidArgument"), QStringLiteral("SetFollow requires enabled, mode, accountKey and sourceKey"));
        const QString mode = args.value(QStringLiteral("mode")).toString();
        if (mode != QStringLiteral("identifiedCurrent") && mode != QStringLiteral("account")
            && mode != QStringLiteral("legacySession") && mode != QStringLiteral("manualMapping"))
            return rejection(requestId, QStringLiteral("invalidArgument"), QStringLiteral("Unsupported follow source mode"));
        if (args.value(QStringLiteral("enabled")).toBool()
            && args.value(QStringLiteral("sourceKey")).toString().trimmed().isEmpty())
            return rejection(requestId, QStringLiteral("invalidArgument"), QStringLiteral("Enabled Follow requires a source key"));
        const ResolveResult resolution = m_registry.resolveTargets(requestEpoch, selection);
        if (resolution.error != ResolveError::None)
            return rejection(requestId, resolveErrorName(resolution.error), QStringLiteral("Target selection rejected"));
        if (resolution.targetIds.size() != selection.targetIds.size())
            return rejection(requestId, QStringLiteral("invalidTarget"), QStringLiteral("Target selection was not uniquely resolved"));
        PendingFollow pending;
        pending.targets = resolution.targetIds;
        pending.remaining = resolution.targetIds.size();
        m_pendingFollows.insert(requestId, pending);
        m_pendingCommands.insert(requestId, resolution.targetIds);
        QMetaObject::invokeMethod(this, [this, requestId, requestEpoch, selection, args] {
            if (!m_pendingFollows.contains(requestId)) return;
            const ResolveResult dispatched = m_registry.setFollow(requestEpoch, selection, requestId, args.toVariantMap());
            if (dispatched.error != ResolveError::None) {
                completeCommand(requestId, QStringLiteral("rejected"),
                    resolveErrorName(dispatched.error), QStringLiteral("Follow request could not be dispatched"));
                m_pendingFollows.remove(requestId);
                return;
            }
            QTimer::singleShot(5000, this, [this, requestId] {
                auto pending = m_pendingFollows.find(requestId);
                if (pending == m_pendingFollows.end()) return;
                for (const QUuid &target : pending->targets) {
                    bool alreadyReported = false;
                    for (const QJsonValue &entry : pending->results)
                        if (entry.toObject().value(QStringLiteral("instanceId")).toString() == uuidString(target))
                            alreadyReported = true;
                    if (!alreadyReported) {
                        pending->results.append(QJsonObject{{QStringLiteral("instanceId"), uuidString(target)},
                            {QStringLiteral("status"), QStringLiteral("rejected")},
                            {QStringLiteral("errorCode"), QStringLiteral("confirmationTimeout")},
                            {QStringLiteral("message"), QStringLiteral("Wallpaper did not confirm the Follow configuration before timeout")}});
                        pending->anyRejected = true;
                        --pending->remaining;
                    }
                }
                const QString status = pending->anyApplied
                    ? QStringLiteral("partial") : QStringLiteral("rejected");
                const QJsonArray results = pending->results;
                m_pendingFollows.erase(pending);
                completeCommand(requestId, status, QStringLiteral("confirmationTimeout"),
                    QStringLiteral("One or more wallpaper targets did not confirm the Follow configuration"), results);
            });
        }, Qt::QueuedConnection);
        QJsonArray targetIds;
        for (const QUuid &target : resolution.targetIds) targetIds.append(uuidString(target));
        return compactJson({{QStringLiteral("requestId"), requestId},
            {QStringLiteral("status"), QStringLiteral("accepted")},
            {QStringLiteral("errorCode"), QString()},
            {QStringLiteral("message"), QStringLiteral("Follow assignment queued")},
            {QStringLiteral("targetIds"), targetIds}});
    }
    if (globalAudio || globalSizing) {
        if (requestEpoch != m_registry.epoch()) {
            return rejection(requestId, QStringLiteral("staleEpoch"), QStringLiteral("Service epoch is stale"));
        }
        if (scope != QStringLiteral("allWallpaperTargets") || !selection.targetIds.isEmpty()) {
            return rejection(requestId, QStringLiteral("invalidScope"), QStringLiteral("Global commands require allWallpaperTargets scope and no target IDs"));
        }
        if (!m_globalPatchBackend) {
            return rejection(requestId, QStringLiteral("serviceUnavailable"), QStringLiteral("Global settings backend is unavailable"));
        }
        QVariantMap patch;
        QStringList keys;
        if (globalAudio) {
            if (args.size() != 1 || !args.contains(QStringLiteral("muted"))
                || !args.value(QStringLiteral("muted")).isBool()) {
                return rejection(requestId, QStringLiteral("invalidArgument"), QStringLiteral("SetGlobalAudioMuted requires a muted boolean"));
            }
            keys = {QStringLiteral("AudioDisabled")};
            patch.insert(keys.first(), args.value(QStringLiteral("muted")).toBool());
        } else {
            if (args.size() != 2 || !args.contains(QStringLiteral("enabled"))
                || !args.value(QStringLiteral("enabled")).isBool()
                || !args.contains(QStringLiteral("mode"))
                || !args.value(QStringLiteral("mode")).isString()) {
                return rejection(requestId, QStringLiteral("invalidArgument"), QStringLiteral("SetGlobalSizing requires enabled boolean and mode string"));
            }
            const QString sizingMode = args.value(QStringLiteral("mode")).toString();
            if (sizingMode != QStringLiteral("crop") && sizingMode != QStringLiteral("stretch")) {
                return rejection(requestId, QStringLiteral("invalidArgument"), QStringLiteral("Unsupported sizing mode"));
            }
            keys = {QStringLiteral("SizingEnabled"), QStringLiteral("SizingMode")};
            patch.insert(keys.at(0), args.value(QStringLiteral("enabled")).toBool());
            patch.insert(keys.at(1), sizingMode == QStringLiteral("crop") ? 1 : 2);
        }
        const QVariantMap baseline = m_globalPatchBackend->currentValues(keys);
        QPointer<GlobalPatchBackend> backend = m_globalPatchBackend;
        m_pendingCommands.insert(requestId, {});
        QMetaObject::invokeMethod(this, [this, backend, requestId, patch, baseline] {
            if (!m_pendingCommands.contains(requestId)) {
                return;
            }
            if (!backend) {
                completeCommand(requestId, QStringLiteral("rejected"),
                    QStringLiteral("serviceUnavailable"),
                    QStringLiteral("Global settings backend was destroyed before dispatch"));
                return;
            }
            backend->submitPatch(requestId, patch, baseline);
        }, Qt::QueuedConnection);
        return compactJson({{QStringLiteral("requestId"), requestId},
            {QStringLiteral("status"), QStringLiteral("accepted")},
            {QStringLiteral("errorCode"), QString()},
            {QStringLiteral("message"), QStringLiteral("Global settings patch queued")}});
    }

    if (args.size() != 1 || !args.contains(QStringLiteral("mode"))
        || !args.value(QStringLiteral("mode")).isString()) {
        return rejection(requestId, QStringLiteral("invalidRequest"), QStringLiteral("SetPlayback requires a mode string"));
    }
    const QString mode = args.value(QStringLiteral("mode")).toString();
    WallpaperPlaybackPolicy::Override desired;
    if (mode == QStringLiteral("automatic")) {
        desired = WallpaperPlaybackPolicy::Override::Automatic;
    } else if (mode == QStringLiteral("paused")) {
        desired = WallpaperPlaybackPolicy::Override::Paused;
    } else if (mode == QStringLiteral("playDespiteWindows")) {
        desired = WallpaperPlaybackPolicy::Override::PlayDespiteWindows;
    } else {
        return rejection(requestId, QStringLiteral("invalidArgument"), QStringLiteral("Unsupported playback mode"));
    }

    const ResolveResult resolution = m_registry.resolveTargets(requestEpoch, selection);
    if (resolution.error != ResolveError::None) {
        return rejection(requestId, resolveErrorName(resolution.error), QStringLiteral("Target selection rejected"));
    }
    if (resolution.targetIds.isEmpty()) {
        return rejection(requestId, QStringLiteral("noTargets"), QStringLiteral("No wallpaper targets are active"));
    }

    m_pendingCommands.insert(requestId, resolution.targetIds);
    const DispatchResult dispatch = m_registry.setPlayback(requestEpoch, selection, desired);
    if (dispatch.error != ResolveError::None) {
        m_pendingCommands.remove(requestId);
        return rejection(requestId, resolveErrorName(dispatch.error), QStringLiteral("Playback request rejected"));
    }

    QJsonArray targetResults;
    int acceptedTargets = 0;
    for (const DispatchTargetResult &target : dispatch.targets) {
        const bool accepted = target.dispatched;
        acceptedTargets += accepted ? 1 : 0;
        targetResults.append(QJsonObject{{QStringLiteral("instanceId"), uuidString(target.instanceId)},
            {QStringLiteral("status"), accepted ? QStringLiteral("applied") : QStringLiteral("rejected")},
            {QStringLiteral("errorCode"), accepted ? QString() : QStringLiteral("targetRemoved")},
            {QStringLiteral("message"), accepted
                ? QStringLiteral("Playback intent accepted by target; effective playback remains unknown until reported")
                : QStringLiteral("Wallpaper target disappeared before accepting playback intent")}});
    }
    const QString completionStatus = acceptedTargets == dispatch.targets.size()
        ? QStringLiteral("applied")
        : (acceptedTargets == 0 ? QStringLiteral("rejected") : QStringLiteral("partial"));
    const QString completionError = completionStatus == QStringLiteral("rejected")
        ? QStringLiteral("targetRemoved") : QString();
    const QString completionMessage = completionStatus == QStringLiteral("applied")
        ? QStringLiteral("Playback intent accepted by all targets; effective playback remains unknown until reported")
        : (completionStatus == QStringLiteral("partial")
            ? QStringLiteral("Playback intent accepted by some targets; effective playback remains unknown until reported")
            : QStringLiteral("No wallpaper target accepted playback intent"));
    // Complete after Execute has returned its accepted acknowledgement. This is
    // a session-intent completion, not a backend playback-state confirmation.
    QMetaObject::invokeMethod(this, [this, requestId, completionStatus,
        completionError, completionMessage, targetResults] {
        completeCommand(requestId, completionStatus, completionError,
            completionMessage, targetResults);
    }, Qt::QueuedConnection);

    QJsonArray targets;
    for (const QUuid &targetId : resolution.targetIds) {
        targets.append(uuidString(targetId));
    }
    return compactJson({{QStringLiteral("requestId"), requestId},
        {QStringLiteral("status"), QStringLiteral("accepted")},
        {QStringLiteral("errorCode"), QString()},
        {QStringLiteral("message"), QStringLiteral("Playback request queued")},
        {QStringLiteral("targetIds"), targets}});
}

void RuntimeService::globalPatchFinished(const QString &requestId,
    const QString &status,
    const QString &errorCode,
    const QString &message)
{
    if (!m_pendingCommands.contains(requestId)) {
        return;
    }
    QMetaObject::invokeMethod(this, [this, requestId, status, errorCode, message] {
        if (status == QStringLiteral("applied") || status == QStringLiteral("rejected")) {
            if (status == QStringLiteral("applied")) {
                refreshGlobalSnapshot();
            }
            completeCommand(requestId, status, errorCode, message);
        } else {
            completeCommand(requestId, QStringLiteral("rejected"),
                QStringLiteral("invalidBackendResult"), QStringLiteral("Global settings backend returned an invalid result"));
        }
    }, Qt::QueuedConnection);
}

void RuntimeService::followTargetFinished(const QString &requestId, const QUuid &instanceId,
    bool applied, bool enabled, const QString &mode, const QString &accountKey,
    const QString &sourceKey, const QString &errorCode, const QString &message)
{
    auto pending = m_pendingFollows.find(requestId);
    if (pending == m_pendingFollows.end() || !pending->targets.contains(instanceId)) return;
    for (const QJsonValue &item : pending->results) {
        if (item.toObject().value(QStringLiteral("instanceId")).toString() == uuidString(instanceId)) return;
    }
    QJsonObject result{{QStringLiteral("instanceId"), uuidString(instanceId)},
        {QStringLiteral("status"), applied ? QStringLiteral("applied") : QStringLiteral("rejected")},
        {QStringLiteral("enabled"), enabled}, {QStringLiteral("mode"), mode},
        {QStringLiteral("accountKey"), accountKey}, {QStringLiteral("sourceKey"), sourceKey},
        {QStringLiteral("errorCode"), errorCode}, {QStringLiteral("message"), message}};
    pending->results.append(result);
    pending->anyApplied |= applied;
    pending->anyRejected |= !applied;
    --pending->remaining;
    if (pending->remaining > 0) return;
    const QString status = pending->anyApplied
        ? (pending->anyRejected ? QStringLiteral("partial") : QStringLiteral("applied"))
        : QStringLiteral("rejected");
    const QJsonArray results = pending->results;
    m_pendingFollows.erase(pending);
    const QString aggregateMessage = status == QStringLiteral("applied")
        ? QStringLiteral("Follow configuration confirmed")
        : QStringLiteral("One or more wallpaper targets could not confirm Follow");
    completeCommand(requestId, status,
        status == QStringLiteral("rejected") ? QStringLiteral("targetRejected") : QString(),
        aggregateMessage, results);
}

void RuntimeService::reloadTargetFinished(const QString &requestId, const QUuid &instanceId,
    bool applied, const QString &errorCode, const QString &message)
{
    auto pending = m_pendingReloads.find(requestId);
    if (pending == m_pendingReloads.end() || !pending->targets.contains(instanceId)) return;
    for (const QJsonValue &item : pending->results)
        if (item.toObject().value(QStringLiteral("instanceId")).toString() == uuidString(instanceId)) return;
    pending->results.append(QJsonObject{{QStringLiteral("instanceId"), uuidString(instanceId)},
        {QStringLiteral("status"), applied ? QStringLiteral("applied") : QStringLiteral("rejected")},
        {QStringLiteral("errorCode"), errorCode}, {QStringLiteral("message"), message}});
    pending->anyApplied |= applied;
    pending->anyRejected |= !applied;
    --pending->remaining;
    if (pending->remaining > 0) return;
    const QString status = pending->anyApplied
        ? (pending->anyRejected ? QStringLiteral("partial") : QStringLiteral("applied"))
        : QStringLiteral("rejected");
    const QJsonArray results = pending->results;
    m_pendingReloads.erase(pending);
    completeCommand(requestId, status,
        status == QStringLiteral("rejected") ? QStringLiteral("reloadFailed") : QString(),
        status == QStringLiteral("applied") ? QStringLiteral("Reload first frame observed")
            : QStringLiteral("One or more reload targets failed or lacked confirmation"), results);
}

void RuntimeService::configurationTargetFinished(const QString& requestId, const QUuid& instanceId,
    bool applied, const QString& errorCode, const QString& message) {
    const auto pending = m_pendingConfigurations.find(requestId);
    if (pending == m_pendingConfigurations.end() || pending.value().size() != 1
        || pending.value().first() != instanceId) return;
    m_pendingConfigurations.erase(pending);
    completeCommand(requestId, applied ? QStringLiteral("applied") : QStringLiteral("rejected"),
        errorCode, message,
        QJsonArray{QJsonObject{{QStringLiteral("instanceId"), uuidString(instanceId)},
            {QStringLiteral("status"), applied ? QStringLiteral("applied") : QStringLiteral("rejected")},
            {QStringLiteral("errorCode"), errorCode}, {QStringLiteral("message"), message}}});
}

void RuntimeService::nextWallpaperTargetFinished(const QString& requestId, const QUuid& instanceId,
    bool applied, const QString& errorCode, const QString& message) {
    auto pending = m_pendingNextWallpapers.find(requestId);
    if (pending == m_pendingNextWallpapers.end() || !pending->targets.contains(instanceId)) return;
    for (const QJsonValue& item : pending->results)
        if (item.toObject().value(QStringLiteral("instanceId")).toString() == uuidString(instanceId)) return;
    pending->results.append(QJsonObject{{QStringLiteral("instanceId"), uuidString(instanceId)},
        {QStringLiteral("status"), applied ? QStringLiteral("applied") : QStringLiteral("rejected")},
        {QStringLiteral("errorCode"), errorCode}, {QStringLiteral("message"), message}});
    pending->anyApplied |= applied;
    pending->anyRejected |= !applied;
    --pending->remaining;
    if (pending->remaining > 0) return;
    const QString status = pending->anyApplied
        ? (pending->anyRejected ? QStringLiteral("partial") : QStringLiteral("applied"))
        : QStringLiteral("rejected");
    const QJsonArray results = pending->results;
    m_pendingNextWallpapers.erase(pending);
    completeCommand(requestId, status,
        status == QStringLiteral("rejected") ? QStringLiteral("selectionFailed") : QString(),
        status == QStringLiteral("applied") ? QStringLiteral("Wallpaper selection confirmed")
            : QStringLiteral("One or more wallpaper selections failed"), results);
}

void RuntimeService::refreshGlobalSnapshot()
{
    if (!m_globalPatchBackend) {
        m_registry.updateGlobal({});
        return;
    }
    const QVariantMap stored = m_globalPatchBackend->currentValues({
        QStringLiteral("AudioDisabled"),
        QStringLiteral("SizingEnabled"),
        QStringLiteral("SizingMode")});
    const int sizingMode = stored.value(QStringLiteral("SizingMode"), 1).toInt();
    m_registry.updateGlobal({
        {QStringLiteral("muted"), stored.value(QStringLiteral("AudioDisabled"), false).toBool()},
        {QStringLiteral("sizingEnabled"), stored.value(QStringLiteral("SizingEnabled"), false).toBool()},
        {QStringLiteral("sizingMode"), sizingMode == 2 ? QStringLiteral("stretch")
                                                        : QStringLiteral("crop")}});
}

void RuntimeService::completeCommand(const QString &requestId,
    const QString &status,
    const QString &errorCode,
    const QString &message,
    const QJsonArray &targetResults)
{
    if (!m_registered || !m_pendingCommands.contains(requestId)) {
        return;
    }
    if (status != QStringLiteral("applied") && status != QStringLiteral("partial")
        && status != QStringLiteral("rejected")) {
        return;
    }
    QJsonObject result{{QStringLiteral("requestId"), requestId},
        {QStringLiteral("status"), status},
        {QStringLiteral("errorCode"), errorCode},
        {QStringLiteral("message"), message},
        {QStringLiteral("targets"), targetResults}};
    m_pendingCommands.remove(requestId);
    QString json = compactJson(result);
    if (json.toUtf8().size() > MaximumMessageBytes) {
        json = compactJson({{QStringLiteral("requestId"), requestId},
            {QStringLiteral("status"), QStringLiteral("partial")},
            {QStringLiteral("errorCode"), QStringLiteral("resultTooLarge")},
            {QStringLiteral("message"), QStringLiteral("Command result exceeds 1 MiB")},
            {QStringLiteral("targets"), QJsonArray{}}});
    }
    emit CommandFinished(json);
}

RuntimeClient::RuntimeClient(const QDBusConnection &connection, QObject *parent)
    : QObject(parent)
    , m_connection(connection)
    , m_serviceWatcher(new QDBusServiceWatcher(
          RuntimeService::ServiceName,
          m_connection,
          QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration
              | QDBusServiceWatcher::WatchForOwnerChange,
          this))
{
    m_connection.connect(QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("NameOwnerChanged"),
        this,
        SLOT(dbusNameOwnerChanged(QString,QString,QString)));
    connect(m_serviceWatcher,
        &QDBusServiceWatcher::serviceOwnerChanged,
        this,
        &RuntimeClient::ownerChanged);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this](const QString &service) {
        if (QDBusConnectionInterface *interface = m_connection.interface()) {
            const QDBusReply<QString> owner = interface->serviceOwner(service);
            if (owner.isValid()) {
                ownerChanged(service, {}, owner.value());
            }
        }
    });
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &service) {
        ownerChanged(service, m_owner, {});
    });
    if (QDBusConnectionInterface *busInterface = m_connection.interface()) {
        const QDBusReply<QString> owner = busInterface->serviceOwner(RuntimeService::ServiceName);
        if (owner.isValid() && !owner.value().isEmpty()) {
            ownerChanged(QString::fromLatin1(RuntimeService::ServiceName), {}, owner.value());
        }
    }
}

RuntimeClient::~RuntimeClient()
{
    disconnectOwnerSignals();
    m_connection.disconnect(QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("NameOwnerChanged"),
        this,
        SLOT(dbusNameOwnerChanged(QString,QString,QString)));
}

bool RuntimeClient::available() const
{
    return m_available;
}

QString RuntimeClient::snapshot() const
{
    return m_snapshot;
}

void RuntimeClient::ownerChanged(const QString &service,
    const QString &oldOwner,
    const QString &newOwner)
{
    if (service != QString::fromLatin1(RuntimeService::ServiceName)) {
        return;
    }
    if (m_owner == newOwner) {
        return;
    }
    Q_UNUSED(oldOwner);
    ++m_ownerGeneration;
    disconnectOwnerSignals();
    m_owner = newOwner;
    m_pendingAcknowledgements.clear();
    m_earlyCompletions.clear();
    m_recentCompletions.clear();
    m_recentCompletionOrder.clear();
    setSnapshot({});
    const bool nowAvailable = !newOwner.isEmpty();
    if (m_available != nowAvailable) {
        m_available = nowAvailable;
        emit availabilityChanged(m_available);
    }
    // Unlike availabilityChanged, this also reports replacement by a new
    // owner while the well-known name remains available.
    emit runtimeOwnerChanged(nowAvailable);
    if (m_available) {
        connectOwnerSignals();
        refreshSnapshot();
    }
}

void RuntimeClient::dbusNameOwnerChanged(const QString &service,
    const QString &oldOwner,
    const QString &newOwner)
{
    ownerChanged(service, oldOwner, newOwner);
}

void RuntimeClient::connectOwnerSignals()
{
    m_connection.connect(m_owner,
        QString::fromLatin1(RuntimeService::ObjectPath),
        QString::fromLatin1(RuntimeService::InterfaceName),
        QStringLiteral("StateChanged"),
        this,
        SLOT(stateChanged(QString,QDBusMessage)));
    m_connection.connect(m_owner,
        QString::fromLatin1(RuntimeService::ObjectPath),
        QString::fromLatin1(RuntimeService::InterfaceName),
        QStringLiteral("CommandFinished"),
        this,
        SLOT(serviceCommandFinished(QString,QDBusMessage)));
}

void RuntimeClient::disconnectOwnerSignals()
{
    if (m_owner.isEmpty()) {
        return;
    }
    m_connection.disconnect(QString::fromLatin1(RuntimeService::ServiceName),
        QString::fromLatin1(RuntimeService::ObjectPath),
        QString::fromLatin1(RuntimeService::InterfaceName),
        QStringLiteral("StateChanged"),
        this,
        SLOT(stateChanged(QString,QDBusMessage)));
    m_connection.disconnect(QString::fromLatin1(RuntimeService::ServiceName),
        QString::fromLatin1(RuntimeService::ObjectPath),
        QString::fromLatin1(RuntimeService::InterfaceName),
        QStringLiteral("CommandFinished"),
        this,
        SLOT(serviceCommandFinished(QString,QDBusMessage)));
}

bool RuntimeClient::validateSnapshot(const QString &json) const
{
    if (json.toUtf8().size() > RuntimeService::MaximumMessageBytes) {
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject snapshot = document.object();
    QUuid epoch;
    return snapshot.value(QStringLiteral("schemaVersion")).toDouble(-1) == 1.0
        && validUuid(snapshot.value(QStringLiteral("epoch")).toString(), &epoch)
        && snapshot.value(QStringLiteral("revision")).isDouble()
        && snapshot.value(QStringLiteral("targets")).isArray()
        && snapshot.value(QStringLiteral("global")).isObject();
}

void RuntimeClient::setSnapshot(const QString &json)
{
    if (!json.isEmpty() && !validateSnapshot(json)) {
        emit requestFailed(QStringLiteral("Received invalid runtime snapshot"));
        return;
    }
    if (m_snapshot == json) {
        return;
    }
    m_snapshot = json;
    if (!json.isEmpty()) {
        emit snapshotChanged(json);
    }
}

void RuntimeClient::refreshSnapshot()
{
    if (!m_available) {
        return;
    }
    const quint64 ownerGeneration = m_ownerGeneration;
    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(RuntimeService::ServiceName),
        QString::fromLatin1(RuntimeService::ObjectPath),
        QString::fromLatin1(RuntimeService::InterfaceName),
        QStringLiteral("GetSnapshot"));
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(m_connection.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, ownerGeneration] {
        QDBusPendingReply<QString> reply = *watcher;
        if (ownerGeneration != m_ownerGeneration) {
            watcher->deleteLater();
            return;
        }
        if (reply.isError()) {
            emit requestFailed(reply.error().message());
        } else {
            setSnapshot(reply.value());
        }
        watcher->deleteLater();
    });
}

void RuntimeClient::submitCommand(const QString &requestJson)
{
    if (!m_available) {
        const QString requestId = QJsonDocument::fromJson(requestJson.toUtf8()).object()
                                      .value(QStringLiteral("requestId")).toString();
        const QString message = QStringLiteral("Runtime service is unavailable");
        emit commandRequestFailed(requestId, message);
        emit requestFailed(message);
        return;
    }
    const QJsonDocument requestDocument = QJsonDocument::fromJson(requestJson.toUtf8());
    const QString requestId = requestDocument.isObject()
        ? requestDocument.object().value(QStringLiteral("requestId")).toString() : QString();
    const quint64 ownerGeneration = m_ownerGeneration;
    if (!requestId.isEmpty()) {
        m_pendingAcknowledgements.insert(requestId);
    }
    if (requestJson.toUtf8().size() > RuntimeService::MaximumMessageBytes) {
        m_pendingAcknowledgements.remove(requestId);
        const QString message = QStringLiteral("Request exceeds 1 MiB");
        emit commandRequestFailed(requestId, message);
        emit requestFailed(message);
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(RuntimeService::ServiceName),
        QString::fromLatin1(RuntimeService::ObjectPath),
        QString::fromLatin1(RuntimeService::InterfaceName),
        QStringLiteral("Execute"));
    message << requestJson;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(m_connection.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
        [this, watcher, requestId, ownerGeneration] {
        QDBusPendingReply<QString> reply = *watcher;
        if (ownerGeneration != m_ownerGeneration) {
            watcher->deleteLater();
            return;
        }
        if (reply.isError()) {
            emit commandRequestFailed(requestId, reply.error().message());
            emit requestFailed(reply.error().message());
            m_pendingAcknowledgements.remove(requestId);
            m_earlyCompletions.remove(requestId);
        } else {
            const QString acknowledgement = reply.value();
            const QJsonDocument ackDocument = QJsonDocument::fromJson(acknowledgement.toUtf8());
            const QJsonObject ack = ackDocument.object();
            emit commandAcknowledgement(acknowledgement);
            m_pendingAcknowledgements.remove(requestId);
            if (ack.value(QStringLiteral("status")).toString() == QStringLiteral("accepted")
                && ack.value(QStringLiteral("requestId")).toString() == requestId) {
                const auto early = m_earlyCompletions.find(requestId);
                if (early != m_earlyCompletions.end()) {
                    const QString completion = early.value();
                    m_earlyCompletions.erase(early);
                    if (!m_recentCompletions.contains(requestId)) {
                        rememberFinished(requestId);
                        emit commandFinished(completion);
                    }
                }
            } else {
                m_earlyCompletions.remove(requestId);
            }
        }
        watcher->deleteLater();
    });
}

void RuntimeClient::stateChanged(const QString &snapshotJson, const QDBusMessage &message)
{
    if (!message.service().isEmpty() && message.service() != m_owner) return;
    setSnapshot(snapshotJson);
}

void RuntimeClient::serviceCommandFinished(const QString &resultJson, const QDBusMessage &message)
{
    if (!message.service().isEmpty() && message.service() != m_owner) return;
    if (resultJson.toUtf8().size() > RuntimeService::MaximumMessageBytes) {
        emit requestFailed(QStringLiteral("Received invalid command result"));
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(resultJson.toUtf8());
    if (!document.isObject()) {
        emit requestFailed(QStringLiteral("Received invalid command result"));
        return;
    }
    const QString requestId = document.object().value(QStringLiteral("requestId")).toString();
    if (requestId.isEmpty()) {
        emit requestFailed(QStringLiteral("Received command result without requestId"));
        return;
    }
    if (m_recentCompletions.contains(requestId)) {
        return;
    }
    if (m_pendingAcknowledgements.contains(requestId)) {
        m_earlyCompletions.insert(requestId, resultJson);
        return;
    }
    rememberFinished(requestId);
    emit commandFinished(resultJson);
}

void RuntimeClient::rememberFinished(const QString &requestId)
{
    if (m_recentCompletions.contains(requestId)) {
        return;
    }
    m_recentCompletions.insert(requestId);
    m_recentCompletionOrder.enqueue(requestId);
    constexpr qsizetype MaximumRememberedCompletions = 256;
    while (m_recentCompletionOrder.size() > MaximumRememberedCompletions) {
        m_recentCompletions.remove(m_recentCompletionOrder.dequeue());
    }
}

} // namespace WallpaperRuntime
