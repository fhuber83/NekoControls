#pragma once

#include <QString>

#include <vector>

namespace nekogrid::markdown_editor {

/// What a Markdown document is made of, as far as an editor needs to know without
/// rendering it: where a front matter block ends and where the headings are.
///
/// Pure; any thread.

/// The number of lines a document's front matter takes, both `---` lines included; 0
/// for a document without one. A front matter is a first line of `---` and a later
/// line of `---` or `...` (trailing blanks allowed on both); without the closing line
/// the first one is a rule, not a front matter. A byte order mark before the first
/// line is ignored.
///
/// The body starts on this line (0-based), so it is what
/// markdown::MarkdownPreviewContent::bodyStartLine wants.
[[nodiscard]] int frontMatterLineCount(const QString& text);

/// A heading of a Markdown document.
struct MarkdownHeading {
    int level = 1;        ///< 1..6
    QString text;         ///< without the ATX / Setext markers
    int line = 0;         ///< 0-based line in the whole document (front matter included)
    qsizetype offset = 0; ///< where that line starts in the whole document
};

/// The ATX (`#`..`######`) and Setext (`===` / `---` underline) headings of `text` from
/// line `bodyStartLine` on, in document order. Fenced and indented code blocks are
/// skipped, and so is a heading without text (`##`).
[[nodiscard]] std::vector<MarkdownHeading> extractMarkdownHeadings(const QString& text, int bodyStartLine);

/// The same below the document's own front matter (frontMatterLineCount()).
[[nodiscard]] std::vector<MarkdownHeading> extractMarkdownHeadings(const QString& text);

} // namespace nekogrid::markdown_editor
