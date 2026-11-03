#include "WallpaperEngineFollower.hpp"
#include "SteamPaths.hpp"

#include <QtConcurrent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

namespace wekde
{
namespace
{
QJsonObject readObject(const QString& path) {
    QFile file(path);
    if (! file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

struct ParsedConfig {
    bool valid = false;
    QJsonObject object;
};

ParsedConfig readConfig(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) return {};
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return {};
    return { true, document.object() };
}

QStringList configSections(const QJsonObject& config) {
    QStringList accounts;
    for (auto it = config.begin(); it != config.end(); ++it) {
        if (it.value().isObject() && it.value().toObject().value(QStringLiteral("general")).isObject())
            accounts.append(it.key());
    }
    return accounts;
}

QVariantMap readAccountSnapshot(const QString& path) {
    const ParsedConfig parsed = readConfig(path);
    return {
        { QStringLiteral("accountsValid"), parsed.valid },
        { QStringLiteral("accounts"), parsed.valid ? configSections(parsed.object) : QStringList() }
    };
}

QString resolveFile(QString path, const QStringList& projectDirs) {
    path.replace(u'\\', u'/');
    if (path.startsWith(QStringLiteral("file:"))) path = QUrl(path).toLocalFile();
    // Proton's Z: drive exposes the Linux filesystem directly.
    if (path.size() > 2 && path.at(1) == u':') path = path.mid(2);
    if (QFileInfo(path).isFile()) return path;

    // Windows paths from another Steam installation are rebased onto the
    // detected library roots, without a recursive scan of wallpaper contents.
    for (const QString& dir : projectDirs) {
        const QString name   = QFileInfo(dir).fileName();
        const QString marker = name == QStringLiteral("431960")
                                   ? QStringLiteral("/workshop/content/431960/")
                                   : u'/' + name + u'/';
        const int     offset = path.indexOf(marker, 0, Qt::CaseInsensitive);
        if (offset < 0) continue;
        const QString tail      = path.mid(offset + marker.size());
        const QString candidate = steam::resolvePath(dir, tail.split(u'/', Qt::SkipEmptyParts));
        if (QFileInfo(candidate).isFile()) return candidate;
    }
    return {};
}

bool validSteamId64(const QString& id) {
    constexpr quint64 publicIndividualBase = 76561197960265728ULL;
    constexpr quint64 maxAccountId32 = 0xFFFFFFFFULL;
    if (id.isEmpty() || id.size() > 20 || id == QStringLiteral("0") ||
        (id.size() > 1 && id.startsWith(u'0'))) return false;
    for (const QChar c : id) if (c < u'0' || c > u'9') return false;
    bool ok = false;
    const quint64 value = id.toULongLong(&ok, 10);
    return ok && value > publicIndividualBase && value - publicIndividualBase <= maxAccountId32;
}

QString sectionIdentity(const QString& section, const QJsonObject& value) {
    QString id = section;
    if (id.startsWith(QStringLiteral("steam:"))) id.remove(0, 6);
    if (validSteamId64(id)) return id;
    id = value.value(QStringLiteral("steamId64")).toString();
    if (!validSteamId64(id))
        id = value.value(QStringLiteral("general")).toObject().value(QStringLiteral("steamId64")).toString();
    return validSteamId64(id) ? id : QString();
}

QString sourceKeyFor(const QString& configPath, const QString& sectionPath) {
    QString path = QFileInfo(configPath).absoluteFilePath();
    if (path.isEmpty()) path = QStringLiteral("config.json");
    return QStringLiteral("config:%1#%2").arg(QDir::cleanPath(path), sectionPath);
}

QString mappedSection(const QString& key, const QString& configPath) {
    const QString prefix = sourceKeyFor(configPath, QStringLiteral("section:"));
    if (!key.startsWith(prefix)) return {};
    return QUrl::fromPercentEncoding(key.mid(prefix.size()).toUtf8());
}

struct FileChoice {
    QString file;
    QString monitor;
    QString sourceType;
    QString identityStatus;
    QString section;
    bool browser { false };
    QString bridgeRecord;
};

FileChoice nativeChoice(const QString& section, const QJsonObject& account) {
    const QJsonObject general = account.value(QStringLiteral("general")).toObject();
    const QJsonObject selected = general.value(QStringLiteral("wallpaperconfig")).toObject()
                                     .value(QStringLiteral("selectedwallpapers")).toObject();
    QString monitor = general.value(QStringLiteral("browser")).toObject()
                          .value(QStringLiteral("lastselectedmonitor")).toString();
    if (!selected.contains(monitor)) {
        if (selected.size() == 1) monitor = selected.begin().key();
        else if (selected.contains(QStringLiteral("Monitor0"))) monitor = QStringLiteral("Monitor0");
    }
    const QString file = selected.value(monitor).toObject().value(QStringLiteral("file")).toString();
    if (file.isEmpty()) return {};
    return { file, monitor, {}, {}, section, false };
}

FileChoice bridgeV1Choice(const QString& section, const QJsonObject& account) {
    const QJsonObject browser = account.value(QStringLiteral("general")).toObject()
                                    .value(QStringLiteral("browser")).toObject();
    const QJsonObject bridge = browser.value(QStringLiteral("wekdeSelection")).toObject();
    if (bridge.value(QStringLiteral("version")).toInt() != 1) return {};
    const QString file = bridge.value(QStringLiteral("file")).toString();
    if (file.isEmpty()) return {};
    return { file, bridge.value(QStringLiteral("monitor")).toString(), {}, {}, section, true,
             QStringLiteral("v1") };
}

FileChoice bridgeV2UnassociatedChoice(const QString& section, const QJsonObject& account) {
    const QJsonObject browser = account.value(QStringLiteral("general")).toObject()
                                    .value(QStringLiteral("browser")).toObject();
    const QJsonObject bridge = browser.value(QStringLiteral("wekdeSelection")).toObject();
    if (bridge.value(QStringLiteral("version")).toInt() != 2) return {};
    const QJsonValue recordValue = bridge.value(QStringLiteral("unassociated"));
    if (!recordValue.isObject()) return {};
    const QJsonObject record = recordValue.toObject();
    const QJsonValue fileValue = record.value(QStringLiteral("file"));
    const QJsonValue monitorValue = record.value(QStringLiteral("monitor"));
    if (!fileValue.isString() || fileValue.toString().isEmpty() || !monitorValue.isString()) return {};
    return { fileValue.toString(), monitorValue.toString(), {}, {}, section, true,
             QStringLiteral("unassociated") };
}

FileChoice bridgeV2Choice(const QJsonObject& config, const QString& steamId64, QString& error) {
    QList<FileChoice> matches;
    for (auto it = config.begin(); it != config.end(); ++it) {
        const QJsonObject general = it.value().toObject().value(QStringLiteral("general")).toObject();
        const QJsonObject browser = general.value(QStringLiteral("browser")).toObject();
        const QJsonObject bridge = browser.value(QStringLiteral("wekdeSelection")).toObject();
        if (bridge.value(QStringLiteral("version")).toInt() != 2) continue;
        const QJsonObject record = bridge.value(QStringLiteral("byUser")).toObject().value(steamId64).toObject();
        const QString file = record.value(QStringLiteral("file")).toString();
        if (!file.isEmpty()) matches.append({ file, record.value(QStringLiteral("monitor")).toString(),
            QStringLiteral("verifiedBridgeAccount"), QStringLiteral("verified"), it.key(), true,
            QStringLiteral("byUser.%1").arg(steamId64) });
    }
    if (matches.isEmpty()) return {};
    const FileChoice first = matches.first();
    for (const FileChoice& match : matches) {
        if (match.file != first.file || match.monitor != first.monitor) {
            error = QStringLiteral("Conflicting matching account selections are available.");
            return {};
        }
    }
    return first;
}
} // namespace

QVariantMap WallpaperEngineFollower::readSelection(const QString& configPath,
                                                   const QStringList& projectDirs,
                                                   const QString& account) {
    FollowBinding binding;
    binding.mode = QStringLiteral("legacySession");
    return readSelection(configPath, projectDirs, binding, account);
}

QVariantMap WallpaperEngineFollower::readSelection(const QString& configPath,
                                                   const QStringList& projectDirs,
                                                   const FollowBinding& binding,
                                                   const QString& legacySection) {
    const ParsedConfig parsed = readConfig(configPath);
    if (!parsed.valid) {
        return { { QStringLiteral("status"),
                     QStringLiteral("Waiting for Wallpaper Engine to save its settings.") },
                 { QStringLiteral("accountsValid"), false } };
    }
    return resolveSelection(parsed.object, configPath, projectDirs, binding, legacySection);
}

QVariantMap WallpaperEngineFollower::resolveSelection(const QJsonObject& config,
                                                      const QString& configPath,
                                                      const QStringList& projectDirs,
                                                      const FollowBinding& binding,
                                                      const QString& legacySection) {
    QVariantMap result;
    const QStringList accounts = configSections(config);
    result["accounts"] = accounts;
    result["accountsValid"] = true;
    if (config.isEmpty()) {
        result["status"] = QStringLiteral("Waiting for Wallpaper Engine to save its settings.");
        return result;
    }

    FileChoice choice;
    QString accountKey;
    QString identityStatus = QStringLiteral("unassociated");
    const auto wait = [&result](const QString& message) {
        result["status"] = message;
        return result;
    };

    if (binding.mode == QStringLiteral("legacySession")) {
        QString section = legacySection;
        if (section.isEmpty()) {
            if (accounts.contains(QStringLiteral("steamuser"))) section = QStringLiteral("steamuser");
            else if (accounts.size() == 1) section = accounts.first();
        }
        if (!accounts.contains(section)) return wait(QStringLiteral("Choose the Wallpaper Engine account to follow."));
        const QJsonObject value = config.value(section).toObject();
        choice = nativeChoice(section, value);
        if (!choice.file.isEmpty()) choice.sourceType = QStringLiteral("legacyUnassociatedNative");
        else {
            choice = bridgeV1Choice(section, value);
            if (!choice.file.isEmpty()) choice.sourceType = QStringLiteral("legacyUnassociatedBridge");
            else {
                choice = bridgeV2UnassociatedChoice(section, value);
                if (!choice.file.isEmpty())
                    choice.sourceType = QStringLiteral("legacyUnassociatedBridge");
            }
        }
        choice.identityStatus = QStringLiteral("unassociated");
        choice.section = section;
    } else if (binding.mode == QStringLiteral("manualMapping")) {
        const QString section = mappedSection(binding.sourceKey, configPath);
        if (!accounts.contains(section)) return wait(QStringLiteral("Choose a Wallpaper Engine section to map manually."));
        const QJsonObject value = config.value(section).toObject();
        choice = nativeChoice(section, value);
        if (choice.file.isEmpty()) choice = bridgeV1Choice(section, value);
        if (choice.file.isEmpty()) return wait(QStringLiteral("The mapped Wallpaper Engine section has no saved selection."));
        choice.sourceType = QStringLiteral("manualMapping");
        choice.identityStatus = QStringLiteral("userAssociated");
        choice.section = section;
        accountKey = binding.accountKey;
        identityStatus = QStringLiteral("userAssociated");
    } else if (binding.mode == QStringLiteral("account") ||
               binding.mode == QStringLiteral("identifiedCurrent")) {
        QString steamId64;
        if (binding.mode == QStringLiteral("account")) {
            if (!binding.accountKey.startsWith(QStringLiteral("steam:")))
                return wait(QStringLiteral("Waiting for a verified Steam account identity."));
            steamId64 = binding.accountKey.mid(6);
        } else {
            if (!binding.currentIdentityVerified || !validSteamId64(binding.currentSteamId64))
                return wait(QStringLiteral("Waiting for a verified current Wallpaper Engine identity."));
            steamId64 = binding.currentSteamId64;
            if (!binding.accountKey.isEmpty() && binding.accountKey != QStringLiteral("steam:") + steamId64)
                return wait(QStringLiteral("The current Wallpaper Engine identity does not match this account."));
        }
        if (!validSteamId64(steamId64)) return wait(QStringLiteral("Waiting for a verified Steam account identity."));
        accountKey = QStringLiteral("steam:") + steamId64;
        QList<FileChoice> matchingNative;
        for (auto it = config.begin(); it != config.end(); ++it) {
            const QJsonObject value = it.value().toObject();
            if (sectionIdentity(it.key(), value) != steamId64) continue;
            FileChoice native = nativeChoice(it.key(), value);
            if (!native.file.isEmpty()) {
                native.sourceType = QStringLiteral("nativeAccount");
                native.identityStatus = QStringLiteral("verified");
                matchingNative.append(native);
            }
        }
        if (!matchingNative.isEmpty()) {
            choice = matchingNative.first();
            for (const FileChoice& match : matchingNative) {
                if (match.file != choice.file || match.monitor != choice.monitor)
                    return wait(QStringLiteral("Conflicting native selections exist for this account."));
            }
        } else {
            QString bridgeError;
            choice = bridgeV2Choice(config, steamId64, bridgeError);
            if (!bridgeError.isEmpty()) return wait(bridgeError);
            if (choice.file.isEmpty())
                return wait(QStringLiteral("Waiting for a matching Wallpaper Engine account selection."));
        }
        identityStatus = QStringLiteral("verified");
    } else {
        return wait(QStringLiteral("Waiting for an explicit Wallpaper Engine follow mode."));
    }

    if (choice.file.isEmpty()) return wait(QStringLiteral("Wallpaper Engine has not saved a selection."));
    if (choice.sourceType.isEmpty()) choice.sourceType = QStringLiteral("legacyUnassociatedNative");
    if (choice.identityStatus.isEmpty()) choice.identityStatus = identityStatus;
    const QString sectionEncoded = QString::fromUtf8(QUrl::toPercentEncoding(choice.section));
    const QString sectionPath = choice.browser
        ? QStringLiteral("section:%1.general.browser.wekdeSelection.%2").arg(sectionEncoded, choice.bridgeRecord)
        : QStringLiteral("section:%1").arg(sectionEncoded);
    const QString actualSourceKey = sourceKeyFor(configPath, sectionPath);
    result["sourceKey"] = actualSourceKey;
    result["provenance"] = QVariantMap {
        { "mode", binding.mode }, { "accountKey", accountKey },
        { "sourceKey", actualSourceKey }, { "sourceType", choice.sourceType },
        { "identityStatus", identityStatus },
        { "label", identityStatus == QStringLiteral("userAssociated")
                       ? QStringLiteral("User-associated mapping")
                       : (identityStatus == QStringLiteral("verified")
                              ? QStringLiteral("Verified account") : QStringLiteral("Unassociated session")) }
    };

    const QString savedFile = choice.file;
    const QString monitor = choice.monitor;
    const bool browserSelection = choice.browser;
    const QString selectedFile = resolveFile(savedFile, projectDirs);
    if (selectedFile.isEmpty()) {
        result["status"] = QStringLiteral("The selected wallpaper is not downloaded in a local Steam library.");
        return result;
    }
    QDir projectDir = QFileInfo(selectedFile).absoluteDir();
    QJsonObject project;
    for (int depth = 0; depth < 8; ++depth) {
        project = readObject(projectDir.filePath(QStringLiteral("project.json")));
        if (!project.isEmpty() || !projectDir.cdUp()) break;
    }
    const QString type = project.value(QStringLiteral("type")).toString().toLower();
    const QString file = project.value(QStringLiteral("file")).toString();
    const QString media = QFileInfo(selectedFile).fileName() == QStringLiteral("project.json")
                              ? projectDir.filePath(file) : selectedFile;
    if ((type != QStringLiteral("scene") && type != QStringLiteral("video") && type != QStringLiteral("web")) ||
        file.isEmpty() || !QFileInfo(media).isFile()) {
        result["status"] = QStringLiteral("This wallpaper is unavailable or uses an unsupported type.");
        return result;
    }
    result["selection"] = QVariantMap {
        { "source", QUrl::fromLocalFile(media).toString(QUrl::FullyEncoded) + u'+' + type },
        { "workshopid", projectDir.dirName() }, { "projectDir", projectDir.absolutePath() }
    };
    result["status"] = browserSelection
        ? QStringLiteral("Following Wallpaper Engine’s browser selection (%1)." ).arg(monitor)
        : QStringLiteral("Following Wallpaper Engine’s selection (%1)." ).arg(monitor);
    return result;
}

QVariantMap WallpaperEngineFollower::resolveSelection(const QJsonObject& config,
                                                      const QString& configPath,
                                                      const QStringList& projectDirs) {
    return resolveSelection(config, configPath, projectDirs, FollowBinding {});
}

WallpaperEngineFollower::WallpaperEngineFollower(QObject* parent): QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(180);
    connect(&m_debounce, &QTimer::timeout, this, &WallpaperEngineFollower::startRead);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        scheduleRead();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        // Directory notifications cover atomic replacement and first creation.
        // Ignore unrelated files when our target is still being watched.
        if (! m_watcher.files().contains(m_configPath)) scheduleRead();
    });
    connect(&m_future, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        if ((m_enabled || m_observeAccounts) && m_readGeneration == m_generation) {
            const QVariantMap result   = m_future.result();
            bool changed = false;
            if (result.value(QStringLiteral("accountsValid")).toBool()) {
                const QStringList accounts = result.value(QStringLiteral("accounts")).toStringList();
                if (accounts != m_accounts) {
                    m_accounts = accounts;
                    changed = true;
                }
            }

            // Observer-only reads publish sections and never resolve or apply
            // a selection. Invalid/incomplete config reads leave the last good
            // section list intact.
            if (m_enabled) {
                const QString status = result.value(QStringLiteral("status")).toString();
                const QVariantMap parsedSelection = result.value(QStringLiteral("selection")).toMap();
                // Incomplete writes/missing content must not blank the desktop.
                const QVariantMap selection = parsedSelection.isEmpty() ? m_selection : parsedSelection;
                // Retain the provenance of the last good image if a same-source
                // rewrite is partial or the new record points to unavailable media.
                const QVariantMap provenance = parsedSelection.isEmpty()
                    ? m_provenance : result.value(QStringLiteral("provenance")).toMap();
                if (selection != m_selection || provenance != m_provenance || status != m_status) {
                    m_selection = selection;
                    m_provenance = provenance;
                    m_status = status;
                    changed = true;
                }
            }
            if (changed) {
                emit resultChanged();
            }
            emit readCompleted();
        }
        if (m_pending && (m_enabled || m_observeAccounts)) m_debounce.start();
    });
}

void WallpaperEngineFollower::setEnabled(bool value) {
    if (value == m_enabled) return;
    m_enabled = value;
    reset(!m_observeAccounts);
    emit enabledChanged();
}
void WallpaperEngineFollower::setObserveAccounts(bool value) {
    if (value == m_observeAccounts) return;
    m_observeAccounts = value;
    reset(!m_enabled && !m_observeAccounts);
    emit observeAccountsChanged();
}
void WallpaperEngineFollower::setConfigPath(const QString& value) {
    if (value == m_configPath) return;
    m_configPath = value;
    reset(true);
    emit inputsChanged();
}
void WallpaperEngineFollower::setProjectDirs(const QStringList& value) {
    if (value == m_projectDirs) return;
    m_projectDirs = value;
    if (m_enabled) reset(!m_observeAccounts);
    emit inputsChanged();
}
void WallpaperEngineFollower::setAccount(const QString& value) {
    if (value == m_account) return;
    m_account = value;
    if (m_enabled) reset(!m_observeAccounts);
    emit inputsChanged();
}
void WallpaperEngineFollower::setFollowMode(const QString& value) {
    if (value == m_binding.mode) return;
    m_binding.mode = value;
    if (m_enabled) reset(!m_observeAccounts);
    emit inputsChanged();
}
void WallpaperEngineFollower::setFollowAccountKey(const QString& value) {
    if (value == m_binding.accountKey) return;
    m_binding.accountKey = value;
    if (m_enabled) reset(!m_observeAccounts);
    emit inputsChanged();
}
void WallpaperEngineFollower::setFollowSourceKey(const QString& value) {
    if (value == m_binding.sourceKey) return;
    m_binding.sourceKey = value;
    if (m_enabled) reset(!m_observeAccounts);
    emit inputsChanged();
}
void WallpaperEngineFollower::setCurrentSteamId64(const QString& value) {
    if (value == m_binding.currentSteamId64) return;
    m_binding.currentSteamId64 = value;
    if (m_enabled) reset(!m_observeAccounts);
    emit inputsChanged();
}
void WallpaperEngineFollower::setCurrentIdentityVerified(bool value) {
    if (value == m_binding.currentIdentityVerified) return;
    m_binding.currentIdentityVerified = value;
    if (m_enabled) reset(!m_observeAccounts);
    emit inputsChanged();
}
void WallpaperEngineFollower::reset(bool clearAccounts) {
    ++m_generation;
    m_debounce.stop();
    m_pending = false;
    if (! m_watcher.files().isEmpty()) m_watcher.removePaths(m_watcher.files());
    if (! m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
    m_selection.clear();
    m_provenance.clear();
    if (clearAccounts) m_accounts.clear();
    m_status.clear();
    emit resultChanged();
    if (m_enabled || m_observeAccounts) scheduleRead();
}
void WallpaperEngineFollower::armWatcher() {
    if (m_configPath.isEmpty()) return;
    if (QFileInfo(m_configPath).isFile() && ! m_watcher.files().contains(m_configPath))
        m_watcher.addPath(m_configPath);
    QString dir = QFileInfo(m_configPath).absolutePath();
    while (! QFileInfo(dir).isDir()) {
        const QString parent = QFileInfo(dir).absolutePath();
        if (parent == dir) break;
        dir = parent;
    }
    if (m_watcher.directories() != QStringList { dir }) {
        if (! m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
        m_watcher.addPath(dir);
    }
}
void WallpaperEngineFollower::scheduleRead() {
    if (!m_enabled && !m_observeAccounts) return;
    ++m_generation; // discard a read overtaken by a newer filesystem event
    m_pending = true;
    armWatcher();
    m_debounce.start();
}
void WallpaperEngineFollower::startRead() {
    if ((!m_enabled && !m_observeAccounts) || m_future.isRunning()) return;
    m_pending                 = false;
    m_readGeneration          = m_generation;
    const QString     path    = m_configPath;
    const QStringList dirs    = m_projectDirs;
    const QString     account = m_account;
    const FollowBinding binding = m_binding;
    const bool followEnabled = m_enabled;
    m_future.setFuture(QtConcurrent::run([path, dirs, account, binding, followEnabled] {
        return followEnabled ? readSelection(path, dirs, binding, account)
                             : readAccountSnapshot(path);
    }));
    emit readStarted();
}
} // namespace wekde
