#pragma once

#include <QList>
#include <QString>
#include <QPair>

// Small, bounded reader for Steam's KeyValues text files (.vdf/.acf).
// This is an internal parser shared by Steam metadata readers; it has no
// dependency on Steam, filesystem layout, or plugin/UI code.
namespace steam::kv
{

struct Node {
    bool isObject { false };
    QString value;
    QList<QPair<QString, Node>> children;

    // Keys compare case-insensitively; duplicate keys are last-wins.
    const Node* child(const QString& key) const;
};

struct Limits {
    qsizetype maxBytes { 8 * 1024 * 1024 };
    int maxDepth { 32 };
    qsizetype maxRecords { 100000 };
};

// Parse an implicit root object. Returns false for malformed or incomplete
// input, including unterminated quotes, unmatched braces, and exceeded bounds.
bool parse(const QString& text, Node& out, const Limits& limits = {});

// Read and parse one UTF-8 KeyValues file, optionally stripping a UTF-8 BOM.
bool readFile(const QString& path, Node& out, const Limits& limits = {});

} // namespace steam::kv
