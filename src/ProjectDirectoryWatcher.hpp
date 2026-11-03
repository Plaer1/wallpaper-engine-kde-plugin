#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>

namespace wekde
{
// Tracks only library roots, immediate project directories, and project metadata.
// Directory events cover downloads arriving before project.json and atomic saves.
class ProjectDirectoryWatcher : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QStringList projectDirs READ projectDirs WRITE setProjectDirs NOTIFY projectDirsChanged)

public:
    explicit ProjectDirectoryWatcher(QObject* parent = nullptr);
    bool enabled() const { return m_enabled; }
    QStringList projectDirs() const { return m_projectDirs; }
    void setEnabled(bool value);
    void setProjectDirs(const QStringList& paths);

signals:
    void enabledChanged();
    void projectDirsChanged();
    void changed();

private:
    void clearWatches();
    void syncWatches();
    void scheduleChange();

    bool m_enabled = false;
    QStringList m_projectDirs;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};
} // namespace wekde
