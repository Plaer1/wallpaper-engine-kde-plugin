#include "PlasmaConfigurationInvoker.hpp"

#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSEngine>
#include <QtTest>

using namespace wekde;
namespace {
constexpr auto Plugin = "com.github.catsout.wallpaperEngineKde";
constexpr auto Activity = "01234567-89ab-cdef-0123-456789abcdef";

class FakeTransport final : public PlasmaScriptTransport {
    Q_OBJECT
public:
    using PlasmaScriptTransport::PlasmaScriptTransport;
    bool available() const override { return online; }
    void evaluate(const QString& id, const QString& script) override {
        requests.append(id);
        scripts.append(script);
    }
    void reply(const QString& id, const QString& output) {
        emit evaluated(id, true, output, {});
    }
    bool online = true;
    QStringList requests;
    QStringList scripts;
};

PlasmaConfigurationTarget target() {
    return {QUuid(QStringLiteral("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")),
        QStringLiteral("31"), QString::fromLatin1(Activity), QStringLiteral("DP-1"),
        QString::fromLatin1(Plugin)};
}

QString response(bool ok = true, QString id = QStringLiteral("31"),
    QString activity = QString::fromLatin1(Activity), QString plugin = QString::fromLatin1(Plugin),
    QString reason = {}) {
    QJsonObject object{{QStringLiteral("ok"), ok}, {QStringLiteral("id"), id},
        {QStringLiteral("activity"), activity}, {QStringLiteral("plugin"), plugin},
        {QStringLiteral("screen"), 1}};
    if (!reason.isEmpty()) object.insert(QStringLiteral("reason"), reason);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
}

class PlasmaConfigurationInvokerTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void fixedScriptAndConfirmedSuccess() {
        FakeTransport transport;
        PlasmaConfigurationInvoker invoker(&transport);
        QSignalSpy finished(&invoker, &PlasmaConfigurationInvoker::finished);
        auto identity = target();
        identity.connectorName = QStringLiteral(R"(DP-1");evil();//)");
        QVERIFY(invoker.open(QStringLiteral("cfg-1"), identity));
        QCOMPARE(transport.requests, QStringList{QStringLiteral("cfg-1")});
        const QString script = transport.scripts.first();
        QVERIFY(script.contains(QStringLiteral("screenForConnector")));
        QVERIFY(script.contains(QStringLiteral("typeof d.showConfigurationInterface")));
        QVERIFY(script.contains(QStringLiteral("currentActivity")));
        QVERIFY(script.contains(QStringLiteral("wallpaperPlugin")));
        QVERIFY(script.contains(QStringLiteral("\\\"")));
        transport.reply(QStringLiteral("cfg-1"), response());
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(1).toBool(), true);
    }

    void rejectsMismatchAndMissingHostMethods() {
        FakeTransport transport;
        PlasmaConfigurationInvoker invoker(&transport);
        QSignalSpy finished(&invoker, &PlasmaConfigurationInvoker::finished);
        QVERIFY(invoker.open(QStringLiteral("wrong-target"), target()));
        transport.reply(QStringLiteral("wrong-target"), response(true, QStringLiteral("32")));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.takeFirst().at(1).toBool(), false);

        QVERIFY(invoker.open(QStringLiteral("no-mapping"), target()));
        transport.reply(QStringLiteral("no-mapping"), response(false, {}, {}, {}, QStringLiteral("mappingUnavailable")));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.takeFirst().at(2).toString(), QStringLiteral("mappingUnavailable"));

        QVERIFY(invoker.open(QStringLiteral("no-method"), target()));
        transport.reply(QStringLiteral("no-method"), response(false, {}, {}, {}, QStringLiteral("configureUnavailable")));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.takeFirst().at(2).toString(), QStringLiteral("configurationUnavailable"));
    }

    void printedScriptUsesFunctionCurrentActivityAndDoesNotReadActivityProperty() {
        FakeTransport transport;
        PlasmaConfigurationInvoker invoker(&transport);
        QVERIFY(invoker.open(QStringLiteral("host-shaped"), target()));
        QJSEngine engine;
        const QString setup = QStringLiteral(
            "var __printed=''; function print(value){__printed+=value;}"
            "function currentActivity(){return '%1';}"
            "function screenForConnector(name){return name==='DP-1'?2:-1;}"
            "var __calls=0;"
            "function desktopForScreen(screen){return {id:31,wallpaperPlugin:'%2',"
            "showConfigurationInterface:function(){__calls++;}};}")
                .arg(QString::fromLatin1(Activity), QString::fromLatin1(Plugin));
        QVERIFY(engine.evaluate(setup).isError() == false);
        QVERIFY(!engine.evaluate(transport.scripts.first()).isError());
        const QJsonDocument printed = QJsonDocument::fromJson(
            engine.globalObject().property(QStringLiteral("__printed")).toString().toUtf8());
        QVERIFY(printed.isObject());
        const QJsonObject result = printed.object();
        QCOMPARE(result.value(QStringLiteral("ok")).toBool(), true);
        QCOMPARE(result.value(QStringLiteral("id")).toString(), QStringLiteral("31"));
        QCOMPARE(result.value(QStringLiteral("activity")).toString(), QString::fromLatin1(Activity));
        QCOMPARE(engine.globalObject().property(QStringLiteral("__calls")).toInt(), 1);
        QVERIFY(!transport.scripts.first().contains(QStringLiteral("d.activity")));
        QVERIFY(transport.scripts.first().contains(QStringLiteral("typeof currentActivity!=='function'")));
    }

    void invalidInputAndTransportTeardownFailClosed() {
        auto* transport = new FakeTransport;
        PlasmaConfigurationInvoker invoker(transport);
        QSignalSpy finished(&invoker, &PlasmaConfigurationInvoker::finished);
        auto invalid = target();
        invalid.containmentId = QStringLiteral("9007199254740992");
        QVERIFY(!invoker.open(QStringLiteral("bad"), invalid));
        invalid = target();
        invalid.pluginId = QStringLiteral("other.plugin");
        QVERIFY(!invoker.open(QStringLiteral("wrong-plugin"), invalid));
        QVERIFY(finished.isEmpty());

        QVERIFY(invoker.open(QStringLiteral("pending"), target()));
        delete transport;
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(0).toString(), QStringLiteral("pending"));
        QCOMPARE(finished.first().at(1).toBool(), false);
        QCOMPARE(finished.first().at(2).toString(), QStringLiteral("serviceUnavailable"));
    }
};

QTEST_GUILESS_MAIN(PlasmaConfigurationInvokerTest)
#include "tst_plasmaconfigurationinvoker.moc"
