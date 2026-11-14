#pragma once

#include "neko/editor/code_editor.hpp"

#include <QString>
#include <QStringList>

class QCompleter;

namespace neko::editor {

class EmojiShortcodeModel;

/// A source editor for Markdown (the one in markdown_editor::MarkdownDocumentEditor,
/// and so in Neko Grid's note tabs): a CodeEditor with an emoji picker.
///
/// Typing a ':' that begins a word (emojiShortcodeQueryAt: not in "10:30", not in
/// code) opens a list of every GitHub emoji shortcode the preview renders
/// (emojiShortcodes), shown as "<emoji>  :name:". Each further name character
/// narrows it (matchEmojiShortcodes); a character that cannot be part of a name,
/// a cursor move, Esc, or an empty match closes it. Enter, Tab or a click inserts the
/// chosen `:name:` in place of what was typed: the shortcode stays text in the note
/// and becomes the emoji in the preview and the exports. Ctrl+Space opens the list for
/// a shortcode the cursor stands in.
///
/// Right after the bare ':' no row is selected, so Enter still ends the line; Down
/// picks the first row. Once a character follows, the best match is selected.
///
/// Threading: UI thread only.
class MarkdownEditor final : public CodeEditor {
    Q_OBJECT

public:
    explicit MarkdownEditor(QWidget* parent = nullptr);

    [[nodiscard]] QCompleter* emojiCompleter() const noexcept { return completer_; }
    [[nodiscard]] bool isEmojiPopupVisible() const;
    /// The shortcode names the list holds, in order (for tests); empty when it is closed.
    [[nodiscard]] QStringList emojiPopupNames() const;
    /// Opens, narrows or closes the list for the cursor's position (what typing does).
    void updateEmojiPopup();
    /// Replaces the shortcode being typed with `:name:` and closes the list.
    void insertEmojiShortcode(const QString& name);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void hideEmojiPopup();

    EmojiShortcodeModel* model_ = nullptr;
    QCompleter* completer_ = nullptr;
};

} // namespace neko::editor
