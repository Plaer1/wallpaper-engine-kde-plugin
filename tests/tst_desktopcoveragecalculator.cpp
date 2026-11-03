#include "DesktopCoverageCalculator.hpp"

#include <QRectF>
#include <QTest>

using wekde::DesktopCoverageCalculator;

class DesktopCoverageCalculatorTest : public QObject {
    Q_OBJECT

private slots:
    void showDesktopAlwaysResumesWithoutGeometry() {
        DesktopCoverageCalculator calculator;
        const QVariantMap         result = calculator.evaluate({}, {}, true, true, false);
        QCOMPARE(result.value(QStringLiteral("decision")).toString(), QStringLiteral("visible"));
        QCOMPARE(result.value(QStringLiteral("known")).toBool(), true);
        QCOMPARE(result.value(QStringLiteral("override")).toString(),
                 QStringLiteral("showDesktop"));
        QCOMPARE(result.value(QStringLiteral("targetArea")).toString(), QStringLiteral("0"));
    }

    void absentUnknownOrUnverifiedInputsStayUnknown() {
        DesktopCoverageCalculator calculator;
        const QVariantList        target { QRectF(0, 0, 100, 100) };
        const QVariantList        fullWindow { QRectF(0, 0, 100, 100) };

        QCOMPARE(calculator.evaluate({}, fullWindow, false, false, true)
                     .value(QStringLiteral("decision"))
                     .toString(),
                 QStringLiteral("unknown"));
        QCOMPARE(calculator.evaluate(target, fullWindow, false, false, false)
                     .value(QStringLiteral("decision"))
                     .toString(),
                 QStringLiteral("unknown"));
        QCOMPARE(calculator.evaluate(target, fullWindow, false, QVariant(), true)
                     .value(QStringLiteral("decision"))
                     .toString(),
                 QStringLiteral("unknown"));
        QCOMPARE(calculator.evaluate(target, fullWindow, false, QStringLiteral("false"), true)
                     .value(QStringLiteral("decision"))
                     .toString(),
                 QStringLiteral("unknown"));
    }

    void rejectsNonRectVariantsAndNonBooleanState() {
        DesktopCoverageCalculator calculator;
        const QVariantList        good { QRectF(0, 0, 100, 100) };
        const QVariantList        malformed { QVariantMap { { QStringLiteral("x"), 0 },
                                                            { QStringLiteral("y"), 0 },
                                                            { QStringLiteral("width"), 100 },
                                                            { QStringLiteral("height"), 100 } } };

        QCOMPARE(calculator.evaluate(malformed, {}, false, false, true)
                     .value(QStringLiteral("decision"))
                     .toString(),
                 QStringLiteral("unknown"));
        QCOMPARE(
            calculator.evaluate(good, { QStringLiteral("not a rectangle") }, false, false, true)
                .value(QStringLiteral("decision"))
                .toString(),
            QStringLiteral("unknown"));
        QCOMPARE(calculator.evaluate(good, {}, 0, false, true)
                     .value(QStringLiteral("decision"))
                     .toString(),
                 QStringLiteral("unknown"));
    }

    void acceptsQRectAndQRectFAndUsesCoverageHysteresis() {
        DesktopCoverageCalculator calculator;
        const QVariantList        target { QRect(0, 0, 100, 100) };

        const QVariantMap belowPause =
            calculator.evaluate(target, { QRectF(0, 0, 97, 100) }, false, false, true);
        QCOMPARE(belowPause.value(QStringLiteral("decision")).toString(),
                 QStringLiteral("visible"));
        QCOMPARE(belowPause.value(QStringLiteral("coveredArea")).toString(),
                 QStringLiteral("9700"));

        const QVariantMap atPause =
            calculator.evaluate(target, { QRectF(0, 0, 98, 100) }, false, false, true);
        QCOMPARE(atPause.value(QStringLiteral("decision")).toString(), QStringLiteral("paused"));

        const QVariantMap atResume =
            calculator.evaluate(target, { QRect(0, 0, 95, 100) }, true, false, true);
        QCOMPARE(atResume.value(QStringLiteral("decision")).toString(), QStringLiteral("paused"));

        const QVariantMap belowResume =
            calculator.evaluate(target, { QRectF(0, 0, 94, 100) }, true, false, true);
        QCOMPARE(belowResume.value(QStringLiteral("decision")).toString(),
                 QStringLiteral("visible"));
        QCOMPARE(belowResume.value(QStringLiteral("targetArea")).toString(),
                 QStringLiteral("10000"));
    }
};

QTEST_APPLESS_MAIN(DesktopCoverageCalculatorTest)
#include "tst_desktopcoveragecalculator.moc"
