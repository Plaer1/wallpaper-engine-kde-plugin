// SPDX-License-Identifier: GPL-2.0-only
// Event-driven account catalog watcher lifecycle checks.
#include <QtTest>
#include <QDir>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QThread>

#include "SteamAccountsWatcher.hpp"

namespace {
QString makeCache(const QString& publishedId) {
    return QStringLiteral("\"appid\" \"431960\" \"subscribedfiles\" { "
                          "\"appid\" \"431960\" \"time_last_updated\" \"1700000000\" "
                          "\"0\" { \"publishedfileid\" \"%1\" \"disabled_locally\" \"0\" } }")
        .arg(publishedId);
}

template<typename Predicate>
bool waitUntil(Predicate predicate, int timeoutMs) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return predicate();
}

bool writeAtomic(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (file.write(bytes) != bytes.size()) return false;
    return file.commit();
}

QByteArray manyRecords(int count) {
    QByteArray text("\"subscribedfiles\" { \"appid\" \"431960\" ");
    for (int i = 0; i < count; ++i) {
        text += QByteArray("\"") + QByteArray::number(i) + QByteArray("\" { \"publishedfileid\" \"")
                + QByteArray::number(100000 + i) + QByteArray("\" } ");
    }
    text += QByteArray("}");
    return text;
}
}

class TestSteamAccountsWatcher : public QObject {
    Q_OBJECT
private slots:
    void detectsFirstCreationAtomicReplaceAndRemovalRetainsLastGood() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = tmp.filePath(QStringLiteral("Steam"));
        const QString login = root + QStringLiteral("/config/loginusers.vdf");
        const QString userdata = root + QStringLiteral("/userdata/42");
        QVERIFY(QDir().mkpath(QFileInfo(login).absolutePath()));
        QVERIFY(QDir().mkpath(userdata));
        QVERIFY(writeAtomic(login, QByteArray("\"users\" { \"76561197960265770\" { "
                                              "\"PersonaName\" \"Watcher Test\" } }")));

        wekde::SteamAccountsWatcher watcher([root] { return QStringList { root }; });
        QVERIFY(waitUntil([&] { return !watcher.catalog().accounts.isEmpty(); }, 3000));
        QCOMPARE(watcher.catalog().caches.first().state, steam::accounts::CacheState::Missing);
        const QString cachePath = userdata + QStringLiteral("/ugc/431960_subscriptions.vdf");

        QVERIFY(writeAtomic(cachePath, makeCache(QStringLiteral("101")).toUtf8()));
        QVERIFY(waitUntil([&] { return watcher.catalog().caches.first().subscribedIds == QStringList({"101"}); }, 5000));
        QCOMPARE(watcher.catalog().caches.first().state, steam::accounts::CacheState::Valid);

        QVERIFY(writeAtomic(cachePath, makeCache(QStringLiteral("202")).toUtf8()));
        QVERIFY(waitUntil([&] { return watcher.catalog().caches.first().subscribedIds == QStringList({"202"}); }, 5000));

        QVERIFY(writeAtomic(cachePath, QByteArray("\"subscribedfiles\" { \"0\" {")));
        QVERIFY(waitUntil([&] { return watcher.catalog().caches.first().state == steam::accounts::CacheState::Invalid; }, 5000));
        QCOMPARE(watcher.catalog().caches.first().subscribedIds, QStringList({"202"}));

        QVERIFY(QFile::remove(cachePath));
        QVERIFY(waitUntil([&] { return watcher.catalog().caches.first().state == steam::accounts::CacheState::Missing; }, 5000));
        QCOMPARE(watcher.catalog().caches.first().subscribedIds, QStringList({"202"}));
    }

    void rejectsWrongAppIdWithoutReplacingMembership() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = tmp.filePath(QStringLiteral("Steam"));
        const QString login = root + QStringLiteral("/config/loginusers.vdf");
        const QString cache = root + QStringLiteral("/userdata/42/ugc/431960_subscriptions.vdf");
        QVERIFY(writeAtomic(login, QByteArray("\"users\" { }")));
        QVERIFY(writeAtomic(cache, makeCache(QStringLiteral("8")).toUtf8()));
        wekde::SteamAccountsWatcher watcher([root] { return QStringList { root }; });
        QVERIFY(waitUntil([&] { return !watcher.catalog().caches.isEmpty() &&
                                      watcher.catalog().caches.first().subscribedIds == QStringList({"8"}); }, 3000));

        const QString wrong = makeCache(QStringLiteral("9")).replace(
            QStringLiteral("\"appid\" \"431960\""), QStringLiteral("\"appid\" \"730\""));
        QVERIFY(writeAtomic(cache, wrong.toUtf8()));
        QVERIFY(waitUntil([&] { return watcher.catalog().caches.first().state == steam::accounts::CacheState::Invalid; }, 5000));
        QCOMPARE(watcher.catalog().caches.first().subscribedIds, QStringList({"8"}));
        QVERIFY(watcher.catalog().caches.first().error.contains(QStringLiteral("appid")));
    }

    void doesNotPublishIdlePollingUpdatesAndCoalescesRefreshes() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = tmp.filePath(QStringLiteral("Steam"));
        QVERIFY(QDir().mkpath(root));
        wekde::SteamAccountsWatcher watcher([root] { return QStringList { root }; });
        QSignalSpy changed(&watcher, &wekde::SteamAccountsWatcher::catalogChanged);
        QVERIFY(waitUntil([&] { return watcher.catalog().generation > 0; }, 3000));
        const int initialSignals = changed.count();
        QTest::qWait(450);
        QCOMPARE(changed.count(), initialSignals);

        changed.clear();
        watcher.refresh();
        watcher.refresh();
        QVERIFY(waitUntil([&] { return watcher.catalog().generation >= 3; }, 3000));
        QVERIFY(waitUntil([&] { return changed.count() == 1; }, 3000));
        QCOMPARE(watcher.catalog().generation, quint64(3));
    }

    void destructionDuringBackgroundScanIsSafe() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = tmp.filePath(QStringLiteral("Steam"));
        const QString cache = root + QStringLiteral("/userdata/42/ugc/431960_subscriptions.vdf");
        QVERIFY(writeAtomic(root + QStringLiteral("/config/loginusers.vdf"), QByteArray("\"users\" { }")));
        QVERIFY(writeAtomic(cache, manyRecords(20000)));

        auto* watcher = new wekde::SteamAccountsWatcher([root] { return QStringList { root }; });
        QThread::msleep(5); // let the worker enter the bounded parse
        QElapsedTimer destructorTime;
        destructorTime.start();
        delete watcher;
        QVERIFY2(destructorTime.elapsed() < 1000,
                 "Destroying the watcher waited on the background catalog scan");
        QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
};

QTEST_GUILESS_MAIN(TestSteamAccountsWatcher)
#include "tst_steamaccountswatcher.moc"
