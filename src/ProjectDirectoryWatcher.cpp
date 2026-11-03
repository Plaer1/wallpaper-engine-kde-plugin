#include "ProjectDirectoryWatcher.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSet>

namespace wekde
{
ProjectDirectoryWatcher::ProjectDirectoryWatcher(QObject* parent): QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(200);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &ProjectDirectoryWatcher::scheduleChange);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged,
            this, &ProjectDirectoryWatcher::scheduleChange);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        if (! m_enabled) return;
        syncWatches();
        emit changed();
    });
}

void ProjectDirectoryWatcher::setEnabled(bool value) {
    if (m_enabled == value) return;
    m_enabled = value;
    m_debounce.stop();
    if (m_enabled) syncWatches();
    else clearWatches();
    emit enabledChanged();
}

void ProjectDirectoryWatcher::setProjectDirs(const QStringList& paths) {
    QStringList normalized;
    for (const auto& path : paths) {
        if (path.isEmpty()) continue;
        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        const QString resolved = canonical.isEmpty() ? info.absoluteFilePath() : canonical;
        if (! normalized.contains(resolved)) normalized.append(resolved);
    }
    if (normalized == m_projectDirs) return;
    m_projectDirs = normalized;
    m_debounce.stop();
    if (m_enabled) syncWatches();
    emit projectDirsChanged();
}

void ProjectDirectoryWatcher::clearWatches() {
    if (! m_watcher.files().isEmpty()) m_watcher.removePaths(m_watcher.files());
    if (! m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
}

void ProjectDirectoryWatcher::syncWatches() {
    QSet<QString> directories;
    QSet<QString> files;
    for (const auto& path : m_projectDirs) {
        // Follow first creation as well as a library that already contains projects.
        QString existing = path;
        while (! QFileInfo(existing).isDir()) {
            const QString parent = QFileInfo(existing).absolutePath();
            if (parent == existing) break;
            existing = parent;
        }
        if (QFileInfo(existing).isDir()) directories.insert(existing);
        if (! QFileInfo(path).isDir()) continue;
        const QDir root(path);
        for (const auto& project : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            directories.insert(project.absoluteFilePath());
            const QString metadata = project.absoluteFilePath() + QStringLiteral("/project.json");
            if (QFileInfo(metadata).isFile()) files.insert(metadata);
        }
    }
    // Keep existing watches intact so repeated changes do not create a blind spot.
    for (const auto& path : m_watcher.directories()) {
        if (directories.remove(path) == 0) m_watcher.removePath(path);
    }
    for (const auto& path : m_watcher.files()) {
        if (files.remove(path) == 0) m_watcher.removePath(path);
    }
    if (! directories.isEmpty()) m_watcher.addPaths(directories.values());
    if (! files.isEmpty()) m_watcher.addPaths(files.values());
}

void ProjectDirectoryWatcher::scheduleChange() {
    if (m_enabled) m_debounce.start();
}
} // namespace wekde
