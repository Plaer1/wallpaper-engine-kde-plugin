#include "SteamAccounts.hpp"

#include "SteamKeyValues.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <limits>

namespace steam::accounts
{
namespace
{

constexpr quint64 kPublicIndividualBase = 76561197960265728ULL;

struct LoginIdentity {
    QString steamId64;
    QString label;
};

QString canonicalRoot(const QString& root) {
    const QFileInfo info(root);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

QString rootDigest(const QString& root) {
    return QString::fromLatin1(QCryptographicHash::hash(canonicalRoot(root).toUtf8(),
                                                       QCryptographicHash::Sha256).toHex());
}

bool decimal(const QString& text, quint64 max, quint64& value) {
    if (text.isEmpty() || (text.size() > 1 && text.startsWith(u'0'))) return false;
    for (const QChar c : text) if (c < u'0' || c > u'9') return false;
    bool ok = false;
    value = text.toULongLong(&ok, 10);
    return ok && value <= max;
}

QHash<QString, QList<LoginIdentity>> parseLoginUsers(const QString& text) {
    QHash<QString, QList<LoginIdentity>> byAccountId;
    steam::kv::Node root;
    if (!steam::kv::parse(text, root)) return byAccountId;
    const auto* users = root.child(QStringLiteral("users"));
    if (!users || !users->isObject) return byAccountId;

    for (const auto& [key, fields] : users->children) {
        quint64 steamId = 0;
        if (!decimal(key, std::numeric_limits<quint64>::max(), steamId) || steamId <= kPublicIndividualBase)
            continue;
        const quint64 accountId = steamId - kPublicIndividualBase;
        if (accountId == 0 || accountId > std::numeric_limits<quint32>::max()) continue;
        // The SteamID64 must be the canonical public individual mapping, not
        // merely any large decimal identity copied from an unrelated field.
        if (kPublicIndividualBase + accountId != steamId) continue;

        LoginIdentity identity;
        identity.steamId64 = key;
        if (const auto* persona = fields.child(QStringLiteral("PersonaName")); persona && !persona->isObject)
            identity.label = persona->value;
        const QString accountKey = QString::number(accountId);
        byAccountId[accountKey].append(identity);
    }
    return byAccountId;
}

QStringList sorted(const QSet<QString>& values) {
    QStringList result = values.values();
    std::sort(result.begin(), result.end());
    return result;
}

QString parseSourceTime(const steam::kv::Node& root) {
    const auto* folders = root.child(QStringLiteral("subscribedfiles"));
    if (!folders) return {};
    const auto* updated = folders->child(QStringLiteral("time_last_updated"));
    if (!updated || updated->isObject) return {};
    quint64 epoch = 0;
    if (!decimal(updated->value, std::numeric_limits<quint64>::max(), epoch) ||
        epoch > static_cast<quint64>(std::numeric_limits<qint64>::max())) return {};
    const QDateTime date = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(epoch), QTimeZone::UTC);
    return date.isValid() ? date.toString(Qt::ISODateWithMs) : QString();
}

CacheSnapshot parseCache(const QString& root, const CacheInput& input, const QString& accountKey) {
    CacheSnapshot cache = input.previous;
    const QString canonical = canonicalRoot(root);
    cache.installationRoot = canonical;
    cache.accountKey = accountKey;
    cache.accountId32 = input.accountId32;
    cache.path = input.path;
    cache.key = QStringLiteral("cache:%1:%2").arg(rootDigest(canonical), input.accountId32);

    if (!input.available) {
        cache.state = CacheState::Unavailable;
        cache.error = QStringLiteral("Unable to read subscription cache");
        return cache;
    }
    if (input.missing) {
        cache.state = CacheState::Missing;
        cache.error = QStringLiteral("Subscription cache is missing");
        return cache;
    }

    steam::kv::Node rootNode;
    if (input.invalid || !steam::kv::parse(input.text, rootNode)) {
        cache.state = CacheState::Invalid;
        cache.error = QStringLiteral("Malformed subscription cache");
        return cache;
    }
    if (const auto* appId = rootNode.child(QStringLiteral("appid"));
        appId && (appId->isObject || appId->value != QStringLiteral("431960"))) {
        cache.state = CacheState::Invalid;
        cache.error = QStringLiteral("Unexpected subscription appid");
        return cache;
    }
    const auto* files = rootNode.child(QStringLiteral("subscribedfiles"));
    if (!files || !files->isObject) {
        cache.state = CacheState::Invalid;
        cache.error = QStringLiteral("Missing subscribedfiles object");
        return cache;
    }

    if (const auto* appId = files->child(QStringLiteral("appid"));
        appId && (appId->isObject || appId->value != QStringLiteral("431960"))) {
        cache.state = CacheState::Invalid;
        cache.error = QStringLiteral("Unexpected subscription appid");
        return cache;
    }

    QSet<QString> subscribed;
    QSet<QString> disabled;
    for (const auto& [recordKey, record] : files->children) {
        if (recordKey.compare(QStringLiteral("appid"), Qt::CaseInsensitive) == 0 ||
            recordKey.compare(QStringLiteral("time_last_updated"), Qt::CaseInsensitive) == 0) continue;
        // Steam subscription records are indexed numerically. Ignore unknown
        // scalar metadata; reject malformed numbered records rather than
        // misreporting a partial cache as a valid empty one.
        bool numericRecord = false;
        recordKey.toUInt(&numericRecord);
        if (!numericRecord) continue;
        if (!record.isObject) {
            cache.state = CacheState::Invalid;
            cache.error = QStringLiteral("Subscription record is not an object");
            return cache;
        }
        const auto* published = record.child(QStringLiteral("publishedfileid"));
        if (!published || published->isObject) {
            cache.state = CacheState::Invalid;
            cache.error = QStringLiteral("Subscription record has no publishedfileid");
            return cache;
        }
        quint64 publishedId = 0;
        if (!decimal(published->value, std::numeric_limits<quint64>::max(), publishedId) || publishedId == 0) {
            cache.state = CacheState::Invalid;
            cache.error = QStringLiteral("Invalid publishedfileid");
            return cache;
        }
        subscribed.insert(published->value);
        const auto* disabledValue = record.child(QStringLiteral("disabled_locally"));
        if (disabledValue && (disabledValue->value == QStringLiteral("1") ||
                              disabledValue->value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0))
            disabled.insert(published->value);
    }

    cache.state = subscribed.isEmpty() ? CacheState::Empty : CacheState::Valid;
    cache.lastSuccessUtc = input.lastSuccessUtc;
    cache.sourceUpdatedUtc = parseSourceTime(rootNode);
    cache.subscribedIds = sorted(subscribed);
    cache.disabledIds = sorted(disabled);
    cache.error.clear();
    return cache;
}

} // namespace

QString cacheStateName(CacheState state) {
    switch (state) {
    case CacheState::Valid: return QStringLiteral("valid");
    case CacheState::Empty: return QStringLiteral("empty");
    case CacheState::Missing: return QStringLiteral("missing");
    case CacheState::Invalid: return QStringLiteral("invalid");
    case CacheState::Unavailable: return QStringLiteral("unavailable");
    }
    return QStringLiteral("invalid");
}

Catalog buildCatalog(const QList<InstallationInput>& inputs, quint64 generation) {
    Catalog catalog;
    catalog.generation = generation;
    QSet<QString> seenRoots;
    QHash<QString, Account> accountByKey;
    QSet<QString> seenCacheKeys;

    for (const InstallationInput& input : inputs) {
        const QString root = canonicalRoot(input.root);
        const auto identities = parseLoginUsers(input.loginUsersText);
        if (!seenRoots.contains(root)) {
            seenRoots.insert(root);
            catalog.installations.append({ root });
        }
        for (const CacheInput& cacheInput : input.caches) {
            quint64 accountIdValue = 0;
            if (!decimal(cacheInput.accountId32, std::numeric_limits<quint32>::max(), accountIdValue) || accountIdValue == 0)
                continue;
            const QString accountId = QString::number(accountIdValue);
            const QList<LoginIdentity> loginMatches = identities.value(accountId);
            const bool currentVerified = loginMatches.size() == 1;
            const bool canRetainPrevious = !cacheInput.previous.accountKey.isEmpty() &&
                (cacheInput.missing || !cacheInput.available || cacheInput.invalid || !currentVerified);
            const bool previousVerified = canRetainPrevious &&
                cacheInput.previous.identityStatus == QStringLiteral("verified") &&
                cacheInput.previous.accountId32 == accountId;
            const bool verified = currentVerified || previousVerified;
            const QString steamId64 = currentVerified ? loginMatches.first().steamId64
                : (previousVerified ? cacheInput.previous.steamId64 : QString());
            const QString accountKey = currentVerified ? QStringLiteral("steam:") + steamId64
                : (canRetainPrevious ? cacheInput.previous.accountKey
                                     : QStringLiteral("userdata:%1:%2").arg(rootDigest(root), accountId));
            const QString cacheKey = QStringLiteral("cache:%1:%2").arg(rootDigest(root), accountId);
            if (seenCacheKeys.contains(cacheKey)) continue;
            seenCacheKeys.insert(cacheKey);

            Account account = accountByKey.value(accountKey);
            if (account.key.isEmpty()) {
                account.key = accountKey;
                account.steamId64 = steamId64;
                account.accountId32 = accountId;
                account.identityStatus = verified ? QStringLiteral("verified") : QStringLiteral("unresolved");
                account.label = currentVerified && !loginMatches.first().label.isEmpty()
                    ? loginMatches.first().label
                    : (previousVerified && !cacheInput.previous.accountLabel.isEmpty()
                        ? cacheInput.previous.accountLabel
                        : QStringLiteral("Steam account %1").arg(accountId));
            }
            CacheSnapshot cache = parseCache(root, cacheInput, accountKey);
            cache.steamId64 = account.steamId64;
            cache.accountLabel = account.label;
            cache.identityStatus = account.identityStatus;
            account.cacheKeys.append(cache.key);
            catalog.caches.append(cache);
            accountByKey.insert(accountKey, account);
        }
    }
    for (auto it = accountByKey.cbegin(); it != accountByKey.cend(); ++it) catalog.accounts.append(it.value());
    std::sort(catalog.accounts.begin(), catalog.accounts.end(), [](const Account& a, const Account& b) {
        return a.key < b.key;
    });
    std::sort(catalog.installations.begin(), catalog.installations.end(), [](const Installation& a, const Installation& b) {
        return a.root < b.root;
    });
    return catalog;
}

QStringList selectedSubscriptionIds(const Catalog& catalog, const QStringList& accountKeys) {
    const QSet<QString> selected(accountKeys.cbegin(), accountKeys.cend());
    QSet<QString> result;
    for (const CacheSnapshot& cache : catalog.caches) {
        if (!selected.contains(cache.accountKey)) continue;
        QSet<QString> disabled(cache.disabledIds.cbegin(), cache.disabledIds.cend());
        for (const QString& id : cache.subscribedIds) if (!disabled.contains(id)) result.insert(id);
    }
    return sorted(result);
}

QJsonObject Catalog::toJson() const {
    QJsonArray installationArray;
    for (const auto& installation : installations)
        installationArray.append(QJsonObject { { QStringLiteral("root"), installation.root } });
    QJsonArray accountArray;
    for (const auto& account : accounts) {
        QJsonArray cacheKeys;
        for (const QString& key : account.cacheKeys) cacheKeys.append(key);
        accountArray.append(QJsonObject {
            { QStringLiteral("key"), account.key }, { QStringLiteral("steamId64"), account.steamId64 },
            { QStringLiteral("accountId32"), account.accountId32 }, { QStringLiteral("label"), account.label },
            { QStringLiteral("identityStatus"), account.identityStatus }, { QStringLiteral("cacheKeys"), cacheKeys },
        });
    }
    QJsonArray cacheArray;
    for (const auto& cache : caches) {
        QJsonArray subscribed;
        for (const QString& id : cache.subscribedIds) subscribed.append(id);
        QJsonArray disabled;
        for (const QString& id : cache.disabledIds) disabled.append(id);
        cacheArray.append(QJsonObject {
            { QStringLiteral("key"), cache.key }, { QStringLiteral("installationRoot"), cache.installationRoot },
            { QStringLiteral("accountKey"), cache.accountKey }, { QStringLiteral("path"), cache.path },
            { QStringLiteral("state"), cacheStateName(cache.state) },
            { QStringLiteral("lastSuccessUtc"), cache.lastSuccessUtc },
            { QStringLiteral("sourceUpdatedUtc"), cache.sourceUpdatedUtc },
            { QStringLiteral("subscribedIds"), subscribed }, { QStringLiteral("disabledIds"), disabled },
            { QStringLiteral("error"), cache.error },
        });
    }
    return { { QStringLiteral("schemaVersion"), schemaVersion },
             { QStringLiteral("generation"), static_cast<double>(generation) },
             { QStringLiteral("installations"), installationArray },
             { QStringLiteral("accounts"), accountArray },
             { QStringLiteral("caches"), cacheArray } };
}

} // namespace steam::accounts
