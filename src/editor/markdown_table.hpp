#pragma once

#include <QString>
#include <QStringList>

#include <vector>

namespace nekogrid::editor {

/// How a GFM table column aligns its cells (the `:---`, `:---:`, `---:` delimiter row).
enum class TableAlignment : int {
    Default = 0, ///< `---`: no alignment given, the renderer's default (left)
    Left = 1,    ///< `:---`
    Center = 2,  ///< `:---:`
    Right = 3,   ///< `---:`
};

/// What the table wizard collects: a title, an alignment and a width per column, and how
/// many empty body rows to pre-create.
struct MarkdownTableSpec {
    QStringList headers;
    /// One per header; missing entries count as Default.
    std::vector<TableAlignment> alignments;
    /// One per header, in characters of the Markdown source (the cell text between the
    /// pipes, without the space on either side); 0 or a missing entry is automatic.
    /// GFM itself has no column widths: this shapes the source -- and the dash count,
    /// which some renderers (Pandoc) read as a relative width.
    std::vector<int> widths;
    int rows = 0;
};

/// A GFM pipe table for `spec`, ending in a newline; empty when there are no columns.
/// Columns are padded to a common width so the source reads as a table too: an automatic
/// column is as wide as its title, at least 5; a given width is used as is, except that
/// a column is never narrower than its title or the 3 dashes + colons its delimiter
/// needs (the title is not cut). Header titles are escaped (`|` becomes `\|`, a line
/// break a space); an empty title stays empty. A negative row count is treated as 0.
///
/// Pure text work, safe on any thread.
[[nodiscard]] QString buildMarkdownTable(const MarkdownTableSpec& spec);

} // namespace nekogrid::editor
