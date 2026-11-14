#include "editor/markdown_highlighter.hpp"

#include <QFont>

namespace nekogrid::editor {

namespace {

// Block states. A fenced block stores its fence: kFenceBase + 1000 * char kind + length.
constexpr int kNormal = 0;
constexpr int kFrontMatter = 1;
constexpr int kMathBlock = 2;
constexpr int kFenceBase = 10;
constexpr int kBacktick = 0;
constexpr int kTilde = 1;

int fenceState(QChar c, qsizetype length) {
    return kFenceBase + 1000 * (c == QLatin1Char('~') ? kTilde : kBacktick) + static_cast<int>(length);
}

QTextCharFormat colored(const QColor& c) {
    QTextCharFormat f;
    f.setForeground(c);
    return f;
}

} // namespace

MarkdownHighlighter::MarkdownHighlighter(QTextDocument* document, bool dark)
    : QSyntaxHighlighter(document) {
    buildFormats(dark);
}

void MarkdownHighlighter::setDark(bool dark) {
    if (dark != dark_) {
        buildFormats(dark);
        rehighlight();
    }
}

void MarkdownHighlighter::buildFormats(bool dark) {
    dark_ = dark;
    const QColor accent = dark ? QColor(0x6c, 0xb6, 0xff) : QColor(0x0b, 0x5c, 0xad);
    const QColor muted = dark ? QColor(0x8b, 0x94, 0x9e) : QColor(0x6a, 0x73, 0x7d);
    const QColor object = dark ? QColor(0xd2, 0xa8, 0xff) : QColor(0x82, 0x50, 0xdf);
    const QColor codeColor = dark ? QColor(0xff, 0xa6, 0x57) : QColor(0xa3, 0x40, 0x0b);

    heading_ = colored(accent);
    heading_.setFontWeight(QFont::Bold);
    quote_ = colored(muted);
    quote_.setFontItalic(true);
    code_ = colored(codeColor);
    frontMatter_ = colored(muted);
    rule_ = colored(muted);

    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    QTextCharFormat italic;
    italic.setFontItalic(true);
    QTextCharFormat strike;
    strike.setFontStrikeOut(true);
    QTextCharFormat sub;
    sub.setVerticalAlignment(QTextCharFormat::AlignSubScript);
    QTextCharFormat sup;
    sup.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    QTextCharFormat emoji = colored(codeColor);
    QTextCharFormat linkText = colored(accent);
    QTextCharFormat linkUrl = colored(muted);
    linkUrl.setFontUnderline(true);
    QTextCharFormat objectLink = colored(object);
    objectLink.setFontUnderline(true);

    // Later rules win where they overlap: code spans last, so nothing inside them is
    // formatted as emphasis or a link.
    inlineRules_ = {
        {QRegularExpression(QStringLiteral(R"(^\s{0,3}(?:[-*+]|\d+[.)])(?=\s))")), colored(accent), 0},
        {QRegularExpression(QStringLiteral(R"(^\s{0,3}:(?=\s))")), colored(accent), 0},
        {QRegularExpression(QStringLiteral(R"((\*|_)(?!\s)[^*_]+?(?<!\s)\1)")), italic, 0},
        {QRegularExpression(QStringLiteral(R"((\*\*|__)(?!\s).+?(?<!\s)\1)")), bold, 0},
        {QRegularExpression(QStringLiteral(R"(~~(?!\s).+?(?<!\s)~~)")), strike, 0},
        {QRegularExpression(QStringLiteral(R"((?<!~)~(?!\s)[^~\s]+?(?<!\s)~(?!~))")), sub, 0},
        {QRegularExpression(QStringLiteral(R"(\^(?!\s)[^^\s]+?(?<!\s)\^)")), sup, 0},
        {QRegularExpression(QStringLiteral(R"(:[a-zA-Z0-9_+-]+:)")), emoji, 0},
        {QRegularExpression(QStringLiteral(R"(!?\[\^?[^\]]*\])")), linkText, 0},
        {QRegularExpression(QStringLiteral(R"(\]\(([^)\s]+)[^)]*\))")), linkUrl, 1},
        {QRegularExpression(QStringLiteral(R"(<https?://[^>\s]+>)")), linkUrl, 0},
        {QRegularExpression(
             QStringLiteral(R"(\]\((<?db:[^)\s>]+>?)|<db:[^>\s]+>)"), QRegularExpression::CaseInsensitiveOption),
         objectLink,
         0},
        {QRegularExpression(QStringLiteral(R"((?<!\\)\$\$(?!\s).+?(?<!\s)\$\$)")), code_, 0},
        {QRegularExpression(QStringLiteral(R"((?<!\\)\$(?!\s)[^$\n]+?(?<!\s)\$(?!\d))")), code_, 0},
        {QRegularExpression(QStringLiteral(R"((`+)(?!`).*?(?<!`)\1(?!`))")), code_, 0},
    };
}

void MarkdownHighlighter::highlightBlock(const QString& text) {
    static const QRegularExpression fence(QStringLiteral(R"(^ {0,3}(`{3,}|~{3,}))"));
    static const QRegularExpression headingLine(QStringLiteral(R"(^ {0,3}#{1,6}(\s|$))"));
    static const QRegularExpression ruleLine(QStringLiteral(R"(^ {0,3}([-*_])(\s*\1){2,}\s*$)"));
    const int previous = previousBlockState();
    const QString trimmed = text.trimmed();

    // Front matter: only from the very first line, up to the closing --- or ...
    if (currentBlock().blockNumber() == 0 && trimmed == QLatin1String("---")) {
        setFormat(0, static_cast<int>(text.size()), frontMatter_);
        setCurrentBlockState(kFrontMatter);
        return;
    }
    if (previous == kFrontMatter) {
        setFormat(0, static_cast<int>(text.size()), frontMatter_);
        const bool closes = trimmed == QLatin1String("---") || trimmed == QLatin1String("...");
        setCurrentBlockState(closes ? kNormal : kFrontMatter);
        return;
    }

    // Inside a multiline $$ math block.
    if (previous == kMathBlock) {
        setFormat(0, static_cast<int>(text.size()), code_);
        const bool closes = trimmed == QLatin1String("$$");
        setCurrentBlockState(closes ? kNormal : kMathBlock);
        return;
    }

    // Inside a fenced block until a fence of the same kind, at least as long.
    if (previous >= kFenceBase) {
        setFormat(0, static_cast<int>(text.size()), code_);
        const int kind = (previous - kFenceBase) / 1000;
        const int length = (previous - kFenceBase) % 1000;
        const QChar c = kind == kTilde ? QLatin1Char('~') : QLatin1Char('`');
        const bool closes = trimmed.size() >= length && trimmed == QString(trimmed.size(), c);
        setCurrentBlockState(closes ? kNormal : previous);
        return;
    }
    if (trimmed == QLatin1String("$$")) {
        setFormat(0, static_cast<int>(text.size()), code_);
        setCurrentBlockState(kMathBlock);
        return;
    }
    if (const auto m = fence.match(text); m.hasMatch()) {
        setFormat(0, static_cast<int>(text.size()), code_);
        setCurrentBlockState(fenceState(m.captured(1).front(), m.captured(1).size()));
        return;
    }
    setCurrentBlockState(kNormal);

    if (headingLine.match(text).hasMatch()) {
        setFormat(0, static_cast<int>(text.size()), heading_);
        return;
    }
    if (ruleLine.match(text).hasMatch()) {
        setFormat(0, static_cast<int>(text.size()), rule_);
        return;
    }
    if (trimmed.startsWith(QLatin1Char('>'))) {
        setFormat(0, static_cast<int>(text.size()), quote_);
    }
    for (const auto& r : inlineRules_) {
        for (auto it = r.pattern.globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            setFormat(
                static_cast<int>(m.capturedStart(r.group)), static_cast<int>(m.capturedLength(r.group)), r.format);
        }
    }
}

} // namespace nekogrid::editor
