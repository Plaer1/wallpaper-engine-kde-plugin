#include "WallpaperRuntime.hpp"

#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDBusMessage>
#include <QProcess>
#include <QSocketNotifier>
#include <QSignalSpy>

#include <unistd.h>

#include <functional>

using namespace WallpaperPlaybackPolicy;
using namespace WallpaperRuntime;

int runtimeServiceWorker(QCoreApplication &app);
std::function<void(const QJsonObject &)> workerEventSink;

class FakeEndpoint : public PlaybackEndpoint
{
    Q_OBJECT

public:
    using PlaybackEndpoint::PlaybackEndpoint;

    void requestPlayback(Override desired) override
    {
        requests.append(desired);
        const auto callback = onRequest;
        if (callback) {
            callback();
        }
    }

    QVector<Override> requests;
    std::function<void()> onRequest;
};

class FakeGlobalPatchBackend : public GlobalPatchBackend
{
public:
    using GlobalPatchBackend::GlobalPatchBackend;

    QVariantMap currentValues(const QStringList &keys) const override
    {
        QVariantMap values;
        for (const QString &key : keys) {
            if (m_values.contains(key)) values.insert(key, m_values.value(key));
        }
        return values;
    }

    void submitPatch(const QString &requestId, const QVariantMap &patch,
        const QVariantMap &baseline) override
    {
        if (workerEventSink) {
            workerEventSink({{QStringLiteral("event"), QStringLiteral("patch")},
                {QStringLiteral("requestId"), requestId},
                {QStringLiteral("patch"), QJsonObject::fromVariantMap(patch)},
                {QStringLiteral("baseline"), QJsonObject::fromVariantMap(baseline)}});
        }
        if (completeSynchronously) {
            emit patchFinished(requestId, QStringLiteral("applied"), {}, QStringLiteral("Committed immediately"));
        }
    }

    bool completeSynchronously = false;

private:
    QVariantMap m_values{{QStringLiteral("AudioDisabled"), false},
        {QStringLiteral("SizingEnabled"), false}, {QStringLiteral("SizingMode"), 1}};
};

class WallpaperRuntimeTest : public QObject
{
    Q_OBJECT

    QString playbackRequest(const QUuid &epoch,
        const QString &requestId,
        const QVector<QUuid> &targets,
        const QString &mode,
        const QString &scope = QStringLiteral("targets")) const
    {
        QJsonArray targetArray;
        for (const QUuid &target : targets) {
            targetArray.append(target.toString(QUuid::WithoutBraces));
        }
        const QJsonObject request{{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("epoch"), epoch.toString(QUuid::WithoutBraces)},
            {QStringLiteral("requestId"), requestId},
            {QStringLiteral("scope"), scope},
            {QStringLiteral("targetIds"), targetArray},
            {QStringLiteral("command"), QStringLiteral("SetPlayback")},
            {QStringLiteral("args"), QJsonObject{{QStringLiteral("mode"), mode}}}};
        return QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));
    }

    QString playbackRequest(const Registry &registry,
        const QString &requestId,
        const QVector<QUuid> &targets,
        const QString &mode,
        const QString &scope = QStringLiteral("targets")) const
    {
        return playbackRequest(registry.epoch(), requestId, targets, mode, scope);
    }

    QString globalRequest(const QUuid &epoch, const QString &requestId,
        const QString &command, const QJsonObject &args,
        const QString &scope = QStringLiteral("allWallpaperTargets"),
        const QJsonArray &targets = {}) const
    {
        return QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("epoch"), epoch.toString(QUuid::WithoutBraces)},
            {QStringLiteral("requestId"), requestId},
            {QStringLiteral("scope"), scope},
            {QStringLiteral("targetIds"), targets},
            {QStringLiteral("command"), command},
            {QStringLiteral("args"), args}}).toJson(QJsonDocument::Compact));
    }

private slots:
    void realTargetsRegisterAndPreviewsDoNot()
    {
        Registry registry;
        FakeEndpoint first;
        FakeEndpoint preview;
        TargetDescriptor firstDescriptor;
        firstDescriptor.containmentId = QStringLiteral("42");
        firstDescriptor.activityId = QStringLiteral("activity-a");
        firstDescriptor.screenName = QStringLiteral("DP-1");
        firstDescriptor.screenLabel = QStringLiteral("Left");
        firstDescriptor.wallpaperSource = QStringLiteral("/wallpapers/one");
        firstDescriptor.title = QStringLiteral("One");
        firstDescriptor.workshopId = QStringLiteral("100");
        firstDescriptor.backend = QStringLiteral("video");
        firstDescriptor.capabilities = {QStringLiteral("SetPlayback")};

        const quint64 initialRevision = registry.revision();
        const Registration registered = registry.registerTarget(firstDescriptor, &first);
        const Registration previewRegistration = registry.registerTarget(
            firstDescriptor, &preview, true);

        QVERIFY(registered.accepted);
        QVERIFY(!registered.instanceId.isNull());
        QVERIFY(!previewRegistration.accepted);
        QVERIFY(previewRegistration.instanceId.isNull());
        QCOMPARE(registry.revision(), initialRevision + 1);

        const RuntimeSnapshot snapshot = registry.snapshot();
        QCOMPARE(snapshot.schemaVersion, 1);
        QCOMPARE(snapshot.epoch, registry.epoch());
        QCOMPARE(snapshot.targets.size(), 1);
        QCOMPARE(snapshot.targets.first().instanceId, registered.instanceId);
        QCOMPARE(snapshot.targets.first().containmentId, QStringLiteral("42"));
        QCOMPARE(snapshot.targets.first().activityId, QStringLiteral("activity-a"));
        QCOMPARE(snapshot.targets.first().screenName, QStringLiteral("DP-1"));
        QCOMPARE(snapshot.targets.first().effectivePlayback, WallpaperRuntime::EffectivePlayback::Unknown);
        QCOMPARE(snapshot.targets.first().backend, QStringLiteral("video"));
    }

    void targetResolutionAndDispatchUseRequestedInstancesOnce()
    {
        Registry registry;
        FakeEndpoint first;
        FakeEndpoint second;
        TargetDescriptor descriptor;
        descriptor.containmentId = QStringLiteral("1");
        const Registration firstTarget = registry.registerTarget(descriptor, &first);
        descriptor.containmentId = QStringLiteral("2");
        const Registration secondTarget = registry.registerTarget(descriptor, &second);

        const TargetSelection selected{false, {secondTarget.instanceId}};
        const auto resolved = registry.resolveTargets(registry.epoch(), selected);
        QCOMPARE(resolved.error, ResolveError::None);
        QCOMPARE(resolved.targetIds, QVector<QUuid>{secondTarget.instanceId});

        const DispatchResult selectedResult = registry.setPlayback(
            registry.epoch(), selected, Override::Paused);
        QCOMPARE(selectedResult.error, ResolveError::None);
        QCOMPARE(selectedResult.targets.size(), 1);
        QVERIFY(selectedResult.targets.first().dispatched);
        QCOMPARE(first.requests.size(), 0);
        QCOMPARE(second.requests, QVector<Override>{Override::Paused});

        const TargetSelection all{true, {}};
        const DispatchResult allResult = registry.setPlayback(
            registry.epoch(), all, Override::Automatic);
        QCOMPARE(allResult.error, ResolveError::None);
        QCOMPARE(allResult.targets.size(), 2);
        QCOMPARE(first.requests, QVector<Override>{Override::Automatic});
        QCOMPARE(second.requests,
            (QVector<Override>{Override::Paused, Override::Automatic}));
        QCOMPARE(registry.snapshot().targets.size(), 2);
        QCOMPARE(registry.snapshot().targets.first().effectivePlayback,
            WallpaperRuntime::EffectivePlayback::Unknown);
        QVERIFY(!firstTarget.instanceId.isNull());
    }

    void staleEpochAndInstanceAreRejected()
    {
        Registry registry;
        FakeEndpoint endpoint;
        TargetDescriptor descriptor;
        descriptor.containmentId = QStringLiteral("7");
        const Registration target = registry.registerTarget(descriptor, &endpoint);

        const TargetSelection selection{false, {target.instanceId}};
        const auto staleEpoch = registry.setPlayback(
            QUuid::createUuid(), selection, Override::Paused);
        QCOMPARE(staleEpoch.error, ResolveError::StaleEpoch);
        QCOMPARE(endpoint.requests.size(), 0);

        const TargetSelection staleInstance{false, {QUuid::createUuid()}};
        const auto unknown = registry.setPlayback(
            registry.epoch(), staleInstance, Override::Paused);
        QCOMPARE(unknown.error, ResolveError::UnknownTarget);
        QCOMPARE(endpoint.requests.size(), 0);
    }

    void destructionRemovesTargetAndMakesInstanceStale()
    {
        Registry registry;
        auto *endpoint = new FakeEndpoint;
        TargetDescriptor descriptor;
        descriptor.containmentId = QStringLiteral("9");
        const Registration target = registry.registerTarget(descriptor, endpoint);
        const quint64 beforeDestroy = registry.revision();
        QCOMPARE(registry.snapshot().targets.size(), 1);

        delete endpoint;

        QCOMPARE(registry.revision(), beforeDestroy + 1);
        QVERIFY(registry.snapshot().targets.isEmpty());
        const auto stale = registry.resolveTargets(
            registry.epoch(), TargetSelection{false, {target.instanceId}});
        QCOMPARE(stale.error, ResolveError::UnknownTarget);
    }

    void outputMappingAndAcknowledgedStateUpdateSnapshot()
    {
        Registry registry;
        FakeEndpoint endpoint;
        TargetDescriptor descriptor;
        descriptor.containmentId = QStringLiteral("13");
        descriptor.screenName = QStringLiteral("DP-1");
        const Registration target = registry.registerTarget(descriptor, &endpoint);
        const quint64 beforeUpdate = registry.revision();

        QVERIFY(registry.updateOutput(target.instanceId, QStringLiteral("DP-2"), QStringLiteral("Right")));
        QVERIFY(registry.confirmPlayback(target.instanceId, WallpaperRuntime::EffectivePlayback::Paused));
        QCOMPARE(registry.revision(), beforeUpdate + 2);

        const TargetSnapshot result = registry.snapshot().targets.first();
        QCOMPARE(result.screenName, QStringLiteral("DP-2"));
        QCOMPARE(result.screenLabel, QStringLiteral("Right"));
        QCOMPARE(result.effectivePlayback, WallpaperRuntime::EffectivePlayback::Paused);
    }

    void repeatedIntentDispatchDoesNotAdvanceSnapshotRevision()
    {
        Registry registry;
        FakeEndpoint endpoint;
        TargetDescriptor descriptor;
        descriptor.containmentId = QStringLiteral("22");
        const Registration target = registry.registerTarget(descriptor, &endpoint);
        const TargetSelection selection{false, {target.instanceId}};

        const DispatchResult first = registry.setPlayback(
            registry.epoch(), selection, Override::Paused);
        QCOMPARE(first.error, ResolveError::None);
        QCOMPARE(registry.revision(), quint64(2));
        QCOMPARE(endpoint.requests.size(), 1);

        const DispatchResult duplicate = registry.setPlayback(
            registry.epoch(), selection, Override::Paused);
        QCOMPARE(duplicate.error, ResolveError::None);
        QCOMPARE(duplicate.targets.size(), 1);
        QVERIFY(duplicate.targets.first().dispatched);
        QCOMPARE(endpoint.requests.size(), 2); // Reissued in case the backend did not act on the first request.
        QCOMPARE(registry.revision(), quint64(2));
    }

    void reentrantAndDisappearingEndpointsAreNotDispatchedAgain()
    {
        Registry registry;
        auto *first = new FakeEndpoint;
        auto *second = new FakeEndpoint;
        TargetDescriptor descriptor;
        descriptor.containmentId = QStringLiteral("31");
        const Registration firstTarget = registry.registerTarget(descriptor, first);
        descriptor.containmentId = QStringLiteral("32");
        const Registration secondTarget = registry.registerTarget(descriptor, second);

        const TargetSelection all{true, {}};
        DispatchResult nestedResult;
        first->onRequest = [&] {
            nestedResult = registry.setPlayback(
                registry.epoch(), all, Override::Automatic);
            delete second;
            second = nullptr;
            delete first;
            first = nullptr;
        };

        const DispatchResult result = registry.setPlayback(
            registry.epoch(), all, Override::Paused);
        QCOMPARE(nestedResult.error, ResolveError::ReentrantDispatch);
        QCOMPARE(result.error, ResolveError::None);
        QCOMPARE(result.targets.size(), 2);
        QCOMPARE(result.targets.at(0).instanceId, firstTarget.instanceId);
        QVERIFY(result.targets.at(0).dispatched);
        QCOMPARE(result.targets.at(1).instanceId, secondTarget.instanceId);
        QVERIFY(!result.targets.at(1).dispatched);
        QVERIFY(registry.snapshot().targets.isEmpty());
        QCOMPARE(registry.revision(), quint64(5));
    }

    void dbusClientGetsSnapshotAcknowledgementCompletionAndOwnerChanges()
    {
        RuntimeClient client;
        QSignalSpy availability(&client, &RuntimeClient::availabilityChanged);
        QSignalSpy acknowledgements(&client, &RuntimeClient::commandAcknowledgement);
        QSignalSpy completions(&client, &RuntimeClient::commandFinished);

        QProcess serviceProcess;
        serviceProcess.setProgram(QCoreApplication::applicationFilePath());
        serviceProcess.setArguments({QStringLiteral("--runtime-service-worker")});
        serviceProcess.start();
        QVERIFY(serviceProcess.waitForStarted());

        QByteArray serviceOutput;
        QVector<QJsonObject> serviceEvents;
        const auto collectServiceOutput = [&] {
            serviceOutput.append(serviceProcess.readAllStandardOutput());
            qsizetype newline = -1;
            while ((newline = serviceOutput.indexOf('\n')) >= 0) {
                const QByteArray line = serviceOutput.left(newline);
                serviceOutput.remove(0, newline + 1);
                const QJsonDocument message = QJsonDocument::fromJson(line);
                if (message.isObject()) {
                    serviceEvents.append(message.object());
                }
            }
        };

        QTRY_VERIFY_WITH_TIMEOUT(client.available(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!client.snapshot().isEmpty(), 5000);
        QVERIFY(availability.count() >= 1);
        QTRY_VERIFY_WITH_TIMEOUT((collectServiceOutput(), !serviceEvents.isEmpty()), 5000);
        QCOMPARE(serviceEvents.first().value(QStringLiteral("event")).toString(), QStringLiteral("ready"));
        const QUuid serviceEpoch = QUuid::fromString(
            serviceEvents.first().value(QStringLiteral("epoch")).toString());
        const QUuid targetId = QUuid::fromString(
            serviceEvents.first().value(QStringLiteral("targetId")).toString());
        QVERIFY(!serviceEpoch.isNull());
        QVERIFY(!targetId.isNull());

        const QJsonObject snapshot = QJsonDocument::fromJson(client.snapshot().toUtf8()).object();
        QCOMPARE(snapshot.value(QStringLiteral("schemaVersion")).toInt(), 1);
        QCOMPARE(snapshot.value(QStringLiteral("epoch")).toString(),
            serviceEpoch.toString(QUuid::WithoutBraces));
        QCOMPARE(snapshot.value(QStringLiteral("targets")).toArray().size(), 1);

        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        client.submitCommand(playbackRequest(
            serviceEpoch, requestId, {targetId}, QStringLiteral("paused")));
        QTRY_COMPARE_WITH_TIMEOUT(acknowledgements.count(), 1, 5000);
        const QJsonObject acknowledgement =
            QJsonDocument::fromJson(acknowledgements.takeFirst().at(0).toString().toUtf8()).object();
        QCOMPARE(acknowledgement.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QCOMPARE(acknowledgement.value(QStringLiteral("requestId")).toString(), requestId);
        QCOMPARE(completions.count(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(
            QJsonDocument::fromJson(client.snapshot().toUtf8()).object()
                    .value(QStringLiteral("targets")).toArray().first().toObject()
                    .value(QStringLiteral("desiredPlayback")).toString()
                == QStringLiteral("paused"),
            5000);
        QTRY_VERIFY_WITH_TIMEOUT((collectServiceOutput(), serviceEvents.size() >= 2), 5000);
        QCOMPARE(serviceEvents.at(1).value(QStringLiteral("event")).toString(), QStringLiteral("playback"));
        QCOMPARE(serviceEvents.at(1).value(QStringLiteral("mode")).toString(), QStringLiteral("paused"));

        serviceProcess.write((QStringLiteral("complete ") + requestId + QLatin1Char('\n')).toUtf8());
        QVERIFY(serviceProcess.waitForBytesWritten());
        QTRY_COMPARE_WITH_TIMEOUT(completions.count(), 1, 5000);
        const QJsonObject completion =
            QJsonDocument::fromJson(completions.takeFirst().at(0).toString().toUtf8()).object();
        QCOMPARE(completion.value(QStringLiteral("status")).toString(), QStringLiteral("applied"));

        serviceProcess.write("exit\n");
        QVERIFY(serviceProcess.waitForBytesWritten());
        QVERIFY(serviceProcess.waitForFinished(5000));
        QTRY_VERIFY_WITH_TIMEOUT(!client.available(), 5000);
        QVERIFY(client.snapshot().isEmpty());

        QProcess replacementService;
        replacementService.setProgram(QCoreApplication::applicationFilePath());
        replacementService.setArguments({QStringLiteral("--runtime-service-worker")});
        replacementService.start();
        QVERIFY(replacementService.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(client.available(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!client.snapshot().isEmpty(), 5000);
        const QJsonObject replacementSnapshot =
            QJsonDocument::fromJson(client.snapshot().toUtf8()).object();
        QVERIFY(replacementSnapshot.value(QStringLiteral("epoch")).toString()
            != snapshot.value(QStringLiteral("epoch")).toString());
        replacementService.write("exit\n");
        QVERIFY(replacementService.waitForBytesWritten());
        QVERIFY(replacementService.waitForFinished(5000));
    }

    void malformedStaleOversizedAndUnsupportedCommandsDoNotMutate()
    {
        Registry registry;
        RuntimeService service(registry);
        FakeEndpoint endpoint;
        TargetDescriptor descriptor;
        descriptor.containmentId = QStringLiteral("61");
        const Registration target = registry.registerTarget(descriptor, &endpoint);
        QVERIFY(service.isRegistered());
        const quint64 before = registry.revision();

        QJsonObject unsupported = QJsonDocument::fromJson(playbackRequest(
            registry, QStringLiteral("unsupported"), {target.instanceId}, QStringLiteral("paused"))
                .toUtf8()).object();
        unsupported.insert(QStringLiteral("command"), QStringLiteral("Quit"));
        const QString unsupportedJson = QString::fromUtf8(
            QJsonDocument(unsupported).toJson(QJsonDocument::Compact));
        const QJsonObject unsupportedResult =
            QJsonDocument::fromJson(service.Execute(unsupportedJson).toUtf8()).object();
        QCOMPARE(unsupportedResult.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
        QCOMPARE(unsupportedResult.value(QStringLiteral("errorCode")).toString(),
            QStringLiteral("unsupportedCommand"));

        QJsonObject badType = unsupported;
        badType.insert(QStringLiteral("command"), QStringLiteral("SetPlayback"));
        badType.insert(QStringLiteral("schemaVersion"), QStringLiteral("1"));
        const QString badTypeJson = QString::fromUtf8(
            QJsonDocument(badType).toJson(QJsonDocument::Compact));
        const QJsonObject badTypeResult =
            QJsonDocument::fromJson(service.Execute(badTypeJson).toUtf8()).object();
        QCOMPARE(badTypeResult.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));

        QJsonObject stale = QJsonDocument::fromJson(playbackRequest(
            registry, QStringLiteral("stale"), {target.instanceId}, QStringLiteral("paused"))
                .toUtf8()).object();
        stale.insert(QStringLiteral("epoch"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString staleJson = QString::fromUtf8(
            QJsonDocument(stale).toJson(QJsonDocument::Compact));
        const QJsonObject staleResult =
            QJsonDocument::fromJson(service.Execute(staleJson).toUtf8()).object();
        QCOMPARE(staleResult.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
        QCOMPARE(staleResult.value(QStringLiteral("errorCode")).toString(), QStringLiteral("staleEpoch"));

        QCOMPARE(QJsonDocument::fromJson(service.Execute(QStringLiteral("not-json")).toUtf8())
                     .object().value(QStringLiteral("errorCode")).toString(), QStringLiteral("invalidJson"));
        const QString oversized(RuntimeService::MaximumMessageBytes + 1, QLatin1Char(' '));
        QCOMPARE(QJsonDocument::fromJson(service.Execute(oversized).toUtf8())
                     .object().value(QStringLiteral("errorCode")).toString(), QStringLiteral("messageTooLarge"));

        QCOMPARE(endpoint.requests.size(), 0);
        QCOMPARE(registry.revision(), before);
        QCOMPARE(registry.snapshot().targets.first().desiredPlayback, Override::Automatic);
    }

    void queuedGlobalPatchReportsBackendDestroyedBeforeDispatch()
    {
        Registry registry;
        auto *backend = new FakeGlobalPatchBackend;
        RuntimeService service(registry, backend);
        FakeEndpoint endpoint;
        TargetDescriptor descriptor;
        descriptor.containmentId = QStringLiteral("detached-backend");
        QVERIFY(registry.registerTarget(descriptor, &endpoint).accepted);
        QSignalSpy completions(&service, &RuntimeService::CommandFinished);
        const QString requestId = QStringLiteral("backend-destroyed");
        const QJsonObject accepted = QJsonDocument::fromJson(service.Execute(globalRequest(
            registry.epoch(), requestId, QStringLiteral("SetGlobalAudioMuted"),
            {{QStringLiteral("muted"), true}})).toUtf8()).object();
        QCOMPARE(accepted.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        delete backend;
        QTRY_COMPARE_WITH_TIMEOUT(completions.count(), 1, 1000);
        const QJsonObject result = QJsonDocument::fromJson(completions.first().at(0).toString().toUtf8()).object();
        QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
        QCOMPARE(result.value(QStringLiteral("errorCode")).toString(), QStringLiteral("serviceUnavailable"));
    }

    void dbusGlobalPatchesValidateAndReportBackendResults()
    {
        RuntimeClient client;
        QSignalSpy acknowledgements(&client, &RuntimeClient::commandAcknowledgement);
        QSignalSpy completions(&client, &RuntimeClient::commandFinished);
        QStringList completionOrder;
        connect(&client, &RuntimeClient::commandAcknowledgement, &client,
            [&completionOrder] { completionOrder.append(QStringLiteral("ack")); });
        connect(&client, &RuntimeClient::commandFinished, &client,
            [&completionOrder] { completionOrder.append(QStringLiteral("finished")); });
        QProcess process;
        process.setProgram(QCoreApplication::applicationFilePath());
        process.setArguments({QStringLiteral("--runtime-service-worker")});
        process.start();
        QVERIFY(process.waitForStarted());
        QByteArray output;
        QVector<QJsonObject> events;
        const auto collect = [&] {
            output.append(process.readAllStandardOutput());
            qsizetype newline = -1;
            while ((newline = output.indexOf('\n')) >= 0) {
                const QJsonDocument doc = QJsonDocument::fromJson(output.left(newline));
                output.remove(0, newline + 1);
                if (doc.isObject()) events.append(doc.object());
            }
        };
        QTRY_VERIFY_WITH_TIMEOUT(client.available(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!client.snapshot().isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT((collect(), !events.isEmpty()), 5000);
        const QUuid epoch = QUuid::fromString(events.first().value(QStringLiteral("epoch")).toString());
        QVERIFY(!epoch.isNull());

        const auto send = [&](const QString &command, const QJsonObject &args,
                              const QString &scope = QStringLiteral("allWallpaperTargets")) {
            client.submitCommand(globalRequest(epoch, QUuid::createUuid().toString(QUuid::WithoutBraces),
                command, args, scope));
        };
        send(QStringLiteral("SetGlobalAudioMuted"), {{QStringLiteral("muted"), true}}, QStringLiteral("targets"));
        QTRY_COMPARE_WITH_TIMEOUT(acknowledgements.count(), 1, 5000);
        QCOMPARE(QJsonDocument::fromJson(acknowledgements.takeFirst().at(0).toString().toUtf8())
                     .object().value(QStringLiteral("errorCode")).toString(), QStringLiteral("invalidScope"));
        send(QStringLiteral("SetGlobalAudioMuted"), {{QStringLiteral("muted"), QStringLiteral("yes")}});
        QTRY_COMPARE_WITH_TIMEOUT(acknowledgements.count(), 1, 5000);
        QCOMPARE(QJsonDocument::fromJson(acknowledgements.takeFirst().at(0).toString().toUtf8())
                     .object().value(QStringLiteral("errorCode")).toString(), QStringLiteral("invalidArgument"));
        send(QStringLiteral("SetGlobalSizing"), {{QStringLiteral("enabled"), true},
            {QStringLiteral("mode"), QStringLiteral("stretch")}, {QStringLiteral("extra"), 1}});
        QTRY_COMPARE_WITH_TIMEOUT(acknowledgements.count(), 1, 5000);
        QCOMPARE(QJsonDocument::fromJson(acknowledgements.takeFirst().at(0).toString().toUtf8())
                     .object().value(QStringLiteral("errorCode")).toString(), QStringLiteral("invalidArgument"));
        QTRY_VERIFY_WITH_TIMEOUT((collect(), events.size() == 1), 250);

        const QString audioId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        client.submitCommand(globalRequest(epoch, audioId, QStringLiteral("SetGlobalAudioMuted"),
            {{QStringLiteral("muted"), true}}));
        QTRY_COMPARE_WITH_TIMEOUT(acknowledgements.count(), 1, 5000);
        QCOMPARE(QJsonDocument::fromJson(acknowledgements.takeFirst().at(0).toString().toUtf8())
                     .object().value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_VERIFY_WITH_TIMEOUT((collect(), events.size() >= 2), 5000);
        const QJsonObject audioEvent = events.at(1);
        QCOMPARE(audioEvent.value(QStringLiteral("patch")).toObject()
                     .value(QStringLiteral("AudioDisabled")).toBool(), true);
        QCOMPARE(audioEvent.value(QStringLiteral("baseline")).toObject()
                     .value(QStringLiteral("AudioDisabled")).toBool(), false);
        process.write((QStringLiteral("patch ") + audioId + QStringLiteral(" applied -\n")).toUtf8());
        QVERIFY(process.waitForBytesWritten());
        QTRY_COMPARE_WITH_TIMEOUT(completions.count(), 1, 5000);
        QCOMPARE(QJsonDocument::fromJson(completions.takeFirst().at(0).toString().toUtf8())
                     .object().value(QStringLiteral("status")).toString(), QStringLiteral("applied"));

        const QString sizingId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        client.submitCommand(globalRequest(epoch, sizingId, QStringLiteral("SetGlobalSizing"),
            {{QStringLiteral("enabled"), true}, {QStringLiteral("mode"), QStringLiteral("crop")}}));
        QTRY_COMPARE_WITH_TIMEOUT(acknowledgements.count(), 1, 5000);
        QCOMPARE(QJsonDocument::fromJson(acknowledgements.takeFirst().at(0).toString().toUtf8())
                     .object().value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_VERIFY_WITH_TIMEOUT((collect(), events.size() >= 3), 5000);
        const QJsonObject sizingEvent = events.at(2).value(QStringLiteral("patch")).toObject();
        QCOMPARE(sizingEvent.value(QStringLiteral("SizingEnabled")).toBool(), true);
        QCOMPARE(sizingEvent.value(QStringLiteral("SizingMode")).toInt(), 1);
        process.write((QStringLiteral("patch ") + sizingId + QStringLiteral(" rejected busy\n")).toUtf8());
        QVERIFY(process.waitForBytesWritten());
        QTRY_COMPARE_WITH_TIMEOUT(completions.count(), 1, 5000);
        const QJsonObject failed = QJsonDocument::fromJson(completions.takeFirst().at(0).toString().toUtf8()).object();
        QCOMPARE(failed.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
        QCOMPARE(failed.value(QStringLiteral("errorCode")).toString(), QStringLiteral("busy"));
        process.write("sync\n");
        QVERIFY(process.waitForBytesWritten());
        const QString syncId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        completionOrder.clear();
        client.submitCommand(globalRequest(epoch, syncId, QStringLiteral("SetGlobalAudioMuted"),
            {{QStringLiteral("muted"), false}}));
        const QString earlyResult = QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("requestId"), syncId}, {QStringLiteral("status"), QStringLiteral("applied")},
            {QStringLiteral("errorCode"), QString()}, {QStringLiteral("message"), QStringLiteral("Immediate result")}})
            .toJson(QJsonDocument::Compact));
        // A default message models a direct in-process early completion; real
        // D-Bus deliveries carry the unique sender and are generation-checked.
        const QDBusMessage inProcessMessage;
        QVERIFY(QMetaObject::invokeMethod(&client, "serviceCommandFinished", Qt::DirectConnection,
            Q_ARG(QString, earlyResult), Q_ARG(QDBusMessage, inProcessMessage)));
        QCOMPARE(completions.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(acknowledgements.count(), 1, 5000);
        QCOMPARE(QJsonDocument::fromJson(acknowledgements.takeFirst().at(0).toString().toUtf8())
                     .object().value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QTRY_COMPARE_WITH_TIMEOUT(completions.count(), 1, 5000);
        QCOMPARE(QJsonDocument::fromJson(completions.takeFirst().at(0).toString().toUtf8())
                     .object().value(QStringLiteral("status")).toString(), QStringLiteral("applied"));
        QCOMPARE(completionOrder, (QStringList{QStringLiteral("ack"), QStringLiteral("finished")}));
        process.write("exit\n");
        QVERIFY(process.waitForBytesWritten());
        QVERIFY(process.waitForFinished(5000));
        QTRY_VERIFY_WITH_TIMEOUT(!client.available(), 5000);
    }
};

int runtimeServiceWorker(QCoreApplication &app)
{
    const auto writeLine = [](const QByteArray &line) {
        ::write(STDOUT_FILENO, line.constData(), size_t(line.size()));
        ::write(STDOUT_FILENO, "\n", 1);
    };

    workerEventSink = [writeLine](const QJsonObject &event) {
        writeLine(QJsonDocument(event).toJson(QJsonDocument::Compact));
    };
    Registry registry;
    FakeGlobalPatchBackend globalBackend;
    RuntimeService service(registry, &globalBackend);
    auto *endpoint = new FakeEndpoint(&app);
    endpoint->onRequest = [endpoint, writeLine] {
        const QString mode = endpoint->requests.isEmpty()
            ? QStringLiteral("unknown")
            : endpoint->requests.last() == Override::Paused ? QStringLiteral("paused")
            : endpoint->requests.last() == Override::Automatic ? QStringLiteral("automatic")
                                                               : QStringLiteral("playDespiteWindows");
        writeLine(QJsonDocument(QJsonObject{{QStringLiteral("event"), QStringLiteral("playback")},
                        {QStringLiteral("mode"), mode}})
                      .toJson(QJsonDocument::Compact));
    };
    TargetDescriptor descriptor;
    descriptor.containmentId = QStringLiteral("700");
    const Registration target = registry.registerTarget(descriptor, endpoint);
    if (!target.accepted || !service.isRegistered()) {
        return 2;
    }
    writeLine(QJsonDocument(QJsonObject{{QStringLiteral("event"), QStringLiteral("ready")},
                    {QStringLiteral("epoch"), registry.epoch().toString(QUuid::WithoutBraces)},
                    {QStringLiteral("targetId"), target.instanceId.toString(QUuid::WithoutBraces)}})
                  .toJson(QJsonDocument::Compact));

    QSocketNotifier inputNotifier(STDIN_FILENO, QSocketNotifier::Read, &app);
    QByteArray inputBuffer;
    QObject::connect(&inputNotifier, &QSocketNotifier::activated, &app, [&] {
        char buffer[512];
        const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count <= 0) {
            app.quit();
            return;
        }
        inputBuffer.append(buffer, qsizetype(count));
        qsizetype newline = -1;
        while ((newline = inputBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = inputBuffer.left(newline).trimmed();
            inputBuffer.remove(0, newline + 1);
            const QList<QByteArray> fields = line.split(' ');
            if (fields.value(0) == QByteArrayLiteral("exit")) {
                app.quit();
                return;
            }
            if (fields.value(0) == QByteArrayLiteral("sync")) {
                globalBackend.completeSynchronously = true;
            }
            if (fields.size() == 2 && fields.first() == QByteArrayLiteral("complete")) {
                QJsonArray targets;
                targets.append(QJsonObject{{QStringLiteral("instanceId"),
                    target.instanceId.toString(QUuid::WithoutBraces)},
                    {QStringLiteral("status"), QStringLiteral("applied")}});
                service.completeCommand(QString::fromUtf8(fields.at(1)),
                    QStringLiteral("applied"), {}, {}, targets);
            }
            if (fields.size() >= 3 && fields.first() == QByteArrayLiteral("patch")) {
                const QString status = QString::fromUtf8(fields.at(2));
                const QString errorCode = fields.size() > 3 && fields.at(3) != QByteArrayLiteral("-")
                    ? QString::fromUtf8(fields.at(3)) : QString();
                emit globalBackend.patchFinished(QString::fromUtf8(fields.at(1)), status,
                    errorCode, status == QStringLiteral("applied")
                        ? QStringLiteral("Committed") : QStringLiteral("Patch failed"));
            }
        }
    });
    const int result = app.exec();
    delete endpoint;
    return result;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() > 1
        && app.arguments().at(1) == QStringLiteral("--runtime-service-worker")) {
        return runtimeServiceWorker(app);
    }
    WallpaperRuntimeTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_wallpaperruntime.moc"
