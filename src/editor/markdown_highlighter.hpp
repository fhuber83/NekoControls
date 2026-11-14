#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <vector>

namespace nekogrid::editor {

/// Markdown highlighting without dependencies -- what Neko Grid's note editor uses
/// when KF6 SyntaxHighlighting is not available (with it, ui::MarkdownTab uses its
/// "Markdown" definition instead).
///
/// Line-based, like the editor's built-in SQL highlighter: headings, emphasis, inline
/// code, links (`db:` links in their own colour), list markers, block quotes, rules,
/// fenced code blocks and the front matter header -- the last two tracked across lines
/// through the block state. Not a parser; nested constructs get the outer format.
class MarkdownHighlighter final : public QSyntaxHighlighter {
public:
    explicit MarkdownHighlighter(QTextDocument* document, bool dark);

    /// Re-colours for a light or dark palette.
    void setDark(bool dark);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
        int group = 0; ///< the capture group that gets the format
    };

    void buildFormats(bool dark);

    bool dark_ = false;
    std::vector<Rule> inlineRules_;
    QTextCharFormat heading_;
    QTextCharFormat quote_;
    QTextCharFormat code_;
    QTextCharFormat frontMatter_;
    QTextCharFormat rule_;
};

} // namespace nekogrid::editor
