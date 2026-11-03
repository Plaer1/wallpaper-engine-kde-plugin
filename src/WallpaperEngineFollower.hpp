#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QTimer>
#include <QVariantMap>

namespace wekde
{

// Watches Wallpaper Engine's saved selection. File I/O and JSON parsing run on
// a worker; the GUI thread only handles change notifications and property updates.
class WallpaperEngineFollower : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool observeAccounts READ observeAccounts WRITE setObserveAccounts NOTIFY observeAccountsChanged)
    Q_PROPERTY(QString configPath READ configPath WRITE setConfigPath NOTIFY inputsChanged)
    Q_PROPERTY(QStringList projectDirs READ projectDirs WRITE setProjectDirs NOTIFY inputsChanged)
    // Legacy section picker remains independent from the identity-aware binding.
    Q_PROPERTY(QString account READ account WRITE setAccount NOTIFY inputsChanged)
    Q_PROPERTY(QString followMode READ followMode WRITE setFollowMode NOTIFY inputsChanged)
    Q_PROPERTY(QString followAccountKey READ followAccountKey WRITE setFollowAccountKey NOTIFY inputsChanged)
    Q_PROPERTY(QString followSourceKey READ followSourceKey WRITE setFollowSourceKey NOTIFY inputsChanged)
    Q_PROPERTY(QString currentSteamId64 READ currentSteamId64 WRITE setCurrentSteamId64 NOTIFY inputsChanged)
    Q_PROPERTY(bool currentIdentityVerified READ currentIdentityVerified
               WRITE setCurrentIdentityVerified NOTIFY inputsChanged)
    Q_PROPERTY(QStringList accounts READ accounts NOTIFY resultChanged)
    Q_PROPERTY(QVariantMap selection READ selection NOTIFY resultChanged)
    Q_PROPERTY(QVariantMap provenance READ provenance NOTIFY resultChanged)
    Q_PROPERTY(QString status READ status NOTIFY resultChanged)

public:
    struct FollowBinding {
        QString mode { QStringLiteral("legacySession") };
        QString accountKey;
        QString sourceKey;
        QString currentSteamId64;
        bool currentIdentityVerified { false };
    };

    explicit WallpaperEngineFollower(QObject* parent = nullptr);
    bool        enabled() const { return m_enabled; }
    bool        observeAccounts() const { return m_observeAccounts; }
    QString     configPath() const { return m_configPath; }
    QStringList projectDirs() const { return m_projectDirs; }
    QString     account() const { return m_account; }
    QString     followMode() const { return m_binding.mode; }
    QString     followAccountKey() const { return m_binding.accountKey; }
    QString     followSourceKey() const { return m_binding.sourceKey; }
    QString     currentSteamId64() const { return m_binding.currentSteamId64; }
    bool        currentIdentityVerified() const { return m_binding.currentIdentityVerified; }
    QStringList accounts() const { return m_accounts; }
    QVariantMap selection() const { return m_selection; }
    QVariantMap provenance() const { return m_provenance; }
    QString     status() const { return m_status; }
    void        setEnabled(bool value);
    void        setObserveAccounts(bool value);
    void        setConfigPath(const QString& value);
    void        setProjectDirs(const QStringList& value);
    void        setAccount(const QString& value);
    void        setFollowMode(const QString& value);
    void        setFollowAccountKey(const QString& value);
    void        setFollowSourceKey(const QString& value);
    void        setCurrentSteamId64(const QString& value);
    void        setCurrentIdentityVerified(bool value);

    // Also used by deterministic fixture tests; never writes to Steam's files.
    static QVariantMap readSelection(const QString& configPath, const QStringList& projectDirs,
                                     const QString& account = {});
    static QVariantMap readSelection(const QString& configPath, const QStringList& projectDirs,
                                     const FollowBinding& binding, const QString& legacySection = {});
    static QVariantMap resolveSelection(const QJsonObject& config, const QString& configPath,
                                        const QStringList& projectDirs,
                                        const FollowBinding& binding,
                                        const QString& legacySection = {});
    static QVariantMap resolveSelection(const QJsonObject& config, const QString& configPath,
                                        const QStringList& projectDirs);

signals:
    void enabledChanged();
    void observeAccountsChanged();
    void inputsChanged();
    void resultChanged();
    void readStarted();
    void readCompleted();

private:
    void reset(bool clearAccounts = false);
    void armWatcher();
    void scheduleRead();
    void startRead();

    bool                        m_enabled        = false;
    bool                        m_observeAccounts = false;
    bool                        m_pending        = false;
    quint64                     m_generation     = 0;
    quint64                     m_readGeneration = 0;
    QString                     m_configPath;
    QStringList                 m_projectDirs;
    QString                     m_account;
    FollowBinding               m_binding;
    QStringList                 m_accounts;
    QVariantMap                 m_selection;
    QVariantMap                 m_provenance;
    QString                     m_status;
    QFileSystemWatcher          m_watcher;
    QTimer                      m_debounce;
    QFutureWatcher<QVariantMap> m_future;
};
} // namespace wekde
