#include "GlobalConfigStore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGlobalStatic>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDebug>

using namespace wekde;

namespace
{
constexpr auto k_enabled_key = "enabled";
}

Q_GLOBAL_STATIC(GlobalConfigBackend, g_backend)

GlobalConfigBackend* GlobalConfigBackend::instance() { return g_backend; }

GlobalConfigBackend::GlobalConfigBackend()
    : GlobalConfigBackend(QString{}, nullptr)
{
}

GlobalConfigBackend::GlobalConfigBackend(QObject* parent)
    : GlobalConfigBackend(QString{}, parent)
{
}

GlobalConfigBackend::GlobalConfigBackend(const QString& configFilePath, QObject* parent)
    : QObject(parent)
    , m_configFilePath(configFilePath)
{
    load();
}

QString GlobalConfigBackend::filePath() const
{
    if (!m_configFilePath.isEmpty()) {
        return m_configFilePath;
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + "/wekde/global.json";
}

void GlobalConfigBackend::load()
{
    const QString path = filePath();
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "GlobalConfigBackend: invalid JSON in" << path << parseError.errorString();
        return;
    }

    QWriteLocker locker(&m_lock);
    m_data = document.object().toVariantMap();
}

QVariant GlobalConfigBackend::value(const QString& key, const QVariant& def) const
{
    QReadLocker locker(&m_lock);
    return m_data.value(key, def);
}

bool GlobalConfigBackend::enabled() const { return value(k_enabled_key, false).toBool(); }

QVariantMap GlobalConfigBackend::all() const
{
    QReadLocker locker(&m_lock);
    return m_data;
}

void GlobalConfigBackend::setValue(const QString& key, const QVariant& val)
{
    setValues({{key, val}});
}

void GlobalConfigBackend::setValues(const QVariantMap& patch)
{
    QVariantMap baseline;
    {
        QReadLocker locker(&m_lock);
        for (auto it = patch.constBegin(); it != patch.constEnd(); ++it) {
            if (m_data.contains(it.key())) {
                baseline.insert(it.key(), m_data.value(it.key()));
            }
        }
    }

    const GlobalConfigPatchResult result = patchValues(patch, baseline);
    if (!result.succeeded()) {
        qWarning() << "GlobalConfigBackend: patch rejected" << int(result.status) << result.message;
    }
}

GlobalConfigPatchResult GlobalConfigBackend::patchValues(
    const QVariantMap& patch, const QVariantMap& baseline)
{
    if (patch.isEmpty()) {
        return {GlobalConfigPatchStatus::NoChange, {}, {}, {}};
    }

    QStringList visibleChangedKeys;
    GlobalConfigPatchResult result;
    {
        QWriteLocker locker(&m_lock);
        const QString path = filePath();
        const QFileInfo fileInfo(path);
        if (!QDir().mkpath(fileInfo.absolutePath())) {
            return {GlobalConfigPatchStatus::IoError, {}, {},
                QStringLiteral("Cannot create config directory")};
        }

        QLockFile processLock(path + QStringLiteral(".lock"));
        if (!processLock.tryLock(0)) {
            return {GlobalConfigPatchStatus::Busy, {}, {},
                QStringLiteral("Another process is updating global config")};
        }

        QJsonObject latestObject;
        QFile currentFile(path);
        if (currentFile.exists()) {
            if (!currentFile.open(QIODevice::ReadOnly)) {
                return {GlobalConfigPatchStatus::IoError, {}, {},
                    QStringLiteral("Cannot read current config: %1").arg(currentFile.errorString())};
            }
            QJsonParseError parseError;
            const QJsonDocument currentDocument = QJsonDocument::fromJson(
                currentFile.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !currentDocument.isObject()) {
                return {GlobalConfigPatchStatus::InvalidJson, {}, {},
                    QStringLiteral("Current config is not a valid JSON object")};
            }
            latestObject = currentDocument.object();
        }

        QStringList conflicts;
        for (auto it = patch.constBegin(); it != patch.constEnd(); ++it) {
            const bool baselineContains = baseline.contains(it.key());
            const bool latestContains = latestObject.contains(it.key());
            if (baselineContains != latestContains
                || (baselineContains
                    && latestObject.value(it.key()).toVariant() != baseline.value(it.key()))) {
                conflicts << it.key();
            }
        }
        if (!conflicts.isEmpty()) {
            return {GlobalConfigPatchStatus::Conflict, {}, conflicts,
                QStringLiteral("Changed keys differ from their baseline")};
        }

        QJsonObject mergedObject = latestObject;
        QStringList writtenKeys;
        for (auto it = patch.constBegin(); it != patch.constEnd(); ++it) {
            const QJsonValue value = QJsonValue::fromVariant(it.value());
            if (!mergedObject.contains(it.key()) || mergedObject.value(it.key()) != value) {
                mergedObject.insert(it.key(), value);
                writtenKeys << it.key();
            }
        }

        if (!writtenKeys.isEmpty()) {
            QSaveFile output(path);
            if (!output.open(QIODevice::WriteOnly)) {
                return {GlobalConfigPatchStatus::IoError, {}, {},
                    QStringLiteral("Cannot open atomic config output: %1").arg(output.errorString())};
            }
            const QByteArray bytes = QJsonDocument(mergedObject).toJson(QJsonDocument::Indented);
            if (output.write(bytes) != bytes.size() || !output.commit()) {
                return {GlobalConfigPatchStatus::IoError, {}, {},
                    QStringLiteral("Cannot commit atomic config output: %1").arg(output.errorString())};
            }
            result.status = GlobalConfigPatchStatus::Applied;
            result.changedKeys = writtenKeys;
        } else {
            result.status = GlobalConfigPatchStatus::NoChange;
        }

        const QVariantMap fresh = mergedObject.toVariantMap();
        for (auto it = m_data.constBegin(); it != m_data.constEnd(); ++it) {
            if (!fresh.contains(it.key()) || fresh.value(it.key()) != it.value()) {
                visibleChangedKeys << it.key();
            }
        }
        for (auto it = fresh.constBegin(); it != fresh.constEnd(); ++it) {
            if (!m_data.contains(it.key()) || m_data.value(it.key()) != it.value()) {
                if (!visibleChangedKeys.contains(it.key())) {
                    visibleChangedKeys << it.key();
                }
            }
        }
        m_data = fresh;
    }

    emitChanges(visibleChangedKeys);
    return result;
}

void GlobalConfigBackend::emitChanges(const QStringList& changedKeys)
{
    for (const QString& key : changedKeys) {
        emit changed(key, value(key));
    }
    if (changedKeys.contains(k_enabled_key)) {
        emit enabledChanged();
    }
}

void GlobalConfigBackend::reloadFromDisk()
{
    QVariantMap fresh;
    const QString path = filePath();
    QFile file(path);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            qWarning() << "GlobalConfigBackend: invalid JSON in" << path << parseError.errorString();
            return;
        }
        fresh = document.object().toVariantMap();
    }

    QStringList changedKeys;
    {
        QWriteLocker locker(&m_lock);
        for (auto it = m_data.constBegin(); it != m_data.constEnd(); ++it) {
            if (!fresh.contains(it.key()) || fresh.value(it.key()) != it.value()) {
                changedKeys << it.key();
            }
        }
        for (auto it = fresh.constBegin(); it != fresh.constEnd(); ++it) {
            if (!m_data.contains(it.key()) || m_data.value(it.key()) != it.value()) {
                if (!changedKeys.contains(it.key())) {
                    changedKeys << it.key();
                }
            }
        }
        if (changedKeys.isEmpty()) {
            return;
        }
        m_data = fresh;
    }
    emitChanges(changedKeys);
}

// --- QML proxy ---

GlobalConfig::GlobalConfig(QObject* parent): QObject(parent)
{
    auto* backend = GlobalConfigBackend::instance();
    connect(backend, &GlobalConfigBackend::changed, this, &GlobalConfig::changed);
    connect(backend, &GlobalConfigBackend::enabledChanged, this, &GlobalConfig::enabledChanged);
}

bool GlobalConfig::enabled() const { return GlobalConfigBackend::instance()->enabled(); }

void GlobalConfig::setEnabled(bool on) { GlobalConfigBackend::instance()->setValue("enabled", on); }

QVariant GlobalConfig::get(const QString& key, const QVariant& def) const
{
    return GlobalConfigBackend::instance()->value(key, def);
}

void GlobalConfig::set(const QString& key, const QVariant& val)
{
    GlobalConfigBackend::instance()->setValue(key, val);
}

void GlobalConfig::setBatch(const QVariantMap& patch)
{
    GlobalConfigBackend::instance()->setValues(patch);
}

QVariantMap GlobalConfig::all() const { return GlobalConfigBackend::instance()->all(); }

void GlobalConfig::reload() { GlobalConfigBackend::instance()->reloadFromDisk(); }
