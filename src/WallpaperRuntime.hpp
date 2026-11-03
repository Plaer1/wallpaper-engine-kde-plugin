#pragma once

#include "WallpaperPlaybackPolicy.hpp"

#include <QObject>
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QJsonArray>
#include <QUuid>
#include <QVariantMap>
#include <QVector>

namespace WallpaperRuntime {

enum class EffectivePlayback {
    Unknown,
    Loading,
    Playing,
    Paused,
    Error,
};

class PlaybackEndpoint : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~PlaybackEndpoint() override = default;

    virtual void requestPlayback(WallpaperPlaybackPolicy::Override desired) = 0;
    virtual bool supportsFollow() const { return false; }
    virtual void requestFollow(const QString &, const QVariantMap &) {}
    virtual bool supportsReload() const { return false; }
    virtual void requestReload(const QString &) {}
    virtual bool supportsOpenConfiguration() const { return false; }
    virtual void requestOpenConfiguration(const QString &) {}
    virtual bool supportsNextWallpaper() const { return false; }
    virtual void requestNextWallpaper(const QString &) {}

signals:
    void followFinished(const QString &requestId, bool applied, bool enabled,
        const QString &mode, const QString &accountKey, const QString &sourceKey,
        const QString &errorCode, const QString &message);
    void reloadFinished(const QString &requestId, bool applied,
        const QString &errorCode, const QString &message);
    void configurationFinished(const QString &requestId, bool applied,
        const QString &errorCode, const QString &message);
    void nextWallpaperFinished(const QString &requestId, bool applied,
        const QString &errorCode, const QString &message);
};

struct TargetDescriptor {
    QUuid instanceId;
    QString containmentId;
    QString activityId;
    QString screenName;
    QString screenLabel;
    QString wallpaperSource;
    QString title;
    QString workshopId;
    QString backend;
    bool followEnabled = false;
    QString followMode;
    QString followAccountKey;
    QString followSourceKey;
    QVariantList followChoices;
    QString followStatus;
    QString followProvenance;
    QStringList capabilities;
};

struct TargetSnapshot {
    QUuid instanceId;
    QString containmentId;
    QString activityId;
    QString screenName;
    QString screenLabel;
    WallpaperPlaybackPolicy::Override desiredPlayback =
        WallpaperPlaybackPolicy::Override::Automatic;
    EffectivePlayback effectivePlayback = EffectivePlayback::Unknown;
    QVector<WallpaperPlaybackPolicy::Reason> pauseReasons;
    QString wallpaperSource;
    QString title;
    QString workshopId;
    QString backend;
    bool followEnabled = false;
    QString followMode;
    QString followAccountKey;
    QString followSourceKey;
    QVariantList followChoices;
    QString followStatus;
    QString followProvenance;
    QStringList capabilities;
};

struct RuntimeSnapshot {
    int schemaVersion = 1;
    QUuid epoch;
    quint64 revision = 0;
    QVector<TargetSnapshot> targets;
    QVariantMap global;
};

struct Registration {
    bool accepted = false;
    QUuid instanceId;
};

struct TargetSelection {
    bool allWallpaperTargets = false;
    QVector<QUuid> targetIds;
};

enum class ResolveError {
    None,
    StaleEpoch,
    InvalidSelection,
    UnknownTarget,
    ReentrantDispatch,
};

struct ResolveResult {
    ResolveError error = ResolveError::None;
    QVector<QUuid> targetIds;
};

struct DispatchTargetResult {
    QUuid instanceId;
    bool dispatched = false;
};

struct DispatchResult {
    ResolveError error = ResolveError::None;
    QVector<DispatchTargetResult> targets;
};

class GlobalPatchBackend : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~GlobalPatchBackend() override = default;

    virtual QVariantMap currentValues(const QStringList &keys) const = 0;
    virtual void submitPatch(const QString &requestId,
        const QVariantMap &patch,
        const QVariantMap &baseline) = 0;

signals:
    void valuesChanged();
    void patchFinished(const QString &requestId,
        const QString &status,
        const QString &errorCode,
        const QString &message);
};

class Registry : public QObject
{
    Q_OBJECT

public:
    explicit Registry(QObject *parent = nullptr);

    QUuid epoch() const;
    quint64 revision() const;
    void renewEpoch();

    Registration registerTarget(const TargetDescriptor &descriptor,
        PlaybackEndpoint *endpoint,
        bool configurationPreview = false);
    bool unregisterTarget(const QUuid &instanceId);
    bool updateOutput(const QUuid &instanceId, const QString &screenName, const QString &screenLabel);
    bool updateWallpaper(const QUuid &instanceId,
        const QString &source,
        const QString &title,
        const QString &workshopId,
        const QString &backend);
    bool updateCapabilities(const QUuid &instanceId, const QStringList &capabilities);
    bool updateFollow(const QUuid &instanceId, bool enabled, const QString &mode,
        const QString &accountKey, const QString &sourceKey, const QVariantList &choices,
        const QString &status, const QString &provenance);
    bool updateGlobal(const QVariantMap &values);
    bool confirmPlayback(const QUuid &instanceId,
        EffectivePlayback effective,
        const QVector<WallpaperPlaybackPolicy::Reason> &pauseReasons = {});

    RuntimeSnapshot snapshot() const;
    ResolveResult resolveTargets(const QUuid &requestEpoch, const TargetSelection &selection) const;
    DispatchResult setPlayback(const QUuid &requestEpoch,
        const TargetSelection &selection,
        WallpaperPlaybackPolicy::Override desired);
    ResolveResult setFollow(const QUuid &requestEpoch, const TargetSelection &selection,
        const QString &requestId, const QVariantMap &args);
    ResolveResult reloadWallpaper(const QUuid &requestEpoch, const TargetSelection &selection,
        const QString &requestId);
    ResolveResult openConfiguration(const QUuid &requestEpoch, const TargetSelection &selection,
        const QString &requestId);
    ResolveResult nextWallpaper(const QUuid &requestEpoch, const TargetSelection &selection,
        const QString &requestId);

signals:
    void snapshotChanged(quint64 revision);
    void followFinished(const QString &requestId, const QUuid &instanceId, bool applied, bool enabled,
        const QString &mode, const QString &accountKey, const QString &sourceKey,
        const QString &errorCode, const QString &message);
    void reloadFinished(const QString &requestId, const QUuid &instanceId, bool applied,
        const QString &errorCode, const QString &message);
    void configurationFinished(const QString &requestId, const QUuid &instanceId, bool applied,
        const QString &errorCode, const QString &message);
    void nextWallpaperFinished(const QString &requestId, const QUuid &instanceId, bool applied,
        const QString &errorCode, const QString &message);

private:
    struct Entry {
        TargetSnapshot snapshot;
        QPointer<PlaybackEndpoint> endpoint;
    };

    void removeTarget(const QUuid &instanceId);
    Entry *find(const QUuid &instanceId);
    const Entry *find(const QUuid &instanceId) const;
    void advanceRevision();

    QUuid m_epoch;
    quint64 m_revision = 0;
    bool m_dispatchingPlayback = false;
    QVector<Entry> m_entries;
    QVariantMap m_global;
};

} // namespace WallpaperRuntime

Q_DECLARE_METATYPE(WallpaperRuntime::EffectivePlayback)

namespace WallpaperRuntime {

class RuntimeService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wekde.Runtime1")

public:
    static constexpr const char *ServiceName = "org.wekde.Runtime1";
    static constexpr const char *ObjectPath = "/org/wekde/Runtime1";
    static constexpr const char *InterfaceName = "org.wekde.Runtime1";
    static constexpr qsizetype MaximumMessageBytes = 1024 * 1024;

    explicit RuntimeService(Registry &registry,
        const QDBusConnection &connection = QDBusConnection::sessionBus(),
        QObject *parent = nullptr);
    RuntimeService(Registry &registry,
        GlobalPatchBackend *globalPatchBackend,
        const QDBusConnection &connection = QDBusConnection::sessionBus(),
        QObject *parent = nullptr);
    ~RuntimeService() override;

    bool isRegistered() const;
    void completeCommand(const QString &requestId,
        const QString &status,
        const QString &errorCode = {},
        const QString &message = {},
        const QJsonArray &targetResults = {});

public slots:
    QString GetSnapshot();
    QString Execute(const QString &requestJson);

signals:
    void StateChanged(const QString &snapshotJson);
    void CommandFinished(const QString &resultJson);

private slots:
    void registryChanged();
    void globalPatchFinished(const QString &requestId,
        const QString &status,
        const QString &errorCode,
        const QString &message);
    void followTargetFinished(const QString &requestId, const QUuid &instanceId, bool applied,
        bool enabled, const QString &mode, const QString &accountKey, const QString &sourceKey,
        const QString &errorCode, const QString &message);
    void reloadTargetFinished(const QString &requestId, const QUuid &instanceId, bool applied,
        const QString &errorCode, const QString &message);
    void configurationTargetFinished(const QString &requestId, const QUuid &instanceId, bool applied,
        const QString &errorCode, const QString &message);
    void nextWallpaperTargetFinished(const QString &requestId, const QUuid &instanceId, bool applied,
        const QString &errorCode, const QString &message);

private:
    bool acquireService();
    void releaseService();
    QString snapshotJson() const;
    void refreshGlobalSnapshot();
    QString rejection(const QString &requestId, const QString &errorCode,
        const QString &message) const;

    Registry &m_registry;
    QDBusConnection m_connection;
    QPointer<GlobalPatchBackend> m_globalPatchBackend;
    bool m_registered = false;
    bool m_transitioning = false;
    QHash<QString, QVector<QUuid>> m_pendingCommands;
    struct PendingFollow {
        QVector<QUuid> targets;
        QJsonArray results;
        int remaining = 0;
        bool anyApplied = false;
        bool anyRejected = false;
    };
    QHash<QString, PendingFollow> m_pendingFollows;
    struct PendingReload {
        QVector<QUuid> targets;
        QJsonArray results;
        int remaining = 0;
        bool anyApplied = false;
        bool anyRejected = false;
    };
    QHash<QString, PendingReload> m_pendingReloads;
    QHash<QString, QVector<QUuid>> m_pendingConfigurations;
    struct PendingNext {
        QVector<QUuid> targets;
        QJsonArray results;
        int remaining = 0;
        bool anyApplied = false;
        bool anyRejected = false;
    };
    QHash<QString, PendingNext> m_pendingNextWallpapers;
};

class RuntimeClient : public QObject
{
    Q_OBJECT

public:
    explicit RuntimeClient(const QDBusConnection &connection = QDBusConnection::sessionBus(),
        QObject *parent = nullptr);
    ~RuntimeClient() override;

    bool available() const;
    QString snapshot() const;
    void refreshSnapshot();
    void submitCommand(const QString &requestJson);

signals:
    void availabilityChanged(bool available);
    void runtimeOwnerChanged(bool available);
    void snapshotChanged(const QString &snapshotJson);
    void commandAcknowledgement(const QString &ackJson);
    void commandFinished(const QString &resultJson);
    void commandRequestFailed(const QString &requestId, const QString &message);
    void requestFailed(const QString &message);

private slots:
    void dbusNameOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void ownerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void stateChanged(const QString &snapshotJson, const QDBusMessage &message);
    void serviceCommandFinished(const QString &resultJson, const QDBusMessage &message);

private:
    bool validateSnapshot(const QString &json) const;
    void setSnapshot(const QString &json);
    void disconnectOwnerSignals();
    void connectOwnerSignals();
    void rememberFinished(const QString &requestId);

    QDBusConnection m_connection;
    QDBusServiceWatcher *m_serviceWatcher = nullptr;
    QString m_owner;
    quint64 m_ownerGeneration = 0;
    QString m_snapshot;
    bool m_available = false;
    QSet<QString> m_pendingAcknowledgements;
    QHash<QString, QString> m_earlyCompletions;
    QSet<QString> m_recentCompletions;
    QQueue<QString> m_recentCompletionOrder;
};

} // namespace WallpaperRuntime
