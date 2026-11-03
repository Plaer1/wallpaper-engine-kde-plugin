#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// Pure parsers and value types for Steam's public account metadata and cached
// Wallpaper Engine subscription lists. Filesystem discovery/watchers live above
// this unit so fixtures can exercise the catalog without a Steam installation.
namespace steam::accounts
{

enum class CacheState { Valid, Empty, Missing, Invalid, Unavailable };

struct CacheSnapshot {
    QString key;
    QString installationRoot;
    QString accountKey;
    QString accountId32; // internal provenance for last-good snapshot matching
    QString steamId64; // internal identity retention during unavailable snapshots
    QString accountLabel;
    QString identityStatus;
    QString path;
    CacheState state { CacheState::Missing };
    QString lastSuccessUtc;
    QString sourceUpdatedUtc;
    QStringList subscribedIds;
    QStringList disabledIds;
    QString error;
};

struct Account {
    QString key;
    QString steamId64;
    QString accountId32;
    QString label;
    QString identityStatus; // "verified" or "unresolved"
    QStringList cacheKeys;
};

struct Installation {
    QString root;
};

struct CacheInput {
    QString accountId32;
    QString path;
    QString text;
    bool available { true };
    bool missing { false };
    bool invalid { false };
    QString lastSuccessUtc;
    CacheSnapshot previous;
};

struct InstallationInput {
    QString root;
    QString loginUsersText;
    QList<CacheInput> caches;
};

struct Catalog {
    int schemaVersion { 1 };
    quint64 generation { 0 };
    QList<Installation> installations;
    QList<Account> accounts;
    QList<CacheSnapshot> caches;

    QJsonObject toJson() const;
};

// Build a catalog snapshot from already-read metadata. Identity is verified
// only when loginusers contains one canonical public individual SteamID64
// whose account component matches the userdata directory ID.
Catalog buildCatalog(const QList<InstallationInput>& installations, quint64 generation = 0);

// Membership applies disabled IDs per source cache before unioning the selected
// accounts. An explicitly empty account-key list returns an empty set.
QStringList selectedSubscriptionIds(const Catalog& catalog, const QStringList& accountKeys);
QString cacheStateName(CacheState state);

} // namespace steam::accounts
