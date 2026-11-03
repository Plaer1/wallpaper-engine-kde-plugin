#pragma once

#include "SteamAccounts.hpp"
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>

namespace wekde {
class SteamAccountsWatcher : public QObject {
    Q_OBJECT
public:
    using RootProvider = std::function<QStringList()>;
    explicit SteamAccountsWatcher(RootProvider rootProvider = {}, QObject* parent = nullptr);
    ~SteamAccountsWatcher() override;
    steam::accounts::Catalog catalog() const { return m_catalog; }
public slots:
    void refresh();
signals:
    void catalogChanged();
private:
    struct ScanResult { quint64 generation { 0 }; steam::accounts::Catalog catalog; };
    void syncWatches();
    void scheduleRefresh();
    void startScan();
    void finishScan(ScanResult result);
    static ScanResult scan(QStringList roots, steam::accounts::Catalog previous, quint64 generation,
                           const std::shared_ptr<std::atomic<quint64>>& current);
    RootProvider m_rootProvider;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QFutureWatcher<ScanResult> m_future;
    steam::accounts::Catalog m_catalog;
    quint64 m_generation { 0 };
    bool m_running { false };
    bool m_pending { false };
    std::shared_ptr<std::atomic<quint64>> m_currentGeneration;
};
} // namespace wekde
