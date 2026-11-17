#include "neko/editor/emoji_shortcodes.hpp"

#include "neko/editor/markdown_link_target.hpp"

#include <algorithm>
#include <string>

namespace neko::editor {

namespace {

constexpr EmojiShortcode kShortcodes[] = {
#include "neko/editor/emoji_shortcodes_data.inc"
};

bool isNameChar(QChar c) noexcept {
    const char16_t u = c.unicode();
    return (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9') || u == u'_' || u == u'+' ||
           u == u'-';
}

} // namespace

std::span<const EmojiShortcode> emojiShortcodes() noexcept {
    return kShortcodes;
}

const EmojiShortcode* findEmojiShortcode(std::string_view name) noexcept {
    const auto it = std::ranges::find(kShortcodes, name, &EmojiShortcode::name);
    return it == std::end(kShortcodes) ? nullptr : &*it;
}

std::optional<EmojiShortcodeQuery> emojiShortcodeQueryAt(const QString& text, qsizetype position) {
    if (position < 1 || position > text.size()) {
        return std::nullopt;
    }
    qsizetype nameStart = position;
    while (nameStart > 0 && isNameChar(text[nameStart - 1])) {
        --nameStart;
    }
    if (nameStart == 0 || text[nameStart - 1] != QLatin1Char(':')) {
        return std::nullopt;
    }
    const qsizetype colon = nameStart - 1;
    if (colon > 0) {
        const QChar before = text[colon - 1];
        if (before.isLetterOrNumber() || before == QLatin1Char(':') || before == QLatin1Char('\\')) {
            return std::nullopt;
        }
    }

    const qsizetype lineStart = colon == 0 ? 0 : text.lastIndexOf(QLatin1Char('\n'), colon - 1) + 1;
    if (QStringView(text).mid(lineStart, colon - lineStart).count(QLatin1Char('`')) % 2 != 0) {
        return std::nullopt; // inside `code`
    }
    if (lineInsideFencedCode(text, lineStart)) {
        return std::nullopt;
    }

    // Inside a finished `:name:`, a choice replaces all of it.
    qsizetype end = position;
    while (end < text.size() && isNameChar(text[end])) {
        ++end;
    }
    if (end < text.size() && text[end] == QLatin1Char(':')) {
        ++end;
    } else {
        end = position;
    }
    return EmojiShortcodeQuery{.start = colon, .end = end, .prefix = text.mid(nameStart, position - nameStart)};
}

std::vector<const EmojiShortcode*> matchEmojiShortcodes(const QString& prefix) {
    std::vector<const EmojiShortcode*> result;
    if (prefix.isEmpty()) {
        result.reserve(std::size(kShortcodes));
        for (const auto& entry : kShortcodes) {
            result.push_back(&entry);
        }
        return result;
    }
    if (!std::ranges::all_of(prefix, isNameChar)) {
        return result;
    }
    const std::string needle = prefix.toLower().toStdString();
    const std::string wordStart = '_' + needle;

    std::vector<std::pair<int, const EmojiShortcode*>> ranked;
    for (const auto& entry : kShortcodes) {
        int tier = -1;
        if (entry.name == needle) {
            tier = 0;
        } else if (entry.name.starts_with(needle)) {
            tier = 1;
        } else if (entry.name.find(wordStart) != std::string_view::npos) {
            tier = 2;
        } else if (entry.name.find(needle) != std::string_view::npos) {
            tier = 3;
        }
        if (tier >= 0) {
            ranked.emplace_back(tier, &entry);
        }
    }
    std::ranges::stable_sort(ranked, {}, &std::pair<int, const EmojiShortcode*>::first);
    result.reserve(ranked.size());
    for (const auto& [tier, entry] : ranked) {
        result.push_back(entry);
    }
    return result;
}

} // namespace neko::editor
