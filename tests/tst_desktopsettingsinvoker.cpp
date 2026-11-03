#include "DesktopSettingsInvoker.hpp"
#include "PlasmaConfigurationInvoker.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

class FakePlasmaScriptTransport final : public wekde::PlasmaScriptTransport {
    Q_OBJECT
public:
    using PlasmaScriptTransport::PlasmaScriptTransport;

    bool isAvailable = true;
    QString requestId;
    QString script;

    bool available() const override { return isAvailable; }
    void evaluate(const QString& id, const QString& js) override {
        requestId = id;
        script = js;
    }
    void complete(const QJsonObject& result) {
        emit evaluated(requestId, true,
            QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)), {});
    }
};

class DesktopSettingsInvokerTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void openUsesPlasmaDesktopConfigurationApi() {
        FakePlasmaScriptTransport transport;
        wekde::DesktopSettingsInvoker invoker(&transport);
        QSignalSpy finished(&invoker, &wekde::DesktopSettingsInvoker::finished);

        QVERIFY(invoker.open());
        QVERIFY(invoker.pending());
        QVERIFY(!transport.requestId.isEmpty());
        QVERIFY(transport.script.contains(QStringLiteral("desktopForScreen(0)")));
        QVERIFY(transport.script.contains(QStringLiteral("desktops()")));
        QVERIFY(transport.script.contains(QStringLiteral("showConfigurationInterface()")));
        QVERIFY(!transport.script.contains(QStringLiteral("runtimeEndpoint")));
        QVERIFY(!transport.script.contains(
            QStringLiteral("com.github.catsout.wallpaperEngineKde")));

        transport.complete({{QStringLiteral("ok"), true}, {QStringLiteral("id"), QStringLiteral("31")},
            {QStringLiteral("screen"), 0},
            {QStringLiteral("plugin"), QStringLiteral("org.kde.image")}});

        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toBool(), true);
        QVERIFY(!invoker.pending());
        QVERIFY(invoker.errorCode().isEmpty());
    }

    void missingWallpaperIsReported() {
        FakePlasmaScriptTransport transport;
        wekde::DesktopSettingsInvoker invoker(&transport);
        QSignalSpy finished(&invoker, &wekde::DesktopSettingsInvoker::finished);

        QVERIFY(invoker.open());
        transport.complete({{QStringLiteral("ok"), false},
            {QStringLiteral("reason"), QStringLiteral("wallpaperUnavailable")}});

        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toBool(), false);
        QCOMPARE(invoker.errorCode(), QStringLiteral("wallpaperUnavailable"));
    }

    void unavailableTransportFailsWithoutPendingRequest() {
        FakePlasmaScriptTransport transport;
        transport.isAvailable = false;
        wekde::DesktopSettingsInvoker invoker(&transport);
        QSignalSpy finished(&invoker, &wekde::DesktopSettingsInvoker::finished);

        QVERIFY(!invoker.open());
        QVERIFY(!invoker.pending());
        QCOMPARE(invoker.errorCode(), QStringLiteral("plasmaUnavailable"));
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toBool(), false);
    }
};

QTEST_GUILESS_MAIN(DesktopSettingsInvokerTest)
#include "tst_desktopsettingsinvoker.moc"
