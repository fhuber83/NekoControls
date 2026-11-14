#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

#include <algorithm>
#include <vector>

/// Column (box) selection for the text editors (CodeEditor): the arithmetic,
/// without widgets. Lines are plain text without their line break; columns are
/// display columns, where a tab advances to the next multiple of `tabWidth`.
///
/// A box covers display columns [left, right) on a range of lines. A line shorter
/// than `left` contributes nothing to a copy; typing into it pads it with spaces up to
/// `left` first, so what is typed lines up (as in Visual Studio and Notepad++). A box
/// with left == right is a column of carets: typing inserts on every line.
///
/// Pure; any thread.
namespace nekogrid::editor::block {

/// The display column where character `index` of `line` starts (index == size: the end).
[[nodiscard]] int columnOf(QStringView line, qsizetype index, int tabWidth);
/// The display width of the whole line.
[[nodiscard]] int lineWidth(QStringView line, int tabWidth);
/// The first character that starts at or after `column`; line.size() when none does.
/// A tab the column falls inside belongs to the box only if the box starts at the tab.
[[nodiscard]] qsizetype indexAtColumn(QStringView line, int column, int tabWidth);

/// Where a box selection started and where it is now, as (line, display column).
/// Either corner may be the top-left one.
struct BlockSelection {
    int anchorLine = 0;
    int anchorColumn = 0;
    int cursorLine = 0;
    int cursorColumn = 0;

    [[nodiscard]] int top() const noexcept { return std::min(anchorLine, cursorLine); }
    [[nodiscard]] int bottom() const noexcept { return std::max(anchorLine, cursorLine); }
    [[nodiscard]] int left() const noexcept { return std::min(anchorColumn, cursorColumn); }
    [[nodiscard]] int right() const noexcept { return std::max(anchorColumn, cursorColumn); }
    [[nodiscard]] int lineCount() const noexcept { return bottom() - top() + 1; }
    /// A column of carets, nothing selected.
    [[nodiscard]] bool isZeroWidth() const noexcept { return anchorColumn == cursorColumn; }
    friend bool operator==(const BlockSelection&, const BlockSelection&) = default;
};

/// Replace characters [start, end) of row `row` with `text`. Rows are relative to the
/// first line handed in.
struct RowEdit {
    int row = 0;
    qsizetype start = 0;
    qsizetype end = 0;
    QString text;
    friend bool operator==(const RowEdit&, const RowEdit&) = default;
};

/// The characters of `row` inside columns [left, right).
[[nodiscard]] QString rowText(QStringView row, int left, int right, int tabWidth);
/// The box's text: one row per line, joined with '\n'. Short lines give "" (no padding).
[[nodiscard]] QString copyText(const QStringList& rows, int left, int right, int tabWidth);

/// Replaces the box [left, right) on every row with text: `insert` with one entry puts
/// it on every row; with several, row i gets insert[i] (rows past the end of `insert`
/// just lose their part of the box). Short rows are padded with spaces to `left`
/// first -- unless the row gets nothing.
[[nodiscard]] std::vector<RowEdit>
replaceEdits(const QStringList& rows, int left, int right, const QStringList& insert, int tabWidth);

enum class DeleteKey {
    Backspace,
    Delete,
};
/// Delete or Backspace in a box. With a width, it removes the box; with none (a column
/// of carets), Backspace removes the character before the column on every row that
/// reaches it, and Delete the one at it.
[[nodiscard]] std::vector<RowEdit>
deleteEdits(const QStringList& rows, int left, int right, DeleteKey key, int tabWidth);
/// Where the carets end up after deleteEdits: `left`, or for a Backspace on a column of
/// carets, the column of the character it removed (on the first row that had one) --
/// one to the left when every caret sits past the end of its line.
[[nodiscard]] int columnAfterDelete(const QStringList& rows, int left, int right, DeleteKey key, int tabWidth);

} // namespace nekogrid::editor::block
