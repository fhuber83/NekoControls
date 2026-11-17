#pragma once

#include "neko/editor/code_editor.hpp"
#include "neko/editor/markdown_link_target.hpp"

#include <QString>
#include <QStringList>

class QCompleter;
class QStandardItemModel;

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
/// **Link destinations**, once setLinkTargetProvider() gave a provider: Ctrl+Space
/// inside `[text](...` (markdownLinkTargetQueryAt) opens a second list with what the
/// provider offers there -- files, or after a `#` the headings of the file before it;
/// typing a `#` in a destination opens the headings by itself. Typing narrows the list
/// (matchMarkdownLinkTargets), a character that ends a destination or a cursor move
/// closes it, and Enter, Tab or a click puts the target in place of the path or
/// fragment being typed. The first row is always selected. Without a provider,
/// Ctrl+Space in a destination does what it does elsewhere.
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

    /// Where link targets come from; an empty function turns link completion off.
    void setLinkTargetProvider(MarkdownLinkTargetProvider provider);
    [[nodiscard]] QCompleter* linkCompleter() const noexcept { return linkCompleter_; }
    [[nodiscard]] bool isLinkPopupVisible() const;
    /// The targets the link list holds, in order (for tests); empty when it is closed.
    [[nodiscard]] QStringList linkPopupTargets() const;
    /// Narrows or closes the link list for the cursor's position; opens it too when
    /// `mayOpen` (Ctrl+Space, a typed `#`). Whether it is open afterwards.
    bool updateLinkPopup(bool mayOpen);
    /// Puts `target` in place of the path or fragment being typed and closes the list.
    void insertLinkTarget(const QString& target);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void hideEmojiPopup();
    void hideLinkPopup();

    EmojiShortcodeModel* model_ = nullptr;
    QCompleter* completer_ = nullptr;

    MarkdownLinkTargetProvider linkProvider_;
    QStandardItemModel* linkModel_ = nullptr;
    QCompleter* linkCompleter_ = nullptr;
    /// What the provider gave for linkCandidatesKey_ (the query's kind and path),
    /// kept while the list is open.
    std::vector<MarkdownLinkTarget> linkCandidates_;
    QString linkCandidatesKey_;
};

} // namespace neko::editor
