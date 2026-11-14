#pragma once

#include "editor/text_search.hpp"

#include <QList>
#include <QTextEdit>
#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTimer;
class QToolButton;

namespace nekogrid::editor {

/// Find / Replace for one text editor: a CodeEditor, or any QPlainTextEdit. Ctrl+F
/// opens it for finding, Ctrl+H with the replace row; F3 / Shift+F3
/// (and Enter / Shift+Enter in the field) go to the next / previous match, wrapping
/// around; Esc closes it. Options: match case, whole word, regular expression.
///
/// Every match is highlighted; the current one is the editor's selection. The matches
/// follow edits while the bar is open. Replace and Replace All are ordinary edits:
/// Ctrl+Z undoes them, Replace All in one step. Matching and `$1`-style replacement
/// expansion are findAll / replacementFor (editor/text_search).
///
/// The buttons carry the application's icons (editor/editor_icons), or short captions
/// where it has none. The bar starts hidden; put it under the editor in a layout and
/// call installShortcuts() with the widget that holds both.
///
/// Threading: UI thread only.
class EditorFindBar final : public QWidget {
    Q_OBJECT

public:
    /// How the highlights reach the editor: CodeEditor::setSearchHighlights merges them
    /// with its own extra selections; a plain QPlainTextEdit takes them directly
    /// (setExtraSelections).
    using Highlighter = std::function<void(const QList<QTextEdit::ExtraSelection>&)>;

    EditorFindBar(QPlainTextEdit* editor, Highlighter highlighter, QWidget* parent = nullptr);

    /// Ctrl+F, Ctrl+H, F3 and Shift+F3 while the focus is inside `scope` -- the widget
    /// holding the editor and this bar, so other panels keep their own Ctrl+F.
    void installShortcuts(QWidget* scope);

    /// Shows the bar (with the replace row or without), puts a one-line selection into
    /// the search field, and focuses the field.
    void openFind();
    void openReplace();
    void closeBar();

    /// Selects the next (or previous) match after (before) the selection, wrapping.
    /// False when there is none.
    bool findNext(bool forward = true);
    /// Replaces the selected match and moves to the next; selects the next one first
    /// if the selection is not a match.
    void replaceCurrent();
    /// Replaces every match as one undoable edit; returns how many.
    int replaceAll();

    [[nodiscard]] QLineEdit* findField() const noexcept { return findEdit_; }
    [[nodiscard]] QLineEdit* replaceField() const noexcept { return replaceEdit_; }
    [[nodiscard]] bool isReplaceMode() const noexcept { return replaceMode_; }
    [[nodiscard]] TextSearchOptions options() const;
    void setOptions(const TextSearchOptions& options);
    /// Matches found by the last search (after edits, re-searched).
    [[nodiscard]] int matchCount() const noexcept { return static_cast<int>(matches_.size()); }
    /// "3 of 12", "No results", the regex error, "Replaced 4", ...
    [[nodiscard]] QString statusText() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void open(bool replace);
    /// Searches again and repaints the highlights; with `selectFromAnchor`, also
    /// selects the first match at or after where the search started (typing).
    void research(bool selectFromAnchor);
    void updateStatus();
    void selectMatch(const TextMatch& match);
    /// The match the editor's selection covers exactly, or -1.
    [[nodiscard]] int selectedMatchIndex() const;

    QPlainTextEdit* editor_ = nullptr;
    Highlighter highlighter_;
    QLineEdit* findEdit_ = nullptr;
    QLineEdit* replaceEdit_ = nullptr;
    QWidget* replaceRow_ = nullptr;
    QToolButton* caseBtn_ = nullptr;
    QToolButton* wordBtn_ = nullptr;
    QToolButton* regexBtn_ = nullptr;
    QToolButton* replaceBtn_ = nullptr;
    QToolButton* replaceAllBtn_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer* researchTimer_ = nullptr;
    bool replaceMode_ = false;
    qsizetype anchor_ = 0; ///< where incremental search starts: the cursor on open
    std::vector<TextMatch> matches_;
    bool truncated_ = false;
    QString error_;
    QString note_; ///< a one-off message ("Replaced 4") until the next search
};

} // namespace nekogrid::editor
