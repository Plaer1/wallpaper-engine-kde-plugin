#include "WallpaperRuntimeQmlClient.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusContext>
#include <QDBusMessage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QProcess>
#include <QSocketNotifier>
#include <QSignalSpy>
#include <QTest>

#include <unistd.h>

namespace {

constexpr auto RuntimeServiceName = "org.wekde.Runtime1";
constexpr auto RuntimeObjectPath = "/org/wekde/Runtime1";
constexpr auto RuntimeInterface = "org.wekde.Runtime1";
constexpr auto OriginalEpoch = "10000000-0000-4000-8000-000000000001";
constexpr auto ReplacementEpoch = "10000000-0000-4000-8000-000000000002";

QByteArray compactJson(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

void writeWorkerLine(const QByteArray &line)
{
    ::write(STDOUT_FILENO, line.constData(), size_t(line.size()));
    ::write(STDOUT_FILENO, "\n", 1);
}

class OwnerReplacementService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wekde.Runtime1")

public:
    OwnerReplacementService(QString epoch, bool delayedExecute, QObject *parent = nullptr)
        : QObject(parent)
        , m_epoch(std::move(epoch))
        , m_delayedExecute(delayedExecute)
    {
    }

public slots:
    QString GetSnapshot() const
    {
        return QString::fromUtf8(compactJson({
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("epoch"), m_epoch},
            {QStringLiteral("revision"), 1},
            {QStringLiteral("targets"), QJsonArray{}},
            {QStringLiteral("global"), QJsonObject{}},
        }));
    }

    QString Execute(const QString &requestJson)
    {
        const QJsonObject request = QJsonDocument::fromJson(requestJson.toUtf8()).object();
        const QString requestId = request.value(QStringLiteral("requestId")).toString();
        if (m_delayedExecute) {
            setDelayedReply(true);
            m_delayedCall = message();
            m_requestId = requestId;
            writeWorkerLine(compactJson({{QStringLiteral("event"), QStringLiteral("executeReceived")},
                {QStringLiteral("requestId"), requestId}}));
            return {};
        }
        return QString::fromUtf8(compactJson({
            {QStringLiteral("requestId"), requestId},
            {QStringLiteral("status"), QStringLiteral("rejected")},
            {QStringLiteral("errorCode"), QStringLiteral("unsupportedCommand")},
            {QStringLiteral("message"), QStringLiteral("Replacement fixture does not execute commands")},
        }));
    }

    void replyToOldRequest()
    {
        if (m_delayedCall.type() != QDBusMessage::MethodCallMessage) return;
        const QString acknowledgement = QString::fromUtf8(compactJson({
            {QStringLiteral("requestId"), m_requestId},
            {QStringLiteral("status"), QStringLiteral("accepted")},
            {QStringLiteral("errorCode"), QString()},
            {QStringLiteral("message"), QStringLiteral("Old owner acknowledgement")},
        }));
        const QString completion = QString::fromUtf8(compactJson({
            {QStringLiteral("requestId"), m_requestId},
            {QStringLiteral("status"), QStringLiteral("applied")},
            {QStringLiteral("errorCode"), QString()},
            {QStringLiteral("message"), QStringLiteral("Old owner completion")},
        }));
        QDBusConnection::sessionBus().send(m_delayedCall.createReply(QVariant(acknowledgement)));
        emit CommandFinished(completion);
        writeWorkerLine(compactJson({{QStringLiteral("event"), QStringLiteral("staleReplySent")},
            {QStringLiteral("requestId"), m_requestId}}));
    }

signals:
    void StateChanged(const QString &snapshotJson);
    void CommandFinished(const QString &resultJson);

private:
    QString m_epoch;
    bool m_delayedExecute = false;
    QDBusMessage m_delayedCall;
    QString m_requestId;
};

int runOwnerReplacementService(QCoreApplication &app, bool replacement)
{
    QDBusConnection connection = QDBusConnection::sessionBus();
    OwnerReplacementService service(replacement ? QString::fromLatin1(ReplacementEpoch)
                                                : QString::fromLatin1(OriginalEpoch),
        !replacement,
        &app);
    if (!connection.registerObject(RuntimeObjectPath, &service,
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
        return 2;
    }
    const auto queueOption = replacement
        ? QDBusConnectionInterface::ReplaceExistingService
        : QDBusConnectionInterface::DontQueueService;
    const auto replacementOption = replacement
        ? QDBusConnectionInterface::DontAllowReplacement
        : QDBusConnectionInterface::AllowReplacement;
    const auto registration = connection.interface()->registerService(
        QString::fromLatin1(RuntimeServiceName), queueOption, replacementOption);
    if (!registration.isValid()
        || registration.value() != QDBusConnectionInterface::ServiceRegistered) {
        return 3;
    }

    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read, &app);
    QByteArray buffer;
    QObject::connect(&input, &QSocketNotifier::activated, &app, [&] {
        char bytes[256];
        const ssize_t count = ::read(STDIN_FILENO, bytes, sizeof(bytes));
        if (count <= 0) {
            app.quit();
            return;
        }
        buffer.append(bytes, qsizetype(count));
        qsizetype newline = -1;
        while ((newline = buffer.indexOf('\n')) >= 0) {
            const QByteArray command = buffer.left(newline).trimmed();
            buffer.remove(0, newline + 1);
            if (command == QByteArrayLiteral("reply")) service.replyToOldRequest();
            if (command == QByteArrayLiteral("exit")) app.quit();
        }
    });
    app.exec();
    connection.unregisterService(RuntimeServiceName);
    connection.unregisterObject(RuntimeObjectPath);
    return 0;
}

} // namespace

class WallpaperRuntimeQmlClientTest : public QObject
{
    Q_OBJECT

private slots:
    void qmlFacadeObservesOwnerSnapshotAndAsyncCommandResults()
    {
        qmlRegisterType<wekde::WallpaperRuntimeQmlClient>(
            "RuntimeClientTest", 1, 0, "WallpaperRuntimeClient");
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"QML(
            import QtQml 2.0
            import RuntimeClientTest 1.0
            QtObject {
                id: root
                property bool ownerAvailable: false
                property int ownerChanges: 0
                property int snapshotSignals: 0
                property string ackStatus: ""
                property string ackError: ""
                property string ackErrorCode: ""
                property string completionStatus: ""
                property string completionRequestId: ""
                property var request: ({})
                property WallpaperRuntimeClient client: WallpaperRuntimeClient {
                    onOwnerChanged: (available) => {
                        root.ownerAvailable = available;
                        root.ownerChanges++;
                    }
                    onStateChanged: (snapshot) => root.snapshotSignals++
                    onCommandFinished: (result) => {
                        root.completionStatus = result.status;
                        root.completionRequestId = result.requestId;
                    }
                }
                readonly property bool available: client.available
                readonly property var snapshot: client.snapshot
                function send() {
                    client.execute(request).then(function(ack) {
                        root.ackStatus = ack.status;
                    }).catch(function(error) {
                        root.ackError = error.message;
                        root.ackErrorCode = error.errorCode;
                    });
                }
                function sendWhileUnavailable() {
                    client.execute({requestId: "offline"}).catch(function(error) {
                        root.ackError = error.message;
                        root.ackErrorCode = error.errorCode;
                    });
                }
            }
        )QML", QUrl(QStringLiteral("inmemory:/RuntimeClientTest.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.isReady() || component.isError(), 5000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QObject *root = component.create();
        QVERIFY2(root, qPrintable(component.errorString()));

        QCOMPARE(root->property("available").toBool(), false);
        QCOMPARE(root->property("snapshot").toMap(), QVariantMap{});
        QTRY_VERIFY(root->property("ownerChanges").toInt() >= 1);
        QVERIFY(QMetaObject::invokeMethod(root, "sendWhileUnavailable"));
        QTRY_COMPARE(root->property("ackErrorCode").toString(), QStringLiteral("serviceUnavailable"));

        QProcess service;
        service.setProgram(QString::fromUtf8(RUNTIME_SERVICE_WORKER_EXECUTABLE));
        service.setArguments({QStringLiteral("--runtime-service-worker")});
        service.start();
        QVERIFY(service.waitForStarted());

        QTRY_VERIFY_WITH_TIMEOUT(root->property("available").toBool(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!root->property("snapshot").toMap().isEmpty(), 5000);
        QVERIFY(root->property("ownerAvailable").toBool());
        QTRY_VERIFY(root->property("ownerChanges").toInt() >= 2);
        const QVariantMap snapshot = root->property("snapshot").toMap();
        QCOMPARE(snapshot.value(QStringLiteral("schemaVersion")).toInt(), 1);
        const QVariantList targets = snapshot.value(QStringLiteral("targets")).toList();
        QCOMPARE(targets.size(), 1);
        const QVariantMap target = targets.first().toMap();
        const QString instanceId = target.value(QStringLiteral("instanceId")).toString();
        QVERIFY(!instanceId.isEmpty());

        const QString requestId = QStringLiteral("qml-client-request-1");
        const QVariantMap request{
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("epoch"), snapshot.value(QStringLiteral("epoch"))},
            {QStringLiteral("requestId"), requestId},
            {QStringLiteral("scope"), QStringLiteral("targets")},
            {QStringLiteral("targetIds"), QStringList{instanceId}},
            {QStringLiteral("command"), QStringLiteral("SetPlayback")},
            {QStringLiteral("args"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("paused")}}},
        };
        root->setProperty("request", request);
        QVERIFY(QMetaObject::invokeMethod(root, "send"));
        QTRY_COMPARE_WITH_TIMEOUT(root->property("ackStatus").toString(),
            QStringLiteral("accepted"), 5000);

        service.write((QStringLiteral("complete ") + requestId + QLatin1Char('\n')).toUtf8());
        QVERIFY(service.waitForBytesWritten());
        QTRY_COMPARE_WITH_TIMEOUT(root->property("completionStatus").toString(),
            QStringLiteral("applied"), 5000);
        QCOMPARE(root->property("completionRequestId").toString(), requestId);

        service.write("exit\n");
        QVERIFY(service.waitForBytesWritten());
        QVERIFY(service.waitForFinished(5000));
        QTRY_VERIFY_WITH_TIMEOUT(!root->property("available").toBool(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(root->property("snapshot").toMap().isEmpty(), 5000);
        QVERIFY(!root->property("ownerAvailable").toBool());

        QProcess replacementService;
        replacementService.setProgram(QString::fromUtf8(RUNTIME_SERVICE_WORKER_EXECUTABLE));
        replacementService.setArguments({QStringLiteral("--runtime-service-worker")});
        replacementService.start();
        QVERIFY(replacementService.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(root->property("available").toBool(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!root->property("snapshot").toMap().isEmpty(), 5000);
        QVERIFY(root->property("ownerAvailable").toBool());
        QTRY_VERIFY(root->property("ownerChanges").toInt() >= 4);
        const QVariantMap replacementSnapshot = root->property("snapshot").toMap();
        QVERIFY(replacementSnapshot.value(QStringLiteral("epoch")).toString()
            != snapshot.value(QStringLiteral("epoch")).toString());
        replacementService.write("exit\n");
        QVERIFY(replacementService.waitForBytesWritten());
        QVERIFY(replacementService.waitForFinished(5000));

        delete root;
    }

    void replacementOwnerRejectsPendingAndIgnoresOldAckAndCompletion()
    {
        qmlRegisterType<wekde::WallpaperRuntimeQmlClient>(
            "RuntimeReplacementTest", 1, 0, "WallpaperRuntimeClient");
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"QML(
            import QtQml 2.0
            import RuntimeReplacementTest 1.0
            QtObject {
                id: root
                property int ownerChanges: 0
                property int ownerLossEvents: 0
                property string currentEpoch: ""
                property string ackStatus: ""
                property string ackError: ""
                property string ackErrorCode: ""
                property int completionCount: 0
                property var request: ({})
                property WallpaperRuntimeClient client: WallpaperRuntimeClient {
                    onOwnerChanged: (available) => {
                        root.ownerChanges++;
                        if (!available) root.ownerLossEvents++;
                    }
                    onStateChanged: (snapshot) => root.currentEpoch = snapshot.epoch || ""
                    onCommandFinished: (result) => root.completionCount++
                }
                function send() {
                    client.execute(request).then(function(ack) {
                        root.ackStatus = ack.status;
                    }).catch(function(error) {
                        root.ackError = error.message;
                        root.ackErrorCode = error.errorCode;
                    });
                }
            }
        )QML", QUrl(QStringLiteral("inmemory:/RuntimeReplacementTest.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.isReady() || component.isError(), 5000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        const QString workerPath = QString::fromUtf8(QCoreApplication::applicationFilePath().toUtf8());
        QProcess original;
        original.setProgram(workerPath);
        original.setArguments({QStringLiteral("--runtime-owner-replacement-worker")});
        original.start();
        QVERIFY(original.waitForStarted());
        QByteArray originalOutput;
        const auto collectOriginalOutput = [&] {
            originalOutput.append(original.readAllStandardOutput());
        };

        QObject *root = component.create();
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *client = root->findChild<wekde::WallpaperRuntimeQmlClient *>();
        QVERIFY(client);
        QSignalSpy acknowledgements(client, &wekde::WallpaperRuntimeQmlClient::commandAcknowledgement);
        QSignalSpy completions(client, &wekde::WallpaperRuntimeQmlClient::commandFinished);
        QTRY_VERIFY_WITH_TIMEOUT(root->property("currentEpoch").toString()
                == QString::fromLatin1(OriginalEpoch), 5000);

        const QString requestId = QStringLiteral("pending-across-owner-replacement");
        root->setProperty("request", QVariantMap{{QStringLiteral("requestId"), requestId}});
        QVERIFY(QMetaObject::invokeMethod(root, "send"));
        QTRY_VERIFY_WITH_TIMEOUT((collectOriginalOutput(), originalOutput.contains("executeReceived")),
            5000);
        const int ownerChangesBeforeReplacement = root->property("ownerChanges").toInt();
        const int ownerLossesBeforeReplacement = root->property("ownerLossEvents").toInt();

        QProcess replacement;
        replacement.setProgram(workerPath);
        replacement.setArguments({QStringLiteral("--runtime-owner-replacement-worker"),
            QStringLiteral("replace")});
        replacement.start();
        QVERIFY(replacement.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(root->property("ownerChanges").toInt()
                > ownerChangesBeforeReplacement, 5000);
        QCOMPARE(root->property("ownerLossEvents").toInt(), ownerLossesBeforeReplacement);
        QTRY_COMPARE_WITH_TIMEOUT(root->property("currentEpoch").toString(),
            QString::fromLatin1(ReplacementEpoch), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(root->property("ackErrorCode").toString(),
            QStringLiteral("serviceChanged"), 5000);
        QCOMPARE(root->property("ackStatus").toString(), QString());

        original.write("reply\n");
        QVERIFY(original.waitForBytesWritten());
        QTRY_VERIFY_WITH_TIMEOUT((collectOriginalOutput(), originalOutput.contains("staleReplySent")),
            5000);
        QTest::qWait(200);
        QCOMPARE(acknowledgements.count(), 0);
        QCOMPARE(completions.count(), 0);
        QCOMPARE(root->property("ackStatus").toString(), QString());
        QCOMPARE(root->property("ackErrorCode").toString(), QStringLiteral("serviceChanged"));
        QCOMPARE(root->property("currentEpoch").toString(), QString::fromLatin1(ReplacementEpoch));

        original.write("exit\n");
        replacement.write("exit\n");
        QVERIFY(original.waitForBytesWritten());
        QVERIFY(replacement.waitForBytesWritten());
        QVERIFY(original.waitForFinished(5000));
        QVERIFY(replacement.waitForFinished(5000));
        delete root;
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() > 1
        && app.arguments().at(1) == QStringLiteral("--runtime-owner-replacement-worker")) {
        return runOwnerReplacementService(app,
            app.arguments().size() > 2 && app.arguments().at(2) == QStringLiteral("replace"));
    }
    WallpaperRuntimeQmlClientTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_wallpaperruntimeqmlclient.moc"
