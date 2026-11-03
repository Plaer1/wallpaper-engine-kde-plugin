#include "PrimaryScreenMonitor.hpp"

#include <Plasma/Corona>

#include <QGuiApplication>
#include <QRegion>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>

class FakeCorona final : public Plasma::Corona
{
    Q_OBJECT

public:
    QVector<QRect> geometries;
    QVector<QRegion> regions;

    uint numScreens() const override { return uint(geometries.size()); }
    QRect screenGeometry(uint id) const override
    {
        return id < uint(geometries.size()) ? geometries.at(int(id)) : QRect();
    }
    QRegion availableScreenRegion(uint id) const override
    {
        return id < uint(regions.size()) ? regions.at(int(id)) : QRegion();
    }

    void replaceGeometry(uint id, const QRect &geometry)
    {
        geometries[int(id)] = geometry;
        Q_EMIT screenGeometryChanged(id);
    }
    void replaceRegion(uint id, const QRegion &region)
    {
        regions[int(id)] = region;
        Q_EMIT availableScreenRegionChanged(id);
    }
};

class PrimaryScreenMonitorTest : public QObject
{
    Q_OBJECT

private slots:
    void exposesMatchedPrimaryScreenAndGlobalWorkArea()
    {
        QScreen *primary = QGuiApplication::primaryScreen();
        QVERIFY(primary);
        const QRect geometry = primary->geometry();
        QVERIFY(geometry.isValid());

        FakeCorona corona;
        corona.geometries = {geometry};
        const QRect usable(geometry.x(), geometry.y() + 40,
                           geometry.width(), geometry.height() - 40);
        QRegion region(usable);
        region += QRect(geometry.x() - 40, usable.y(), 40, usable.height());
        corona.regions = {region};

        wekde::PrimaryScreenMonitor monitor;
        monitor.setCorona(&corona);

        QVERIFY(monitor.valid());
        QCOMPARE(monitor.screenName(), primary->name());
        QCOMPARE(monitor.screenGeometry(), geometry);
        QCOMPARE(monitor.usableRegion().size(), 1);
        QCOMPARE(monitor.usableRegion().first().toRectF(), QRectF(usable));
    }

    void clipsGlobalUsableRegionAtOffsetPrimaryAndExcludesPanelArea()
    {
        const QRect primary(-1920, -200, 1920, 1080);
        const QRect workArea(-1920, -160, 1920, 1040);
        QRegion region(workArea);
        region += QRect(-2000, -160, 80, 1040);

        const QVariantList clipped = wekde::PrimaryScreenMonitor::clipUsableRegion(region, primary);
        QCOMPARE(clipped.size(), 1);
        QCOMPARE(clipped.first().toRectF(), QRectF(workArea));
    }

    void rejectsMissingOrAmbiguousGeometryMatches()
    {
        QScreen *primary = QGuiApplication::primaryScreen();
        QVERIFY(primary);
        const QRect geometry = primary->geometry();
        const QRegion region(geometry);

        FakeCorona missing;
        missing.geometries = {geometry.translated(1, 0)};
        missing.regions = {region};
        wekde::PrimaryScreenMonitor monitor;
        monitor.setCorona(&missing);
        QVERIFY(!monitor.valid());

        FakeCorona ambiguous;
        ambiguous.geometries = {geometry, geometry};
        ambiguous.regions = {region, region};
        monitor.setCorona(&ambiguous);
        QVERIFY(!monitor.valid());
        QVERIFY(monitor.usableRegion().isEmpty());
    }

    void rechecksOnPlasmaGeometryAndWorkAreaChanges()
    {
        QScreen *primary = QGuiApplication::primaryScreen();
        QVERIFY(primary);
        const QRect geometry = primary->geometry();

        FakeCorona corona;
        corona.geometries = {geometry.translated(0, 1)};
        corona.regions = {QRegion(geometry)};
        wekde::PrimaryScreenMonitor monitor;
        QSignalSpy changed(&monitor, &wekde::PrimaryScreenMonitor::changed);
        monitor.setCorona(&corona);
        QVERIFY(!monitor.valid());

        corona.replaceGeometry(0, geometry);
        QVERIFY(monitor.valid());
        QCOMPARE(changed.size(), 1);

        const QRect usable(geometry.x(), geometry.y() + 40,
                           geometry.width(), geometry.height() - 40);
        corona.replaceRegion(0, QRegion(usable));
        QCOMPARE(changed.size(), 2);
        QCOMPARE(monitor.usableRegion().first().toRectF(), QRectF(usable));
    }
};

QTEST_MAIN(PrimaryScreenMonitorTest)
#include "tst_primaryscreenmonitor.moc"
