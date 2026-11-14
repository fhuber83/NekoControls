#pragma once

#include "neko/markdown/markdown_preview.hpp"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QWebEnginePage>

namespace neko::markdown {

class MarkdownRequestFilter;

/// A diagram or formula of a rendered document, as a picture (see
/// MarkdownPage::exportContentReady).
struct MarkdownExportImage {
    QByteArray png; ///< PNG data, at twice the on-screen size, on white
    QByteArray svg; ///< the same as SVG XML where there is one; else empty
};

/// The page a document is rendered in, for the preview (MarkdownPreview) and for
/// anything else that needs the rendering -- printing a PDF, exporting to a word
/// processor: page/preview.html with the bundled markdown-it, KaTeX, Mermaid and
/// MathJax, loaded from qrc on construction.
///
/// Hardening, all of it here so every user gets it:
///   - one off-the-record profile for every page (no cache, cookies or storage on disk);
///   - a request filter: `qrc:`, `data:`, `blob:`, and images below the document's
///     folder through the `neko-note:` scheme (registerMarkdownPreviewScheme()),
///     which serves image types only. Nothing from the network, no `file:` URLs;
///   - navigation stays on preview.html. Link clicks are decided by preview.js; links
///     of the content's own schemes and links to other Markdown files arrive as
///     console messages and come out as the signals below, http(s)/mailto open in the
///     system browser, anything else does nothing.
///
/// Rendering is asynchronous and queued until the page has loaded; renderFinished()
/// follows each render() (the page reports it through its title). UI thread only.
class MarkdownPage final : public QWebEnginePage {
    Q_OBJECT

public:
    enum class Mode {
        Screen, ///< the preview
        /// For export: Mermaid draws its labels as SVG text, not HTML, so diagrams can
        /// be turned into images; the page is meant to be light.
        Export,
    };

    explicit MarkdownPage(QObject* parent = nullptr);

    void render(const MarkdownPreviewContent& content, bool dark, Mode mode = Mode::Screen);
    /// See MarkdownPreview::scrollToSourceLine(); remembered and re-applied after renders.
    void scrollToSourceLine(int line);
    /// Asks preview.js for the rendered content with every diagram as a PNG;
    /// exportContentReady() follows.
    void requestExportContent();

    /// The local file a `neko-note:` URL names, cleaned ("" for another scheme).
    [[nodiscard]] static QString localPathOf(const QUrl& url);
    [[nodiscard]] static QUrl pageUrl();
    /// Whether an application may take `scheme` as one of its own (MarkdownLinkScheme):
    /// a valid scheme name that the page does not handle itself.
    [[nodiscard]] static bool isCustomLinkScheme(const QString& scheme);

signals:
    void customLinkActivated(const QString& url);
    void documentLinkActivated(const QString& path);
    void renderFinished();
    /// requestExportContent() is done: `html` is the rendered body (diagrams and formulas as
    /// `<img src="neko-export:N">`, images with absolute `neko-note:` sources),
    /// `images[N]` the diagrams and formulas with PNG fallback and vector SVG.
    void exportContentReady(const QString& html, const QList<neko::markdown::MarkdownExportImage>& images);

protected:
    void javaScriptConsoleMessage(
        JavaScriptConsoleMessageLevel level, const QString& message, int line, const QString& source) override;
    bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) override;

private:
    void run(const QString& script);

    MarkdownRequestFilter* filter_ = nullptr;
    bool loaded_ = false;
    QStringList pendingScripts_; ///< run once the page has loaded
    int lastLine_ = -1;
};

} // namespace neko::markdown
