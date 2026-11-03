#pragma once

#include "GlobalConfigStore.hpp"
#include "WallpaperRuntime.hpp"
#include "WallpaperRuntimeRegistration.hpp"
#include "PlasmaConfigurationInvoker.hpp"

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QHash>
#include <QStringList>
#include <QDBusConnection>
#include <memory>

namespace wekde
{

class WallpaperRuntimeQmlEndpoint;

class GlobalConfigRuntimeBridge final : public QObject {
    Q_OBJECT
public:
    explicit GlobalConfigRuntimeBridge(GlobalConfigBackend* backend,
        const QDBusConnection& connection,
        QObject* parent = nullptr);
    void broadcastGlobalChanged();

private slots:
    void handleGlobalConfigChanged(const QString& senderId);

private:
    QPointer<GlobalConfigBackend> m_backend;
    QDBusConnection m_connection;
    QString m_senderId;
};

class WallpaperRuntimeHost final {
public:
    explicit WallpaperRuntimeHost(
        WallpaperRuntime::GlobalPatchBackend* patchBackend = nullptr,
        const QDBusConnection& connection = QDBusConnection::sessionBus(),
        PlasmaConfigurationInvoker* configurationInvoker = nullptr);
    ~WallpaperRuntimeHost() = default;

    WallpaperRuntimeHost(const WallpaperRuntimeHost&) = delete;
    WallpaperRuntimeHost& operator=(const WallpaperRuntimeHost&) = delete;

    static WallpaperRuntimeHost& instance();

    WallpaperRuntime::Registry& registry();
    const WallpaperRuntime::Registry& registry() const;
    WallpaperRuntime::RuntimeService& service();
    const WallpaperRuntime::RuntimeService& service() const;

    WallpaperRuntime::Registration registerEndpoint(
        const WallpaperRuntime::TargetDescriptor& descriptor,
        WallpaperRuntime::PlaybackEndpoint* endpoint,
        bool configurationPreview = false);
    bool unregisterEndpoint(const QUuid& instanceId);
    bool supportsGlobalPatches() const;
    QStringList capabilities(bool playbackConnected, bool followConnected = false,
        bool reloadConnected = false, bool configurationConnected = false,
        bool nextConnected = false) const;
    bool requestConfiguration(WallpaperRuntimeQmlEndpoint* endpoint,
        const QString& requestId, const PlasmaConfigurationTarget& target);

private:
    std::unique_ptr<GlobalConfigRuntimeBridge> m_syncBridge;
    std::unique_ptr<WallpaperRuntime::GlobalPatchBackend> m_ownedPatchBackend;
    QPointer<WallpaperRuntime::GlobalPatchBackend> m_patchBackend;
    std::unique_ptr<DBusPlasmaScriptTransport> m_ownedScriptTransport;
    std::unique_ptr<PlasmaConfigurationInvoker> m_ownedConfigurationInvoker;
    QPointer<PlasmaConfigurationInvoker> m_configurationInvoker;
    QHash<QString, QPointer<WallpaperRuntimeQmlEndpoint>> m_pendingConfigurations;
    WallpaperRuntime::Registry m_registry;
    WallpaperRuntime::RuntimeService m_service;
};

class WallpaperRuntimeQmlEndpoint : public WallpaperRuntime::PlaybackEndpoint {
    Q_OBJECT
    Q_PROPERTY(WallpaperRuntimeRegistration* registration READ registration WRITE setRegistration
                   NOTIFY registrationChanged)
    Q_PROPERTY(bool registered READ isRegistered NOTIFY registeredChanged)
    Q_PROPERTY(bool playbackRequestConnected READ playbackRequestConnected
                   WRITE setPlaybackRequestConnected NOTIFY playbackRequestConnectedChanged)
    Q_PROPERTY(bool followRequestConnected READ followRequestConnected
                   WRITE setFollowRequestConnected NOTIFY followRequestConnectedChanged)
    Q_PROPERTY(bool reloadRequestConnected READ reloadRequestConnected
                   WRITE setReloadRequestConnected NOTIFY reloadRequestConnectedChanged)
    Q_PROPERTY(bool nextRequestConnected READ nextRequestConnected
                   WRITE setNextRequestConnected NOTIFY nextRequestConnectedChanged)
    Q_PROPERTY(QString instanceId READ instanceId NOTIFY registeredChanged)
    Q_PROPERTY(QString wallpaperSource READ wallpaperSource WRITE setWallpaperSource
                   NOTIFY metadataChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY metadataChanged)
    Q_PROPERTY(QString workshopId READ workshopId WRITE setWorkshopId NOTIFY metadataChanged)
    Q_PROPERTY(QString backend READ backend WRITE setBackend NOTIFY metadataChanged)
    Q_PROPERTY(QVariantMap followState READ followState WRITE setFollowState
                   NOTIFY followStateChanged)

public:
    explicit WallpaperRuntimeQmlEndpoint(QObject* parent = nullptr);
    WallpaperRuntimeQmlEndpoint(WallpaperRuntimeHost* host, QObject* parent);
    ~WallpaperRuntimeQmlEndpoint() override;

    WallpaperRuntimeRegistration* registration() const;
    void setRegistration(WallpaperRuntimeRegistration* registration);

    bool isRegistered() const;
    bool playbackRequestConnected() const;
    void setPlaybackRequestConnected(bool connected);
    bool followRequestConnected() const;
    void setFollowRequestConnected(bool connected);
    bool reloadRequestConnected() const;
    void setReloadRequestConnected(bool connected);
    bool nextRequestConnected() const;
    void setNextRequestConnected(bool connected);
    QString instanceId() const;

    QString wallpaperSource() const;
    void setWallpaperSource(const QString& source);
    QString title() const;
    void setTitle(const QString& title);
    QString workshopId() const;
    void setWorkshopId(const QString& workshopId);
    QString backend() const;
    void setBackend(const QString& backend);
    QVariantMap followState() const;
    void setFollowState(const QVariantMap& state);

    Q_INVOKABLE bool reportPlaybackState(const QString& state,
        const QStringList& pauseReasons = {});
    Q_INVOKABLE bool completeFollowRequest(const QString& requestId,
        const QString& instanceId, bool applied, bool enabled, const QString& mode,
        const QString& accountKey, const QString& sourceKey,
        const QString& errorCode = {}, const QString& message = {});
    Q_INVOKABLE bool completeReloadRequest(const QString& requestId,
        const QString& instanceId, bool applied,
        const QString& errorCode = {}, const QString& message = {});
    Q_INVOKABLE bool completeConfigurationRequest(const QString& requestId,
        const QString& instanceId, bool opened, const QString& errorCode = {},
        const QString& message = {});
    Q_INVOKABLE bool completeNextRequest(const QString& requestId,
        const QString& instanceId, bool applied, const QString& errorCode = {},
        const QString& message = {});

    void requestPlayback(WallpaperPlaybackPolicy::Override desired) override;
    bool supportsFollow() const override;
    void requestFollow(const QString& requestId, const QVariantMap& args) override;
    bool supportsReload() const override;
    void requestReload(const QString& requestId) override;
    bool supportsOpenConfiguration() const override;
    void requestOpenConfiguration(const QString& requestId) override;
    bool supportsNextWallpaper() const override;
    void requestNextWallpaper(const QString& requestId) override;

signals:
    void registrationChanged();
    void registeredChanged();
    void playbackRequestConnectedChanged();
    void followRequestConnectedChanged();
    void reloadRequestConnectedChanged();
    void nextRequestConnectedChanged();
    void metadataChanged();
    void followStateChanged();
    void playbackRequested(const QString& mode);
    void followRequested(const QString& requestId, const QString& instanceId,
        const QVariantMap& args);
    void reloadRequested(const QString& requestId, const QString& instanceId);
    void nextRequested(const QString& requestId, const QString& instanceId);
    void endpointRejected(const QString& reason);

private:
    void synchronizeRegistration();
    void unregisterCurrentTarget();
    void updateRegisteredMetadata();
    void updateRegisteredFollowState();
    WallpaperRuntime::TargetDescriptor descriptor() const;

    WallpaperRuntimeHost* m_host = nullptr;
    QPointer<WallpaperRuntimeRegistration> m_registration;
    QUuid m_registeredInstance;
    QString m_wallpaperSource;
    QString m_title;
    QString m_workshopId;
    QString m_backend;
    QVariantMap m_followState;
    bool m_playbackRequestConnected = false;
    bool m_followRequestConnected = false;
    QSet<QString> m_pendingFollowRequests;
    QSet<QString> m_pendingReloadRequests;
    QSet<QString> m_pendingConfigurationRequests;
    QSet<QString> m_pendingNextRequests;
    bool m_reloadRequestConnected = false;
    bool m_nextRequestConnected = false;
};

} // namespace wekde
