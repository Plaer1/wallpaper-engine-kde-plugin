#include "WallpaperRuntimeRegistration.hpp"

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include <memory>

using namespace wekde;

namespace
{

constexpr auto PluginId = "com.github.catsout.wallpaperEngineKde";
constexpr auto ActivityId = "b0169dd2-b1c7-4f46-817e-c21e6f7b6907";
constexpr quint32 Desktop = 0;

class TestRegistration final : public WallpaperRuntimeRegistration {
public:
    using WallpaperRuntimeRegistration::WallpaperRuntimeRegistration;

    QString syntheticOutput = QStringLiteral("synthetic-output");
    QStringList syntheticOutputs { QStringLiteral("synthetic-output") };
    QRectF syntheticGeometry;

protected:
    OutputIdentity currentOutput() const override {
        OutputIdentity output;
        output.window = targetItem() ? targetItem()->window() : nullptr;
        output.screen = output.window ? output.window->screen() : nullptr;
        output.name = syntheticOutput;
        output.availableNames = syntheticOutputs;
        output.geometry = syntheticGeometry.isValid()
            ? syntheticGeometry
            : (output.screen ? QRectF(output.screen->geometry()) : QRectF());
        return output;
    }
};

void attachToWindow(QQuickItem& item, QQuickWindow& window) {
    item.setParentItem(window.contentItem());
    window.show();
    QCoreApplication::processEvents();
}

QVariantMap containmentContext(QObject* wallpaper, QObject* activeWallpaper,
                               const QRectF& geometry, QString activity = QString::fromLatin1(ActivityId),
                               quint32 screen = 0, int containmentType = Desktop,
                               QVariantList region = {}) {
    if (region.isEmpty()) {
        region.append(QVariant::fromValue(QRectF(0.0, 0.0, geometry.width(), geometry.height())));
    }
    return {
        { QStringLiteral("wallpaperObject"), QVariant::fromValue(wallpaper) },
        { QStringLiteral("activeWallpaperObject"), QVariant::fromValue(activeWallpaper) },
        { QStringLiteral("containmentId"), QStringLiteral("31") },
        { QStringLiteral("activityId"), activity },
        { QStringLiteral("containmentType"), containmentType },
        { QStringLiteral("screen"), screen },
        { QStringLiteral("screenGeometry"), QVariant::fromValue(geometry) },
        { QStringLiteral("availableScreenRegion"), region },
        { QStringLiteral("wallpaperPlugin"), QString::fromLatin1(PluginId) }
    };
}

} // namespace

class WallpaperRuntimeRegistrationTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void qmlRectAndListConvertThroughNativeContextProperty() {
        static const int registration = qmlRegisterType<WallpaperRuntimeRegistration>(
            "Wekde.RuntimeRegistrationTest", 1, 0, "WallpaperRuntimeRegistration");
        Q_UNUSED(registration);

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"QML(
            import QtQuick
            import Wekde.RuntimeRegistrationTest 1.0

            Item {
                id: root
                property var nativeRegistration: runtimeRegistration
                readonly property var contextForNative: ({
                    wallpaperObject: root,
                    activeWallpaperObject: root,
                    screenGeometry: Qt.rect(0, 0, 1920, 1080),
                    availableScreenRegion: [Qt.rect(0, 0, 1920, 1080)]
                })
                WallpaperRuntimeRegistration {
                    id: runtimeRegistration
                    containmentContext: root.contextForNative
                }
            }
        )QML", QUrl(QStringLiteral("qrc:/runtime-registration-conversion.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root != nullptr, qPrintable(component.errorString()));
        QObject* nativeRegistration = root->property("nativeRegistration").value<QObject*>();
        QVERIFY(nativeRegistration != nullptr);

        const QVariantMap context = nativeRegistration->property("containmentContext").toMap();
        const QVariant geometry = context.value(QStringLiteral("screenGeometry"));
        QCOMPARE(geometry.metaType().id(), QMetaType::QRectF);
        QVERIFY(geometry.canConvert<QRectF>());
        QCOMPARE(geometry.toRectF(), QRectF(0, 0, 1920, 1080));

        const QVariant region = context.value(QStringLiteral("availableScreenRegion"));
        QCOMPARE(region.metaType().id(), QMetaType::QVariantList);
        const QVariantList rects = region.toList();
        QCOMPARE(rects.size(), 1);
        QCOMPARE(rects.constFirst().metaType().id(), QMetaType::QRectF);
        QVERIFY(rects.constFirst().canConvert<QRectF>());
        QCOMPARE(rects.constFirst().toRectF(), QRectF(0, 0, 1920, 1080));
    }

    void nativeQRectListContainmentRegionValidation() {
        QQuickWindow window;
        QQuickItem root;
        attachToWindow(root, window);

        TestRegistration registration;
        registration.setTargetItem(&root);
        auto context = containmentContext(&root, &root, QRectF(window.screen()->geometry()));
        const QRectF validRect(0, 0, window.screen()->geometry().width(),
                               window.screen()->geometry().height());
        const QVariant validRegion = QVariant::fromValue(QList<QRectF>{validRect});
        QCOMPARE(validRegion.metaType().id(), qMetaTypeId<QList<QRectF>>());
        context.insert(QStringLiteral("availableScreenRegion"), validRegion);
        registration.setContainmentContext(context);
        QVERIFY(registration.isRegistered());

        context.insert(QStringLiteral("availableScreenRegion"),
                       QVariant::fromValue(QList<QRectF>{}));
        registration.setContainmentContext(context);
        QVERIFY(!registration.isRegistered());

        context.insert(QStringLiteral("availableScreenRegion"),
                       QVariant::fromValue(QList<QRectF>{QRectF(0, 0, 0, validRect.height())}));
        registration.setContainmentContext(context);
        QVERIFY(!registration.isRegistered());

        context.insert(QStringLiteral("availableScreenRegion"), validRegion);
        registration.setContainmentContext(context);
        QVERIFY(registration.isRegistered());
    }

    void activeContainmentWallpaperRegisters() {
        QQuickWindow window;
        QQuickItem root;
        attachToWindow(root, window);

        TestRegistration registration;
        QSignalSpy changed(&registration, &WallpaperRuntimeRegistration::registrationChanged);
        registration.setTargetItem(&root);
        registration.setContainmentContext(
            containmentContext(&root, &root, QRectF(window.screen()->geometry())));

        QVERIFY(registration.isRegistered());
        QVERIFY(!QUuid::fromString(registration.instanceId()).isNull());
        QCOMPARE(registration.outputName(), QStringLiteral("synthetic-output"));
        QCOMPARE(registration.activityId(), QString::fromLatin1(ActivityId));
        QCOMPARE(registration.containmentId(), QStringLiteral("31"));
        QCOMPARE(changed.count(), 1);
    }

    void sameOutputPreviewFailsActiveRootIdentityGuard() {
        QQuickWindow window;
        QQuickItem desktopWallpaper;
        QQuickItem preview;
        attachToWindow(desktopWallpaper, window);
        attachToWindow(preview, window);

        TestRegistration registration;
        registration.setTargetItem(&preview);
        registration.setContainmentContext(containmentContext(
            &preview, &desktopWallpaper, QRectF(window.screen()->geometry())));

        QVERIFY(!registration.isRegistered());
        QVERIFY(registration.instanceId().isEmpty());
        QVERIFY(registration.outputName().isEmpty());
    }

    void activeWallpaperPointerChangeInvalidatesAndRestores() {
        QQuickWindow window;
        QQuickItem root;
        QQuickItem preview;
        attachToWindow(root, window);

        TestRegistration registration;
        registration.setTargetItem(&root);
        auto context = containmentContext(&root, &preview, QRectF(window.screen()->geometry()));
        registration.setContainmentContext(context);
        QVERIFY(!registration.isRegistered());

        context.insert(QStringLiteral("activeWallpaperObject"), QVariant::fromValue(&root));
        registration.setContainmentContext(context);
        QVERIFY(registration.isRegistered());
        const QString activeId = registration.instanceId();

        context.insert(QStringLiteral("activeWallpaperObject"), QVariant::fromValue(&preview));
        registration.setContainmentContext(context);
        QVERIFY(!registration.isRegistered());
        QVERIFY(registration.instanceId().isEmpty());

        context.insert(QStringLiteral("activeWallpaperObject"), QVariant::fromValue(&root));
        registration.setContainmentContext(context);
        QVERIFY(registration.isRegistered());
        QVERIFY(registration.instanceId() != activeId);
    }

    void nullOrMissingContainmentContextFailsClosed() {
        QQuickWindow window;
        QQuickItem root;
        attachToWindow(root, window);

        TestRegistration registration;
        QSignalSpy rejected(&registration, &WallpaperRuntimeRegistration::registrationRejected);
        registration.setTargetItem(&root);
        registration.setContainmentContext({});

        QVERIFY(!registration.isRegistered());
        QVERIFY(registration.instanceId().isEmpty());
        QCOMPARE(rejected.last().at(0).toString(), QStringLiteral("notActiveContainmentWallpaper"));
    }

    void invalidActivityScreenTypeAndRegionFailClosed() {
        QQuickWindow window;
        QQuickItem root;
        attachToWindow(root, window);

        TestRegistration registration;
        registration.setTargetItem(&root);
        auto context = containmentContext(&root, &root, QRectF(window.screen()->geometry()));
        registration.setContainmentContext(context);
        QVERIFY(registration.isRegistered());

        context.insert(QStringLiteral("activityId"), QString());
        registration.setContainmentContext(context);
        QVERIFY(!registration.isRegistered());

        context = containmentContext(&root, &root, QRectF(window.screen()->geometry()),
                                    QString::fromLatin1(ActivityId), 0xffffffffU);
        registration.setContainmentContext(context);
        QVERIFY(!registration.isRegistered());

        context = containmentContext(&root, &root, QRectF(window.screen()->geometry()),
                                    QString::fromLatin1(ActivityId), 0, 1);
        registration.setContainmentContext(context);
        QVERIFY(!registration.isRegistered());

        context = containmentContext(&root, &root, QRectF(window.screen()->geometry()),
                                    QString::fromLatin1(ActivityId), 0, Desktop, {});
        context.insert(QStringLiteral("availableScreenRegion"), QVariantList{});
        registration.setContainmentContext(context);
        QVERIFY(!registration.isRegistered());
    }

    void outputAmbiguityAndGeometryMismatchInvalidate() {
        QQuickWindow window;
        QQuickItem root;
        attachToWindow(root, window);

        TestRegistration registration;
        registration.setTargetItem(&root);
        registration.setContainmentContext(
            containmentContext(&root, &root, QRectF(window.screen()->geometry())));
        QVERIFY(registration.isRegistered());

        registration.syntheticOutputs = { registration.syntheticOutput, registration.syntheticOutput };
        registration.refresh();
        QVERIFY(!registration.isRegistered());

        registration.syntheticOutputs = { registration.syntheticOutput };
        registration.refresh();
        QVERIFY(registration.isRegistered());

        registration.syntheticGeometry = QRectF(1, 1, 3, 3);
        registration.refresh();
        QVERIFY(!registration.isRegistered());

        registration.syntheticGeometry = {};
        registration.refresh();
        QVERIFY(registration.isRegistered());

        auto context = containmentContext(&root, &root, QRectF(window.screen()->geometry()));
        context.insert(QStringLiteral("screenGeometry"), QVariant::fromValue(QRectF(1, 1, 3, 3)));
        registration.setContainmentContext(context);
        QVERIFY(!registration.isRegistered());
    }

    void targetTeardownUnregisters() {
        QQuickWindow window;
        auto* root = new QQuickItem;
        attachToWindow(*root, window);

        TestRegistration registration;
        registration.setTargetItem(root);
        registration.setContainmentContext(
            containmentContext(root, root, QRectF(window.screen()->geometry())));
        QVERIFY(registration.isRegistered());

        delete root;
        QVERIFY(!registration.isRegistered());
        QVERIFY(registration.instanceId().isEmpty());
        QVERIFY(registration.containmentId().isEmpty());
    }

    void duplicateOutputNamesAreAmbiguous() {
        QCOMPARE(WallpaperRuntimeRegistration::uniqueOutputName(
                     QStringLiteral("HDMI-A-1"), { QStringLiteral("HDMI-A-1"), QStringLiteral("DP-1") }),
                 QStringLiteral("HDMI-A-1"));
        QVERIFY(WallpaperRuntimeRegistration::uniqueOutputName(
                    QStringLiteral("HDMI-A-1"),
                    { QStringLiteral("HDMI-A-1"), QStringLiteral("HDMI-A-1") }).isEmpty());
        QVERIFY(WallpaperRuntimeRegistration::uniqueOutputName({}, { QStringLiteral("DP-1") }).isEmpty());
    }
};

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    WallpaperRuntimeRegistrationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_wallpaperruntimeregistration.moc"
