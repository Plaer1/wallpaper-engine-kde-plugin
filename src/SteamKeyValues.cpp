#include "SteamKeyValues.hpp"

#include <QFile>

#include <utility>

namespace steam::kv
{
namespace
{

class Lexer {
public:
    explicit Lexer(const QString& text): m_text(text) {}

    bool next(QString& token, bool& quoted) {
        skipGap();
        if (m_pos >= m_text.size()) return false;

        const QChar c = m_text.at(m_pos);
        quoted = false;
        if (c == u'{' || c == u'}') {
            token = QString(c);
            ++m_pos;
            return true;
        }
        if (c == u'"') {
            ++m_pos;
            quoted = true;
            if (readQuoted(token)) return true;
            m_error = true;
            return false;
        }
        token = readBare();
        return !token.isEmpty();
    }

    bool hasError() const { return m_error; }

private:
    void skipGap() {
        while (m_pos < m_text.size()) {
            const QChar c = m_text.at(m_pos);
            if (c.isSpace()) {
                ++m_pos;
            } else if (c == u'/' && m_pos + 1 < m_text.size() && m_text.at(m_pos + 1) == u'/') {
                while (m_pos < m_text.size() && m_text.at(m_pos) != u'\n') ++m_pos;
            } else {
                return;
            }
        }
    }

    bool readQuoted(QString& out) {
        out.clear();
        while (m_pos < m_text.size()) {
            const QChar c = m_text.at(m_pos++);
            if (c == u'"') return true;
            if (c != u'\\') {
                out.append(c);
                continue;
            }
            if (m_pos >= m_text.size()) return false;
            const QChar esc = m_text.at(m_pos++);
            switch (esc.unicode()) {
            case 'n': out.append(u'\n'); break;
            case 't': out.append(u'\t'); break;
            default: out.append(esc); break;
            }
        }
        return false;
    }

    QString readBare() {
        const int start = m_pos;
        while (m_pos < m_text.size()) {
            const QChar c = m_text.at(m_pos);
            if (c.isSpace() || c == u'{' || c == u'}' || c == u'"') break;
            ++m_pos;
        }
        return m_text.mid(start, m_pos - start);
    }

    const QString& m_text;
    qsizetype m_pos { 0 };
    bool m_error { false };
};

bool parseBody(Lexer& lexer, Node& out, int depth, qsizetype& records, const Limits& limits,
               bool nested) {
    if (depth > limits.maxDepth) return false;
    out.isObject = true;
    QString token;
    bool quoted = false;
    while (lexer.next(token, quoted)) {
        if (!quoted && token == QStringLiteral("}")) return nested;
        if (!quoted && token == QStringLiteral("{")) return false;

        const QString key = token;
        if (!lexer.next(token, quoted)) return false;
        if (!quoted && token == QStringLiteral("{")) {
            Node child;
            if (!parseBody(lexer, child, depth + 1, records, limits, true)) return false;
            if (++records > limits.maxRecords) return false;
            out.children.append({ key, child });
            continue;
        }
        if (!quoted && token == QStringLiteral("}")) return false;

        // Steam platform conditions follow a value as a bare token. Preserve
        // the legacy library reader's behavior of dropping that key/value.
        Node leaf;
        leaf.value = token;
        if (++records > limits.maxRecords) return false;
        out.children.append({ key, leaf });
        if (!quoted && token.startsWith(u'[')) out.children.removeLast();
    }
    return !nested && !lexer.hasError();
}

} // namespace

const Node* Node::child(const QString& key) const {
    for (auto it = children.crbegin(); it != children.crend(); ++it) {
        if (it->first.compare(key, Qt::CaseInsensitive) == 0) return &it->second;
    }
    return nullptr;
}

bool parse(const QString& text, Node& out, const Limits& limits) {
    out = {};
    if (limits.maxBytes < 0 || limits.maxDepth < 0 || limits.maxRecords < 0 ||
        text.toUtf8().size() > limits.maxBytes) return false;
    QString normalized = text;
    if (!normalized.isEmpty() && normalized.at(0) == QChar(0xFEFF)) normalized.remove(0, 1);
    Lexer lexer(normalized);
    qsizetype records = 0;
    Node parsed;
    if (!parseBody(lexer, parsed, 0, records, limits, false)) return false;
    out = std::move(parsed);
    return true;
}

bool readFile(const QString& path, Node& out, const Limits& limits) {
    out = {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text) || file.size() > limits.maxBytes) return false;
    QString text = QString::fromUtf8(file.readAll());
    if (!text.isEmpty() && text.at(0) == QChar(0xFEFF)) text.remove(0, 1);
    return parse(text, out, limits);
}

} // namespace steam::kv
