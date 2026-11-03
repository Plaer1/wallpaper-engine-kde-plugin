#include <QtTest>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include "WallpaperEngineFollower.hpp"

using wekde::WallpaperEngineFollower;

class TestWallpaperEngineFollower : public QObject {
    Q_OBJECT
    QTemporaryDir tmp;
    QString       root() const { return tmp.filePath("steamapps/workshop/content/431960"); }
    QString       configPath() const { return tmp.filePath("config.json"); }
    QString       project(const QString& id) const { return root() + "/" + id; }
    static bool   write(const QString& path, const QByteArray& data, bool atomic = false) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (atomic) {
            QSaveFile f(path);
            return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
        }
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
    }
    QByteArray config(const QString& path, const QString& user = "steamuser") const {
        return QJsonDocument(
                   QJsonObject {
                       { user,
                         QJsonObject {
                             { "general",
                               QJsonObject {
                                   { "browser",
                                     QJsonObject { { "lastselectedmonitor", "Monitor0" } } },
                                   { "wallpaperconfig",
                                     QJsonObject {
                                         { "selectedwallpapers",
                                           QJsonObject {
                                               { "Monitor0",
                                                 QJsonObject {
                                                     { "file", path } } } } } } } } } } } })
            .toJson();
    }
    QJsonObject userWithNative(const QString& path) const {
        return QJsonObject {
            { "general", QJsonObject {
                { "browser", QJsonObject { { "lastselectedmonitor", "Monitor0" } } },
                { "wallpaperconfig", QJsonObject { { "selectedwallpapers", QJsonObject {
                    { "Monitor0", QJsonObject { { "file", path } } }
                } } } }
            } }
        };
    }
    QJsonObject userWithoutNative() const {
        QJsonObject value = userWithNative({});
        QJsonObject general = value.value("general").toObject();
        general["wallpaperconfig"] = QJsonObject { { "selectedwallpapers", QJsonValue::Null } };
        value["general"] = general;
        return value;
    }
    QByteArray sectionsConfig(const QStringList& sections) const {
        QJsonObject document;
        for (const QString& section : sections) document[section] = userWithoutNative();
        return QJsonDocument(document).toJson();
    }
    QString bindingSource(const QString& section) const {
        return QStringLiteral("config:%1#section:%2")
            .arg(QDir::cleanPath(QFileInfo(configPath()).absoluteFilePath()),
                 QString::fromUtf8(QUrl::toPercentEncoding(section)));
    }
private slots:
    void initTestCase() {
        QVERIFY(tmp.isValid());
        for (const QString& id : { QString("123"), QString("456"), QString("789") }) {
            QVERIFY(
                write(project(id) + "/project.json", R"({"type":"video","file":"clip #?%.mp4"})"));
            QVERIFY(write(project(id) + "/clip #?%.mp4", "fixture"));
        }
    }
    void readsNativeAndWindowsPaths() {
        const QStringList paths {
            project("123") + "/project.json",
            "Z:" + project("123") + "/clip #?%.mp4",
            "D:\\SteamLibrary\\steamapps\\workshop\\content\\431960\\123\\project.json",
            QUrl::fromLocalFile(project("123") + "/project.json").toString()
        };
        for (const auto& path : paths) {
            QVERIFY(write(configPath(), config(path)));
            const auto result   = WallpaperEngineFollower::readSelection(configPath(), { root() });
            const auto selected = result["selection"].toMap();
            QCOMPARE(selected["workshopid"].toString(), "123");
            const QString source = selected["source"].toString();
            QVERIFY(source.endsWith("+video"));
            QCOMPARE(QUrl(source.chopped(6)).toLocalFile(), project("123") + "/clip #?%.mp4");
        }
    }
    void resolvesNestedWebMedia() {
        const QString dir = project("789");
        QVERIFY(write(dir + "/project.json", R"({"type":"web","file":"web/index.html"})"));
        QVERIFY(write(dir + "/web/index.html", "<html></html>"));
        QVERIFY(write(configPath(),
                      config("D:/Steam/steamapps/workshop/content/431960/789/web/index.html")));
        const auto selection =
            WallpaperEngineFollower::readSelection(configPath(), { root() })["selection"].toMap();
        QCOMPARE(selection["workshopid"].toString(), "789");
        QCOMPARE(selection["projectDir"].toString(), dir);
        QVERIFY(selection["source"].toString().endsWith("/web/index.html+web"));
    }

    void followsLastSelectedMonitor() {
        auto doc     = QJsonDocument::fromJson(config(project("123") + "/project.json")).object();
        auto user    = doc["steamuser"].toObject();
        auto general = user["general"].toObject();
        general["browser"]         = QJsonObject { { "lastselectedmonitor", "Monitor1" } };
        general["wallpaperconfig"] = QJsonObject {
            { "selectedwallpapers",
              QJsonObject {
                  { "Monitor0", QJsonObject { { "file", project("123") + "/project.json" } } },
                  { "Monitor1", QJsonObject { { "file", project("456") + "/project.json" } } } } }
        };
        user["general"]  = general;
        doc["steamuser"] = user;
        QVERIFY(write(configPath(), QJsonDocument(doc).toJson()));
        QCOMPARE(WallpaperEngineFollower::readSelection(configPath(), { root() })["selection"]
                     .toMap()["workshopid"]
                     .toString(),
                 "456");
    }
    void multipleAccountsRequireAChoice() {
        auto doc =
            QJsonDocument::fromJson(config(project("123") + "/project.json", "Alice")).object();
        doc["Bob"] = QJsonDocument::fromJson(config(project("456") + "/project.json", "Bob"))
                         .object()["Bob"];
        QVERIFY(write(configPath(), QJsonDocument(doc).toJson()));
        auto result = WallpaperEngineFollower::readSelection(configPath(), { root() });
        QCOMPARE(result["accounts"].toStringList().size(), 2);
        QVERIFY(result["selection"].toMap().isEmpty());
        result = WallpaperEngineFollower::readSelection(configPath(), { root() }, "Bob");
        QCOMPARE(result["selection"].toMap()["workshopid"].toString(), "456");
    }
    void readsProtonBrowserSelectionWhenNativeMapIsNull() {
        auto doc = QJsonDocument::fromJson(config(project("123") + "/project.json")).object();
        auto user = doc["steamuser"].toObject();
        auto general = user["general"].toObject();
        auto browser = general["browser"].toObject();
        browser["wekdeSelection"] = QJsonObject {
            { "version", 1 }, { "file", "Z:" + project("456") + "/project.json" },
            { "monitor", "Monitor0" }
        };
        general["browser"] = browser;
        user["general"] = general;
        doc["steamuser"] = user;
        QVERIFY(write(configPath(), QJsonDocument(doc).toJson()));
        // A valid native applied selection still takes precedence over old bridge data.
        QCOMPARE(WallpaperEngineFollower::readSelection(configPath(), { root() })["selection"]
                     .toMap()["workshopid"].toString(), "123");
        general["wallpaperconfig"] = QJsonObject { { "selectedwallpapers", QJsonValue::Null } };
        user["general"] = general;
        doc["steamuser"] = user;
        QVERIFY(write(configPath(), QJsonDocument(doc).toJson()));
        auto result = WallpaperEngineFollower::readSelection(configPath(), { root() });
        QCOMPARE(result["selection"].toMap()["workshopid"].toString(), "456");
        QVERIFY(result["status"].toString().contains("browser selection"));
        // An unrecognized bridge record is not interpreted as a chosen wallpaper.
        browser["wekdeSelection"] = QJsonObject { { "version", 99 }, { "file", project("456") + "/project.json" } };
        general["browser"] = browser;
        user["general"] = general;
        doc["steamuser"] = user;
        QVERIFY(write(configPath(), QJsonDocument(doc).toJson()));
        QVERIFY(WallpaperEngineFollower::readSelection(configPath(), { root() })["selection"].toMap().isEmpty());
    }

    void explicitAccountUsesMatchedNativeThenMatchedBridge() {
        const QString idA = QStringLiteral("76561197960265729");
        const QString idB = QStringLiteral("76561198060265728");
        QJsonObject generic = userWithNative(project("456") + "/project.json");
        QJsonObject browser = generic.value("general").toObject().value("browser").toObject();
        browser["wekdeSelection"] = QJsonObject {
            { "version", 2 }, { "currentSteamId64", idA },
            { "byUser", QJsonObject {
                { idA, QJsonObject { { "file", project("456") + "/project.json" }, { "monitor", "Monitor0" } } },
                { idB, QJsonObject { { "file", project("123") + "/project.json" }, { "monitor", "Monitor0" } } }
            } }
        };
        QJsonObject general = generic.value("general").toObject();
        general["browser"] = browser;
        generic["general"] = general;
        QJsonObject doc { { "steamuser", generic } };

        WallpaperEngineFollower::FollowBinding binding;
        binding.mode = QStringLiteral("account");
        binding.accountKey = QStringLiteral("steam:") + idB;
        auto result = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        QCOMPARE(result["selection"].toMap()["workshopid"].toString(), QStringLiteral("123"));
        QCOMPARE(result["provenance"].toMap()["sourceType"].toString(),
                 QStringLiteral("verifiedBridgeAccount"));
        QCOMPARE(result["provenance"].toMap()["accountKey"].toString(), binding.accountKey);
        QVERIFY(result["sourceKey"].toString().contains(idB));

        // A native section tied to B outranks B's bridge entry; generic native
        // data remains ineligible for explicit account mode.
        doc[idB] = userWithNative(project("456") + "/project.json");
        result = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        QCOMPARE(result["selection"].toMap()["workshopid"].toString(), QStringLiteral("456"));
        QCOMPARE(result["provenance"].toMap()["sourceType"].toString(), QStringLiteral("nativeAccount"));
        QCOMPARE(result["sourceKey"].toString(), bindingSource(idB));
    }

    void explicitAccountNeverFallsBackAcrossUsersAndRejectsUnsafeIdentity() {
        const QString idA = QStringLiteral("76561197960265729");
        const QString idB = QStringLiteral("76561198060265728");
        QJsonObject generic = userWithNative(project("123") + "/project.json");
        QJsonObject browser = generic.value("general").toObject().value("browser").toObject();
        browser["wekdeSelection"] = QJsonObject {
            { "version", 2 }, { "byUser", QJsonObject {
                { idA, QJsonObject { { "file", project("456") + "/project.json" }, { "monitor", "Monitor0" } } }
            } }
        };
        QJsonObject general = generic.value("general").toObject();
        general["browser"] = browser;
        generic["general"] = general;
        const QJsonObject doc { { "steamuser", generic } };
        WallpaperEngineFollower::FollowBinding binding;
        binding.mode = QStringLiteral("account");
        binding.accountKey = QStringLiteral("steam:") + idB;
        auto result = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        QVERIFY(result["selection"].toMap().isEmpty());
        QVERIFY(result["status"].toString().contains(QStringLiteral("matching")));

        binding.accountKey = QStringLiteral("steam:0");
        result = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        QVERIFY(result["selection"].toMap().isEmpty());
        binding.accountKey = QStringLiteral("steam:999999999999999999999999999999");
        result = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        QVERIFY(result["selection"].toMap().isEmpty());

        binding.mode = QStringLiteral("identifiedCurrent");
        binding.currentSteamId64 = idA;
        binding.currentIdentityVerified = false;
        result = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        QVERIFY(result["status"].toString().contains(QStringLiteral("verified current")));
        QVERIFY(result["selection"].toMap().isEmpty());
    }

    void validatesCanonicalPublicIndividualSteamIds() {
        const QStringList validIds {
            QStringLiteral("76561197960265729"), // Account ID 1: lowest valid individual ID.
            QStringLiteral("76561202255233023"), // Account ID 2^32 - 1.
        };
        for (const QString& id : validIds) {
            QJsonObject doc { { id, userWithNative(project("123") + "/project.json") } };
            WallpaperEngineFollower::FollowBinding binding;
            binding.mode = QStringLiteral("account");
            binding.accountKey = QStringLiteral("steam:") + id;
            const auto result = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
            QCOMPARE(result["selection"].toMap()["workshopid"].toString(), QStringLiteral("123"));
        }

        const QStringList invalidIds {
            QStringLiteral("1"),                     // Not a public individual SteamID64.
            QStringLiteral("76561197960265728"),     // Zero account component.
            QStringLiteral("076561197960265729"),    // Noncanonical leading zero.
            QStringLiteral("76561202255233024"),     // Account component exceeds uint32.
            QStringLiteral("18446744073709551616"),  // Exceeds uint64.
        };
        for (const QString& id : invalidIds) {
            QJsonObject doc { { id, userWithNative(project("456") + "/project.json") } };
            WallpaperEngineFollower::FollowBinding binding;
            binding.mode = QStringLiteral("account");
            binding.accountKey = QStringLiteral("steam:") + id;
            const auto result = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
            QVERIFY2(result["selection"].toMap().isEmpty(), qPrintable(id));
            QVERIFY2(result["status"].toString().contains(QStringLiteral("verified")), qPrintable(id));
        }
    }

    void legacyAndManualSourcesKeepExplicitProvenance() {
        const QString idA = QStringLiteral("76561197960265729");
        QJsonObject legacy = userWithNative(project("123") + "/project.json");
        QJsonObject doc { { "steamuser", legacy } };
        auto result = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() });
        QCOMPARE(result["selection"].toMap()["workshopid"].toString(), QStringLiteral("123"));
        auto provenance = result["provenance"].toMap();
        QCOMPARE(provenance["sourceType"].toString(), QStringLiteral("legacyUnassociatedNative"));
        QVERIFY(provenance["accountKey"].toString().isEmpty());
        QCOMPARE(provenance["identityStatus"].toString(), QStringLiteral("unassociated"));

        QJsonObject noNative = userWithoutNative();
        QJsonObject general = noNative.value("general").toObject();
        QJsonObject browser = general.value("browser").toObject();
        browser["wekdeSelection"] = QJsonObject {
            { "version", 1 }, { "file", project("456") + "/project.json" }, { "monitor", "Monitor0" }
        };
        general["browser"] = browser;
        noNative["general"] = general;
        doc["steamuser"] = noNative;
        result = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() });
        QCOMPARE(result["selection"].toMap()["workshopid"].toString(), QStringLiteral("456"));
        QCOMPARE(result["provenance"].toMap()["sourceType"].toString(), QStringLiteral("legacyUnassociatedBridge"));
        QVERIFY(result["sourceKey"].toString().contains(QStringLiteral("wekdeSelection.v1")));

        WallpaperEngineFollower::FollowBinding binding;
        binding.mode = QStringLiteral("manualMapping");
        binding.accountKey = QStringLiteral("steam:") + idA;
        binding.sourceKey = bindingSource(QStringLiteral("steamuser"));
        result = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        QCOMPARE(result["selection"].toMap()["workshopid"].toString(), QStringLiteral("456"));
        provenance = result["provenance"].toMap();
        QCOMPARE(provenance["sourceType"].toString(), QStringLiteral("manualMapping"));
        QCOMPARE(provenance["identityStatus"].toString(), QStringLiteral("userAssociated"));
        QCOMPARE(provenance["label"].toString(), QStringLiteral("User-associated mapping"));

        binding.sourceKey = QStringLiteral("config:%1#section:steamuser")
            .arg(QDir::cleanPath(tmp.filePath(QStringLiteral("other/config.json"))));
        result = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        QVERIFY(result["selection"].toMap().isEmpty());
        QVERIFY(result["status"].toString().contains(QStringLiteral("map manually")));
    }

    void legacySessionReadsMigratedV2UnassociatedWithoutAttribution() {
        const QString idA = QStringLiteral("76561197960265729");
        const QString idB = QStringLiteral("76561197960265730");
        QString fixturePath = QDir::current().filePath(
            QStringLiteral("tests/fixtures/we_kde_selection_v1_migrated.json"));
        if (!QFileInfo::exists(fixturePath))
            fixturePath = QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()
                + QStringLiteral("/fixtures/we_kde_selection_v1_migrated.json");
        QFile fixture(fixturePath);
        QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(fixturePath));
        const QJsonObject migratedEnvelope =
            QJsonDocument::fromJson(fixture.readAll()).object();
        QCOMPARE(migratedEnvelope.value("version").toInt(), 2);
        QJsonObject user = userWithoutNative();
        QJsonObject general = user.value("general").toObject();
        QJsonObject browser = general.value("browser").toObject();
        browser["wekdeSelection"] = migratedEnvelope;
        general["browser"] = browser;
        user["general"] = general;
        QJsonObject doc { { "steamuser", user } };

        // The migrated v1 value is available to legacy follow but remains unassociated.
        auto result = WallpaperEngineFollower::resolveSelection(
            doc, configPath(), { root() }, WallpaperEngineFollower::FollowBinding {});
        QCOMPARE(result["selection"].toMap()["workshopid"].toString(), QStringLiteral("456"));
        auto provenance = result["provenance"].toMap();
        QCOMPARE(provenance["sourceType"].toString(), QStringLiteral("legacyUnassociatedBridge"));
        QCOMPARE(provenance["identityStatus"].toString(), QStringLiteral("unassociated"));
        QVERIFY(provenance["accountKey"].toString().isEmpty());
        QVERIFY(result["sourceKey"].toString().contains(QStringLiteral("wekdeSelection.unassociated")));

        // Explicit account B cannot inherit the session record; account A only
        // sees its own byUser record, never the migrated unassociated choice.
        WallpaperEngineFollower::FollowBinding binding;
        binding.mode = QStringLiteral("account");
        binding.accountKey = QStringLiteral("steam:") + idB;
        result = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        QVERIFY(result["selection"].toMap().isEmpty());
        binding.accountKey = QStringLiteral("steam:") + idA;
        result = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        QCOMPARE(result["selection"].toMap()["workshopid"].toString(), QStringLiteral("123"));
        QCOMPARE(result["provenance"].toMap()["sourceType"].toString(),
                 QStringLiteral("verifiedBridgeAccount"));

        // Native legacy data still wins over either bridge representation.
        user = userWithNative(project("123") + "/project.json");
        general = user.value("general").toObject();
        browser = general.value("browser").toObject();
        browser["wekdeSelection"] = migratedEnvelope;
        general["browser"] = browser;
        user["general"] = general;
        doc["steamuser"] = user;
        result = WallpaperEngineFollower::resolveSelection(
            doc, configPath(), { root() }, WallpaperEngineFollower::FollowBinding {});
        QCOMPARE(result["selection"].toMap()["workshopid"].toString(), QStringLiteral("123"));
        QCOMPARE(result["provenance"].toMap()["sourceType"].toString(),
                 QStringLiteral("legacyUnassociatedNative"));
    }

    void malformedV2UnassociatedRecordsAreIgnored() {
        const QList<QJsonValue> malformedRecords {
            QJsonValue(QJsonValue::Undefined),
            QJsonValue(QStringLiteral("not-an-object")),
            QJsonValue(QJsonObject {}),
            QJsonValue(QJsonObject {
                { "file", 456 }, { "monitor", "Monitor0" },
                { "savedAtUtc", "2026-11-02T12:00:00.000Z" }
            }),
            QJsonValue(QJsonObject {
                { "file", project("456") + "/project.json" }, { "monitor", 0 },
                { "savedAtUtc", "2026-11-02T12:00:00.000Z" }
            })
        };
        for (const QJsonValue& record : malformedRecords) {
            QJsonObject envelope { { "version", 2 } };
            if (!record.isUndefined()) envelope["unassociated"] = record;
            QJsonObject user = userWithoutNative();
            QJsonObject general = user.value("general").toObject();
            QJsonObject browser = general.value("browser").toObject();
            browser["wekdeSelection"] = envelope;
            general["browser"] = browser;
            user["general"] = general;
            const QJsonObject doc { { "steamuser", user } };
            const auto result = WallpaperEngineFollower::resolveSelection(
                doc, configPath(), { root() }, WallpaperEngineFollower::FollowBinding {});
            QVERIFY(result["selection"].toMap().isEmpty());
        }
    }

    void sameFileCanRemainAssociatedWithTwoDistinctAccounts() {
        const QString idA = QStringLiteral("76561197960265729");
        const QString idB = QStringLiteral("76561198060265728");
        QJsonObject noNative = userWithoutNative();
        QJsonObject general = noNative.value("general").toObject();
        QJsonObject browser = general.value("browser").toObject();
        browser["wekdeSelection"] = QJsonObject {
            { "version", 2 }, { "byUser", QJsonObject {
                { idA, QJsonObject { { "file", project("123") + "/project.json" }, { "monitor", "Monitor0" } } },
                { idB, QJsonObject { { "file", project("123") + "/project.json" }, { "monitor", "Monitor0" } } }
            } }
        };
        general["browser"] = browser;
        noNative["general"] = general;
        const QJsonObject doc { { "steamuser", noNative } };

        WallpaperEngineFollower::FollowBinding binding;
        binding.mode = QStringLiteral("account");
        binding.accountKey = QStringLiteral("steam:") + idA;
        const auto a = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        binding.accountKey = QStringLiteral("steam:") + idB;
        const auto b = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        QCOMPARE(a["selection"].toMap()["workshopid"].toString(), QStringLiteral("123"));
        QCOMPARE(b["selection"].toMap()["workshopid"].toString(), QStringLiteral("123"));
        QVERIFY(a["sourceKey"].toString() != b["sourceKey"].toString());
        QVERIFY(a["sourceKey"].toString().contains(idA));
        QVERIFY(b["sourceKey"].toString().contains(idB));

        binding.mode = QStringLiteral("identifiedCurrent");
        binding.accountKey.clear();
        binding.currentSteamId64 = idA;
        binding.currentIdentityVerified = true;
        const auto activeA = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        binding.currentSteamId64 = idB;
        const auto activeB = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        QCOMPARE(activeA["selection"].toMap()["workshopid"].toString(), QStringLiteral("123"));
        QCOMPARE(activeB["selection"].toMap()["workshopid"].toString(), QStringLiteral("123"));
        QVERIFY(activeA["sourceKey"].toString() != activeB["sourceKey"].toString());
    }
    void invalidSelectionDoesNotInventAWallpaper() {
        for (const QByteArray& data :
             { QByteArray("{"), QByteArray("{}"), config("Z:/missing/project.json") }) {
            QVERIFY(write(configPath(), data));
            QVERIFY(WallpaperEngineFollower::readSelection(configPath(), { root() })["selection"]
                        .toMap()
                        .isEmpty());
        }
    }
    void watchesChangesWithoutPolling() {
        QVERIFY(write(configPath(), config(project("123") + "/project.json")));
        WallpaperEngineFollower follower;
        follower.setConfigPath(configPath());
        follower.setProjectDirs({ root() });
        QSignalSpy reads(&follower, &WallpaperEngineFollower::readCompleted);
        QSignalSpy changes(&follower, &WallpaperEngineFollower::resultChanged);
        follower.setEnabled(true);
        QTRY_COMPARE(follower.selection()["workshopid"].toString(), "123");
        int count = reads.count();
        QTest::qWait(550);
        QCOMPARE(reads.count(), count); // no periodic reads when idle
        QVERIFY(write(tmp.filePath("unrelated.txt"), "unrelated"));
        QTest::qWait(350);
        QCOMPARE(reads.count(), count);

        QVERIFY(write(configPath(), "{"));
        QTRY_VERIFY(reads.count() > count);
        QCOMPARE(follower.selection()["workshopid"].toString(), "123");
        QVERIFY(write(configPath(), config(project("456") + "/project.json"), true));
        QTRY_COMPARE(follower.selection()["workshopid"].toString(), "456");
        const int changeCount = changes.count();
        count                 = reads.count();
        QVERIFY(write(configPath(), config(project("456") + "/project.json"), true));
        QTRY_VERIFY(reads.count() > count);
        QCOMPARE(changes.count(), changeCount); // same selection does not reload renderer

        QVERIFY(QFile::remove(configPath()));
        QTest::qWait(250);
        QVERIFY(write(configPath(), config(project("123") + "/project.json")));
        QTRY_COMPARE(follower.selection()["workshopid"].toString(), "123");
        follower.setEnabled(false);
        count = reads.count();
        QVERIFY(write(configPath(), config(project("456") + "/project.json")));
        QTest::qWait(400);
        QCOMPARE(reads.count(), count);
        QVERIFY(follower.selection().isEmpty());
    }
    void watchesFirstCreation() {
        const QString           later = tmp.filePath("new-install/config.json");
        WallpaperEngineFollower follower;
        follower.setConfigPath(later);
        follower.setProjectDirs({ root() });
        follower.setEnabled(true);
        QTest::qWait(250);
        QVERIFY(write(later, config(project("456") + "/project.json"), true));
        QTRY_COMPARE(follower.selection()["workshopid"].toString(), "456");
    }

    void observesAccountsWithoutEnablingFollowOrReadingAtIdle() {
        QVERIFY(write(configPath(), sectionsConfig({QStringLiteral("Other"), QStringLiteral("steamuser")})));
        WallpaperEngineFollower follower;
        follower.setConfigPath(configPath());
        follower.setProjectDirs({root()});
        QSignalSpy reads(&follower, &WallpaperEngineFollower::readCompleted);
        follower.setObserveAccounts(true);
        QTRY_COMPARE(follower.accounts().size(), 2);
        QVERIFY(follower.accounts().contains(QStringLiteral("Other")));
        QVERIFY(follower.accounts().contains(QStringLiteral("steamuser")));
        QVERIFY(!follower.enabled());
        QVERIFY(follower.selection().isEmpty());
        QVERIFY(follower.provenance().isEmpty());
        QVERIFY(follower.status().isEmpty());
        QCOMPARE(reads.count(), 1);

        const int idleCount = reads.count();
        QTest::qWait(500);
        QCOMPARE(reads.count(), idleCount);
    }

    void observationRetainsLastGoodSectionsAcrossMalformedWrites() {
        QVERIFY(write(configPath(), sectionsConfig({QStringLiteral("steamuser"), QStringLiteral("Other")})));
        WallpaperEngineFollower follower;
        follower.setConfigPath(configPath());
        QSignalSpy reads(&follower, &WallpaperEngineFollower::readCompleted);
        follower.setObserveAccounts(true);
        QTRY_COMPARE(follower.accounts().size(), 2);
        const QStringList lastGood = follower.accounts();
        int readCount = reads.count();

        QVERIFY(write(configPath(), "{"));
        QTRY_VERIFY(reads.count() > readCount);
        QCOMPARE(follower.accounts(), lastGood);
        QVERIFY(follower.selection().isEmpty());
        QVERIFY(follower.provenance().isEmpty());

        readCount = reads.count();
        QVERIFY(write(configPath(), sectionsConfig({QStringLiteral("Other")}), true));
        QTRY_VERIFY(reads.count() > readCount);
        QCOMPARE(follower.accounts(), QStringList{QStringLiteral("Other")});
    }

    void observationDisableTearsDownAndReenableReadsAgain() {
        QVERIFY(write(configPath(), sectionsConfig({QStringLiteral("steamuser")})));
        WallpaperEngineFollower follower;
        follower.setConfigPath(configPath());
        QSignalSpy reads(&follower, &WallpaperEngineFollower::readCompleted);
        follower.setObserveAccounts(true);
        QTRY_COMPARE(follower.accounts(), QStringList{QStringLiteral("steamuser")});
        int readCount = reads.count();

        follower.setObserveAccounts(false);
        QVERIFY(follower.accounts().isEmpty());
        QVERIFY(write(configPath(), sectionsConfig({QStringLiteral("Other")}), true));
        QTest::qWait(400);
        QCOMPARE(reads.count(), readCount);

        follower.setObserveAccounts(true);
        QTRY_COMPARE(follower.accounts(), QStringList{QStringLiteral("Other")});
        QVERIFY(!follower.enabled());
        QVERIFY(follower.selection().isEmpty());
    }

    void observationClearsOnPathChangeAndHandlesFirstCreation() {
        const QString firstPath = configPath();
        const QString secondPath = tmp.filePath(QStringLiteral("second-install/config.json"));
        QVERIFY(write(firstPath, sectionsConfig({QStringLiteral("steamuser")})));
        WallpaperEngineFollower follower;
        follower.setConfigPath(firstPath);
        QSignalSpy reads(&follower, &WallpaperEngineFollower::readCompleted);
        follower.setObserveAccounts(true);
        QTRY_COMPARE(follower.accounts(), QStringList{QStringLiteral("steamuser")});

        follower.setConfigPath(secondPath);
        QVERIFY(follower.accounts().isEmpty());
        QTest::qWait(250);
        const int readCount = reads.count();
        QVERIFY(write(secondPath, sectionsConfig({QStringLiteral("NewUser")} ), true));
        QTRY_VERIFY(reads.count() > readCount);
        QCOMPARE(follower.accounts(), QStringList{QStringLiteral("NewUser")});
        QVERIFY(follower.selection().isEmpty());
        QVERIFY(follower.provenance().isEmpty());
    }

    void observerModeDoesNotCancelNormalFollowOrSelectionResetSemantics() {
        QVERIFY(write(configPath(), config(project("123") + "/project.json")));
        WallpaperEngineFollower follower;
        follower.setConfigPath(configPath());
        follower.setProjectDirs({root()});
        follower.setObserveAccounts(true);
        follower.setEnabled(true);
        QTRY_COMPARE(follower.selection().value("workshopid").toString(), QStringLiteral("123"));
        const QStringList sections = follower.accounts();
        QVERIFY(sections.contains(QStringLiteral("steamuser")));

        follower.setEnabled(false);
        QVERIFY(follower.selection().isEmpty());
        QVERIFY(follower.provenance().isEmpty());
        QVERIFY(follower.status().isEmpty());
        QCOMPARE(follower.accounts(), sections);
        QVERIFY(!follower.enabled());
    }

    void asyncAccountSwitchDiscardsAnInFlightOldBinding() {
        const QString idA = QStringLiteral("76561197960265729");
        const QString idB = QStringLiteral("76561197960265730");
        const QJsonObject doc {
            { idA, userWithNative(project("123") + "/project.json") },
            { idB, userWithNative(project("456") + "/project.json") }
        };
        QVERIFY(write(configPath(), QJsonDocument(doc).toJson()));

        WallpaperEngineFollower follower;
        follower.setConfigPath(configPath());
        follower.setProjectDirs({ root() });
        follower.setFollowMode(QStringLiteral("account"));
        follower.setFollowAccountKey(QStringLiteral("steam:") + idA);
        QSignalSpy reads(&follower, &WallpaperEngineFollower::readCompleted);
        int starts = 0;
        connect(&follower, &WallpaperEngineFollower::readStarted, &follower, [&] {
            ++starts;
            if (starts == 1) follower.setFollowAccountKey(QStringLiteral("steam:") + idB);
        });

        follower.setEnabled(true);
        QTRY_COMPARE(follower.selection().value("workshopid").toString(), QStringLiteral("456"));
        QTRY_COMPARE(reads.count(), 1); // the overtaken account A generation is discarded.
        QCOMPARE(starts, 2);
        QCOMPARE(follower.provenance().value("accountKey").toString(), QStringLiteral("steam:") + idB);
        QCOMPARE(follower.provenance().value("identityStatus").toString(), QStringLiteral("verified"));
        QVERIFY(follower.status().contains(QStringLiteral("selection")));
        QTest::qWait(250);
        QCOMPARE(reads.count(), 1);
    }

    void asyncManualSourceSwitchUpdatesSelectionAndProvenance() {
        const QJsonObject doc {
            { "steamuser", userWithNative(project("123") + "/project.json") },
            { "Other", userWithNative(project("456") + "/project.json") }
        };
        QVERIFY(write(configPath(), QJsonDocument(doc).toJson()));
        WallpaperEngineFollower follower;
        follower.setConfigPath(configPath());
        follower.setProjectDirs({ root() });
        follower.setFollowMode(QStringLiteral("manualMapping"));
        follower.setFollowSourceKey(bindingSource(QStringLiteral("steamuser")));
        follower.setEnabled(true);
        QTRY_COMPARE(follower.selection().value("workshopid").toString(), QStringLiteral("123"));
        QCOMPARE(follower.provenance().value("identityStatus").toString(), QStringLiteral("userAssociated"));

        const QString firstSource = follower.provenance().value("sourceKey").toString();
        follower.setFollowSourceKey(bindingSource(QStringLiteral("Other")));
        QTRY_COMPARE(follower.selection().value("workshopid").toString(), QStringLiteral("456"));
        QVERIFY(follower.provenance().value("sourceKey").toString() != firstSource);
        QVERIFY(follower.provenance().value("sourceKey").toString().contains(QStringLiteral("Other")));
        QCOMPARE(follower.provenance().value("mode").toString(), QStringLiteral("manualMapping"));
    }

    void bridgeOutputEndToEndSwitchesAccountsAndLegacy() {
        const QString fixturePath = qEnvironmentVariable("WEKDE_BRIDGE_FOLLOWER_FIXTURE");
        if (fixturePath.isEmpty()) QSKIP("Run through test_bridge_follower_handoff.js to provide the bridge output fixture.");
        QFile fixture(fixturePath);
        QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(fixturePath));
        const QJsonObject envelope = QJsonDocument::fromJson(fixture.readAll()).object();
        QCOMPARE(envelope.value("version").toInt(), 2);

        const QString idA = QStringLiteral("76561197960265729");
        const QString idB = QStringLiteral("76561197960265730");
        QJsonObject user = userWithoutNative();
        QJsonObject general = user.value("general").toObject();
        QJsonObject browser = general.value("browser").toObject();
        browser["wekdeSelection"] = envelope;
        general["browser"] = browser;
        user["general"] = general;
        const QJsonObject doc { { "steamuser", user } };

        WallpaperEngineFollower::FollowBinding binding;
        binding.mode = QStringLiteral("account");
        binding.accountKey = QStringLiteral("steam:") + idA;
        const auto a = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        QCOMPARE(a["selection"].toMap().value("workshopid").toString(), QStringLiteral("123"));
        QCOMPARE(a["provenance"].toMap().value("identityStatus").toString(), QStringLiteral("verified"));

        binding.accountKey = QStringLiteral("steam:") + idB;
        const auto b = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        QCOMPARE(b["selection"].toMap().value("workshopid").toString(), QStringLiteral("456"));
        QVERIFY(a["sourceKey"].toString() != b["sourceKey"].toString());

        binding.mode = QStringLiteral("legacySession");
        binding.accountKey.clear();
        const auto legacy = WallpaperEngineFollower::resolveSelection(doc, configPath(), { root() }, binding);
        QCOMPARE(legacy["selection"].toMap().value("workshopid").toString(), QStringLiteral("789"));
        QCOMPARE(legacy["provenance"].toMap().value("identityStatus").toString(),
                 QStringLiteral("unassociated"));
        QVERIFY(legacy["provenance"].toMap().value("accountKey").toString().isEmpty());

        // Re-run through the async class: the read starts with A, then the
        // binding changes before completion. Only B's actual output may land.
        QVERIFY(write(configPath(), QJsonDocument(doc).toJson()));
        WallpaperEngineFollower follower;
        follower.setConfigPath(configPath());
        follower.setProjectDirs({ root() });
        follower.setFollowMode(QStringLiteral("account"));
        follower.setFollowAccountKey(QStringLiteral("steam:") + idA);
        QSignalSpy reads(&follower, &WallpaperEngineFollower::readCompleted);
        int starts = 0;
        connect(&follower, &WallpaperEngineFollower::readStarted, &follower, [&] {
            if (++starts == 1) follower.setFollowAccountKey(QStringLiteral("steam:") + idB);
        });
        follower.setEnabled(true);
        QTRY_COMPARE(follower.selection().value("workshopid").toString(), QStringLiteral("456"));
        QTRY_COMPARE(reads.count(), 1);
        QCOMPARE(starts, 2);
        QCOMPARE(follower.provenance().value("sourceKey").toString(), b["sourceKey"].toString());
        QCOMPARE(follower.provenance().value("accountKey").toString(), QStringLiteral("steam:") + idB);
    }
};
QTEST_GUILESS_MAIN(TestWallpaperEngineFollower)
#include "tst_wallpaperenginefollower.moc"
