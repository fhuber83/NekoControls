#include "neko/markdown_editor/markdown_structure.hpp"

#include <QRegularExpression>

#include <algorithm>

namespace neko::markdown_editor {

namespace {

/// The line without blanks at its end (and without a `\r` before the line break).
QStringView withoutTrailingBlanks(QStringView line) {
    while (!line.isEmpty() &&
           (line.back() == QLatin1Char(' ') || line.back() == QLatin1Char('\t') || line.back() == QLatin1Char('\r'))) {
        line.chop(1);
    }
    return line;
}

/// Columns of white space a line starts with; a tab goes to the next multiple of four.
int indentWidth(QStringView line) {
    int width = 0;
    for (const QChar c : line) {
        if (c == QLatin1Char(' ')) {
            ++width;
        } else if (c == QLatin1Char('\t')) {
            width += 4 - (width % 4);
        } else {
            break;
        }
    }
    return width;
}

} // namespace

int frontMatterLineCount(const QString& text) {
    QStringView rest(text);
    if (rest.startsWith(QChar(0xFEFF))) {
        rest = rest.sliced(1);
    }
    for (int line = 0;; ++line) {
        const qsizetype newline = rest.indexOf(QLatin1Char('\n'));
        const QStringView content = withoutTrailingBlanks(newline < 0 ? rest : rest.first(newline));
        if (line == 0) {
            if (content != QLatin1String("---")) {
                return 0;
            }
        } else if (content == QLatin1String("---") || content == QLatin1String("...")) {
            return line + 1;
        }
        if (newline < 0) {
            return 0; // no closing line: a rule, not a front matter
        }
        rest = rest.sliced(newline + 1);
    }
}

std::vector<MarkdownHeading> extractMarkdownHeadings(const QString& text) {
    return extractMarkdownHeadings(text, frontMatterLineCount(text));
}

std::vector<MarkdownHeading> extractMarkdownHeadings(const QString& text, int bodyStartLine) {
    std::vector<MarkdownHeading> headings;

    static const QRegularExpression fenceOpen(QStringLiteral(R"(^ {0,3}(`{3,}|~{3,})(.*)$)"));
    static const QRegularExpression atxHeading(QStringLiteral(R"(^ {0,3}(#{1,6})(?:[ \t]+(.*)|$))"));
    static const QRegularExpression trailingHashes(QStringLiteral(R"([ \t]+#+[ \t]*$)"));
    static const QRegularExpression setextUnderline(QStringLiteral(R"(^ {0,3}(=+|-+)[ \t]*$)"));
    static const QRegularExpression nonSetextParagraph(
        QStringLiteral(R"(^ {0,3}(?:>|#{1,6}(?:[ \t]|$)|[-*+][ \t]|\d{1,9}[.)][ \t]))"));

    QChar fenceChar;
    qsizetype fenceLen = 0;
    bool inFence = false;

    // The paragraph line a Setext underline would turn into a heading.
    bool hasCandidate = false;
    QString candidateText;
    int candidateLine = 0;
    qsizetype candidateOffset = 0;

    int lineNum = 0;
    qsizetype pos = 0;
    const qsizetype len = text.size();
    while (pos <= len) {
        qsizetype eol = text.indexOf(QLatin1Char('\n'), pos);
        if (eol < 0) {
            eol = len;
        }
        qsizetype lineEnd = eol;
        if (lineEnd > pos && text.at(lineEnd - 1) == QLatin1Char('\r')) {
            --lineEnd;
        }
        const QString line = text.mid(pos, lineEnd - pos);

        if (lineNum >= bodyStartLine) {
            if (inFence) {
                const QString trimmed = line.trimmed();
                if (indentWidth(line) <= 3 && trimmed.size() >= fenceLen &&
                    std::ranges::all_of(trimmed, [fenceChar](QChar c) { return c == fenceChar; })) {
                    inFence = false;
                    fenceLen = 0;
                }
                hasCandidate = false;
            } else if (const auto fm = fenceOpen.match(line); fm.hasMatch()) {
                const QString fence = fm.captured(1);
                const QString info = fm.captured(2);
                // CommonMark: the info string of a backtick fence cannot hold a backtick.
                if (fence.at(0) != QLatin1Char('`') || !info.contains(QLatin1Char('`'))) {
                    inFence = true;
                    fenceChar = fence.at(0);
                    fenceLen = fence.size();
                }
                hasCandidate = false;
            } else if (indentWidth(line) >= 4) {
                hasCandidate = false;
            } else if (const auto am = atxHeading.match(line); am.hasMatch()) {
                QString title = am.captured(2).trimmed();
                title.remove(trailingHashes);
                title = title.trimmed();
                // `##` or `## ###`: a heading without text has no place in an outline.
                if (!title.isEmpty() && !std::ranges::all_of(title, [](QChar c) { return c == QLatin1Char('#'); })) {
                    headings.push_back({
                        .level = static_cast<int>(am.captured(1).size()),
                        .text = title,
                        .line = lineNum,
                        .offset = pos,
                    });
                }
                hasCandidate = false;
            } else if (hasCandidate && setextUnderline.match(line).hasMatch()) {
                headings.push_back({
                    .level = line.trimmed().startsWith(QLatin1Char('=')) ? 1 : 2,
                    .text = candidateText,
                    .line = candidateLine,
                    .offset = candidateOffset,
                });
                hasCandidate = false;
            } else {
                const QString trimmed = line.trimmed();
                hasCandidate = !trimmed.isEmpty() && !nonSetextParagraph.match(line).hasMatch();
                if (hasCandidate) {
                    candidateText = trimmed;
                    candidateLine = lineNum;
                    candidateOffset = pos;
                }
            }
        }

        if (eol == len) {
            break;
        }
        pos = eol + 1;
        ++lineNum;
    }
    return headings;
}

} // namespace neko::markdown_editor
