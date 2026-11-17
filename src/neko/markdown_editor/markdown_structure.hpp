#pragma once

#include <QString>

#include <vector>

namespace neko::markdown_editor {

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
    /// The heading's id in the preview, what a link to it names after the `#`
    /// (`[x](other.md#totals)`): markdownHeadingSlug() of its text, `-1`, `-2` ...
    /// appended to the second, third ... heading with the same slug.
    QString anchor;
};

/// The slug the Markdown preview gives a heading with this text (as
/// extractMarkdownHeadings() reports it): the text as rendered -- link and image
/// targets, emphasis markers and emoji shortcodes dropped -- in lower case, with
/// everything but letters, digits, spaces, `_` and `-` removed and runs of spaces as
/// one `-`; "section" when nothing is left. It follows the preview's own
/// slugifyHeading() (neko::markdown's preview.js); change both together.
[[nodiscard]] QString markdownHeadingSlug(const QString& headingText);

/// The ATX (`#`..`######`) and Setext (`===` / `---` underline) headings of `text` from
/// line `bodyStartLine` on, in document order. Fenced and indented code blocks are
/// skipped, and so is a heading without text (`##`). Each has its `anchor`.
[[nodiscard]] std::vector<MarkdownHeading> extractMarkdownHeadings(const QString& text, int bodyStartLine);

/// The same below the document's own front matter (frontMatterLineCount()).
[[nodiscard]] std::vector<MarkdownHeading> extractMarkdownHeadings(const QString& text);

} // namespace neko::markdown_editor
