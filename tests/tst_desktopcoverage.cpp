#include "DesktopCoverage.hpp"

#include <QTest>

#include <limits>

using namespace DesktopCoverage;

class DesktopCoverageTest : public QObject
{
    Q_OBJECT

private slots:
    void oneHalfDoesNotPause()
    {
        const Result result = evaluate({QRectF(0, 0, 100, 100)}, {QRectF(0, 0, 50, 100)}, false);
        QVERIFY(result.known());
        QCOMPARE(result.coveredArea, quint64(5000));
        QCOMPARE(result.targetArea, quint64(10000));
        QCOMPARE(result.fraction, 0.5);
        QCOMPARE(result.decision, Decision::Visible);
    }

    void twoHalvesAndFourQuadrantsCoverTarget()
    {
        const Result halves = evaluate({QRectF(0, 0, 100, 100)},
            {QRectF(0, 0, 50, 100), QRectF(50, 0, 50, 100)}, false);
        QCOMPARE(halves.coveredArea, quint64(10000));
        QCOMPARE(halves.decision, Decision::Paused);

        const Result quadrants = evaluate({QRectF(0, 0, 100, 100)},
            {QRectF(0, 0, 50, 50),
                QRectF(50, 0, 50, 50),
                QRectF(0, 50, 50, 50),
                QRectF(50, 50, 50, 50)},
            false);
        QCOMPARE(quadrants.coveredArea, quint64(10000));
        QCOMPARE(quadrants.decision, Decision::Paused);
    }

    void duplicatesAndOverlapAreCountedOnlyOnce()
    {
        const QVector<QRectF> oneWindow{QRectF(0, 0, 60, 100)};
        const Result one = evaluate({QRectF(0, 0, 100, 100)}, oneWindow, false);
        const Result duplicate = evaluate(
            {QRectF(0, 0, 100, 100)}, {oneWindow.first(), oneWindow.first()}, false);
        QCOMPARE(one.coveredArea, quint64(6000));
        QCOMPARE(duplicate.coveredArea, one.coveredArea);

        const Result overlap = evaluate({QRectF(0, 0, 100, 100)},
            {QRectF(0, 0, 70, 100), QRectF(30, 0, 70, 100)}, false);
        QCOMPARE(overlap.coveredArea, quint64(10000));
        QCOMPARE(overlap.targetArea, quint64(10000));
    }

    void hysteresisUsesInclusivePauseAndStrictResumeThresholds()
    {
        const QRectF target(0, 0, 100, 100);
        QCOMPARE(evaluate({target}, {QRectF(0, 0, 97, 100)}, false).decision,
            Decision::Visible);
        QCOMPARE(evaluate({target}, {QRectF(0, 0, 98, 100)}, false).decision,
            Decision::Paused);
        QCOMPARE(evaluate({target}, {QRectF(0, 0, 95, 100)}, true).decision,
            Decision::Paused);
        QCOMPARE(evaluate({target}, {QRectF(0, 0, 94, 100)}, true).decision,
            Decision::Visible);
    }

    void negativeOriginsAndCrossOutputWindowsUseGlobalCoordinates()
    {
        const Result negative = evaluate({QRectF(-1920, -200, 1920, 1080)},
            {QRectF(-1920, -200, 1920, 1080)}, false);
        QCOMPARE(negative.coveredArea, negative.targetArea);
        QCOMPARE(negative.decision, Decision::Paused);

        const QVector<QRectF> outputs{QRectF(0, 0, 100, 100), QRectF(100, 0, 100, 100)};
        const Result crossing = evaluate(outputs, {QRectF(50, 0, 100, 100)}, false);
        QCOMPARE(crossing.targetArea, quint64(20000));
        QCOMPARE(crossing.coveredArea, quint64(10000));
        QCOMPARE(crossing.decision, Decision::Visible);
    }

    void usableRegionExcludesPanelAndScaledLogicalCoordinatesStayConservative()
    {
        const Result panel = evaluate({QRectF(0, 0, 100, 80)}, {QRectF(0, 0, 100, 100)}, false);
        QCOMPARE(panel.coveredArea, quint64(8000));
        QCOMPARE(panel.targetArea, quint64(8000));
        QCOMPARE(panel.fraction, 1.0);

        const QRectF fractionalTarget(-500.25, 125.25, 200.5, 100.5);
        const Result rounded = evaluate({fractionalTarget}, {fractionalTarget}, false);
        QCOMPARE(rounded.targetArea, quint64(202 * 101));
        QCOMPARE(rounded.coveredArea, quint64(200 * 99));
        QVERIFY(rounded.fraction < 0.98);
        QCOMPARE(rounded.decision, Decision::Visible);
    }

    void emptyOrInvalidTargetIsUnknownAndInvalidWindowsAreIgnored()
    {
        QCOMPARE(evaluate({}, {}, false).decision, Decision::Unknown);
        QCOMPARE(evaluate({QRectF(0, 0, 100, 100), QRectF(1, 1, -10, 20)}, {}, false).decision,
            Decision::Unknown);
        const QRectF nonFinite(std::numeric_limits<double>::quiet_NaN(), 0, 10, 10);
        QCOMPARE(evaluate({nonFinite}, {}, false).decision, Decision::Unknown);

        const Result invalidWindow = evaluate({QRectF(0, 0, 100, 100)},
            {QRectF(0, 0, -10, 20), QRectF(0, 0,
                std::numeric_limits<double>::infinity(), 10)},
            false);
        QVERIFY(invalidWindow.known());
        QCOMPARE(invalidWindow.coveredArea, quint64(0));
        QCOMPARE(invalidWindow.decision, Decision::Visible);
    }

    void areaUses64BitArithmetic()
    {
        constexpr int side = 1000000000;
        const Result result = evaluate({QRectF(0, 0, side, side)},
            {QRectF(0, 0, side, side)}, false);
        QCOMPARE(result.targetArea, quint64(1000000000000000000ULL));
        QCOMPARE(result.coveredArea, result.targetArea);
        QCOMPARE(result.decision, Decision::Paused);
    }
};

QTEST_APPLESS_MAIN(DesktopCoverageTest)
#include "tst_desktopcoverage.moc"
