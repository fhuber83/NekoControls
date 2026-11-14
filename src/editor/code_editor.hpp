#pragma once

#include "editor/block_selection.hpp"

#include <QList>
#include <QPlainTextEdit>
#include <QTextEdit>

#include <optional>

class QAction;
class QMenu;
class QMimeData;

namespace nekogrid::editor {

/// A text editor control for source text: Neko Grid's SQL tabs (ui::SqlEditor derives
/// from it) and the Markdown source of a note (MarkdownEditor). A QPlainTextEdit with:
///
///   - a line-number gutter and a current-line highlight, in the widget's palette;
///   - the font and line wrapping of EditorDefaults, followed live, tabs 4 wide;
///   - search highlights for EditorFindBar, layered with the current line;
///   - setTextUndoable(), which replaces everything as one undoable edit;
///   - **column (box) selection**, as in Visual Studio and Notepad++.
///
/// Column selection: Alt + drag with the mouse, or Alt+Shift+arrow keys, select a box
/// of display columns over several lines -- not running to the ends of the lines.
/// Copy, Cut, Delete and Backspace act on the box; typing replaces it on every line
/// (short lines are padded with spaces so the text lines up); a box without width is a
/// column of carets. Paste puts a one-line text on every line, and a block copied here
/// is pasted as a block (one row per line) even without a box selected. Each of these
/// is one undo step. A plain click, a navigation key, Esc, or any edit from elsewhere
/// (undo included) ends the box. The arithmetic is block.
///
/// On some Linux desktops Alt + drag moves the window instead; Alt+Shift+arrows work
/// everywhere.
///
/// It has no syntax highlighting of its own: attach a QSyntaxHighlighter to document().
/// Find / Replace is EditorFindBar.
///
/// Threading: UI thread only.
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    static constexpr int kTabWidth = 4;
    /// The MIME type that marks clipboard text as a block (one row per line).
    static constexpr const char* kBlockMimeType = "application/x-nekogrid-block";

    explicit CodeEditor(QWidget* parent = nullptr);

    /// Replaces the whole text as one edit: unlike setPlainText() or clear(), Ctrl+Z
    /// brings back what was there.
    void setTextUndoable(const QString& text);

    /// The Find bar's match highlights, drawn together with the current-line highlight
    /// (both are extra selections; setting them directly would drop the other).
    void setSearchHighlights(const QList<QTextEdit::ExtraSelection>& highlights);

    /// Whether long lines wrap at the editor's width; starts from
    /// EditorDefaults::lineWrap() and follows its lineWrapChanged, or can be toggled per
    /// editor (Alt+Z). lineWrapAction() is that toggle, for a tool bar or menu; its icon
    /// is the application's "line-wrap" (editor/editor_icons).
    [[nodiscard]] bool isLineWrapEnabled() const noexcept { return lineWrapMode() != QPlainTextEdit::NoWrap; }
    void setLineWrapEnabled(bool enabled);
    [[nodiscard]] QAction* lineWrapAction() const noexcept { return lineWrapAct_; }

    /// Whether typing overwrites the character after the caret instead of inserting
    /// ahead of it; toggled with the Insert key.
    [[nodiscard]] bool isOverwriteMode() const noexcept { return overwriteMode(); }
    void setOverwriteMode(bool overwrite);

    // ---- Column selection -------------------------------------------------------------

    [[nodiscard]] bool hasBlockSelection() const noexcept { return block_.has_value(); }
    [[nodiscard]] const std::optional<block::BlockSelection>& blockSelection() const noexcept { return block_; }
    /// Starts (or moves) a box; lines are clamped to the document.
    void setBlockSelection(const block::BlockSelection& selection);
    void clearBlockSelection();
    /// The box's text, one row per line; "" without a box.
    [[nodiscard]] QString blockSelectedText() const;
    /// Replaces the box with `text` on every line (or row by row for several lines).
    void insertIntoBlock(const QString& text);
    void deleteBlock(block::DeleteKey key);
    /// Copies the box as a block; Cut also removes it.
    void copyBlock() const;
    void cutBlock();

    void lineNumberAreaPaintEvent(QPaintEvent* event);
    [[nodiscard]] int lineNumberAreaWidth() const;

signals:
    void lineWrapChanged(bool enabled);
    void overwriteModeChanged(bool overwrite);

protected:
    /// Column-selection keys. True if the key was used; a key that ends the box returns
    /// false so the caller handles it normally. Subclasses with their own
    /// keyPressEvent call this first.
    bool handleBlockKey(QKeyEvent* event);
    /// The context menu: Cut / Copy / Paste / Delete for a box, else Qt's standard one.
    /// The caller owns it.
    [[nodiscard]] QMenu* createEditorContextMenu();

    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();

    /// (line, display column) under a viewport point; past a line's end the column
    /// keeps counting in space widths.
    [[nodiscard]] std::pair<int, int> lineColumnAt(const QPoint& viewportPos) const;
    /// Viewport x of `column` on `line`, past the end in space widths.
    [[nodiscard]] int xOfColumn(int line, int column) const;
    [[nodiscard]] QString lineText(int line) const;
    [[nodiscard]] QStringList lineTexts(int first, int count) const;
    /// Applies row edits (rows relative to `firstLine`) as one undoable edit.
    void applyRowEdits(int firstLine, const std::vector<block::RowEdit>& edits);
    /// Puts the real (invisible) text cursor on the box's moving corner, for scrolling.
    void syncCursorToBlock();

    QWidget* lineNumberArea_ = nullptr;
    QAction* lineWrapAct_ = nullptr;
    QList<QTextEdit::ExtraSelection> searchHighlights_;
    std::optional<block::BlockSelection> block_;
    bool blockDragging_ = false;
    bool applyingBlockEdit_ = false; ///< our own edits and cursor moves do not end the box
};

} // namespace nekogrid::editor
