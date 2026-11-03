#include "DesktopCoverageCalculator.hpp"

#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRect>
#include <QRectF>
#include <QTest>
#include <QtQml/qqml.h>

using wekde::DesktopCoverageCalculator;

class DesktopCoverageCalculatorQmlTest : public QObject {
    Q_OBJECT

private slots:
    void qmlVariantListsPreserveQRectAndQRectF() {
        qmlRegisterType<DesktopCoverageCalculator>("wekde.coverage", 1, 0,
            "DesktopCoverageCalculator");

        QFile fixture(QString::fromUtf8(COVERAGE_QML_FIXTURE));
        QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(fixture.errorString()));
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(fixture.readAll(), QUrl::fromLocalFile(fixture.fileName()));
        QScopedPointer<QObject> harness(component.create());
        QVERIFY2(harness, qPrintable(component.errorString()));

        const QVariantList targets {
            QVariant::fromValue(QRect(0, 0, 100, 100)),
            QVariant::fromValue(QRectF(100, 0, 100, 100)),
        };
        const QVariantList windows {
            QVariant::fromValue(QRectF(0, 0, 100, 100)),
            QVariant::fromValue(QRect(100, 0, 100, 100)),
        };
        QVariant resultValue;
        const bool invoked = QMetaObject::invokeMethod(harness.data(), "evaluate",
            Q_RETURN_ARG(QVariant, resultValue),
            Q_ARG(QVariant, QVariant::fromValue(targets)),
            Q_ARG(QVariant, QVariant::fromValue(windows)));
        QVERIFY(invoked);

        const QVariantMap result = resultValue.toMap();
        QCOMPARE(result.value(QStringLiteral("decision")).toString(), QStringLiteral("paused"));
        QCOMPARE(result.value(QStringLiteral("coveredArea")).toString(), QStringLiteral("20000"));
        QCOMPARE(result.value(QStringLiteral("targetArea")).toString(), QStringLiteral("20000"));
    }
};

QTEST_GUILESS_MAIN(DesktopCoverageCalculatorQmlTest)
#include "tst_desktopcoveragecalculatorqml.moc"
