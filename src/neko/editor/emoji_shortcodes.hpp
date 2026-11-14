#pragma once

#include <QString>

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace neko::editor {

/// One GitHub emoji shortcode: `:name:` in a note, `emoji` in the preview.
struct EmojiShortcode {
    std::string_view name;  ///< lower case ASCII, `[a-z0-9_+-]`, without the colons
    std::string_view emoji; ///< the character(s), UTF-8
};

/// Every shortcode the Markdown preview renders: GitHub's gemoji set (aliases are
/// entries of their own), in gemoji's category order -- faces first. The table is
/// generated from the pinned markdown-it-emoji build by
/// scripts/generate-emoji-shortcodes.py; regenerate it when that pin changes.
/// GitHub's image-only names (`:octocat:`) are not in it: the preview cannot draw them.
///
/// Static data, safe on any thread.
[[nodiscard]] std::span<const EmojiShortcode> emojiShortcodes() noexcept;

/// The entry called `name` (exact, without colons), or nullptr.
[[nodiscard]] const EmojiShortcode* findEmojiShortcode(std::string_view name) noexcept;

/// A shortcode being typed in a note: `:` and what follows it up to the cursor.
struct EmojiShortcodeQuery {
    qsizetype start = 0; ///< position of the opening ':'
    /// End of what a chosen shortcode replaces: the cursor -- or, when the cursor sits
    /// inside a finished `:name:` (name characters and a ':' follow it), past that ':'.
    qsizetype end = 0;
    QString prefix; ///< the characters between ':' and the cursor (may be empty)
};

/// The shortcode being typed at `position` in the Markdown `text`, if any: the cursor
/// follows a ':' and nothing but name characters (`[A-Za-z0-9_+-]`). The ':' must begin
/// a word -- at the start of the text or after a space or punctuation other than ':'
/// and '\\' -- so "10:30", "Note:" and "http://" do not count, nor does the closing
/// colon of a finished `:name:`. Nothing counts inside a fenced code block (``` or ~~~)
/// or inside an inline code span on the same line (an odd number of backticks before
/// the ':').
///
/// Pure text work, safe on any thread.
[[nodiscard]] std::optional<EmojiShortcodeQuery> emojiShortcodeQueryAt(const QString& text, qsizetype position);

/// The shortcodes for `prefix` (case-insensitive), best first: all of them in table
/// order for an empty prefix; otherwise the exact name, then names starting with it,
/// then names with a `_`-separated word starting with it, then names containing it --
/// each group in table order. Empty when nothing matches.
[[nodiscard]] std::vector<const EmojiShortcode*> matchEmojiShortcodes(const QString& prefix);

} // namespace neko::editor
