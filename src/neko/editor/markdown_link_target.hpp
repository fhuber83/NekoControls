#pragma once

#include <QString>

#include <functional>
#include <optional>
#include <vector>

namespace neko::editor {

/// Completing the destination of a Markdown link: `[text](` and Ctrl+Space in a
/// MarkdownEditor lists the files an application offers, and `#` after one lists its
/// headings. What can be linked is the application's to say (a
/// MarkdownLinkTargetProvider): the editor only finds where a destination is being
/// typed, narrows the list and puts the choice in.
///
/// Pure text work, safe on any thread.

/// Whether the line starting at `lineStart` lies inside a fenced code block: CommonMark
/// fences (up to three spaces of indent, three or more ` or ~), closed by a fence of the
/// same character at least as long with nothing after it.
[[nodiscard]] bool lineInsideFencedCode(const QString& text, qsizetype lineStart);

/// A link destination being typed.
struct MarkdownLinkTargetQuery {
    /// Where a chosen target goes: right after `](`, or right after the `#` when
    /// `fragment` is set.
    qsizetype start = 0;
    /// What the choice replaces up to: the end of the destination's path (before a
    /// `#`) or of its fragment -- the next space, `)` or end of line, so a choice made
    /// inside a finished destination replaces that part of it.
    qsizetype end = 0;
    /// The destination's path as written (percent-encoding and all): up to the cursor
    /// while it is being typed, the whole path before the `#` when `fragment` is set.
    /// Empty with `fragment` for a link into the document itself (`(#`).
    QString path;
    /// A `#` stands between the path and the cursor: the headings of `path` are wanted.
    bool fragment = false;
    /// What narrows the list: `path` up to the cursor, or the fragment typed so far.
    QString prefix;
};

/// The destination being typed at `position` in the Markdown `text`, if any: the
/// cursor follows `[...](` (not `![...](`, an image) on the same line, with nothing
/// but destination characters between -- no space, parenthesis, `<`, `>` or quote.
/// Nothing counts inside a fenced code block or an inline code span (an odd number of
/// backticks before the `](` on its line).
[[nodiscard]] std::optional<MarkdownLinkTargetQuery> markdownLinkTargetQueryAt(const QString& text, qsizetype position);

/// One thing a link can point to.
struct MarkdownLinkTarget {
    /// What goes into the destination: a path (`docs/orders%20spec.md`, written as a
    /// link needs it) or, for a heading, its anchor without the `#` (`totals`).
    QString target;
    /// What the list shows ("docs/orders spec.md", "## Totals"); empty shows `target`.
    QString label;
};

/// Hands out what can be linked from the document at a query: files when
/// `query.fragment` is false, the headings of `query.path` when it is true. The editor
/// narrows the list itself (matchMarkdownLinkTargets) and asks again only when the
/// query's kind or path changes. Called on the UI thread, while the user types: keep it
/// quick.
using MarkdownLinkTargetProvider = std::function<std::vector<MarkdownLinkTarget>(const MarkdownLinkTargetQuery& query)>;

/// The targets for `prefix` (case-insensitive), best first: all of them, in order, for
/// an empty prefix; otherwise those whose target or label starts with it, then those
/// with a path segment or word starting with it, then those containing it -- each
/// group in the given order. Empty when nothing matches.
[[nodiscard]] std::vector<MarkdownLinkTarget>
matchMarkdownLinkTargets(const std::vector<MarkdownLinkTarget>& targets, const QString& prefix);

} // namespace neko::editor
