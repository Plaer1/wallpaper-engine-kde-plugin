#include "SteamAccountsWatcher.hpp"
#include "SteamPaths.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>
#include <limits>
#include <utility>

namespace wekde {
namespace {
constexpr qint64 kMaxKeyValuesBytes = 8 * 1024 * 1024;
QString canonicalRoot(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}
QString nearestExistingDirectory(QString path) {
    path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    while (!path.isEmpty() && !QFileInfo(path).isDir()) {
        const QString parent = QFileInfo(path).absolutePath();
        if (parent == path) return {};
        path = parent;
    }
    return path;
}
QStringList rootCandidates() {
    const QString home = QDir::homePath();
    QString data = qEnvironmentVariable("XDG_DATA_HOME");
    if (data.isEmpty()) data = home + QStringLiteral("/.local/share");
    return { home + QStringLiteral("/.steam/steam"), home + QStringLiteral("/.steam/root"),
             data + QStringLiteral("/Steam"), home + QStringLiteral("/.steam/debian-installation"),
             home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam"),
             home + QStringLiteral("/.var/app/com.valvesoftware.Steam/data/Steam"),
             home + QStringLiteral("/snap/steam/common/.local/share/Steam"), home + QStringLiteral("/Steam") };
}
QString readText(const QString& path, bool& available) {
    available = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 0 || file.size() > kMaxKeyValuesBytes) return {};
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError || bytes.size() != file.size()) return {};
    available = true;
    return QString::fromUtf8(bytes);
}
QString accountIdFromDir(const QFileInfo& info) {
    bool ok = false;
    const qulonglong id = info.fileName().toULongLong(&ok, 10);
    if (!ok || id == 0 || id > std::numeric_limits<quint32>::max()) return {};
    return QString::number(id);
}
const steam::accounts::CacheSnapshot* previousCache(const steam::accounts::Catalog& previous,
                                                      const QString& root, const QString& accountId) {
    for (const auto& cache : previous.caches)
        if (cache.installationRoot == root && cache.accountId32 == accountId) return &cache;
    return nullptr;
}
void attachPrevious(steam::accounts::CacheInput& input, const steam::accounts::Catalog& previous,
                    const QString& root, const QString& accountId) {
    if (const auto* old = previousCache(previous, root, accountId)) input.previous = *old;
}
}

SteamAccountsWatcher::SteamAccountsWatcher(RootProvider rootProvider, QObject* parent)
    : QObject(parent),
      m_rootProvider(rootProvider ? std::move(rootProvider) : [] { return steam::steamRoots(); }),
      m_currentGeneration(std::make_shared<std::atomic<quint64>>(0)) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(180);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &SteamAccountsWatcher::scheduleRefresh);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &SteamAccountsWatcher::scheduleRefresh);
    connect(&m_debounce, &QTimer::timeout, this, &SteamAccountsWatcher::refresh);
    connect(&m_future, &QFutureWatcher<ScanResult>::finished, this, [this] {
        finishScan(m_future.result());
    });
    refresh();
}

void SteamAccountsWatcher::refresh() {
    ++m_generation;
    m_currentGeneration->store(m_generation, std::memory_order_relaxed);
    m_debounce.stop();
    syncWatches();
    if (m_running) { m_pending = true; return; }
    startScan();
}
void SteamAccountsWatcher::scheduleRefresh() { m_debounce.start(); }
void SteamAccountsWatcher::startScan() {
    if (m_running) { m_pending = true; return; }
    m_running = true;
    const quint64 generation = m_generation;
    const QStringList roots = m_rootProvider();
    const steam::accounts::Catalog previous = m_catalog;
    const auto current = m_currentGeneration;
    m_future.setFuture(QtConcurrent::run([roots, previous, generation, current] {
        return scan(roots, previous, generation, current);
    }));
}

SteamAccountsWatcher::~SteamAccountsWatcher() {
    m_currentGeneration->fetch_add(1, std::memory_order_relaxed);
}

void SteamAccountsWatcher::finishScan(ScanResult result) {
    m_running = false;
    if (result.generation == m_generation) {
        m_catalog = std::move(result.catalog);
        syncWatches();
        emit catalogChanged();
    }
    if (m_pending || result.generation != m_generation) {
        m_pending = false;
        startScan();
    }
}

void SteamAccountsWatcher::syncWatches() {
    QSet<QString> directories;
    QSet<QString> files;
    const auto addExistingParent = [&directories](const QString& path) {
        const QString existing = nearestExistingDirectory(path);
        if (!existing.isEmpty()) directories.insert(existing);
    };
    for (const QString& candidate : rootCandidates()) addExistingParent(QFileInfo(candidate).absolutePath());
    for (const QString& rawRoot : m_rootProvider()) {
        const QString root = canonicalRoot(rawRoot);
        addExistingParent(root);
        const QString loginFile = root + QStringLiteral("/config/loginusers.vdf");
        addExistingParent(QFileInfo(loginFile).absolutePath());
        if (QFileInfo(loginFile).isFile()) files.insert(loginFile);
        const QString userdata = root + QStringLiteral("/userdata");
        addExistingParent(userdata);
        if (!QFileInfo(userdata).isDir()) continue;
        directories.insert(userdata);
        for (const QFileInfo& userInfo : QDir(userdata).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (accountIdFromDir(userInfo).isEmpty()) continue;
            const QString userDir = userInfo.absoluteFilePath();
            const QString ugcDir = userDir + QStringLiteral("/ugc");
            const QString cacheFile = ugcDir + QStringLiteral("/431960_subscriptions.vdf");
            directories.insert(userDir);
            addExistingParent(ugcDir);
            if (QFileInfo(ugcDir).isDir()) directories.insert(ugcDir);
            if (QFileInfo(cacheFile).isFile()) files.insert(cacheFile);
        }
    }
    for (const QString& path : m_watcher.directories())
        if (!directories.remove(path)) m_watcher.removePath(path);
    for (const QString& path : m_watcher.files())
        if (!files.remove(path)) m_watcher.removePath(path);
    if (!directories.isEmpty()) m_watcher.addPaths(directories.values());
    if (!files.isEmpty()) m_watcher.addPaths(files.values());
}

SteamAccountsWatcher::ScanResult SteamAccountsWatcher::scan(
    QStringList roots, steam::accounts::Catalog previous, quint64 generation,
    const std::shared_ptr<std::atomic<quint64>>& current) {
    ScanResult result;
    result.generation = generation;
    QList<steam::accounts::InstallationInput> inputs;
    QSet<QString> seenRoots;
    QHash<QString, QSet<QString>> seenAccountIds;
    for (const QString& rawRoot : roots) {
        if (current->load(std::memory_order_relaxed) != generation) return result;
        const QString root = canonicalRoot(rawRoot);
        if (seenRoots.contains(root)) continue;
        seenRoots.insert(root);
        steam::accounts::InstallationInput install;
        install.root = root;
        bool loginAvailable = false;
        const QString loginPath = root + QStringLiteral("/config/loginusers.vdf");
        if (QFileInfo(loginPath).isFile()) install.loginUsersText = readText(loginPath, loginAvailable);
        const QString userdataPath = root + QStringLiteral("/userdata");
        const QFileInfoList userEntries = QDir(userdataPath).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& userInfo : userEntries) {
            if (current->load(std::memory_order_relaxed) != generation) return result;
            const QString accountId = accountIdFromDir(userInfo);
            if (accountId.isEmpty()) continue;
            seenAccountIds[root].insert(accountId);
            steam::accounts::CacheInput cache;
            cache.accountId32 = accountId;
            cache.path = userInfo.absoluteFilePath() + QStringLiteral("/ugc/431960_subscriptions.vdf");
            const QFileInfo cacheInfo(cache.path);
            cache.missing = !cacheInfo.isFile();
            if (!cache.missing) {
                if (cacheInfo.size() > kMaxKeyValuesBytes) cache.invalid = true;
                else {
                    cache.text = readText(cache.path, cache.available);
                    if (cache.available)
                        cache.lastSuccessUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
                }
            }
            attachPrevious(cache, previous, root, accountId);
            install.caches.append(cache);
        }
        for (const auto& old : previous.caches) {
            if (old.installationRoot != root || seenAccountIds[root].contains(old.accountId32)) continue;
            steam::accounts::CacheInput cache;
            cache.accountId32 = old.accountId32;
            cache.path = old.path;
            cache.missing = true;
            cache.previous = old;
            install.caches.append(cache);
            seenAccountIds[root].insert(old.accountId32);
        }
        inputs.append(install);
    }
    for (const auto& oldInstallation : previous.installations) {
        const QString root = oldInstallation.root;
        if (seenRoots.contains(root)) continue;
        steam::accounts::InstallationInput install;
        install.root = root;
        for (const auto& old : previous.caches) {
            if (old.installationRoot != root) continue;
            steam::accounts::CacheInput cache;
            cache.accountId32 = old.accountId32;
            cache.path = old.path;
            cache.missing = true;
            cache.previous = old;
            install.caches.append(cache);
        }
        inputs.append(install);
    }
    result.catalog = steam::accounts::buildCatalog(inputs, generation);
    return result;
}
} // namespace wekde
