// Markdown preview library test suite: src/markdown only.
//
// This target links nekogrid_markdown alone -- no engine, no UI, no editor library.
// That keeps the library usable by other projects: if src/markdown ever includes
// src/core or src/ui, this binary stops linking. It is also the whole test of the
// library, so it can be taken along with it; it needs nothing from tests/.
//
// It runs the real page in Qt WebEngine, offscreen. What Neko Grid puts into the
// preview (a note's front matter as chips, `db:` links, the ER diagrams) is tested in
// tests/test_ui.cpp.

#include "markdown/markdown_page.hpp"
#include "markdown/markdown_preview.hpp"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QPalette>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QtTest>

#include <memory>

namespace md = nekogrid::markdown;

namespace {

constexpr int kRenderTimeoutMs = 60000;

/// Runs `script` in `page` and waits for its result (Qt WebEngine answers
/// asynchronously); an invalid QVariant after `timeoutMs`.
QVariant evalJs(QWebEnginePage* page, const QString& script, int timeoutMs = 10000) {
    QVariant result;
    bool done = false;
    page->runJavaScript(script, [&](const QVariant& v) {
        result = v;
        done = true;
    });
    QElapsedTimer timer;
    timer.start();
    while (!done && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return result;
}

/// A preview on screen (offscreen), its page, and the count of finished renders.
struct Fixture {
    std::unique_ptr<md::MarkdownPreview> preview{md::createMarkdownPreview(nullptr)};
    QSignalSpy rendered{preview.get(), &md::MarkdownPreview::renderFinished};
    QWebEnginePage* page = nullptr;

    Fixture() {
        preview->resize(900, 700);
        preview->show();
        page = preview->findChild<QWebEngineView*>()->page();
    }

    /// Shows `content` and waits for it; false when the render does not arrive.
    [[nodiscard]] bool show(md::MarkdownPreviewContent content) {
        const qsizetype before = rendered.size();
        preview->setContent(std::move(content));
        QElapsedTimer timer;
        timer.start();
        while (rendered.size() <= before && timer.elapsed() < kRenderTimeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        return rendered.size() > before;
    }

    [[nodiscard]] QVariant js(const QString& script) const { return evalJs(page, script); }
    [[nodiscard]] QString text(const QString& selector) const {
        return js(QStringLiteral("(document.querySelector('%1') || {textContent: ''}).textContent").arg(selector))
            .toString();
    }
    [[nodiscard]] int count(const QString& selector) const {
        return js(QStringLiteral("document.querySelectorAll('%1').length").arg(selector)).toInt();
    }
};

} // namespace

class TestMarkdown final : public QObject {
    Q_OBJECT

private slots:
    /// Which schemes an application may take as its own, and the document scheme's paths.
    void testLinkSchemeRules() {
        QVERIFY(md::MarkdownPage::isCustomLinkScheme(QStringLiteral("db")));
        QVERIFY(md::MarkdownPage::isCustomLinkScheme(QStringLiteral("DB")));
        QVERIFY(md::MarkdownPage::isCustomLinkScheme(QStringLiteral("x-app+v1.2")));
        // What the page handles itself cannot be taken over, in any case.
        for (const char* own :
             {"http", "https", "HTTPS", "mailto", "file", "qrc", "data", "blob", "javascript", "nekogrid-note"}) {
            QVERIFY2(!md::MarkdownPage::isCustomLinkScheme(QString::fromLatin1(own)), own);
        }
        // Not scheme names at all.
        for (const char* bad : {"", "1db", "d b", "db:", "db/x", "-x"}) {
            QVERIFY2(!md::MarkdownPage::isCustomLinkScheme(QString::fromLatin1(bad)), bad);
        }

        QCOMPARE(md::MarkdownPage::pageUrl(), QUrl(QStringLiteral("qrc:/markdown/preview.html")));
        const QString file = QDir::tempPath() + QStringLiteral("/a dir/pic.png");
        QUrl url;
        url.setScheme(QStringLiteral("nekogrid-note"));
        url.setPath(QUrl::fromLocalFile(file).path());
        QCOMPARE(md::MarkdownPage::localPathOf(url), QDir::cleanPath(file));
        QCOMPARE(md::MarkdownPage::localPathOf(QUrl(QStringLiteral("https://example.org/pic.png"))), QString());
        QCOMPARE(md::MarkdownPage::localPathOf(QUrl::fromLocalFile(file)), QString());
    }

    /// Markdown and its extensions render; the chips are text; raw HTML is not rendered;
    /// images load from the document's folder only.
    void testRendersDocumentChipsAndImages() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("doc")));
        QImage pixel(4, 4, QImage::Format_RGB32);
        pixel.fill(Qt::red);
        QVERIFY(pixel.save(dir.filePath(QStringLiteral("doc/inside.png"))));
        QVERIFY(pixel.save(dir.filePath(QStringLiteral("outside.png"))));

        Fixture f;
        QVERIFY(f.show({
            .body = QStringLiteral(
                "# Orders\n\n"
                "Some <b>raw</b> HTML.\n\n"
                "![in](inside.png) ![out](../outside.png)\n\n"
                "Footnote[^1], H~2~O, 2^10^, :tada:\n\n"
                "Term\n: Definition\n\n"
                "Price is $10 and $20. Formula: $E = mc^2$.\n\n"
                "$$\n\\frac{a}{b}\n$$\n\n"
                "```mermaid\nflowchart LR\n  A --> B\n```\n\n"
                "[^1]: Details\n"),
            .baseDirectory = dir.filePath(QStringLiteral("doc")),
            .title = QStringLiteral("Not shown"),
            .chips =
                {{.text = QStringLiteral("Database"), .value = QStringLiteral("SAMPLE")},
                 {.text = QStringLiteral("#sales")},
                 {.value = QStringLiteral("only <i>bold</i>")},
                 {.text = QStringLiteral("Hidden"), .toolTip = QStringLiteral("why \"so\""), .dashed = true},
                 {}}, // nothing to show: left out
        }));
        QCOMPARE(f.preview->content().title, QStringLiteral("Not shown"));

        QCOMPARE(f.text(QStringLiteral("#content h1")), QStringLiteral("Orders"));
        QVERIFY(!f.text(QStringLiteral("body")).contains(QStringLiteral("Not shown")));

        // Chips: in order, value in bold, text escaped, tool tip and dashed outline.
        QCOMPARE(f.count(QStringLiteral("#meta .chip")), 4);
        QCOMPARE(f.text(QStringLiteral("#meta .chip:nth-child(1)")), QStringLiteral("Database SAMPLE"));
        QCOMPARE(f.text(QStringLiteral("#meta .chip:nth-child(1) b")), QStringLiteral("SAMPLE"));
        QCOMPARE(f.text(QStringLiteral("#meta .chip:nth-child(2)")), QStringLiteral("#sales"));
        QCOMPARE(f.text(QStringLiteral("#meta .chip:nth-child(3)")), QStringLiteral("only <i>bold</i>"));
        QCOMPARE(f.count(QStringLiteral("#meta i")), 0);
        QCOMPARE(f.count(QStringLiteral("#meta .chip.dashed")), 1);
        QCOMPARE(
            f.js(QStringLiteral("document.querySelector('#meta .chip.dashed').title")).toString(),
            QStringLiteral("why \"so\""));

        // Raw HTML stays text.
        QCOMPARE(f.count(QStringLiteral("#content b")), 0);
        QVERIFY(f.text(QStringLiteral("#content")).contains(QStringLiteral("<b>raw</b>")));

        // Images load from the document's folder, not from outside it.
        QTRY_COMPARE_WITH_TIMEOUT(
            f.js(QStringLiteral("document.querySelector('img[alt=in]').naturalWidth")).toInt(), 4, 10000);
        QCOMPARE(f.js(QStringLiteral("document.querySelector('img[alt=out]').naturalWidth")).toInt(), 0);

        // Footnotes, emoji, sub / sup, definition lists.
        QCOMPARE(f.text(QStringLiteral("sub")), QStringLiteral("2"));
        QCOMPARE(f.text(QStringLiteral("sup:not(.footnote-ref)")), QStringLiteral("10"));
        QCOMPARE(f.text(QStringLiteral("dl dt")), QStringLiteral("Term"));
        QCOMPARE(f.text(QStringLiteral("dl dd")), QStringLiteral("Definition"));
        QVERIFY(f.text(QStringLiteral("#content")).contains(QStringLiteral("🎉")));
        QCOMPARE(f.count(QStringLiteral(".footnote-ref")), 1);
        QVERIFY(f.text(QStringLiteral(".footnotes")).contains(QStringLiteral("Details")));

        // Formulas, and dollars that are prices.
        QVERIFY(f.text(QStringLiteral("#content")).contains(QStringLiteral("$10 and $20")));
        QCOMPARE(f.count(QStringLiteral(".katex math")), 2);
        QCOMPARE(f.count(QStringLiteral(".katex-display")), 1);
        QCOMPARE(f.text(QStringLiteral("math annotation")), QStringLiteral("E = mc^2"));

        // The diagram is drawn.
        QCOMPARE(f.js(QStringLiteral("document.body.dataset.mermaidErrors")).toString(), QStringLiteral("0"));
        QCOMPARE(f.count(QStringLiteral(".mermaid-block svg")), 1);

        // No chips: the row is empty. A broken diagram shows Mermaid's message instead.
        QVERIFY(f.show({.body = QStringLiteral("```mermaid\nerDiagram\n  A ||--o{\n```\n")}));
        QCOMPARE(f.count(QStringLiteral("#meta .chip")), 0);
        QCOMPARE(f.js(QStringLiteral("document.body.dataset.mermaidErrors")).toString(), QStringLiteral("1"));
        QCOMPARE(f.count(QStringLiteral(".mermaid-error")), 1);
    }

    /// Links: the application's schemes come out as customLinkActivated(), links to
    /// other Markdown files as documentLinkActivated(), and nothing else goes anywhere.
    void testLinksAndCustomSchemes() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Fixture f;
        QSignalSpy custom(f.preview.get(), &md::MarkdownPreview::customLinkActivated);
        QSignalSpy documents(f.preview.get(), &md::MarkdownPreview::documentLinkActivated);
        const QString body = QStringLiteral(
            "[object](db:APP.PARENT) [ticket](issue:42) [other](other:thing) "
            "[next](sub/next%20doc.md) [ftp](ftp://x/y) [txt](plain.txt) [top](#orders)\n\n# Orders\n");
        QVERIFY(f.show({
            .body = body,
            .baseDirectory = dir.path(),
            .linkSchemes =
                {{.scheme = QStringLiteral("db"), .marker = QStringLiteral("⛁ ")},
                 {.scheme = QStringLiteral("Issue")},       // case does not matter, no marker
                 {.scheme = QStringLiteral("ftp://")},      // not a scheme name: ignored
                 {.scheme = QStringLiteral("javascript")}}, // the page's own: ignored
        }));

        // The application's links are marked; the others are ordinary links.
        QCOMPARE(f.count(QStringLiteral("a.custom-link")), 2);
        QCOMPARE(f.count(QStringLiteral("a.custom-link[data-scheme=db]")), 1);
        QCOMPARE(
            f.js(QStringLiteral("document.querySelector('a[data-scheme=db]').dataset.marker")).toString(),
            QStringLiteral("⛁ "));
        QCOMPARE(
            f.js(QStringLiteral("document.querySelector('a[data-scheme=db]').title")).toString(),
            QStringLiteral("APP.PARENT"));
        QVERIFY(!f.js(QStringLiteral("document.querySelector('a[data-scheme=issue]').hasAttribute('data-marker')"))
                     .toBool());
        // The marker is drawn in front of the link, through CSS.
        QVERIFY(
            f.js(QStringLiteral("getComputedStyle(document.querySelector('a[data-scheme=db]'), '::before').content"))
                .toString()
                .contains(QStringLiteral("⛁")));

        // Click every link: two custom links, one document link, and the page stays.
        (void)f.js(QStringLiteral("for (const a of document.querySelectorAll('a')) a.click(); true"));
        QTRY_COMPARE_WITH_TIMEOUT(custom.size(), 2, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(documents.size(), 1, 10000);
        QCOMPARE(custom.at(0).front().toString(), QStringLiteral("db:APP.PARENT"));
        QCOMPARE(custom.at(1).front().toString(), QStringLiteral("issue:42"));
        QCOMPARE(documents.front().front().toString(), dir.filePath(QStringLiteral("sub/next doc.md")));
        QTest::qWait(200); // anything else that was going to happen has
        QCOMPARE(custom.size(), 2);
        QCOMPARE(documents.size(), 1);
        QCOMPARE(f.page->url(), md::MarkdownPage::pageUrl());

        // The schemes belong to a content: without them the same links are inert.
        QVERIFY(f.show({.body = body, .baseDirectory = dir.path()}));
        QCOMPARE(f.count(QStringLiteral("a.custom-link")), 0);
        (void)f.js(QStringLiteral("for (const a of document.querySelectorAll('a')) a.click(); true"));
        QTRY_COMPARE_WITH_TIMEOUT(documents.size(), 2, 10000);
        QCOMPARE(custom.size(), 2);
        QCOMPARE(f.page->url(), md::MarkdownPage::pageUrl());
    }

    /// A file line brings the block rendered from it to the top; lines before the body
    /// (bodyStartLine) count.
    void testScrollToSourceLine() {
        Fixture f;
        QString body;
        for (int i = 0; i < 60; ++i) {
            body += QStringLiteral("Paragraph %1\n\n").arg(i);
        }
        QVERIFY(f.show({.body = body, .bodyStartLine = 3}));
        f.preview->scrollToSourceLine(3 + 2 * 30); // "Paragraph 30"
        QTRY_VERIFY_WITH_TIMEOUT(f.js(QStringLiteral("window.scrollY")).toDouble() > 100, 5000);
        QCOMPARE(
            f.js(QStringLiteral("document.elementFromPoint(20, 12).closest('[data-source-line]').textContent"))
                .toString(),
            QStringLiteral("Paragraph 30"));
        f.preview->scrollToSourceLine(0); // before the body: the top
        QTRY_COMPARE_WITH_TIMEOUT(f.js(QStringLiteral("window.scrollY")).toDouble(), 0.0, 5000);
    }

    /// Light or dark follows the widget's palette, also when it changes; a background
    /// colour of the content's own decides instead.
    void testThemeFollowsPaletteAndBackground() {
        Fixture f;
        const auto theme = [&f] {
            return f.js(QStringLiteral("document.documentElement.dataset.theme")).toString();
        };
        const auto background = [&f] {
            return f.js(QStringLiteral("document.documentElement.style.getPropertyValue('--bg')")).toString();
        };

        QPalette light = f.preview->palette();
        light.setColor(QPalette::Base, Qt::white);
        f.preview->setPalette(light);
        QVERIFY(f.show({.body = QStringLiteral("text\n")}));
        QCOMPARE(theme(), QStringLiteral("light"));
        QCOMPARE(background(), QString());

        // A palette change re-renders by itself.
        const qsizetype before = f.rendered.size();
        QPalette dark = light;
        dark.setColor(QPalette::Base, QColor(0x20, 0x20, 0x20));
        f.preview->setPalette(dark);
        QTRY_VERIFY_WITH_TIMEOUT(f.rendered.size() > before, kRenderTimeoutMs);
        QCOMPARE(theme(), QStringLiteral("dark"));

        // The content's own background: a light one is light, whatever the palette says.
        QVERIFY(f.show({.body = QStringLiteral("text\n"), .backgroundColor = QStringLiteral("#ffffcc")}));
        QCOMPARE(theme(), QStringLiteral("light"));
        QCOMPARE(background(), QStringLiteral("#ffffcc"));
        QVERIFY(f.show({.body = QStringLiteral("text\n")}));
        QCOMPARE(theme(), QStringLiteral("dark"));
        QCOMPARE(background(), QString());
    }

    /// MarkdownPage on its own, as an exporter uses it: export mode, then the rendered
    /// body with every diagram and formula as a picture.
    void testPageExportContent() {
        QWebEngineView view;
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(760, 1000);
        auto* page = new md::MarkdownPage(&view);
        view.setPage(page);
        view.show();
        QSignalSpy rendered(page, &md::MarkdownPage::renderFinished);
        QString html;
        QList<md::MarkdownExportImage> images;
        bool exported = false;
        connect(
            page,
            &md::MarkdownPage::exportContentReady,
            this,
            [&](const QString& h, const QList<md::MarkdownExportImage>& i) {
                html = h;
                images = i;
                exported = true;
            });

        page->render(
            {.body = QStringLiteral("# Report\n\nText.\n\n```mermaid\nflowchart LR\n  A --> B\n```\n")},
            false,
            md::MarkdownPage::Mode::Export);
        QTRY_VERIFY_WITH_TIMEOUT(rendered.size() >= 1, kRenderTimeoutMs);
        page->requestExportContent();
        QTRY_VERIFY_WITH_TIMEOUT(exported, kRenderTimeoutMs);

        QVERIFY(html.contains(QStringLiteral("Report")));
        QVERIFY2(html.contains(QStringLiteral("nekogrid-export:0")), qPrintable(html.left(400)));
        QCOMPARE(images.size(), 1);
        QVERIFY(images.front().png.startsWith(QByteArrayLiteral("\x89PNG")));
        QVERIFY(images.front().svg.contains(QByteArrayLiteral("<svg")));
        QVERIFY(!QImage::fromData(images.front().png, "PNG").isNull());
    }
};

int main(int argc, char* argv[]) {
    // Offscreen from a desktop session, Qt's offscreen platform takes GLX from the X
    // server in DISPLAY -- which some drivers refuse with a fatal X error the first time
    // Qt WebEngine starts. The tests need no GLX.
    if (!qEnvironmentVariableIsSet("QT_QPA_OFFSCREEN_NO_GLX")) {
        qputenv("QT_QPA_OFFSCREEN_NO_GLX", QByteArrayLiteral("1"));
    }
    // What every application using the preview does before its QApplication exists.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    nekogrid::markdown::registerMarkdownPreviewScheme();
    QApplication app(argc, argv);
    TestMarkdown testObject;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&testObject, argc, argv);
}

#include "test_markdown.moc"
