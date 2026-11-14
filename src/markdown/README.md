# nekogrid_markdown — a Markdown preview widget for Qt

The Markdown preview of [Neko Grid](../../README.md), as a small static library that
other Qt 6 projects can use as it is. It links Qt Core, Gui, Widgets and
WebEngineWidgets and nothing else of Neko Grid. C++23, MIT license.

`MarkdownPreview` is a widget that renders a Markdown document:

- CommonMark with tables, footnotes, definition lists, sub / superscript, GitHub emoji
  shortcodes, heading anchors and `[ToC]`;
- LaTeX formulas (`$…$`, `$$…$$`) and Mermaid diagrams (` ```mermaid `);
- images relative to the document's folder;
- light or dark from the widget's palette;
- optional **chips** above the document and the application's own **link schemes**.

It is built to show documents that may come from someone else: raw HTML is not
rendered, Mermaid runs in its strict mode, the page loads nothing from the network and
no `file:` URLs, images come from the document's folder only, and a link with an
unknown scheme goes nowhere. The page keeps no cache, cookies or storage on disk.

| Class | What it is |
|---|---|
| `MarkdownPreview` | The widget, behind an interface; `createMarkdownPreview()` makes the Qt WebEngine one |
| `MarkdownPreviewContent` | What it shows: the Markdown, its folder, chips, link schemes |
| `MarkdownPage` | The hardened `QWebEnginePage` underneath, for rendering without the widget: printing a PDF (`printToPdf`), or getting the rendered HTML with every diagram and formula as a picture (`requestExportContent()`) |

Everything is in the namespace `nekogrid::markdown`. Each header documents its class;
that is the reference.

For the preview beside an editor of its source -- one widget with both, view modes and
an outline -- see [`nekogrid_markdown_editor`](../markdown_editor/README.md), which
builds on this library and on the [text editor](../editor/README.md).

## Using it

Copy this directory (or add the repository as a submodule) and add it to your build:

```cmake
add_subdirectory(third_party/markdown)        # wherever this directory lives
target_link_libraries(myapp PRIVATE nekogrid_markdown)
```

Headers are included with the directory's name, `#include "markdown/markdown_preview.hpp"`;
the target adds the directory's parent to the include path. Keep the directory named
`markdown`.

Qt WebEngine needs two things **before the `QApplication` exists**:

```cpp
#include "markdown/markdown_preview.hpp"

namespace md = nekogrid::markdown;

int main(int argc, char* argv[]) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    md::registerMarkdownPreviewScheme();      // or relative images do not show
    QApplication app(argc, argv);

    md::MarkdownPreview* preview = md::createMarkdownPreview(nullptr);
    preview->setContent({
        .body = QStringLiteral("# Hello\n\nSee [the ticket](issue:42).\n"),
        .baseDirectory = QStringLiteral("/path/to/the/document's/folder"),
        .chips = {{.text = QStringLiteral("Project"), .value = QStringLiteral("Demo")}},
        .linkSchemes = {{.scheme = QStringLiteral("issue"), .marker = QStringLiteral("# ")}},
    });
    QObject::connect(preview, &md::MarkdownPreview::customLinkActivated, [](const QString& url) {
        // "issue:42" -- open it your way
    });
    preview->show();
    return app.exec();
}
```

Rendering is asynchronous: `setContent()` returns at once, `renderFinished()` follows.
`scrollToSourceLine()` brings the block rendered from a line of the file to the top,
for a preview that follows an editor.

## Links

| A link to… | does |
|---|---|
| `http:`, `https:`, `mailto:` | opens in the system browser / mail program |
| one of the content's `linkSchemes` | `customLinkActivated(url)`; drawn apart from web links, with the scheme's marker |
| another Markdown file, relative (`[x](other.md)`) | `documentLinkActivated(path)` |
| a heading (`#section`) | scrolls there |
| anything else | nothing |

## What is downloaded at configure time

`CMakeLists.txt` fetches the page's JavaScript libraries with `FetchContent`, each
pinned by URL and SHA-256, and compiles them into the library. Nothing is fetched at
run time. An application that ships this library ships them, and owes their notices:

| Library | Version | License |
|---|---|---|
| markdown-it | 14.3.3 | MIT |
| markdown-it-sub, -sup, -footnote, -deflist, -emoji | 2.0.0, 2.0.0, 4.0.0, 3.0.0, 3.0.0 | MIT |
| KaTeX | 0.16.11 | MIT |
| Mermaid | 11.17.2 | MIT (bundles further MIT / BSD / Apache-2.0 code) |
| MathJax | 3.2.2 | Apache-2.0 |

Neko Grid's own notices are in `resources/licenses/THIRD_PARTY_NOTICES.txt`.

## Tests

`tests/test_markdown.cpp` in the Neko Grid repository is the library's whole test
suite. It links this library and Qt Test only, includes nothing else from the
repository, and runs the real page in Qt WebEngine:

```bash
QT_QPA_PLATFORM=offscreen ./build/test_markdown
```

## Rules for changes here

- No `#include` from outside this directory except Qt and the standard library.
  `test_markdown` links the library alone, so a dependency on the application fails to
  link there.
- The library renders Markdown. What a document's metadata means, and what a custom
  link refers to, is the application's: it comes in as chips and link schemes.
- **Never let an unknown scheme navigate.** Every link click is decided in
  `page/preview.js`; Chromium hands unknown schemes to the operating system.
- The page (`page/preview.html`, `.css`, `.js`) is compiled in: edit and rebuild.
- A new or updated JavaScript library: pin it in `CMakeLists.txt`, add it to
  `markdown_preview.qrc.in` and `page/preview.html`, and to the table above.
