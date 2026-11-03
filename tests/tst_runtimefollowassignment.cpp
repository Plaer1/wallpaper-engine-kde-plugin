#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QSignalSpy>
#include <QTest>

namespace {
const QString FixturePath = QString::fromUtf8(RUNTIME_FOLLOW_ASSIGNMENT_QML);

void initializeMap(QQmlPropertyMap& map, bool enabled = false) {
    map.insert(QStringLiteral("FollowWallpaperEngine"), enabled);
    map.insert(QStringLiteral("WallpaperEngineAccount"), QStringLiteral("steamuser"));
    map.insert(QStringLiteral("FollowSourceMode"), QStringLiteral("legacySession"));
    map.insert(QStringLiteral("FollowSourceAccountKey"), QString());
    map.insert(QStringLiteral("FollowSourceKey"),
        QStringLiteral("config:/fixture/loginusers.vdf#section:steamuser"));
}

QObject* createAdapter(QQmlEngine& engine, QQmlPropertyMap& map, const QString& id) {
    QQmlComponent component(&engine, QUrl::fromLocalFile(FixturePath));
    if (component.isError()) qWarning().noquote() << component.errorString();
    QObject* adapter = component.create();
    if (!adapter) return nullptr;
    adapter->setProperty("configuration", QVariant::fromValue(static_cast<QObject*>(&map)));
    adapter->setProperty("instanceId", id);
    adapter->setProperty("configPath", QStringLiteral("/fixture/loginusers.vdf"));
    adapter->setProperty("targetVerified", true);
    adapter->setProperty("configurationPreview", false);
    return adapter;
}

bool invokeApply(QObject* adapter, const QString& requestId, const QString& targetId,
                 const QVariantMap& args) {
    QVariant result;
    const bool invoked = QMetaObject::invokeMethod(adapter, "apply", Q_RETURN_ARG(QVariant, result),
        Q_ARG(QVariant, requestId), Q_ARG(QVariant, targetId), Q_ARG(QVariant, args));
    return invoked && result.toBool();
}
}

class RuntimeFollowAssignmentTest : public QObject {
    Q_OBJECT
private slots:
    void writesOnlyTheExactTargetMapAndWaitsForMapSignals() {
        QQmlEngine engine;
        QQmlPropertyMap first;
        QQmlPropertyMap second;
        initializeMap(first);
        initializeMap(second);
        QObject* adapter = createAdapter(engine, first, QStringLiteral("instance-A"));
        QVERIFY(adapter);
        QSignalSpy changed(&first, &QQmlPropertyMap::valueChanged);
        QSignalSpy completion(adapter, SIGNAL(completed(QString,QString,bool,bool,QString,QString,QString,QString,QString)));
        const QVariantMap args{{QStringLiteral("enabled"), true},
            {QStringLiteral("mode"), QStringLiteral("manualMapping")},
            {QStringLiteral("accountKey"), QStringLiteral("userdata:root:7")},
            {QStringLiteral("sourceKey"), QStringLiteral("config:/fixture/loginusers.vdf#section:seven")}};
        QVERIFY(invokeApply(adapter, QStringLiteral("r1"), QStringLiteral("instance-A"), args));
        QTRY_COMPARE(completion.size(), 1);
        QVERIFY(completion.first().at(2).toBool());
        QVERIFY(first.value(QStringLiteral("FollowWallpaperEngine")).toBool());
        QCOMPARE(first.value(QStringLiteral("FollowSourceMode")).toString(), QStringLiteral("manualMapping"));
        QCOMPARE(first.value(QStringLiteral("FollowSourceKey")).toString(), args.value("sourceKey").toString());
        QVERIFY(!second.value(QStringLiteral("FollowWallpaperEngine")).toBool());
        QVERIFY(changed.size() >= 4);
        delete adapter;
    }

    void matchingNoOpNeedsReadbackButNoValueChangedSignal() {
        QQmlEngine engine;
        QQmlPropertyMap map;
        initializeMap(map, true);
        QObject* adapter = createAdapter(engine, map, QStringLiteral("instance-A"));
        QVERIFY(adapter);
        QSignalSpy changed(&map, &QQmlPropertyMap::valueChanged);
        QSignalSpy completion(adapter, SIGNAL(completed(QString,QString,bool,bool,QString,QString,QString,QString,QString)));
        const QVariantMap args{{QStringLiteral("enabled"), true},
            {QStringLiteral("mode"), QStringLiteral("legacySession")},
            {QStringLiteral("accountKey"), QString()},
            {QStringLiteral("sourceKey"), QStringLiteral("config:/fixture/loginusers.vdf#section:steamuser")}};
        QVERIFY(invokeApply(adapter, QStringLiteral("r2"), QStringLiteral("instance-A"), args));
        QTRY_COMPARE(completion.size(), 1);
        QVERIFY(completion.first().at(2).toBool());
        QCOMPARE(changed.size(), 0);
        delete adapter;
    }

    void unverifiedIdentityAndPreviewFailClosed() {
        QQmlEngine engine;
        QQmlPropertyMap map;
        initializeMap(map);
        QObject* adapter = createAdapter(engine, map, QStringLiteral("instance-A"));
        QVERIFY(adapter);
        QSignalSpy completion(adapter, SIGNAL(completed(QString,QString,bool,bool,QString,QString,QString,QString,QString)));
        QVariantMap args{{QStringLiteral("enabled"), true},
            {QStringLiteral("mode"), QStringLiteral("identifiedCurrent")},
            {QStringLiteral("accountKey"), QStringLiteral("steam:76561198000000001")},
            {QStringLiteral("sourceKey"), QStringLiteral("config:/fixture#account:76561198000000001")}};
        QVERIFY(!invokeApply(adapter, QStringLiteral("r3"), QStringLiteral("instance-A"), args));
        QTRY_COMPARE(completion.size(), 1);
        QCOMPARE(completion.first().at(7).toString(), QStringLiteral("unverifiedIdentity"));
        adapter->setProperty("configurationPreview", true);
        QVERIFY(!invokeApply(adapter, QStringLiteral("r4"), QStringLiteral("instance-A"), args));
        QTRY_COMPARE(completion.size(), 2);
        QVERIFY(!map.value(QStringLiteral("FollowWallpaperEngine")).toBool());
        delete adapter;
    }
};

QTEST_MAIN(RuntimeFollowAssignmentTest)
#include "tst_runtimefollowassignment.moc"
