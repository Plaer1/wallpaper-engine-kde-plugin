#include "GlobalConfigStore.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>
#include <sys/resource.h>

using namespace wekde;

namespace
{
QByteArray jsonBytes(const QVariantMap& value)
{
    return QJsonDocument(QJsonObject::fromVariantMap(value)).toJson(QJsonDocument::Compact);
}

QVariantMap variantMap(const QByteArray& bytes)
{
    return QJsonDocument::fromJson(bytes).object().toVariantMap();
}

int patchWorker(const QStringList& arguments)
{
    const QString path = arguments.value(2);
    const QVariantMap baseline = variantMap(QByteArray::fromBase64(arguments.value(3).toLatin1()));
    const QVariantMap patch = variantMap(QByteArray::fromBase64(arguments.value(4).toLatin1()));
    if (arguments.contains(QStringLiteral("--fail-write"))) {
        std::signal(SIGXFSZ, SIG_IGN);
        const rlimit limit{128, 128};
        if (setrlimit(RLIMIT_FSIZE, &limit) != 0) {
            return 90;
        }
    }

    GlobalConfigBackend backend(path);
    const GlobalConfigPatchResult result = backend.patchValues(patch, baseline);
    QJsonObject output;
    output.insert(QStringLiteral("status"), int(result.status));
    output.insert(QStringLiteral("changedKeys"), QJsonArray::fromStringList(result.changedKeys));
    output.insert(QStringLiteral("conflictingKeys"),
        QJsonArray::fromStringList(result.conflictingKeys));
    output.insert(QStringLiteral("message"), result.message);
    QFile stdoutFile;
    if (!stdoutFile.open(stdout, QIODevice::WriteOnly)) {
        return 91;
    }
    stdoutFile.write(QJsonDocument(output).toJson(QJsonDocument::Compact));
    stdoutFile.write("\n");
    return 0;
}

QVariantMap readJsonFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return variantMap(file.readAll());
}
}

class GlobalConfigStoreTest : public QObject
{
    Q_OBJECT

private:
    QJsonObject runWriter(const QString& path,
        const QVariantMap& baseline,
        const QVariantMap& patch,
        bool failWrite = false)
    {
        QProcess process;
        process.setProgram(QCoreApplication::applicationFilePath());
        QStringList arguments{QStringLiteral("--patch-worker"),
            path,
            QString::fromLatin1(jsonBytes(baseline).toBase64()),
            QString::fromLatin1(jsonBytes(patch).toBase64())};
        if (failWrite) {
            arguments.append(QStringLiteral("--fail-write"));
        }
        process.setArguments(arguments);
        process.start();
        if (!process.waitForStarted() || !process.waitForFinished(10000)) {
            qWarning() << "writer process failed:" << process.errorString() << process.readAllStandardError();
            return {};
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            qWarning() << "writer exited unsuccessfully:" << process.exitCode()
                       << process.readAllStandardError();
            return {};
        }
        return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    }

    QString makeConfigPath(QTemporaryDir& temporaryDirectory)
    {
        const QString directory = temporaryDirectory.filePath(QStringLiteral("wekde"));
        QDir().mkpath(directory);
        return directory + QStringLiteral("/global.json");
    }

    bool writeBytes(const QString& path, const QByteArray& bytes)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        return file.write(bytes) == bytes.size();
    }

private slots:
    void twoWriterProcessesMergeDisjointPatches()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = makeConfigPath(directory);
        const QVariantMap initial{{QStringLiteral("alpha"), 1},
            {QStringLiteral("beta"), 2},
            {QStringLiteral("unknownNested"), QVariantMap{{QStringLiteral("keep"), true}}}};
        QVERIFY(writeBytes(path, jsonBytes(initial)));

        const QJsonObject first = runWriter(
            path, {{QStringLiteral("alpha"), 1}}, {{QStringLiteral("alpha"), 10}});
        QCOMPARE(first.value(QStringLiteral("status")).toInt(),
            int(GlobalConfigPatchStatus::Applied));
        const QJsonObject second = runWriter(
            path, {{QStringLiteral("beta"), 2}}, {{QStringLiteral("beta"), 20}});
        QCOMPARE(second.value(QStringLiteral("status")).toInt(),
            int(GlobalConfigPatchStatus::Applied));

        const QVariantMap final = readJsonFile(path);
        QCOMPARE(final.value(QStringLiteral("alpha")).toInt(), 10);
        QCOMPARE(final.value(QStringLiteral("beta")).toInt(), 20);
        QCOMPARE(final.value(QStringLiteral("unknownNested")).toMap().value(QStringLiteral("keep")),
            QVariant(true));
    }

    void sameKeyConflictDoesNotWrite()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = makeConfigPath(directory);
        QVERIFY(writeBytes(path, jsonBytes({{QStringLiteral("volume"), 5}})));

        const QJsonObject first = runWriter(
            path, {{QStringLiteral("volume"), 5}}, {{QStringLiteral("volume"), 6}});
        QCOMPARE(first.value(QStringLiteral("status")).toInt(),
            int(GlobalConfigPatchStatus::Applied));
        const QByteArray beforeConflict = QFile(path).exists() ? [&] {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                return QByteArray{};
            }
            return file.readAll();
        }() : QByteArray{};

        const QJsonObject conflict = runWriter(
            path, {{QStringLiteral("volume"), 5}}, {{QStringLiteral("volume"), 7}});
        QCOMPARE(conflict.value(QStringLiteral("status")).toInt(),
            int(GlobalConfigPatchStatus::Conflict));
        QCOMPARE(conflict.value(QStringLiteral("conflictingKeys")).toArray().first().toString(),
            QStringLiteral("volume"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), beforeConflict);
    }

    void busyLockIsReportedWithoutWaiting()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = makeConfigPath(directory);
        QVERIFY(writeBytes(path, jsonBytes({{QStringLiteral("x"), 1}})));
        QLockFile lock(path + QStringLiteral(".lock"));
        QVERIFY(lock.tryLock(0));

        QElapsedTimer timer;
        timer.start();
        const QJsonObject busy = runWriter(
            path, {{QStringLiteral("x"), 1}}, {{QStringLiteral("x"), 2}});
        QCOMPARE(busy.value(QStringLiteral("status")).toInt(), int(GlobalConfigPatchStatus::Busy));
        QVERIFY(timer.elapsed() < 3000);
        lock.unlock();
    }

    void malformedJsonIsRejectedAndPreserved()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = makeConfigPath(directory);
        const QByteArray malformed("{\"broken\":");
        QVERIFY(writeBytes(path, malformed));

        const QJsonObject result = runWriter(
            path, {}, {{QStringLiteral("newKey"), 1}});
        QCOMPARE(result.value(QStringLiteral("status")).toInt(),
            int(GlobalConfigPatchStatus::InvalidJson));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), malformed);
    }

    void failedAtomicCommitLeavesOriginalFileIntact()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = makeConfigPath(directory);
        const QByteArray initial = jsonBytes({{QStringLiteral("keep"), 11}});
        QVERIFY(writeBytes(path, initial));

        const QJsonObject result = runWriter(
            path, {{QStringLiteral("keep"), 11}}, {{QStringLiteral("new"), QString(4096, QLatin1Char('x'))}},
            true);
        QCOMPARE(result.value(QStringLiteral("status")).toInt(), int(GlobalConfigPatchStatus::IoError));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), initial);
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() > 1 && arguments.at(1) == QStringLiteral("--patch-worker")) {
        return patchWorker(arguments);
    }
    GlobalConfigStoreTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_globalconfigstore.moc"
