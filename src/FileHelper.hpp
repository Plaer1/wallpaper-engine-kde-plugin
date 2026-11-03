#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>

namespace wekde
{

class SteamAccountsWatcher;

class FileHelper : public QObject {
    Q_OBJECT

public:
    explicit FileHelper(QObject* parent = nullptr);
    virtual ~FileHelper();

    // File operations
    Q_INVOKABLE QByteArray  readFile(const QString& path);
    Q_INVOKABLE qint64      getDirSize(const QString& path, int depth = 3);
    Q_INVOKABLE QVariantMap getFolderList(const QString& path, const QVariantMap& opt = {});

    // Steam auto-detection. configuredLibrary is the library the user picked
    // by hand, empty when there is none; it takes precedence over detection so
    // the folder button stays a real override. The map holds:
    //   library      the library root to seed SteamLibraryPath with, "" if none
    //   libraries    available canonical library roots, including empty libraries
    //   projectDirs  wallpaper directories in the selected library
    //   allProjectDirs all detected wallpaper directories (for external selection)
    //   configFile   expected config.json path, including before its first save
    //   selectedLibrary canonical configured path, or empty when unavailable
    //   assets       <install>/assets, "" if the app is not installed
    //   globalConfig <install>/config.json, "" if the app is not installed
    // All values are native paths without a trailing slash, deduplicated.
    Q_INVOKABLE QVariantMap detectSteam(const QString& configuredLibrary = {}, bool refresh = false);
    // detectSteam() restricted to the given Steam roots, for tests.
    QVariantMap detectSteamIn(const QStringList& steamRoots, const QString& configuredLibrary = {});

    // Cached account subscription catalog. Snapshot refreshes are emitted after
    // filesystem events; explicit rescans also rediscover Steam roots.
    Q_INVOKABLE QVariantMap steamAccountsCatalog();
    Q_INVOKABLE void rescanSteamAccounts();

    // Wallpaper config operations
    Q_INVOKABLE QVariantMap readWallpaperConfig(const QString& id);
    Q_INVOKABLE void        writeWallpaperConfig(const QString& id, const QVariantMap& changed);
    Q_INVOKABLE void        resetWallpaperConfig(const QString& id);

signals:
    void steamAccountsCatalogChanged(const QVariantMap& catalog);

private:
    SteamAccountsWatcher* m_steamAccountsWatcher { nullptr };
    void ensureSteamAccountsWatcher();

    QString configDir() const;
    QString wallpaperConfigDir() const;
    QString wallpaperConfigFile(const QString& id) const;
};

} // namespace wekde
