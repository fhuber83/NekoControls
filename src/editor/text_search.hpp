#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringView>

#include <cstddef>
#include <vector>

namespace nekogrid::editor {

/// How the editor's Find / Replace bar matches.
struct TextSearchOptions {
    bool caseSensitive = false;
    /// Only matches with no word character (letter, digit, `_`) directly before or after.
    bool wholeWord = false;
    /// The needle is a regular expression (PCRE2 via QRegularExpression); otherwise it
    /// matches literally.
    bool regex = false;
};

/// One match: a range of UTF-16 code units in the searched text.
struct TextMatch {
    qsizetype start = 0;
    qsizetype length = 0;
    friend bool operator==(const TextMatch&, const TextMatch&) = default;
};

struct TextSearchResult {
    /// False for an invalid regular expression; `error` then says why.
    bool valid = true;
    QString error;
    /// In order; never overlapping, never empty (a regex that matches the empty string
    /// -- `^`, `x*` -- yields only its non-empty matches).
    std::vector<TextMatch> matches;
    /// More than `limit` matches exist; `matches` holds the first `limit`.
    bool truncated = false;
};

/// The pattern the search runs, built from the needle and options; invalid for a bad
/// regular expression. Literal needles are escaped, so `.` or `(` match themselves.
[[nodiscard]] QRegularExpression searchPattern(const QString& needle, const TextSearchOptions& options);

/// Every match of `needle` in `text`, at most `limit`. An empty needle matches nothing.
/// Pure; any thread. (QString, not QStringView: the view overloads of
/// QRegularExpression need Qt 6.5, and the release build's Ubuntu runner has 6.4.)
[[nodiscard]] TextSearchResult
findAll(const QString& text, const QString& needle, const TextSearchOptions& options, std::size_t limit = 10000);

/// What `match` (one of findAll's) is replaced with. Literal searches use `replacement`
/// as it is. Regex searches expand it: `$0`..`$9` and `\0`..`\9` are capture groups,
/// `\n` and `\t` a newline and a tab, `$$` and `\\` a literal `$` and `\`.
[[nodiscard]] QString replacementFor(
    const QString& text,
    const TextMatch& match,
    const QString& needle,
    const QString& replacement,
    const TextSearchOptions& options);

} // namespace nekogrid::editor
