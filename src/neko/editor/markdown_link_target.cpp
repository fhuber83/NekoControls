#include "neko/editor/markdown_link_target.hpp"

#include <QStringView>

namespace neko::editor {

namespace {

/// A character that cannot be part of a link destination written without `<...>`.
bool endsDestination(QChar c) noexcept {
    return c.isSpace() || c == QLatin1Char('(') || c == QLatin1Char(')') || c == QLatin1Char('<') ||
           c == QLatin1Char('>') || c == QLatin1Char('"') || c == QLatin1Char('\'');
}

/// Whether `text` has a word -- after a `/`, space, `-`, `_`, `.` or `#` -- that starts
/// with `prefix`.
bool wordStartsWith(const QString& text, const QString& prefix) {
    for (qsizetype i = 1; i < text.size(); ++i) {
        const QChar before = text[i - 1];
        if ((before == QLatin1Char('/') || before == QLatin1Char(' ') || before == QLatin1Char('-') ||
             before == QLatin1Char('_') || before == QLatin1Char('.') || before == QLatin1Char('#')) &&
            QStringView(text).mid(i).startsWith(prefix, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

} // namespace

bool lineInsideFencedCode(const QString& text, qsizetype lineStart) {
    QChar fenceChar;
    qsizetype fenceLength = 0;
    qsizetype pos = 0;
    while (pos < lineStart) {
        qsizetype eol = text.indexOf(QLatin1Char('\n'), pos);
        if (eol < 0 || eol > lineStart) {
            eol = lineStart;
        }
        const QStringView line = QStringView(text).mid(pos, eol - pos);
        qsizetype i = 0;
        while (i < line.size() && i < 3 && line[i] == QLatin1Char(' ')) {
            ++i;
        }
        if (i < line.size() && (line[i] == QLatin1Char('`') || line[i] == QLatin1Char('~'))) {
            const QChar ch = line[i];
            qsizetype run = 0;
            while (i + run < line.size() && line[i + run] == ch) {
                ++run;
            }
            const QStringView rest = line.mid(i + run);
            if (run >= 3) {
                if (fenceLength == 0) {
                    // A backtick fence's info string cannot hold a backtick.
                    if (ch != QLatin1Char('`') || !rest.contains(QLatin1Char('`'))) {
                        fenceChar = ch;
                        fenceLength = run;
                    }
                } else if (ch == fenceChar && run >= fenceLength && rest.trimmed().isEmpty()) {
                    fenceLength = 0;
                }
            }
        }
        pos = eol + 1;
    }
    return fenceLength > 0;
}

std::optional<MarkdownLinkTargetQuery> markdownLinkTargetQueryAt(const QString& text, qsizetype position) {
    if (position < 2 || position > text.size()) {
        return std::nullopt;
    }
    // Back from the cursor over destination characters to the `](`.
    qsizetype open = position;
    while (open > 0 && text[open - 1] != QLatin1Char('\n') && !endsDestination(text[open - 1])) {
        --open;
    }
    if (open < 2 || text[open - 1] != QLatin1Char('(') || text[open - 2] != QLatin1Char(']')) {
        return std::nullopt;
    }
    const qsizetype lineStart = text.lastIndexOf(QLatin1Char('\n'), open - 1) + 1;
    const qsizetype bracket = text.lastIndexOf(QLatin1Char('['), open - 2);
    if (bracket < lineStart) {
        return std::nullopt; // no link text on this line
    }
    if (bracket > lineStart && text[bracket - 1] == QLatin1Char('!')) {
        return std::nullopt; // an image
    }
    if (QStringView(text).mid(lineStart, open - lineStart).count(QLatin1Char('`')) % 2 != 0) {
        return std::nullopt; // inside `code`
    }
    if (lineInsideFencedCode(text, lineStart)) {
        return std::nullopt;
    }

    qsizetype destinationEnd = position;
    while (destinationEnd < text.size() && !endsDestination(text[destinationEnd])) {
        ++destinationEnd;
    }
    const qsizetype hash = text.indexOf(QLatin1Char('#'), open);
    if (hash >= 0 && hash < position) {
        return MarkdownLinkTargetQuery{
            .start = hash + 1,
            .end = destinationEnd,
            .path = text.mid(open, hash - open),
            .fragment = true,
            .prefix = text.mid(hash + 1, position - hash - 1),
        };
    }
    const qsizetype pathEnd = hash >= 0 && hash < destinationEnd ? hash : destinationEnd;
    return MarkdownLinkTargetQuery{
        .start = open,
        .end = pathEnd,
        .path = text.mid(open, position - open),
        .fragment = false,
        .prefix = text.mid(open, position - open),
    };
}

std::vector<MarkdownLinkTarget>
matchMarkdownLinkTargets(const std::vector<MarkdownLinkTarget>& targets, const QString& prefix) {
    if (prefix.isEmpty()) {
        return targets;
    }
    std::vector<MarkdownLinkTarget> starts;
    std::vector<MarkdownLinkTarget> words;
    std::vector<MarkdownLinkTarget> contains;
    for (const MarkdownLinkTarget& t : targets) {
        const QString& label = t.label.isEmpty() ? t.target : t.label;
        if (t.target.startsWith(prefix, Qt::CaseInsensitive) || label.startsWith(prefix, Qt::CaseInsensitive)) {
            starts.push_back(t);
        } else if (wordStartsWith(t.target, prefix) || wordStartsWith(label, prefix)) {
            words.push_back(t);
        } else if (t.target.contains(prefix, Qt::CaseInsensitive) || label.contains(prefix, Qt::CaseInsensitive)) {
            contains.push_back(t);
        }
    }
    starts.insert(starts.end(), words.begin(), words.end());
    starts.insert(starts.end(), contains.begin(), contains.end());
    return starts;
}

} // namespace neko::editor
