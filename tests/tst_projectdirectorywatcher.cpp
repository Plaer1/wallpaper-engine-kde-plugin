#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include "ProjectDirectoryWatcher.hpp"

using wekde::ProjectDirectoryWatcher;

class TestProjectDirectoryWatcher : public QObject {
    Q_OBJECT
    static bool write(const QString& path, const QByteArray& data, bool atomic = false) {
        if (! QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        if (atomic) {
            QSaveFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
        }
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }
private slots:
    void followsDownloadsAndMetadata() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString root = temp.filePath("431960");
        QVERIFY(QDir().mkpath(root));
        ProjectDirectoryWatcher watcher;
        watcher.setProjectDirs({ root, root });
        QCOMPARE(watcher.projectDirs().size(), 1);
        QSignalSpy changed(&watcher, &ProjectDirectoryWatcher::changed);
        watcher.setEnabled(true);
        QTest::qWait(300);
        QCOMPARE(changed.count(), 0); // no periodic activity
        QVERIFY(QDir().mkpath(root + "/123"));
        QTRY_COMPARE(changed.count(), 1);
        // Steam can create a project directory before its metadata arrives.
        QVERIFY(write(root + "/123/project.json", "{"));
        QTRY_COMPARE(changed.count(), 2);
        QVERIFY(write(root + "/123/project.json", R"({"title":"Alpine","type":"scene"})"));
        QTRY_COMPARE(changed.count(), 3);
        QVERIFY(write(root + "/123/project.json", R"({"title":"Mountains","type":"scene"})", true));
        QTRY_COMPARE(changed.count(), 4);
        // The replacement's new inode remains watched.
        QVERIFY(write(root + "/123/project.json", R"({"title":"Alpine"})"));
        QTRY_COMPARE(changed.count(), 5);
        QVERIFY(QFile::remove(root + "/123/project.json"));
        QTRY_COMPARE(changed.count(), 6);
        QVERIFY(write(root + "/123/project.json", R"({"title":"Returned"})"));
        QTRY_COMPARE(changed.count(), 7);
        QTest::qWait(350);
        QCOMPARE(changed.count(), 7);
        watcher.setEnabled(false);
        QVERIFY(write(root + "/123/project.json", "{}"));
        QTest::qWait(300);
        QCOMPARE(changed.count(), 7);
    }
    void followsFirstRootCreationAndDetachesOldRoots() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        ProjectDirectoryWatcher watcher;
        const QString first = temp.filePath("new/content/431960");
        watcher.setProjectDirs({ first });
        watcher.setEnabled(true);
        QSignalSpy changed(&watcher, &ProjectDirectoryWatcher::changed);
        QVERIFY(write(first + "/123/project.json", "{}"));
        QTRY_COMPARE(changed.count(), 1);
        const QString second = temp.filePath("other");
        QVERIFY(QDir().mkpath(second));
        watcher.setProjectDirs({ second });
        QTest::qWait(300);
        const int before = changed.count();
        QVERIFY(write(first + "/123/project.json", R"({"ignored":true})"));
        QTest::qWait(300);
        QCOMPARE(changed.count(), before);
        QVERIFY(write(second + "/456/project.json", "{}"));
        QTRY_COMPARE(changed.count(), before + 1);
    }
};
QTEST_GUILESS_MAIN(TestProjectDirectoryWatcher)
#include "tst_projectdirectorywatcher.moc"
