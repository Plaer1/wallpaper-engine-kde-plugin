#include "WallpaperPlaybackPolicy.hpp"

#include <QTest>

using namespace WallpaperPlaybackPolicy;

Q_DECLARE_METATYPE(WallpaperPlaybackPolicy::Override)
Q_DECLARE_METATYPE(WallpaperPlaybackPolicy::Conditions)
Q_DECLARE_METATYPE(WallpaperPlaybackPolicy::EffectivePlayback)

class WallpaperPlaybackPolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void overrideTruthTable_data()
    {
        QTest::addColumn<Override>("desired");
        QTest::addColumn<Conditions>("conditions");
        QTest::addColumn<EffectivePlayback>("expected");

        const Override overrides[] = {
            Override::Automatic, Override::Paused, Override::PlayDespiteWindows};
        for (Override desired : overrides) {
            const char *overrideName = desired == Override::Automatic ? "automatic"
                : desired == Override::Paused ? "manual" : "playDespiteWindows";
            for (int mask = 0; mask < 64; ++mask) {
                Conditions conditions{bool(mask & 1),
                    bool(mask & 2),
                    bool(mask & 4),
                    bool(mask & 8),
                    bool(mask & 16),
                    bool(mask & 32)};
                EffectivePlayback expected = EffectivePlayback::Playing;
                if (conditions.sleep || conditions.inactiveSession || desired == Override::Paused
                    || conditions.battery || conditions.gameMode
                    || (conditions.fullscreenApp && desired != Override::PlayDespiteWindows)
                    || (conditions.windowCoverage && desired == Override::Automatic)) {
                    expected = EffectivePlayback::Paused;
                }
                const QByteArray name = QByteArray(overrideName) + "_mask_" + QByteArray::number(mask);
                QTest::newRow(name.constData()) << desired << conditions << expected;
            }
        }
    }

    void overrideTruthTable()
    {
        QFETCH(Override, desired);
        QFETCH(Conditions, conditions);
        QFETCH(EffectivePlayback, expected);

        const Resolution result = resolve(desired, conditions);
        QCOMPARE(result.desired, desired);
        QCOMPARE(result.effective, expected);
    }

    void reasonsFollowContractOrder()
    {
        Conditions conditions{true, true, true, true, true, true};
        const auto result = resolve(Override::Paused, conditions);
        const QVector<Reason> expected{Reason::Sleep,
            Reason::InactiveSession,
            Reason::Manual,
            Reason::Battery,
            Reason::GameMode,
            Reason::FullscreenApp,
            Reason::WindowCoverage};
        QCOMPARE(result.pauseReasons, expected);
    }

    void playDespiteWindowsOverridesWindowAndFullscreenOnly()
    {
        Conditions conditions;
        conditions.windowCoverage = true;
        conditions.fullscreenApp = true;
        auto result = resolve(Override::PlayDespiteWindows, conditions);
        QCOMPARE(result.effective, EffectivePlayback::Playing);
        QVERIFY(result.pauseReasons.isEmpty());

        conditions.battery = true;
        result = resolve(Override::PlayDespiteWindows, conditions);
        QCOMPARE(result.effective, EffectivePlayback::Paused);
        QCOMPARE(result.pauseReasons, QVector<Reason>{Reason::Battery});

        conditions.battery = false;
        conditions.gameMode = true;
        result = resolve(Override::PlayDespiteWindows, conditions);
        QCOMPARE(result.effective, EffectivePlayback::Paused);
        QCOMPARE(result.pauseReasons, QVector<Reason>{Reason::GameMode});

        conditions.gameMode = false;
        conditions.sleep = true;
        result = resolve(Override::PlayDespiteWindows, conditions);
        QCOMPARE(result.effective, EffectivePlayback::Paused);
        QCOMPARE(result.pauseReasons, QVector<Reason>{Reason::Sleep});

        conditions.sleep = false;
        conditions.inactiveSession = true;
        result = resolve(Override::PlayDespiteWindows, conditions);
        QCOMPARE(result.effective, EffectivePlayback::Paused);
        QCOMPARE(result.pauseReasons, QVector<Reason>{Reason::InactiveSession});
    }

    void fullscreenIsGlobalEvenWhenOrdinaryCoverageIsOff()
    {
        Conditions conditions;
        conditions.fullscreenApp = true;

        auto result = resolve(Override::Automatic, conditions);
        QCOMPARE(result.effective, EffectivePlayback::Paused);
        QCOMPARE(result.pauseReasons, QVector<Reason>{Reason::FullscreenApp});

        result = resolve(Override::PlayDespiteWindows, conditions);
        QCOMPARE(result.effective, EffectivePlayback::Playing);
        QVERIFY(result.pauseReasons.isEmpty());

        result = resolve(Override::Paused, conditions);
        QCOMPARE(result.effective, EffectivePlayback::Paused);
        QCOMPARE(result.pauseReasons, (QVector<Reason>{Reason::Manual, Reason::FullscreenApp}));
    }

    void sourceReloadKeepsSessionIntent()
    {
        const Override sessionIntent = Override::Paused;
        const Conditions clear;

        // A source swap replaces renderer content, not the session-owned intent.
        const auto afterReload = resolve(sessionIntent, clear);
        QCOMPARE(afterReload.desired, Override::Paused);
        QCOMPARE(afterReload.effective, EffectivePlayback::Paused);
    }
};

QTEST_APPLESS_MAIN(WallpaperPlaybackPolicyTest)
#include "tst_wallpaperplaybackpolicy.moc"
