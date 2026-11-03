// SPDX-License-Identifier: GPL-2.0-only
// Pure fixtures for Steam account identity, subscription cache state and merge.
#include <QtTest>
#include <QDir>
#include <QTemporaryDir>

#include <utility>

#include "SteamAccounts.hpp"

using namespace steam::accounts;

namespace
{
const QString kSteam64A = QStringLiteral("76561197960265729");
const QString kSteam64B = QStringLiteral("76561198060265728"); // account component 100000000

QString loginUsers() {
    return QStringLiteral("\"users\" { \"%1\" { \"PersonaName\" \"Player A\" } "
                          "\"%2\" { \"PersonaName\" \"Player B\" } }")
        .arg(kSteam64A, kSteam64B);
}

QString subscriptionCache(const QStringList& ids, const QStringList& disabled = {}) {
    QString result = QStringLiteral("\"AppState\" { \"appid\" \"431960\" } "
                                    "\"subscribedfiles\" { \"appid\" \"431960\" "
                                    "\"time_last_updated\" \"1700000000\" ");
    for (qsizetype i = 0; i < ids.size(); ++i) {
        const QString id = ids.at(i);
        result += QStringLiteral("\"%1\" { \"publishedfileid\" \"%2\" "
                                 "\"disabled_locally\" \"%3\" } ")
                      .arg(i).arg(id, disabled.contains(id) ? QStringLiteral("1") : QStringLiteral("0"));
    }
    result += QStringLiteral("}");
    return result;
}

CacheInput cache(QString accountId, QString path, QString text) {
    CacheInput input;
    input.accountId32 = std::move(accountId);
    input.path = std::move(path);
    input.text = std::move(text);
    input.lastSuccessUtc = QStringLiteral("2026-11-02T12:00:00Z");
    return input;
}
}

class TestSteamAccounts : public QObject {
    Q_OBJECT

private slots:
    void unionsUsersAndSubtractsDisabledPerCache() {
        const QString root = QStringLiteral("/tmp/steam-a");
        InstallationInput install { root, loginUsers(), {
            cache(QStringLiteral("1"), QStringLiteral("a/subscriptions.vdf"), subscriptionCache({"11", "22"})),
            cache(QStringLiteral("100000000"), QStringLiteral("b/subscriptions.vdf"),
                  subscriptionCache({"22", "33"}, {"22"})),
        } };
        const Catalog catalog = buildCatalog({ install }, 7);
        QCOMPARE(catalog.schemaVersion, 1);
        QCOMPARE(catalog.generation, quint64(7));
        QCOMPARE(catalog.accounts.size(), 2);
        QCOMPARE(catalog.caches.size(), 2);
        QCOMPARE(catalog.accounts.first().identityStatus, QStringLiteral("verified"));
        QCOMPARE(catalog.accounts.first().steamId64, kSteam64A);
        QCOMPARE(selectedSubscriptionIds(catalog, { QStringLiteral("steam:") + kSteam64A }),
                 QStringList({"11", "22"}));
        QCOMPARE(selectedSubscriptionIds(catalog, { QStringLiteral("steam:") + kSteam64B }),
                 QStringList({"33"}));
        QCOMPARE(selectedSubscriptionIds(catalog, { QStringLiteral("steam:") + kSteam64A,
                                                     QStringLiteral("steam:") + kSteam64B }),
                 QStringList({"11", "22", "33"}));
        QVERIFY(selectedSubscriptionIds(catalog, {}).isEmpty());
        QCOMPARE(catalog.toJson().value(QStringLiteral("schemaVersion")).toInt(), 1);
    }

    void unresolvedIdentityIncludesCanonicalRootAndExactId() {
        const QString root = QStringLiteral("/tmp/unresolved/root");
        const Catalog catalog = buildCatalog({
            { root, QStringLiteral("\"users\" { }") ,
              { cache(QStringLiteral("42"), QStringLiteral("ugc/cache.vdf"), subscriptionCache({"123456789012345"})) } },
        });
        QCOMPARE(catalog.accounts.size(), 1);
        const Account account = catalog.accounts.first();
        QCOMPARE(account.identityStatus, QStringLiteral("unresolved"));
        QCOMPARE(account.accountId32, QStringLiteral("42"));
        QVERIFY(account.steamId64.isEmpty());
        QVERIFY(account.key.startsWith(QStringLiteral("userdata:")));
        QCOMPARE(catalog.caches.first().subscribedIds, QStringList({"123456789012345"}));
    }

    void distinguishesEmptyInvalidAndRetainsPriorMembership() {
        CacheInput prior = cache(QStringLiteral("1"), QStringLiteral("cache.vdf"), subscriptionCache({"9"}));
        InstallationInput base { QStringLiteral("/tmp/steam-state"), loginUsers(), { prior } };
        const Catalog good = buildCatalog({base});
        QCOMPARE(good.caches.first().state, CacheState::Valid);
        QCOMPARE(good.caches.first().subscribedIds, QStringList({"9"}));

        CacheInput emptyInput = cache(QStringLiteral("1"), QStringLiteral("cache.vdf"),
                                       subscriptionCache({}));
        InstallationInput emptyInstall { base.root, base.loginUsersText, { emptyInput } };
        const Catalog empty = buildCatalog({emptyInstall});
        QCOMPARE(empty.caches.first().state, CacheState::Empty);
        QVERIFY(empty.caches.first().subscribedIds.isEmpty());

        CacheInput malformed = cache(QStringLiteral("1"), QStringLiteral("cache.vdf"),
                                     QStringLiteral("\"subscribedfiles\" { \"0\" {"));
        malformed.previous = good.caches.first();
        InstallationInput malformedInstall { base.root, base.loginUsersText, { malformed } };
        const Catalog invalid = buildCatalog({malformedInstall});
        QCOMPARE(invalid.caches.first().state, CacheState::Invalid);
        QCOMPARE(invalid.caches.first().subscribedIds, QStringList({"9"}));
        QCOMPARE(invalid.caches.first().lastSuccessUtc, good.caches.first().lastSuccessUtc);

        CacheInput wrongApp = cache(QStringLiteral("1"), QStringLiteral("cache.vdf"),
                                    QStringLiteral("\"appid\" \"999\" ") +
                                    subscriptionCache({"44"}));
        wrongApp.previous = good.caches.first();
        const Catalog wrongAppCatalog = buildCatalog({{base.root, base.loginUsersText, {wrongApp}}});
        QCOMPARE(wrongAppCatalog.caches.first().state, CacheState::Invalid);
        QCOMPARE(wrongAppCatalog.caches.first().subscribedIds, QStringList({"9"}));

        CacheInput missing;
        missing.accountId32 = QStringLiteral("1");
        missing.path = QStringLiteral("cache.vdf");
        missing.missing = true;
        missing.previous = good.caches.first();
        const Catalog absent = buildCatalog({{base.root, base.loginUsersText, {missing}}});
        QCOMPARE(absent.caches.first().state, CacheState::Missing);
        QCOMPARE(absent.caches.first().subscribedIds, QStringList({"9"}));

        CacheInput unreadable = missing;
        unreadable.missing = false;
        unreadable.available = false;
        unreadable.previous = good.caches.first();
        const Catalog unavailable = buildCatalog({{base.root, base.loginUsersText, {unreadable}}});
        QCOMPARE(unavailable.caches.first().state, CacheState::Unavailable);
        QCOMPARE(unavailable.caches.first().subscribedIds, QStringList({"9"}));
    }

    void deduplicatesCanonicalInstallationAndCacheRoots() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = tmp.filePath(QStringLiteral("Steam"));
        const QString alias = tmp.filePath(QStringLiteral("alias"));
        QVERIFY(QDir().mkpath(root));
        if (!QFile::link(root, alias)) QSKIP("Directory symlinks are unavailable in this environment");

        CacheInput input = cache(QStringLiteral("1"), QStringLiteral("userdata/1/cache.vdf"),
                                 subscriptionCache({"22"}));
        const InstallationInput first { root, loginUsers(), { input } };
        const InstallationInput second { alias, loginUsers(), { input } };
        const Catalog catalog = buildCatalog({first, second});
        QCOMPARE(catalog.installations.size(), 1);
        QCOMPARE(catalog.accounts.size(), 1);
        QCOMPARE(catalog.caches.size(), 1);
    }
};

QTEST_APPLESS_MAIN(TestSteamAccounts)
#include "tst_steamaccounts.moc"
