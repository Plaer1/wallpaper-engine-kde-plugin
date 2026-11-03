#include <QColor>
#include <QFileInfo>
#include <QFile>
#include <QQmlComponent>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QTest>
#include <QUrl>

class PanelAppletQmlTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void usesSupportedKirigamiThemeAttachedProperties() {
        QFile mainQml(QString::fromUtf8(PANEL_APPLET_QML));
        QVERIFY(QFileInfo::exists(mainQml.fileName()));
        QVERIFY(mainQml.open(QIODevice::ReadOnly | QIODevice::Text));
        const QByteArray appletSource = mainQml.readAll();
        QVERIFY(appletSource.contains("import org.kde.kirigami as Kirigami"));
        QVERIFY(appletSource.contains("Kirigami.Theme.textColor"));
        QVERIFY(!appletSource.contains("PlasmaCore.Theme."));

        QQmlApplicationEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"QML(
            import QtQuick
            import org.kde.kirigami as Kirigami
            Item {
                Kirigami.Theme.colorSet: Kirigami.Theme.Window
                Kirigami.Theme.inherit: false
                readonly property color textColor: Kirigami.Theme.textColor
                readonly property color disabledTextColor: Kirigami.Theme.disabledTextColor
                readonly property color highlightColor: Kirigami.Theme.highlightColor
            }
        )QML", QUrl(QStringLiteral("inmemory:/PanelThemeProbe.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QObject *root = component.create();
        QVERIFY2(root, qPrintable(component.errorString()));

        for (const char *property : {"textColor", "disabledTextColor", "highlightColor"}) {
            const QVariant value = root->property(property);
            QCOMPARE(value.metaType(), QMetaType::fromType<QColor>());
        }
        delete root;
    }
};

QTEST_MAIN(PanelAppletQmlTest)
#include "tst_panelappletqml.moc"
