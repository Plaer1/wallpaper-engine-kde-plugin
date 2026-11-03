// Read-only, anonymized real-data check for the native account catalog.
#include "SteamAccounts.hpp"
#include "SteamAccountsWatcher.hpp"
#include "SteamPaths.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>
#include <QTimer>

#include <algorithm>

namespace {

using steam::accounts::Account;
using steam::accounts::CacheSnapshot;
using steam::accounts::CacheState;
using steam::accounts::Catalog;

QJsonObject summarize(const Catalog& catalog) {
    QList<Account> verified;
    int unresolved = 0;
    for (const Account& account : catalog.accounts) {
        if (account.identityStatus == QStringLiteral("verified")) verified.append(account);
        else ++unresolved;
    }

    QJsonObject stateCounts {
        { "valid", 0 }, { "empty", 0 }, { "missing", 0 }, { "invalid", 0 }, { "unavailable", 0 }
    };
    for (const CacheSnapshot& cache : catalog.caches) {
        const QString name = steam::accounts::cacheStateName(cache.state);
        stateCounts[name] = stateCounts.value(name).toInt() + 1;
    }

    QList<int> subscriptionCounts;
    QStringList verifiedKeys;
    for (const Account& account : verified) {
        const int count = steam::accounts::selectedSubscriptionIds(catalog, { account.key }).size();
        subscriptionCounts.append(count);
        verifiedKeys.append(account.key);
    }
    std::sort(subscriptionCounts.begin(), subscriptionCounts.end());
    QJsonArray accountCounts;
    for (int count : subscriptionCounts) accountCounts.append(count);

    const QStringList unionIds = steam::accounts::selectedSubscriptionIds(catalog, verifiedKeys);
    QJsonValue overlapCount = QJsonValue::Null;
    QJsonObject comparison { { "status", "not-comparable" } };
    if (verified.size() == 2) {
        const QStringList first = steam::accounts::selectedSubscriptionIds(catalog, { verified.at(0).key });
        const QStringList second = steam::accounts::selectedSubscriptionIds(catalog, { verified.at(1).key });
        const QSet<QString> firstSet(first.cbegin(), first.cend());
        int overlap = 0;
        for (const QString& id : second) if (firstSet.contains(id)) ++overlap;
        overlapCount = overlap;

        bool healthy = true;
        for (const Account& account : verified) {
            for (const CacheSnapshot& cache : catalog.caches) {
                if (cache.accountKey != account.key) continue;
                if (cache.state != CacheState::Valid && cache.state != CacheState::Empty) healthy = false;
            }
        }
        if (healthy) {
            comparison = QJsonObject {
                { "status", subscriptionCounts == QList<int> { 61, 103 } && overlap == 7
                                && unionIds.size() == 157 ? "matches-audited-snapshot" : "changed" },
                { "subscription_count_deltas_sorted", QJsonArray {
                    subscriptionCounts.at(0) - 61, subscriptionCounts.at(1) - 103 } },
                { "overlap_delta", overlap - 7 }, { "union_delta", unionIds.size() - 157 }
            };
        } else {
            comparison = QJsonObject { { "status", "not-comparable-stale-or-invalid-cache" } };
        }
    }

    return {
        { "installation_count", catalog.installations.size() },
        { "account_count", catalog.accounts.size() },
        { "verified_account_count", verified.size() },
        { "unresolved_account_count", unresolved },
        { "cache_state_counts", stateCounts },
        { "verified_subscription_counts_sorted", accountCounts },
        { "verified_subscription_union_count", unionIds.size() },
        { "two_account_overlap_count", overlapCount },
        { "audited_snapshot_comparison", comparison }
    };
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList roots = steam::steamRoots();
    if (roots.isEmpty()) {
        QTextStream(stdout) << "{\"status\":\"no-steam-root-detected\",\"installation_count\":0}\n";
        return 2;
    }

    wekde::SteamAccountsWatcher watcher([roots] { return roots; });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &app, [&app] {
        QTextStream(stdout) << "{\"status\":\"catalog-scan-timeout\"}\n";
        app.exit(3);
    });
    QObject::connect(&watcher, &wekde::SteamAccountsWatcher::catalogChanged, &app, [&] {
        QTextStream(stdout) << QJsonDocument(summarize(watcher.catalog())).toJson(QJsonDocument::Compact)
                            << Qt::endl;
        app.exit(0);
    });
    timeout.start(30000);
    return app.exec();
}
