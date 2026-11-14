#pragma once

#include <QList>
#include <QString>
#include <QWidget>

namespace nekogrid::markdown {

/// A small label above the rendered document: "Database **SAMPLE**", "#sales". For
/// what a document's metadata says about it; plain text, never Markdown or HTML.
struct MarkdownChip {
    QString text = {};    ///< shown as is; may be empty when there is a `value`
    QString value = {};   ///< shown in bold after `text`; may be empty
    QString toolTip = {}; ///< shown on hover; may be empty
    bool dashed = false;  ///< a dashed outline, for a chip that says something is off
};

/// A URL scheme whose links the application handles itself, such as Neko Grid's `db:`
/// for database objects. A link `[text](scheme:rest)` is drawn apart from web links,
/// with `marker` in front, and a click comes out as customLinkActivated() instead of
/// going anywhere.
///
/// `scheme` is the name without the colon, letters first (RFC 3986). The schemes the
/// preview handles itself -- http, https, mailto, file, qrc, data, blob, javascript --
/// cannot be taken over and are ignored here.
struct MarkdownLinkScheme {
    QString scheme = {};
    QString marker = {}; ///< put in front of such a link's text ("⛁ "); may be empty
};

/// What a preview shows: one Markdown document and what the application says about it.
struct MarkdownPreviewContent {
    QString body = {}; ///< the Markdown to render (without a front matter block, if the file has one)
    /// The 0-based line of the file that `body` starts on, so preview positions can be
    /// mapped back to editor lines. 0 when the body is the whole file.
    int bodyStartLine = 0;
    /// The document's folder: relative images and links resolve against it. "" for a
    /// document that was never saved.
    QString baseDirectory = {};
    /// Background colour override (CSS hex or colour name); the text colours then follow
    /// its lightness instead of the palette. "" for the palette's.
    QString backgroundColor = {};
    /// The document's title, for whoever needs one (an export's document property). It
    /// is not rendered.
    QString title = {};
    QList<MarkdownChip> chips = {};             ///< above the document, in this order
    QList<MarkdownLinkScheme> linkSchemes = {}; ///< the application's own link schemes
};

/// A widget that renders Markdown: behind an interface, so its users do not depend on
/// how. createMarkdownPreview() returns the Qt WebEngine one, a MarkdownPage in a view:
/// markdown-it with footnotes, definition lists, sub / superscript and GitHub emoji,
/// KaTeX and MathJax formulas, Mermaid diagrams, heading anchors and `[ToC]`.
///
/// The page may load its own resources and images below `baseDirectory` (relative
/// image links, served through the `nekogrid-note:` scheme) and nothing else -- no
/// network, no file: URLs, ever; raw HTML in a document is not rendered and Mermaid
/// runs in its "strict" security level. Links: a click on one of the content's
/// `linkSchemes` comes out as customLinkActivated(), a link to another Markdown file
/// as documentLinkActivated(), web links open in the system browser, and every other
/// link does nothing (an unknown scheme must never reach the OS).
///
/// Light or dark follows the widget's palette (QPalette::Base), also when it changes.
///
/// Rendering is asynchronous: setContent() returns at once and renderFinished()
/// follows, diagrams included.
///
/// UI thread only. An application using this must call registerMarkdownPreviewScheme()
/// and set Qt::AA_ShareOpenGLContexts before its QApplication exists.
class MarkdownPreview : public QWidget {
    Q_OBJECT

public:
    using QWidget::QWidget;

    /// Shows `content`, replacing what was shown.
    void setContent(MarkdownPreviewContent content);
    [[nodiscard]] const MarkdownPreviewContent& content() const noexcept { return content_; }

    /// Brings the part rendered from file line `line` (0-based) to the top, as near as
    /// the renderer can tell. Lines before the body map to the top.
    virtual void scrollToSourceLine(int line) = 0;

    /// A real size, like a text editor's. QWebEngineView hints 0x0, and a QSplitter
    /// shares space out by hint: beside the Markdown source it got none at all.
    [[nodiscard]] QSize sizeHint() const override { return {256, 192}; }
    [[nodiscard]] QSize minimumSizeHint() const override { return {120, 80}; }

signals:
    /// A link of one of the content's `linkSchemes` was clicked; `url` as the page
    /// resolved it, scheme included (percent-encoding may differ from the document's
    /// text).
    void customLinkActivated(const QString& url);
    /// A link to another Markdown file (`[x](other.md)`, relative to this one) was
    /// clicked; `path` is the file it points to, which may not exist.
    void documentLinkActivated(const QString& path);
    /// The last setContent() is on screen, Mermaid diagrams drawn.
    void renderFinished();

protected:
    virtual void render(const MarkdownPreviewContent& content) = 0;

private:
    MarkdownPreviewContent content_;
};

/// Registers the `nekogrid-note:` URL scheme the preview serves a document's images
/// through. Qt WebEngine requires this **before the QApplication exists**; without it
/// relative images do not show.
void registerMarkdownPreviewScheme();

/// The preview this build renders with, parented to `parent`.
[[nodiscard]] MarkdownPreview* createMarkdownPreview(QWidget* parent);

} // namespace nekogrid::markdown
