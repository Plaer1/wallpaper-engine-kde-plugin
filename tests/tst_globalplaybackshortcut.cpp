#include "GlobalPlaybackShortcut.hpp"

#include <QtTest>

class GlobalPlaybackShortcutTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void shortcutStartsDisabled() {
        wekde::GlobalPlaybackShortcut shortcut;
        QVERIFY(shortcut.shortcut().isEmpty());
        QVERIFY(shortcut.errorCode().isEmpty());
        QVERIFY(shortcut.setShortcut(QString()));
        QVERIFY(shortcut.shortcut().isEmpty());
    }

    void invalidShortcutIsRejectedWithoutRegistration() {
        wekde::GlobalPlaybackShortcut shortcut;
        QVERIFY(!shortcut.setShortcut(QStringLiteral("Ctrl+__invalid__")));
        QVERIFY(shortcut.shortcut().isEmpty());
        QCOMPARE(shortcut.errorCode(), QStringLiteral("invalidShortcut"));
    }
};

QTEST_GUILESS_MAIN(GlobalPlaybackShortcutTest)
#include "tst_globalplaybackshortcut.moc"
