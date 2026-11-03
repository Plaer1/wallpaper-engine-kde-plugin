#include "WallpaperRuntimeHost.hpp"

#include <QGuiApplication>
#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace wekde;

namespace
{

constexpr auto PluginId = "com.github.catsout.wallpaperEngineKde";
constexpr auto ActivityId = "b0169dd2-b1c7-4f46-817e-c21e6f7b6907";

class FakePatchBackend final : public WallpaperRuntime::GlobalPatchBackend {
public:
    using GlobalPatchBackend::GlobalPatchBackend;

    FakePatchBackend() {
        values = {{QStringLiteral("AudioDisabled"), false},
            {QStringLiteral("SizingEnabled"), false},
            {QStringLiteral("SizingMode"), 1}};
    }

    QVariantMap currentValues(const QStringList& keys) const override {
        QVariantMap result;
        for (const QString& key : keys) {
            if (values.contains(key)) result.insert(key, values.value(key));
        }
        return result;
    }

    void submitPatch(const QString& requestId, const QVariantMap& patch,
                     const QVariantMap& baseline) override {
        ++submitCount;
        lastRequestId = requestId;
        lastPatch = patch;
        lastBaseline = baseline;
        if (failNext) {
            failNext = false;
            emit patchFinished(requestId, QStringLiteral("rejected"),
                               QStringLiteral("testBackendFailure"),
                               QStringLiteral("Synthetic persistence failure"));
            return;
        }
        for (auto it = patch.cbegin(); it != patch.cend(); ++it) values.insert(it.key(), it.value());
        emit valuesChanged();
        emit patchFinished(requestId, QStringLiteral("applied"), {}, {});
    }

    QVariantMap values;
    QVariantMap lastPatch;
    QVariantMap lastBaseline;
    QString lastRequestId;
    int submitCount = 0;
    bool failNext = false;
};

class FakePlasmaScriptTransport final : public PlasmaScriptTransport {
public:
    FakePlasmaScriptTransport(bool online, QObject* parent = nullptr)
        : PlasmaScriptTransport(parent), m_online(online) {}
    bool available() const override { return m_online; }
    void evaluate(const QString& requestId, const QString&) override {
        requests.append(requestId);
    }
    void reply(const QString& requestId, const QString& output) {
        emit evaluated(requestId, true, output, {});
    }
    QStringList requests;
private:
    bool m_online;
};

class FakeFollowEndpoint final : public WallpaperRuntime::PlaybackEndpoint {
    Q_OBJECT
public:
    using PlaybackEndpoint::PlaybackEndpoint;
    void requestPlayback(WallpaperPlaybackPolicy::Override) override {}
    bool supportsFollow() const override { return enabled; }
    void requestFollow(const QString& requestId, const QVariantMap& args) override {
        emit followRequestedForTest(requestId, args);
    }
    void acknowledge(const QString& requestId, bool applied, bool enabled,
        const QVariantMap& args, const QString& error = {}) {
        emit followFinished(requestId, applied, enabled, args.value("mode").toString(),
            args.value("accountKey").toString(), args.value("sourceKey").toString(), error, {});
    }
    bool enabled = true;
signals:
    void followRequestedForTest(const QString& requestId, const QVariantMap& args);
};

class FakeReloadEndpoint final : public WallpaperRuntime::PlaybackEndpoint {
    Q_OBJECT
public:
    using PlaybackEndpoint::PlaybackEndpoint;
    void requestPlayback(WallpaperPlaybackPolicy::Override) override {}
    bool supportsReload() const override { return enabled; }
    void requestReload(const QString& requestId) override { emit reloadRequestedForTest(requestId); }
    void acknowledge(const QString& requestId, bool applied, const QString& error = {}) {
        emit reloadFinished(requestId, applied, error,
            applied ? QStringLiteral("First frame observed") : QStringLiteral("No reload acknowledgment"));
    }
    bool enabled = true;
signals:
    void reloadRequestedForTest(const QString& requestId);
};

class FakeNextEndpoint final : public WallpaperRuntime::PlaybackEndpoint {
    Q_OBJECT
public:
    using PlaybackEndpoint::PlaybackEndpoint;
    void requestPlayback(WallpaperPlaybackPolicy::Override) override {}
    bool supportsNextWallpaper() const override { return enabled; }
    void requestNextWallpaper(const QString& requestId) override {
        emit nextRequestedForTest(requestId);
    }
    void acknowledge(const QString& requestId, bool applied, const QString& error = {}) {
        emit nextWallpaperFinished(requestId, applied, error,
            applied ? QStringLiteral("Selection confirmed") : QStringLiteral("Selection rejected"));
    }
    bool enabled = true;
signals:
    void nextRequestedForTest(const QString& requestId);
};

class TestRegistration final : public WallpaperRuntimeRegistration {
public:
    using WallpaperRuntimeRegistration::WallpaperRuntimeRegistration;

    QString output = QStringLiteral("synthetic-output");
    QStringList outputs { QStringLiteral("synthetic-output") };

protected:
    OutputIdentity currentOutput() const override {
        OutputIdentity result;
        result.window = targetItem() ? targetItem()->window() : nullptr;
        result.screen = result.window ? result.window->screen() : nullptr;
        result.name = output;
        result.availableNames = outputs;
        result.geometry = result.screen ? QRectF(result.screen->geometry()) : QRectF();
        return result;
    }
};

void attachToWindow(QQuickItem& item, QQuickWindow& window) {
    item.setParentItem(window.contentItem());
    window.show();
    QCoreApplication::processEvents();
}

QVariantMap containmentContext(QObject* wallpaper, QObject* activeWallpaper,
                               const QRectF& geometry) {
    return {
        { QStringLiteral("wallpaperObject"), QVariant::fromValue(wallpaper) },
        { QStringLiteral("activeWallpaperObject"), QVariant::fromValue(activeWallpaper) },
        { QStringLiteral("containmentId"), QStringLiteral("31") },
        { QStringLiteral("activityId"), QString::fromLatin1(ActivityId) },
        { QStringLiteral("containmentType"), 0 },
        { QStringLiteral("screen"), 0u },
        { QStringLiteral("screenGeometry"), QVariant::fromValue(geometry) },
        { QStringLiteral("availableScreenRegion"), QVariantList{
              QVariant::fromValue(QRectF(0, 0, geometry.width(), geometry.height())) } },
        { QStringLiteral("wallpaperPlugin"), QString::fromLatin1(PluginId) }
    };
}

QString commandJson(const QUuid& epoch, const QString& requestId, const QString& command,
                    const QString& scope, const QJsonArray& targetIds, const QJsonObject& args) {
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        { QStringLiteral("schemaVersion"), 1 },
        { QStringLiteral("epoch"), epoch.toString(QUuid::WithoutBraces) },
        { QStringLiteral("requestId"), requestId },
        { QStringLiteral("scope"), scope },
        { QStringLiteral("targetIds"), targetIds },
        { QStringLiteral("command"), command },
        { QStringLiteral("args"), args }
    }).toJson(QJsonDocument::Compact));
}

} // namespace

class WallpaperRuntimeHostTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void nextWallpaperRuntimeCommandTargetsExactInstanceAndWaitsForConfirmation() {
        FakeNextEndpoint first;
        FakeNextEndpoint second;
        WallpaperRuntime::Registry registry;
        WallpaperRuntime::TargetDescriptor descriptor;
        descriptor.containmentId = QStringLiteral("71");
        descriptor.capabilities = {QStringLiteral("NextWallpaper")};
        const auto firstTarget = registry.registerTarget(descriptor, &first);
        descriptor.containmentId = QStringLiteral("72");
        const auto secondTarget = registry.registerTarget(descriptor, &second);
        WallpaperRuntime::RuntimeService service(registry);
        QSignalSpy firstRequest(&first, &FakeNextEndpoint::nextRequestedForTest);
        QSignalSpy secondRequest(&second, &FakeNextEndpoint::nextRequestedForTest);
        QSignalSpy finished(&service, &WallpaperRuntime::RuntimeService::CommandFinished);

        const auto wrong = QJsonDocument::fromJson(service.Execute(commandJson(registry.epoch(),
            QStringLiteral("next-wrong"), QStringLiteral("NextWallpaper"),
            QStringLiteral("targets"), QJsonArray{QUuid::createUuid().toString(QUuid::WithoutBraces)},
            QJsonObject{})).toUtf8()).object();
        QCOMPARE(wrong.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
        QCOMPARE(firstRequest.size(), 0);
        QCOMPARE(secondRequest.size(), 0);

        const auto accepted = QJsonDocument::fromJson(service.Execute(commandJson(registry.epoch(),
            QStringLiteral("next-second"), QStringLiteral("NextWallpaper"),
            QStringLiteral("targets"), QJsonArray{secondTarget.instanceId.toString(QUuid::WithoutBraces)},
            QJsonObject{})).toUtf8()).object();
        QCOMPARE(accepted.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_COMPARE(secondRequest.size(), 1);
        QCOMPARE(firstRequest.size(), 0);
        QCOMPARE(finished.size(), 0);
        second.acknowledge(QStringLiteral("next-second"), true);
        QTRY_COMPARE(finished.size(), 1);
        auto result = QJsonDocument::fromJson(finished.takeFirst().first().toString().toUtf8()).object();
        QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("applied"));
        QCOMPARE(result.value(QStringLiteral("targets")).toArray().first().toObject()
            .value(QStringLiteral("instanceId")).toString(), secondTarget.instanceId.toString(QUuid::WithoutBraces));

        const auto removed = QJsonDocument::fromJson(service.Execute(commandJson(registry.epoch(),
            QStringLiteral("next-removed"), QStringLiteral("NextWallpaper"),
            QStringLiteral("targets"), QJsonArray{firstTarget.instanceId.toString(QUuid::WithoutBraces)},
            QJsonObject{})).toUtf8()).object();
        QCOMPARE(removed.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_COMPARE(firstRequest.size(), 1);
        registry.unregisterTarget(firstTarget.instanceId);
        QTRY_COMPARE(finished.size(), 1);
        result = QJsonDocument::fromJson(finished.takeFirst().first().toString().toUtf8()).object();
        QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
        QCOMPARE(result.value(QStringLiteral("errorCode")).toString(), QStringLiteral("targetRemoved"));
    }

    void reloadCommandTargetsExactInstanceAndWaitsForFirstFrameResult() {
        FakeReloadEndpoint first;
        FakeReloadEndpoint second;
        WallpaperRuntime::Registry registry;
        WallpaperRuntime::TargetDescriptor descriptor;
        descriptor.containmentId = QStringLiteral("61");
        descriptor.capabilities = {QStringLiteral("ReloadWallpaper")};
        registry.registerTarget(descriptor, &first);
        descriptor.containmentId = QStringLiteral("62");
        const auto secondTarget = registry.registerTarget(descriptor, &second);
        WallpaperRuntime::RuntimeService service(registry);
        QSignalSpy firstRequest(&first, &FakeReloadEndpoint::reloadRequestedForTest);
        QSignalSpy secondRequest(&second, &FakeReloadEndpoint::reloadRequestedForTest);
        QSignalSpy finished(&service, &WallpaperRuntime::RuntimeService::CommandFinished);
        const QString request = commandJson(registry.epoch(), QStringLiteral("reload-b"),
            QStringLiteral("ReloadWallpaper"), QStringLiteral("targets"),
            QJsonArray{secondTarget.instanceId.toString(QUuid::WithoutBraces)}, QJsonObject{});
        const QJsonObject accepted = QJsonDocument::fromJson(service.Execute(request).toUtf8()).object();
        QCOMPARE(accepted.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_COMPARE(secondRequest.size(), 1);
        QCOMPARE(firstRequest.size(), 0);
        QCOMPARE(finished.size(), 0);
        second.acknowledge(QStringLiteral("reload-b"), true);
        QTRY_COMPARE(finished.size(), 1);
        QJsonObject result = QJsonDocument::fromJson(finished.takeFirst().first().toString().toUtf8()).object();
        QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("applied"));

        const QString failedRequest = commandJson(registry.epoch(), QStringLiteral("reload-failed"),
            QStringLiteral("ReloadWallpaper"), QStringLiteral("targets"),
            QJsonArray{secondTarget.instanceId.toString(QUuid::WithoutBraces)}, QJsonObject{});
        QCOMPARE(QJsonDocument::fromJson(service.Execute(failedRequest).toUtf8()).object()
            .value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_COMPARE(secondRequest.size(), 2);
        second.acknowledge(QStringLiteral("reload-failed"), false, QStringLiteral("backendError"));
        QTRY_COMPARE(finished.size(), 1);
        result = QJsonDocument::fromJson(finished.takeFirst().first().toString().toUtf8()).object();
        QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
        QCOMPARE(result.value(QStringLiteral("targets")).toArray().first().toObject()
            .value(QStringLiteral("errorCode")).toString(), QStringLiteral("backendError"));

        const QString removedRequest = commandJson(registry.epoch(), QStringLiteral("reload-removed"),
            QStringLiteral("ReloadWallpaper"), QStringLiteral("targets"),
            QJsonArray{secondTarget.instanceId.toString(QUuid::WithoutBraces)}, QJsonObject{});
        QCOMPARE(QJsonDocument::fromJson(service.Execute(removedRequest).toUtf8()).object()
            .value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_COMPARE(secondRequest.size(), 3);
        registry.unregisterTarget(secondTarget.instanceId);
        QTRY_COMPARE(finished.size(), 1);
        result = QJsonDocument::fromJson(finished.first().first().toString().toUtf8()).object();
        QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
        QCOMPARE(result.value(QStringLiteral("errorCode")).toString(), QStringLiteral("targetRemoved"));
    }

    void setFollowDispatchesOnlyToExplicitTargetsAndWaitsForAcknowledgement() {
        FakeFollowEndpoint first;
        FakeFollowEndpoint second;
        WallpaperRuntime::Registry registry;
        WallpaperRuntime::TargetDescriptor descriptor;
        descriptor.containmentId = QStringLiteral("51");
        descriptor.capabilities = {QStringLiteral("SetFollow")};
        const auto firstTarget = registry.registerTarget(descriptor, &first);
        descriptor.containmentId = QStringLiteral("52");
        const auto secondTarget = registry.registerTarget(descriptor, &second);
        WallpaperRuntime::RuntimeService service(registry);
        QSignalSpy firstRequest(&first, &FakeFollowEndpoint::followRequestedForTest);
        QSignalSpy secondRequest(&second, &FakeFollowEndpoint::followRequestedForTest);
        QSignalSpy finished(&service, &WallpaperRuntime::RuntimeService::CommandFinished);
        const QJsonObject args{{QStringLiteral("enabled"), true},
            {QStringLiteral("mode"), QStringLiteral("manualMapping")},
            {QStringLiteral("accountKey"), QStringLiteral("userdata:root:7")},
            {QStringLiteral("sourceKey"), QStringLiteral("config:/fixture/loginusers.vdf#section:seven")}};
        const QString request = commandJson(registry.epoch(), QStringLiteral("follow-target-b"),
            QStringLiteral("SetFollow"), QStringLiteral("targets"),
            QJsonArray{secondTarget.instanceId.toString(QUuid::WithoutBraces)}, args);
        const QJsonObject accepted = QJsonDocument::fromJson(service.Execute(request).toUtf8()).object();
        QCOMPARE(accepted.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_COMPARE(firstRequest.size(), 0);
        QTRY_COMPARE(secondRequest.size(), 1);
        QCOMPARE(registry.snapshot().targets.at(1).followEnabled, false);
        QCOMPARE(finished.size(), 0);
        registry.updateFollow(secondTarget.instanceId, true, QStringLiteral("manualMapping"),
            QStringLiteral("userdata:root:7"), QStringLiteral("config:/fixture/loginusers.vdf#section:seven"),
            QVariantList{QVariantMap{{QStringLiteral("mode"), QStringLiteral("manualMapping")},
                {QStringLiteral("accountKey"), QStringLiteral("userdata:root:7")},
                {QStringLiteral("sourceKey"), QStringLiteral("config:/fixture/loginusers.vdf#section:seven")},
                {QStringLiteral("identityVerified"), false}}},
            QStringLiteral("Resolved manual mapping"), QStringLiteral("userAssociated"));
        second.acknowledge(QStringLiteral("follow-target-b"), true, true, args.toVariantMap());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(registry.snapshot().targets.at(1).followEnabled, true);
        QCOMPARE(registry.snapshot().targets.at(1).followMode, QStringLiteral("manualMapping"));
        QCOMPARE(registry.snapshot().targets.at(1).followAccountKey, QStringLiteral("userdata:root:7"));
        QCOMPARE(registry.snapshot().targets.at(1).followStatus, QStringLiteral("Resolved manual mapping"));
        QCOMPARE(registry.snapshot().targets.at(1).followProvenance, QStringLiteral("userAssociated"));
        const QJsonObject result = QJsonDocument::fromJson(finished.first().first().toString().toUtf8()).object();
        QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("applied"));
        QCOMPARE(result.value(QStringLiteral("targets")).toArray().size(), 1);
        Q_UNUSED(firstTarget);
    }

    void qmlFollowStateSeedsSnapshotAndOnlyAdvancesOnVisibleChanges() {
        FakePatchBackend patchBackend;
        WallpaperRuntimeHost host(&patchBackend);
        QQuickWindow window;
        QQuickItem root;
        attachToWindow(root, window);
        TestRegistration registration;
        registration.setTargetItem(&root);
        registration.setContainmentContext(containmentContext(&root, &root,
            QRectF(window.screen()->geometry())));
        QVERIFY(registration.isRegistered());

        WallpaperRuntimeQmlEndpoint endpoint(&host, nullptr);
        QQmlPropertyMap configuration;
        configuration.insert(QStringLiteral("FollowWallpaperEngine"), true);
        configuration.insert(QStringLiteral("FollowSourceMode"), QStringLiteral("account"));
        configuration.insert(QStringLiteral("FollowSourceAccountKey"), QStringLiteral("steam:76561198000000001"));
        configuration.insert(QStringLiteral("FollowSourceKey"), QStringLiteral("config:/fixture/loginusers.vdf#account:76561198000000001"));
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("targetConfiguration"), &configuration);
        engine.rootContext()->setContextProperty(QStringLiteral("runtimeEndpoint"), &endpoint);
        QFile fixture(QStringLiteral(RUNTIME_FOLLOW_STATE_BINDING_QML));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        QQmlComponent binder(&engine);
        binder.setData(fixture.readAll(), QUrl(QStringLiteral("inmemory:follow-state-binding.qml")));
        QTRY_VERIFY(binder.isReady() || binder.isError());
        QObject* binding = binder.create();
        QVERIFY2(binding, qPrintable(binder.errorString()));
        endpoint.setRegistration(&registration);
        QVERIFY(endpoint.isRegistered());
        auto target = host.registry().snapshot().targets.first();
        QVERIFY(target.followEnabled);
        QCOMPARE(target.followMode, QStringLiteral("account"));
        QCOMPARE(target.followAccountKey, QStringLiteral("steam:76561198000000001"));
        QCOMPARE(target.followSourceKey, QStringLiteral("config:/fixture/loginusers.vdf#account:76561198000000001"));
        QCOMPARE(target.followStatus, QStringLiteral("Waiting for saved selection"));
        QCOMPARE(target.followChoices.size(), 1);
        QCOMPARE(target.followChoices.first().toMap().value(QStringLiteral("identityVerified")).toBool(), true);

        binding->setProperty("resolvedProvenance", QVariantMap{
            {QStringLiteral("sourceType"), QStringLiteral("nativeAccount")},
            {QStringLiteral("accountKey"), QStringLiteral("steam:76561198000000001")},
            {QStringLiteral("identityStatus"), QStringLiteral("verified")}});
        binding->setProperty("resolvedOrigin", QStringLiteral("previouslyFollowed"));
        QVERIFY(QMetaObject::invokeMethod(binding, "synchronize"));
        target = host.registry().snapshot().targets.first();
        QJsonParseError provenanceError;
        const QJsonObject displayedProvenance = QJsonDocument::fromJson(
            target.followProvenance.toUtf8(), &provenanceError).object();
        QCOMPARE(provenanceError.error, QJsonParseError::NoError);
        QCOMPARE(displayedProvenance.value(QStringLiteral("accountKey")).toString(),
            QStringLiteral("steam:76561198000000001"));
        QCOMPARE(displayedProvenance.value(QStringLiteral("origin")).toString(),
            QStringLiteral("previouslyFollowed"));

        const quint64 revision = host.registry().revision();
        endpoint.setFollowState(endpoint.followState());
        QCOMPARE(host.registry().revision(), revision);
        QVERIFY(QMetaObject::invokeMethod(binding, "externalQmlChange"));
        QTRY_VERIFY(host.registry().revision() > revision);
        target = host.registry().snapshot().targets.first();
        QVERIFY(!target.followEnabled);
        QCOMPARE(target.followStatus, QStringLiteral("disabled"));
        delete binding;
    }

    void setFollowRemovalCompletesPartialPerTargetResults() {
        FakeFollowEndpoint first;
        FakeFollowEndpoint second;
        WallpaperRuntime::Registry registry;
        WallpaperRuntime::TargetDescriptor descriptor;
        descriptor.capabilities = {QStringLiteral("SetFollow")};
        descriptor.containmentId = QStringLiteral("53");
        const auto firstTarget = registry.registerTarget(descriptor, &first);
        descriptor.containmentId = QStringLiteral("54");
        const auto secondTarget = registry.registerTarget(descriptor, &second);
        WallpaperRuntime::RuntimeService service(registry);
        QSignalSpy firstRequest(&first, &FakeFollowEndpoint::followRequestedForTest);
        QSignalSpy secondRequest(&second, &FakeFollowEndpoint::followRequestedForTest);
        QSignalSpy finished(&service, &WallpaperRuntime::RuntimeService::CommandFinished);
        const QJsonObject args{{QStringLiteral("enabled"), true},
            {QStringLiteral("mode"), QStringLiteral("manualMapping")},
            {QStringLiteral("accountKey"), QStringLiteral("userdata:root:7")},
            {QStringLiteral("sourceKey"), QStringLiteral("config:/fixture/loginusers.vdf#section:seven")}};
        const QString requestId = QStringLiteral("follow-mixed-removal");
        const QString request = commandJson(registry.epoch(), requestId,
            QStringLiteral("SetFollow"), QStringLiteral("targets"),
            QJsonArray{firstTarget.instanceId.toString(QUuid::WithoutBraces),
                       secondTarget.instanceId.toString(QUuid::WithoutBraces)}, args);
        const QJsonObject accepted = QJsonDocument::fromJson(service.Execute(request).toUtf8()).object();
        QCOMPARE(accepted.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_COMPARE(firstRequest.size(), 1);
        QTRY_COMPARE(secondRequest.size(), 1);
        first.acknowledge(requestId, true, true, args.toVariantMap());
        QCOMPARE(finished.size(), 0);

        QVERIFY(registry.unregisterTarget(secondTarget.instanceId));
        QTRY_COMPARE(finished.size(), 1);
        const QJsonObject result = QJsonDocument::fromJson(
            finished.first().first().toString().toUtf8()).object();
        QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("partial"));
        const QJsonArray targetResults = result.value(QStringLiteral("targets")).toArray();
        QCOMPARE(targetResults.size(), 2);
        QCOMPARE(targetResults.at(0).toObject().value(QStringLiteral("status")).toString(),
            QStringLiteral("applied"));
        QCOMPARE(targetResults.at(1).toObject().value(QStringLiteral("status")).toString(),
            QStringLiteral("rejected"));
        QCOMPARE(targetResults.at(1).toObject().value(QStringLiteral("errorCode")).toString(),
            QStringLiteral("targetRemoved"));
    }

    void setFollowTimeoutPreservesAppliedTargetAsPartial() {
        FakeFollowEndpoint first;
        FakeFollowEndpoint second;
        WallpaperRuntime::Registry registry;
        WallpaperRuntime::TargetDescriptor descriptor;
        descriptor.capabilities = {QStringLiteral("SetFollow")};
        descriptor.containmentId = QStringLiteral("55");
        const auto firstTarget = registry.registerTarget(descriptor, &first);
        descriptor.containmentId = QStringLiteral("56");
        const auto secondTarget = registry.registerTarget(descriptor, &second);
        WallpaperRuntime::RuntimeService service(registry);
        QSignalSpy firstRequest(&first, &FakeFollowEndpoint::followRequestedForTest);
        QSignalSpy secondRequest(&second, &FakeFollowEndpoint::followRequestedForTest);
        QSignalSpy finished(&service, &WallpaperRuntime::RuntimeService::CommandFinished);
        const QJsonObject args{{QStringLiteral("enabled"), true},
            {QStringLiteral("mode"), QStringLiteral("manualMapping")},
            {QStringLiteral("accountKey"), QStringLiteral("userdata:root:7")},
            {QStringLiteral("sourceKey"), QStringLiteral("config:/fixture/loginusers.vdf#section:seven")}};
        const QString requestId = QStringLiteral("follow-confirmation-timeout");
        const QString request = commandJson(registry.epoch(), requestId,
            QStringLiteral("SetFollow"), QStringLiteral("targets"),
            QJsonArray{firstTarget.instanceId.toString(QUuid::WithoutBraces),
                       secondTarget.instanceId.toString(QUuid::WithoutBraces)}, args);
        const QJsonObject accepted = QJsonDocument::fromJson(service.Execute(request).toUtf8()).object();
        QCOMPARE(accepted.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_COMPARE(firstRequest.size(), 1);
        QTRY_COMPARE(secondRequest.size(), 1);
        first.acknowledge(requestId, true, true, args.toVariantMap());
        QCOMPARE(finished.size(), 0);

        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 6500);
        const QJsonObject result = QJsonDocument::fromJson(
            finished.first().first().toString().toUtf8()).object();
        QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("partial"));
        QCOMPARE(result.value(QStringLiteral("errorCode")).toString(), QStringLiteral("confirmationTimeout"));
        const QJsonArray targetResults = result.value(QStringLiteral("targets")).toArray();
        QCOMPARE(targetResults.size(), 2);
        QCOMPARE(targetResults.at(0).toObject().value(QStringLiteral("status")).toString(),
            QStringLiteral("applied"));
        QCOMPARE(targetResults.at(1).toObject().value(QStringLiteral("status")).toString(),
            QStringLiteral("rejected"));
        QCOMPARE(targetResults.at(1).toObject().value(QStringLiteral("instanceId")).toString(),
            secondTarget.instanceId.toString(QUuid::WithoutBraces));
        QCOMPARE(targetResults.at(1).toObject().value(QStringLiteral("errorCode")).toString(),
            QStringLiteral("confirmationTimeout"));
    }

    void activeRegistrationCreatesOneTargetAndPreviewDoesNot() {
        FakePatchBackend patchBackend;
        WallpaperRuntimeHost host(&patchBackend);
        QQuickWindow window;
        QQuickItem activeWallpaper;
        QQuickItem previewWallpaper;
        attachToWindow(activeWallpaper, window);
        attachToWindow(previewWallpaper, window);

        TestRegistration previewRegistration;
        previewRegistration.setTargetItem(&previewWallpaper);
        previewRegistration.setContainmentContext(containmentContext(
            &previewWallpaper, &activeWallpaper, QRectF(window.screen()->geometry())));
        WallpaperRuntimeQmlEndpoint previewEndpoint(&host, nullptr);
        previewEndpoint.setRegistration(&previewRegistration);
        QVERIFY(!previewRegistration.isRegistered());
        QVERIFY(!previewEndpoint.isRegistered());
        QVERIFY(host.registry().snapshot().targets.isEmpty());
        QVERIFY(!host.service().isRegistered());

        TestRegistration registration;
        registration.setTargetItem(&activeWallpaper);
        registration.setContainmentContext(containmentContext(
            &activeWallpaper, &activeWallpaper, QRectF(window.screen()->geometry())));
        QVERIFY(registration.isRegistered());

        WallpaperRuntimeQmlEndpoint endpoint(&host, nullptr);
        endpoint.setWallpaperSource(QStringLiteral("workshop:8123"));
        endpoint.setWorkshopId(QStringLiteral("8123"));
        endpoint.setBackend(QStringLiteral("scene"));
        endpoint.setRegistration(&registration);

        QVERIFY(endpoint.isRegistered());
        QCOMPARE(endpoint.instanceId(), registration.instanceId());
        QVERIFY(host.service().isRegistered());
        const auto snapshot = host.registry().snapshot();
        QCOMPARE(snapshot.targets.size(), 1);
        QCOMPARE(snapshot.targets.first().instanceId.toString(QUuid::WithoutBraces),
                 registration.instanceId());
        QCOMPARE(snapshot.targets.first().effectivePlayback,
                 WallpaperRuntime::EffectivePlayback::Unknown);
        QCOMPARE(snapshot.targets.first().capabilities,
                 (QStringList{QStringLiteral("OpenConfiguration"),
                              QStringLiteral("SetGlobalAudioMuted"),
                              QStringLiteral("SetGlobalSizing")}));
        QCOMPARE(snapshot.targets.first().wallpaperSource, QStringLiteral("workshop:8123"));

        endpoint.setPlaybackRequestConnected(true);
        QCOMPARE(host.registry().snapshot().targets.first().capabilities,
                 (QStringList{QStringLiteral("SetPlayback"),
                              QStringLiteral("OpenConfiguration"),
                              QStringLiteral("SetGlobalAudioMuted"),
                              QStringLiteral("SetGlobalSizing")}));
        endpoint.setPlaybackRequestConnected(false);
        QCOMPARE(host.registry().snapshot().targets.first().capabilities,
                 (QStringList{QStringLiteral("OpenConfiguration"),
                              QStringLiteral("SetGlobalAudioMuted"),
                              QStringLiteral("SetGlobalSizing")}));
        endpoint.setReloadRequestConnected(true);
        QCOMPARE(host.registry().snapshot().targets.first().capabilities,
                 (QStringList{QStringLiteral("ReloadWallpaper"),
                              QStringLiteral("OpenConfiguration"),
                              QStringLiteral("SetGlobalAudioMuted"),
                              QStringLiteral("SetGlobalSizing")}));
        endpoint.setReloadRequestConnected(false);
        QCOMPARE(host.registry().snapshot().targets.first().capabilities,
                 (QStringList{QStringLiteral("OpenConfiguration"),
                              QStringLiteral("SetGlobalAudioMuted"),
                              QStringLiteral("SetGlobalSizing")}));
        endpoint.setNextRequestConnected(true);
        QCOMPARE(host.registry().snapshot().targets.first().capabilities,
                 (QStringList{QStringLiteral("OpenConfiguration"),
                              QStringLiteral("NextWallpaper"),
                              QStringLiteral("SetGlobalAudioMuted"),
                              QStringLiteral("SetGlobalSizing")}));
        endpoint.setNextRequestConnected(false);
        QCOMPARE(host.registry().snapshot().targets.first().capabilities,
                 (QStringList{QStringLiteral("OpenConfiguration"),
                              QStringLiteral("SetGlobalAudioMuted"),
                              QStringLiteral("SetGlobalSizing")}));

        auto invalidated = containmentContext(&activeWallpaper, &previewWallpaper,
                                               QRectF(window.screen()->geometry()));
        registration.setContainmentContext(invalidated);
        QVERIFY(!endpoint.isRegistered());
        QVERIFY(host.registry().snapshot().targets.isEmpty());
        QVERIFY(!host.service().isRegistered());

        registration.setContainmentContext(containmentContext(
            &activeWallpaper, &activeWallpaper, QRectF(window.screen()->geometry())));
        QVERIFY(endpoint.isRegistered());
        QCOMPARE(host.registry().snapshot().targets.size(), 1);
        endpoint.setRegistration(nullptr);
        QVERIFY(!endpoint.isRegistered());
        QVERIFY(host.registry().snapshot().targets.isEmpty());
        QVERIFY(!host.service().isRegistered());
    }

    void isolatedBusPlaybackIntentCompletesWithoutClaimingBackendState() {
        FakePatchBackend patchBackend;
        WallpaperRuntimeHost host(&patchBackend);
        QQuickWindow window;
        QQuickItem root;
        attachToWindow(root, window);

        TestRegistration registration;
        registration.setTargetItem(&root);
        registration.setContainmentContext(
            containmentContext(&root, &root, QRectF(window.screen()->geometry())));
        QVERIFY(registration.isRegistered());

        WallpaperRuntimeQmlEndpoint endpoint(&host, nullptr);
        endpoint.setRegistration(&registration);
        QVERIFY(endpoint.isRegistered());
        QSignalSpy playbackRequested(&endpoint, &WallpaperRuntimeQmlEndpoint::playbackRequested);
        endpoint.setPlaybackRequestConnected(true);
        QCOMPARE(host.registry().snapshot().targets.first().capabilities,
                 (QStringList{QStringLiteral("SetPlayback"),
                              QStringLiteral("OpenConfiguration"),
                              QStringLiteral("SetGlobalAudioMuted"),
                              QStringLiteral("SetGlobalSizing")}));
        QSignalSpy finished(&host.service(), &WallpaperRuntime::RuntimeService::CommandFinished);

        WallpaperRuntime::RuntimeClient client;
        QSignalSpy acknowledgements(&client,
            &WallpaperRuntime::RuntimeClient::commandAcknowledgement);
        QSignalSpy clientFinished(&client, &WallpaperRuntime::RuntimeClient::commandFinished);
        QTRY_VERIFY(client.available());
        client.refreshSnapshot();
        QTRY_VERIFY(!client.snapshot().isEmpty());

        const QString requestId = QStringLiteral("pause-request-1");
        const QString request = commandJson(host.registry().epoch(), requestId,
            QStringLiteral("SetPlayback"), QStringLiteral("targets"),
            QJsonArray{endpoint.instanceId()}, QJsonObject{{QStringLiteral("mode"),
                                                             QStringLiteral("paused")} });
        client.submitCommand(request);
        QTRY_COMPARE(acknowledgements.size(), 1);
        QTRY_COMPARE(playbackRequested.size(), 1);
        QCOMPARE(playbackRequested.first().first().toString(), QStringLiteral("paused"));
        QCOMPARE(host.registry().snapshot().targets.first().desiredPlayback,
                 WallpaperPlaybackPolicy::Override::Paused);
        QCOMPARE(host.registry().snapshot().targets.first().effectivePlayback,
                 WallpaperRuntime::EffectivePlayback::Unknown);

        QTRY_COMPARE(finished.size(), 1);
        QTRY_COMPARE(clientFinished.size(), 1);
        const QJsonObject completion = QJsonDocument::fromJson(
            clientFinished.first().first().toString().toUtf8()).object();
        QCOMPARE(completion.value(QStringLiteral("status")).toString(), QStringLiteral("applied"));
        QVERIFY(completion.value(QStringLiteral("message")).toString().contains(
            QStringLiteral("effective playback remains unknown")));
        QCOMPARE(completion.value(QStringLiteral("targets")).toArray().first().toObject()
            .value(QStringLiteral("instanceId")).toString(), endpoint.instanceId());
        QCOMPARE(host.registry().snapshot().targets.first().effectivePlayback,
                 WallpaperRuntime::EffectivePlayback::Unknown);

        QVERIFY(endpoint.reportPlaybackState(QStringLiteral("paused"), {QStringLiteral("manual")}));
        QCOMPARE(host.registry().snapshot().targets.first().effectivePlayback,
                 WallpaperRuntime::EffectivePlayback::Paused);
        QCOMPARE(host.registry().snapshot().targets.first().pauseReasons.size(), 1);
    }

    void injectedGlobalPatchAdapterIsUsedByService() {
        FakePatchBackend patchBackend;
        WallpaperRuntimeHost host(&patchBackend);
        QQuickWindow window;
        QQuickItem root;
        attachToWindow(root, window);
        TestRegistration registration;
        registration.setTargetItem(&root);
        registration.setContainmentContext(
            containmentContext(&root, &root, QRectF(window.screen()->geometry())));
        WallpaperRuntimeQmlEndpoint endpoint(&host, nullptr);
        endpoint.setRegistration(&registration);
        QVERIFY(host.service().isRegistered());

        WallpaperRuntime::RuntimeClient client;
        QSignalSpy acknowledgements(&client,
            &WallpaperRuntime::RuntimeClient::commandAcknowledgement);
        QSignalSpy completions(&client, &WallpaperRuntime::RuntimeClient::commandFinished);
        QTRY_VERIFY(client.available());
        const QString requestId = QStringLiteral("audio-patch-1");
        client.submitCommand(commandJson(host.registry().epoch(), requestId,
            QStringLiteral("SetGlobalAudioMuted"), QStringLiteral("allWallpaperTargets"),
            {}, QJsonObject{{QStringLiteral("muted"), true}}));
        QTRY_COMPARE(acknowledgements.size(), 1);
        QTRY_COMPARE(patchBackend.submitCount, 1);
        QTRY_COMPARE(completions.size(), 1);
        QCOMPARE(patchBackend.lastRequestId, requestId);
        QCOMPARE(patchBackend.lastPatch.value(QStringLiteral("AudioDisabled")).toBool(), true);
    }

    void globalCommandsRefreshSnapshotAndReportPersistenceErrors() {
        FakePatchBackend patchBackend;
        WallpaperRuntimeHost host(&patchBackend);
        QQuickWindow window;
        QQuickItem root;
        attachToWindow(root, window);
        TestRegistration registration;
        registration.setTargetItem(&root);
        registration.setContainmentContext(
            containmentContext(&root, &root, QRectF(window.screen()->geometry())));
        WallpaperRuntimeQmlEndpoint endpoint(&host, nullptr);
        endpoint.setRegistration(&registration);
        endpoint.setPlaybackRequestConnected(true);

        const auto initial = host.registry().snapshot();
        QCOMPARE(initial.global.value(QStringLiteral("muted")).toBool(), false);
        QCOMPARE(initial.global.value(QStringLiteral("sizingEnabled")).toBool(), false);
        QCOMPARE(initial.global.value(QStringLiteral("sizingMode")).toString(),
                 QStringLiteral("crop"));
        QCOMPARE(initial.targets.first().capabilities,
                 (QStringList{QStringLiteral("SetPlayback"),
                              QStringLiteral("OpenConfiguration"),
                              QStringLiteral("SetGlobalAudioMuted"),
                              QStringLiteral("SetGlobalSizing")}));

        QSignalSpy finished(&host.service(), &WallpaperRuntime::RuntimeService::CommandFinished);
        const auto submit = [&](const QString& id, const QString& command,
                                const QJsonObject& args) {
            return QJsonDocument::fromJson(host.service().Execute(commandJson(
                host.registry().epoch(), id, command, QStringLiteral("allWallpaperTargets"),
                {}, args)).toUtf8()).object();
        };

        const auto audioAck = submit(QStringLiteral("global-audio-1"),
            QStringLiteral("SetGlobalAudioMuted"), {{QStringLiteral("muted"), true}});
        QCOMPARE(audioAck.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(host.registry().snapshot().global.value(QStringLiteral("muted")).toBool(), true);

        const auto sizingAck = submit(QStringLiteral("global-sizing-1"),
            QStringLiteral("SetGlobalSizing"), {{QStringLiteral("enabled"), true},
                                                   {QStringLiteral("mode"), QStringLiteral("stretch")}});
        QCOMPARE(sizingAck.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_COMPARE(finished.size(), 2);
        const auto sized = host.registry().snapshot();
        QCOMPARE(sized.global.value(QStringLiteral("sizingEnabled")).toBool(), true);
        QCOMPARE(sized.global.value(QStringLiteral("sizingMode")).toString(),
                 QStringLiteral("stretch"));

        const quint64 revisionBeforeFailure = sized.revision;
        patchBackend.failNext = true;
        const auto failedAck = submit(QStringLiteral("global-audio-2"),
            QStringLiteral("SetGlobalAudioMuted"), {{QStringLiteral("muted"), false}});
        QCOMPARE(failedAck.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_COMPARE(finished.size(), 3);
        const QJsonObject failed = QJsonDocument::fromJson(
            finished.last().first().toString().toUtf8()).object();
        QCOMPARE(failed.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
        QCOMPARE(failed.value(QStringLiteral("errorCode")).toString(),
                 QStringLiteral("testBackendFailure"));
        const auto unchanged = host.registry().snapshot();
        QCOMPARE(unchanged.revision, revisionBeforeFailure);
        QCOMPARE(unchanged.global.value(QStringLiteral("muted")).toBool(), true);
        QCOMPARE(unchanged.targets.first().capabilities, sized.targets.first().capabilities);
    }

    void externalGlobalSyncReloadsIsolatedConfigAndUpdatesSnapshot() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString configPath = temp.filePath(QStringLiteral("global.json"));
        GlobalConfigBackend writer(configPath);
        GlobalConfigBackend reader(configPath);
        FakePatchBackend patchBackend;
        WallpaperRuntimeHost host(&patchBackend);
        QQuickWindow window;
        QQuickItem root;
        attachToWindow(root, window);
        TestRegistration registration;
        registration.setTargetItem(&root);
        registration.setContainmentContext(
            containmentContext(&root, &root, QRectF(window.screen()->geometry())));
        WallpaperRuntimeQmlEndpoint endpoint(&host, nullptr);
        endpoint.setRegistration(&registration);

        GlobalConfigRuntimeBridge bridge(&reader, QDBusConnection::sessionBus());
        connect(&reader, &GlobalConfigBackend::changed, &patchBackend, [&] {
            const QVariantMap all = reader.all();
            patchBackend.values.insert(QStringLiteral("AudioDisabled"), all.value(QStringLiteral("AudioDisabled")));
            patchBackend.values.insert(QStringLiteral("SizingEnabled"), all.value(QStringLiteral("SizingEnabled")));
            patchBackend.values.insert(QStringLiteral("SizingMode"), all.value(QStringLiteral("SizingMode")));
            emit patchBackend.valuesChanged();
        });
        const auto before = host.registry().snapshot();

        const QVariantMap patch{{QStringLiteral("AudioDisabled"), true},
            {QStringLiteral("SizingEnabled"), true}, {QStringLiteral("SizingMode"), 2}};
        const auto writeResult = writer.patchValues(patch, {});
        QCOMPARE(writeResult.status, GlobalConfigPatchStatus::Applied);
        QDBusMessage externalChange = QDBusMessage::createSignal(
            QStringLiteral("/WallpaperSync"),
            QStringLiteral("com.github.catsout.wallpaperEngineKde.Sync"),
            QStringLiteral("GlobalConfigChanged"));
        externalChange << QStringLiteral("external-config-writer");
        QVERIFY(QDBusConnection::sessionBus().send(externalChange));

        QTRY_COMPARE(host.registry().snapshot().global.value(QStringLiteral("muted")).toBool(), true);
        const auto refreshed = host.registry().snapshot();
        QCOMPARE(refreshed.global.value(QStringLiteral("sizingEnabled")).toBool(), true);
        QCOMPARE(refreshed.global.value(QStringLiteral("sizingMode")).toString(),
                 QStringLiteral("stretch"));
        QVERIFY(refreshed.revision > before.revision);
    }

    void destroyedPatchBackendRemovesGlobalCapabilities() {
        auto* patchBackend = new FakePatchBackend;
        WallpaperRuntimeHost host(patchBackend);
        QQuickWindow window;
        QQuickItem root;
        attachToWindow(root, window);
        TestRegistration registration;
        registration.setTargetItem(&root);
        registration.setContainmentContext(
            containmentContext(&root, &root, QRectF(window.screen()->geometry())));
        WallpaperRuntimeQmlEndpoint endpoint(&host, nullptr);
        endpoint.setRegistration(&registration);
        endpoint.setPlaybackRequestConnected(true);
        QCOMPARE(host.registry().snapshot().targets.first().capabilities.size(), 4);

        delete patchBackend;
        QTRY_COMPARE(host.registry().snapshot().targets.first().capabilities,
                     (QStringList{QStringLiteral("SetPlayback"),
                                  QStringLiteral("OpenConfiguration")}));
        QVERIFY(!host.supportsGlobalPatches());
    }

    void configurationCapabilityIsAbsentWithoutScriptTransport() {
        FakePatchBackend patchBackend;
        FakePlasmaScriptTransport transport(false);
        PlasmaConfigurationInvoker invoker(&transport);
        WallpaperRuntimeHost host(&patchBackend, QDBusConnection::sessionBus(), &invoker);
        QVERIFY(!host.capabilities(false, false, false, true)
            .contains(QStringLiteral("OpenConfiguration")));
    }

    void openConfigurationRuntimeCommandIsExactAndWaitsForHostRevalidation() {
        FakePatchBackend patchBackend;
        FakePlasmaScriptTransport transport(true);
        PlasmaConfigurationInvoker invoker(&transport);
        WallpaperRuntimeHost host(&patchBackend, QDBusConnection::sessionBus(), &invoker);

        QQuickWindow window;
        QQuickItem firstRoot;
        QQuickItem secondRoot;
        attachToWindow(firstRoot, window);
        attachToWindow(secondRoot, window);

        TestRegistration firstRegistration;
        firstRegistration.setTargetItem(&firstRoot);
        firstRegistration.setContainmentContext(
            containmentContext(&firstRoot, &firstRoot, QRectF(window.screen()->geometry())));
        QVERIFY(firstRegistration.isRegistered());
        WallpaperRuntimeQmlEndpoint firstEndpoint(&host, nullptr);
        firstEndpoint.setRegistration(&firstRegistration);
        QVERIFY(firstEndpoint.isRegistered());

        TestRegistration secondRegistration;
        secondRegistration.output = QStringLiteral("synthetic-output-2");
        secondRegistration.outputs = {secondRegistration.output};
        secondRegistration.setTargetItem(&secondRoot);
        secondRegistration.setContainmentContext(
            containmentContext(&secondRoot, &secondRoot, QRectF(window.screen()->geometry())));
        QVERIFY(secondRegistration.isRegistered());
        WallpaperRuntimeQmlEndpoint secondEndpoint(&host, nullptr);
        secondEndpoint.setRegistration(&secondRegistration);
        QVERIFY(secondEndpoint.isRegistered());

        FakeReloadEndpoint unsupportedEndpoint;
        WallpaperRuntime::TargetDescriptor unsupportedDescriptor;
        unsupportedDescriptor.containmentId = QStringLiteral("99");
        const auto unsupported = host.registry().registerTarget(unsupportedDescriptor,
            &unsupportedEndpoint);
        QVERIFY(unsupported.accepted);

        QSignalSpy finished(&host.service(), &WallpaperRuntime::RuntimeService::CommandFinished);
        const QUuid firstId = QUuid::fromString(firstRegistration.instanceId());
        const QUuid secondId = QUuid::fromString(secondRegistration.instanceId());
        auto submit = [&](const QString& requestId, const QJsonArray& ids) {
            return QJsonDocument::fromJson(host.service().Execute(commandJson(
                host.registry().epoch(), requestId, QStringLiteral("OpenConfiguration"),
                QStringLiteral("targets"), ids, QJsonObject{})).toUtf8()).object();
        };

        const auto wrong = submit(QStringLiteral("config-wrong"),
            QJsonArray{QUuid::createUuid().toString(QUuid::WithoutBraces)});
        QCOMPARE(wrong.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
        QCOMPARE(transport.requests.size(), 0);

        const auto multiple = submit(QStringLiteral("config-multiple"), QJsonArray{
            firstId.toString(QUuid::WithoutBraces), secondId.toString(QUuid::WithoutBraces)});
        QCOMPARE(multiple.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
        QCOMPARE(multiple.value(QStringLiteral("errorCode")).toString(), QStringLiteral("invalidScope"));
        QCOMPARE(transport.requests.size(), 0);

        const auto noEndpoint = submit(QStringLiteral("config-no-endpoint"),
            QJsonArray{unsupported.instanceId.toString(QUuid::WithoutBraces)});
        QCOMPARE(noEndpoint.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
        QCOMPARE(noEndpoint.value(QStringLiteral("errorCode")).toString(), QStringLiteral("unsupportedCommand"));
        QCOMPARE(transport.requests.size(), 0);

        const auto accepted = submit(QStringLiteral("config-valid"),
            QJsonArray{secondId.toString(QUuid::WithoutBraces)});
        QCOMPARE(accepted.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_COMPARE(transport.requests, QStringList{QStringLiteral("config-valid")});
        QCOMPARE(finished.size(), 0);
        const QString confirmed = QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("ok"), true}, {QStringLiteral("id"), QStringLiteral("31")},
            {QStringLiteral("activity"), QString::fromLatin1(ActivityId)},
            {QStringLiteral("plugin"), QString::fromLatin1(PluginId)},
            {QStringLiteral("screen"), 2}}).toJson(QJsonDocument::Compact));
        transport.reply(QStringLiteral("config-valid"), confirmed);
        QTRY_COMPARE(finished.size(), 1);
        auto result = QJsonDocument::fromJson(finished.takeFirst().first().toString().toUtf8()).object();
        QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("applied"));
        QCOMPARE(result.value(QStringLiteral("targets")).toArray().first().toObject()
            .value(QStringLiteral("instanceId")).toString(), secondId.toString(QUuid::WithoutBraces));

        const auto revalidationAccepted = submit(QStringLiteral("config-revalidation-failed"),
            QJsonArray{secondId.toString(QUuid::WithoutBraces)});
        QCOMPARE(revalidationAccepted.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_COMPARE(transport.requests.size(), 2);
        transport.reply(QStringLiteral("config-revalidation-failed"),
            QStringLiteral("{\"ok\":false,\"reason\":\"targetMismatch\"}"));
        QTRY_COMPARE(finished.size(), 1);
        result = QJsonDocument::fromJson(finished.takeFirst().first().toString().toUtf8()).object();
        QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
        QCOMPARE(result.value(QStringLiteral("errorCode")).toString(), QStringLiteral("targetMismatch"));
    }
};

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    WallpaperRuntimeHostTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_wallpaperruntimehost.moc"
