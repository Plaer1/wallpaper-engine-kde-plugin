#include <QCoreApplication>
#include <QEventLoop>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTest>
#include <QUrl>

#include <algorithm>

class NativeLegacySections final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList accounts READ accounts CONSTANT)

public:
    using QObject::QObject;
    QStringList accounts() const {
        return { QStringLiteral("steamuser"), QStringLiteral("Other") };
    }
};

class FollowChoiceBuilderQmlTest final : public QObject {
    Q_OBJECT

private slots:
    void buildsLegacyCandidatesFromNativeQStringList() {
        const QString helperPath = qEnvironmentVariable("FOLLOW_CHOICE_BUILDER_PATH");
        QVERIFY2(! helperPath.isEmpty(), "Set FOLLOW_CHOICE_BUILDER_PATH to the helper .mjs path");
        const QString    importUrl = QUrl::fromLocalFile(helperPath).toString(QUrl::FullyEncoded);
        const QByteArray source    = QStringLiteral(R"QML(
import QtQml
import "%1" as FollowChoiceBuilder
QtObject {
    property string configPath: "/tmp/Wallpaper Engine/config.json"
    property var catalog: ({schemaVersion: 1, generation: 1, installations: [],
        accounts: [{key: "steam:76561197960265729", steamId64: "76561197960265729",
            accountId32: "1", label: "Account One", identityStatus: "verified"}], caches: []})
    property bool followEnabled: false
    property bool runtimeTargetRegistered: true
    property var observedSections: runtimeTargetRegistered ? legacySections.accounts : []
    property var validBinding: ({mode: "manualMapping", accountKey: "steam:76561197960265729",
        sourceKey: "config:/tmp/Wallpaper Engine/config.json#section:steamuser"})
    property bool nativeListIsArray: Array.isArray(legacySections.accounts)
    property var choices: FollowChoiceBuilder.buildFollowChoices(
        null, legacySections.accounts, configPath)
    property var validMappingChoices: FollowChoiceBuilder.buildFollowChoices(
        catalog, observedSections, configPath, validBinding)
    property var disabledFollowChoices: followEnabled ? [] : FollowChoiceBuilder.buildFollowChoices(
        catalog, observedSections, configPath, validBinding)
    property var staleAccountChoices: FollowChoiceBuilder.buildFollowChoices(catalog,
        legacySections.accounts, configPath, ({mode: "manualMapping", accountKey: "steam:76561197960265730",
            sourceKey: validBinding.sourceKey}))
    property var staleSectionChoices: FollowChoiceBuilder.buildFollowChoices(catalog,
        legacySections.accounts, configPath, ({mode: "manualMapping", accountKey: validBinding.accountKey,
            sourceKey: "config:/tmp/Wallpaper Engine/config.json#section:Removed"}))
    property var wrongConfigChoices: FollowChoiceBuilder.buildFollowChoices(catalog,
        legacySections.accounts, configPath, ({mode: "manualMapping", accountKey: validBinding.accountKey,
            sourceKey: "config:/tmp/other/config.json#section:steamuser"}))
    property var malformedBindingChoices: FollowChoiceBuilder.buildFollowChoices(catalog,
        legacySections.accounts, configPath, ({mode: "manualMapping", accountKey: 76561197960265729,
            sourceKey: "config:/tmp/Wallpaper Engine/config.json#section:%ZZ"}))
}
)QML")
                                         .arg(importUrl)
                                         .toUtf8();

        QQmlEngine           engine;
        NativeLegacySections legacySections;
        engine.rootContext()->setContextProperty(QStringLiteral("legacySections"), &legacySections);
        QQmlComponent component(&engine);
        component.setData(source,
                          QUrl(QStringLiteral("file:///tmp/follow-choice-builder-test.qml")));
        if (component.status() == QQmlComponent::Loading) {
            QEventLoop loop;
            connect(&component, &QQmlComponent::statusChanged, &loop, &QEventLoop::quit);
            loop.exec();
        }
        QObject* root = component.create();
        QVERIFY2(root, qPrintable(component.errorString()));
        QVERIFY(! root->property("nativeListIsArray").toBool());
        QVERIFY(!root->property("followEnabled").toBool());

        const QVariantList choices = root->property("choices").toList();
        QCOMPARE(choices.size(), 2);
        const QVariantMap first  = choices.at(0).toMap();
        const QVariantMap second = choices.at(1).toMap();
        QCOMPARE(first.value(QStringLiteral("mode")).toString(), QStringLiteral("legacySession"));
        QCOMPARE(second.value(QStringLiteral("mode")).toString(), QStringLiteral("legacySession"));
        QCOMPARE(first.value(QStringLiteral("sourceKey")).toString(),
                 QStringLiteral("config:/tmp/Wallpaper Engine/config.json#section:Other"));
        QCOMPARE(second.value(QStringLiteral("sourceKey")).toString(),
                 QStringLiteral("config:/tmp/Wallpaper Engine/config.json#section:steamuser"));
        QVERIFY(first.value(QStringLiteral("accountKey")).toString().isEmpty());
        QVERIFY(second.value(QStringLiteral("accountKey")).toString().isEmpty());
        QVERIFY(! first.value(QStringLiteral("identityVerified")).toBool());
        QVERIFY(! second.value(QStringLiteral("identityVerified")).toBool());

        const auto mappings = [](const QVariantList& items) {
            QVariantList result;
            for (const QVariant& item : items) {
                if (item.toMap().value(QStringLiteral("mode")).toString() ==
                    QStringLiteral("manualMapping"))
                    result.append(item);
            }
            return result;
        };
        const QVariantList validMappings = mappings(root->property("validMappingChoices").toList());
        QCOMPARE(validMappings.size(), 1);
        const QVariantMap mapping = validMappings.first().toMap();
        QCOMPARE(mapping.value(QStringLiteral("mode")).toString(), QStringLiteral("manualMapping"));
        QCOMPARE(mapping.value(QStringLiteral("accountKey")).toString(),
                 QStringLiteral("steam:76561197960265729"));
        QCOMPARE(mapping.value(QStringLiteral("sourceKey")).toString(),
                 QStringLiteral("config:/tmp/Wallpaper Engine/config.json#section:steamuser"));
        QVERIFY(mapping.value(QStringLiteral("label"))
                    .toString()
                    .startsWith(QStringLiteral("User-associated")));
        QVERIFY(! mapping.value(QStringLiteral("identityVerified")).toBool());
        QVERIFY(mapping.value(QStringLiteral("userConfirmed")).toBool());
        const QVariantList disabledChoices = root->property("disabledFollowChoices").toList();
        QVERIFY(std::any_of(disabledChoices.cbegin(), disabledChoices.cend(), [](const QVariant& item) {
            const QVariantMap choice = item.toMap();
            return choice.value(QStringLiteral("mode")).toString() == QStringLiteral("legacySession");
        }));
        QVERIFY(std::any_of(disabledChoices.cbegin(), disabledChoices.cend(), [](const QVariant& item) {
            const QVariantMap choice = item.toMap();
            return choice.value(QStringLiteral("mode")).toString() == QStringLiteral("manualMapping");
        }));
        for (const QString& property : { QStringLiteral("staleAccountChoices"),
                                         QStringLiteral("staleSectionChoices"),
                                         QStringLiteral("wrongConfigChoices"),
                                         QStringLiteral("malformedBindingChoices") }) {
            QVERIFY(mappings(root->property(property.toUtf8().constData()).toList()).isEmpty());
        }
        delete root;
    }
};

QTEST_GUILESS_MAIN(FollowChoiceBuilderQmlTest)
#include "tst_followchoicebuilderqml.moc"
