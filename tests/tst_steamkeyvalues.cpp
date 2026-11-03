// SPDX-License-Identifier: GPL-2.0-only
// Focused strictness and compatibility checks for Steam KeyValues parsing.
#include <QtTest>

#include "SteamKeyValues.hpp"

class TestSteamKeyValues : public QObject {
    Q_OBJECT

private slots:
    void readsNestedObjectsAndCaseInsensitiveLastWins() {
        steam::kv::Node root;
        QVERIFY(steam::kv::parse(QStringLiteral("\"Root\" { \"key\" \"old\" \"KEY\" \"new\" \"child\" { \"x\" \"1\" } }"), root));
        const auto* object = root.child(QStringLiteral("root"));
        QVERIFY(object);
        QVERIFY(object->isObject);
        QCOMPARE(object->child(QStringLiteral("Key"))->value, QStringLiteral("new"));
        QCOMPARE(object->child(QStringLiteral("child"))->child(QStringLiteral("x"))->value,
                 QStringLiteral("1"));
    }

    void acceptsCommentsEscapesAndBom() {
        steam::kv::Node root;
        QVERIFY(steam::kv::parse(QString::fromUtf8("\uFEFF// comment\n\"k\" \"say \\\"hi\\\"\""), root));
        QCOMPARE(root.child(QStringLiteral("k"))->value, QStringLiteral("say \"hi\""));
    }

    void rejectsIncompleteOrUnbalancedStructures() {
        for (const QString& input : {
                 QStringLiteral("\"outer\" { \"inner\" { \"x\" \"1\" }"),
                 QStringLiteral("\"outer\" { \"key\" \"value\""),
                 QStringLiteral("\"key\" \"unterminated"),
                 QStringLiteral("\"good\" \"value\" \"unfinished"),
                 QStringLiteral("\"outer\" { \"good\" \"value\" \"unfinished"),
                 QStringLiteral("\"key\" \"value\" }"),
                 QStringLiteral("{ \"key\" \"value\" }"),
             }) {
            steam::kv::Node root;
            QVERIFY2(!steam::kv::parse(input, root), qPrintable(input));
            QVERIFY(!root.isObject);
            QVERIFY(root.children.isEmpty());
        }
    }

    void enforcesDepthAndRecordBounds() {
        steam::kv::Limits limits;
        limits.maxDepth = 1;
        steam::kv::Node root;
        QVERIFY(!steam::kv::parse(QStringLiteral("\"a\" { \"b\" { \"x\" \"1\" } }"), root, limits));

        limits = {};
        limits.maxRecords = 1;
        QVERIFY(!steam::kv::parse(QStringLiteral("\"a\" \"1\" \"b\" \"2\""), root, limits));

        limits = {};
        limits.maxBytes = 2;
        QVERIFY(!steam::kv::parse(QStringLiteral("\"x\" \"long\""), root, limits));
    }
};

QTEST_APPLESS_MAIN(TestSteamKeyValues)
#include "tst_steamkeyvalues.moc"
