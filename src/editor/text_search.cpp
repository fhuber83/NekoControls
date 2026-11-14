#include "editor/text_search.hpp"

namespace nekogrid::editor {

QRegularExpression searchPattern(const QString& needle, const TextSearchOptions& options) {
    QString pattern = options.regex ? needle : QRegularExpression::escape(needle);
    if (options.wholeWord) {
        pattern = QStringLiteral("(?<!\\w)(?:%1)(?!\\w)").arg(pattern);
    }
    QRegularExpression::PatternOptions flags = QRegularExpression::UseUnicodePropertiesOption;
    if (!options.caseSensitive) {
        flags |= QRegularExpression::CaseInsensitiveOption;
    }
    if (options.regex) {
        flags |= QRegularExpression::MultilineOption; // ^ and $ per line, as in an editor
    }
    return QRegularExpression(pattern, flags);
}

TextSearchResult
findAll(const QString& text, const QString& needle, const TextSearchOptions& options, std::size_t limit) {
    TextSearchResult result;
    if (needle.isEmpty()) {
        return result;
    }
    const QRegularExpression re = searchPattern(needle, options);
    if (!re.isValid()) {
        result.valid = false;
        result.error = re.errorString();
        return result;
    }
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (m.capturedLength() == 0) {
            continue; // the iterator itself steps past empty matches
        }
        if (result.matches.size() >= limit) {
            result.truncated = true;
            break;
        }
        result.matches.push_back({.start = m.capturedStart(), .length = m.capturedLength()});
    }
    return result;
}

QString replacementFor(
    const QString& text,
    const TextMatch& match,
    const QString& needle,
    const QString& replacement,
    const TextSearchOptions& options) {
    if (!options.regex) {
        return replacement;
    }
    // Re-run the pattern anchored at the match, over the whole text, so lookbehinds
    // and ^ see the same context as findAll did.
    const QRegularExpression re = searchPattern(needle, options);
    const QRegularExpressionMatch m =
        re.match(text, match.start, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
    if (!m.hasMatch()) {
        return replacement;
    }
    QString out;
    out.reserve(replacement.size());
    for (qsizetype i = 0; i < replacement.size(); ++i) {
        const QChar c = replacement[i];
        const QChar next = i + 1 < replacement.size() ? replacement[i + 1] : QChar();
        if ((c == u'$' || c == u'\\') && next.isDigit()) {
            out += m.captured(next.digitValue());
            ++i;
        } else if (c == u'$' && next == u'$') {
            out += u'$';
            ++i;
        } else if (c == u'\\' && next == u'\\') {
            out += u'\\';
            ++i;
        } else if (c == u'\\' && next == u'n') {
            out += u'\n';
            ++i;
        } else if (c == u'\\' && next == u't') {
            out += u'\t';
            ++i;
        } else {
            out += c;
        }
    }
    return out;
}

} // namespace nekogrid::editor
