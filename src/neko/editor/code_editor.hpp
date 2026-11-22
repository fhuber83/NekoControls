#pragma once

#include "neko/editor/block_selection.hpp"

#include <QList>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>

#include <optional>

class QAction;
class QMenu;
class QMimeData;

namespace neko::editor {

/// A text editor control for source text: Neko Grid's SQL tabs (ui::SqlEditor derives
/// from it) and the Markdown source of a note (MarkdownEditor). A QPlainTextEdit with:
///
///   - a line-number gutter and a current-line highlight, in the widget's palette;
///   - the font and line wrapping of EditorDefaults, followed live, tabs 4 wide;
///   - search highlights for EditorFindBar, layered with the current line;
///   - setTextUndoable(), which replaces everything as one undoable edit;
///   - **column (box) selection**, as in Visual Studio and Notepad++;
///   - an **inline suggestion** ("ghost text") from a completion engine, see below.
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
    static constexpr const char* kBlockMimeType = "application/x-neko-block";

    explicit CodeEditor(QWidget* parent = nullptr);

    /// Replaces the whole text as one edit: unlike setPlainText() or clear(), Ctrl+Z
    /// brings back what was there.
    void setTextUndoable(const QString& text);

    /// Shows `document` instead of the current one, and keeps everything that follows the
    /// document working on it: column selection, the inline suggestion, the tab width,
    /// the search highlights (dropped: they belong to the old text) and an EditorFindBar on
    /// this editor (it re-connects on documentReplaced()). Use this rather than
    /// QPlainTextEdit::setDocument(), which leaves those connected to the old document.
    ///
    /// What it is for: a large text is expensive to put into a QTextDocument (a block per
    /// line), and a QTextDocument may be built in any thread. Build it on a worker, give it
    /// a QPlainTextDocumentLayout there, moveToThread() this one, and hand it over here:
    ///
    ///     auto* doc = new QTextDocument;                         // worker thread
    ///     doc->setDocumentLayout(new QPlainTextDocumentLayout(doc));
    ///     doc->setPlainText(text);
    ///     doc->moveToThread(editorThread);
    ///     ...
    ///     editor->replaceDocument(doc);                          // UI thread
    ///
    /// `document` must live in this editor's thread and have a QPlainTextDocumentLayout;
    /// otherwise nothing changes and this returns false. Ownership is QPlainTextEdit's:
    /// the editor deletes the document it replaces only if it was the editor's own (the
    /// one it started with), and never takes ownership of `document` (parent it to the
    /// editor to tie their lifetimes). Emits documentReplaced().
    bool replaceDocument(QTextDocument* document);

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

    // ---- Inline suggestion ("ghost text") -----------------------------------------------
    //
    // Text proposed for insertion at the caret, e.g. by a completion engine such as GitHub
    // Copilot. It is drawn after the caret in the placeholder colour -- the lines of a
    // multi-line suggestion push the lines below down -- but is not part of the document:
    // no undo step, no modification, highlighters, Find and toPlainText() don't see it.
    //
    //   - Tab accepts all of it, Ctrl+Right the next word (the rest stays); each is one
    //     undo step.
    //   - Typing what it starts with eats into it.
    //   - Esc, any other edit, moving the caret, a click, losing the focus or a column
    //     selection dismiss it.

    /// Shows `text` (`\n` line breaks) at the caret, replacing any suggestion shown.
    /// Ignored -- nothing is shown -- while the editor is read-only, has a selection or a
    /// column selection; an empty `text` clears it. Neither clearing emits a signal.
    void setInlineSuggestion(const QString& text);
    void clearInlineSuggestion();
    [[nodiscard]] bool hasInlineSuggestion() const noexcept { return suggestion_.has_value(); }
    /// What is still shown (the part not yet typed or accepted).
    [[nodiscard]] QString inlineSuggestion() const;
    /// Tab and Ctrl+Right. False without a suggestion.
    bool acceptInlineSuggestion();
    bool acceptInlineSuggestionWord();
    /// The part of `text` that Ctrl+Right accepts: a line break with the indentation after
    /// it, or leading blanks and then a word (letters, digits, `_`) or a run of other
    /// characters.
    [[nodiscard]] static qsizetype nextSuggestionWordLength(const QString& text);

    void lineNumberAreaPaintEvent(QPaintEvent* event);
    [[nodiscard]] int lineNumberAreaWidth() const;

signals:
    void lineWrapChanged(bool enabled);
    void overwriteModeChanged(bool overwrite);
    /// A suggestion was accepted with Tab / Ctrl+Right (or the accept functions).
    /// `acceptedLength`: how much of the text given to setInlineSuggestion() is now in the
    /// document, from its start, in UTF-16 code units -- typed or accepted. `complete`: all
    /// of it; otherwise the rest is still shown.
    void inlineSuggestionAccepted(qsizetype acceptedLength, bool complete);
    /// The editor dropped a suggestion that was not completely accepted: Esc, an edit, a
    /// caret move, a click, focus loss. Not emitted for clearInlineSuggestion() or
    /// setInlineSuggestion(), nor when the user typed it to its end.
    void inlineSuggestionDismissed();
    /// replaceDocument() put a new document in: whoever follows document() re-connects.
    void documentReplaced();

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
    void focusOutEvent(QFocusEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;

private:
    /// Follows edits of document(): they end a column selection and may dismiss the
    /// inline suggestion.
    void connectDocument();
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

    // Inline suggestion. It is shown as the pre-edit area of its block's QTextLayout (as an
    // input method's uncommitted text is): laid out with the line, line breaks as
    // QChar::LineSeparator, so it takes up room without being in the document.
    struct Suggestion {
        QTextCursor anchor;     ///< where it is inserted; moves with edits before it
        QString text;           ///< what is still shown
        qsizetype consumed = 0; ///< how much of the original text is in the document
    };
    void showSuggestion();
    /// Removes the pre-edit area and its format from the anchor's block and from `other`
    /// (the block an edit happened in, which may have held it before the edit).
    void hideSuggestion(const QTextBlock& other = {});
    /// Puts the format back if a highlighter replaced the block's formats.
    void ensureSuggestionShown();
    void dismissSuggestion();
    bool acceptSuggestionPart(qsizetype length);
    void suggestionContentsChanged(int position, int removed, int added);
    [[nodiscard]] QTextCharFormat suggestionFormat() const;

    std::optional<Suggestion> suggestion_;
    bool editingSuggestion_ = false; ///< our own accept edit, not one that dismisses it

    QMetaObject::Connection documentConnection_;
    QWidget* lineNumberArea_ = nullptr;
    QAction* lineWrapAct_ = nullptr;
    QList<QTextEdit::ExtraSelection> searchHighlights_;
    std::optional<block::BlockSelection> block_;
    bool blockDragging_ = false;
    bool applyingBlockEdit_ = false; ///< our own edits and cursor moves do not end the box
};

} // namespace neko::editor
