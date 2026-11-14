# neko::markdown_editor — a Markdown document editor for Qt Widgets

The note editor of Neko Grid without the notes: one widget that edits a Markdown
document, source and rendering side by side. A small static library that other Qt 6
projects can use as it is. It is made of the two libraries beside it —
[`neko::editor`](../editor/README.md) (the source editor) and
[`neko::markdown`](../markdown/README.md) (the preview on Qt WebEngine) — and links
nothing else. C++23, MIT license. Part of [NekoControls](../../../README.md).

| Class | What it is |
|---|---|
| `MarkdownDocumentEditor` | The widget: source editor with Find / Replace, emoji picker and highlighting; preview; three views (Source, Split, Preview); an outline of the headings; "Table..." and line-wrap actions; an optional tool bar |
| `MarkdownOutline` | The headings of a document as a tree; a click says where the heading is |
| `markdown_structure` | Widget-free: `frontMatterLineCount()`, `extractMarkdownHeadings()` |

Everything is in the namespace `neko::markdown_editor`. Each header documents its
class; that is the reference.

If you want only the text editor, link `neko::editor`; only the preview, `neko::markdown`. This
library is for both together.

## Using it

Add [NekoControls](../../../README.md) to your build -- with `FetchContent` or
`add_subdirectory` -- and link the target:

```cmake
include(FetchContent)
FetchContent_Declare(neko_controls
    URL      https://github.com/fhuber83/NekoControls/releases/download/v0.1.0/NekoControls-0.1.0.tar.gz
    URL_HASH SHA256=f8b70ca509f770f9c3d23efd43ee088a0347fc332a1c9ac0d4d832ba75bedb9f)
FetchContent_MakeAvailable(neko_controls)
target_link_libraries(myapp PRIVATE neko::markdown_editor)
```

Headers are included as `#include "neko/markdown_editor/<name>.hpp"`.

The preview is Qt WebEngine, which needs two things **before the `QApplication`
exists** (see `markdown/README.md`):

```cpp
#include "neko/markdown_editor/markdown_document_editor.hpp"

namespace mde = neko::markdown_editor;

int main(int argc, char* argv[]) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    neko::markdown::registerMarkdownPreviewScheme();
    QApplication app(argc, argv);

    mde::MarkdownDocumentEditor editor;
    editor.setBaseDirectory(QStringLiteral("/path/to/the/document's/folder"));
    editor.setText(QStringLiteral("# Hello\n\nSome *Markdown*.\n"));
    editor.show();
    return app.exec();
}
```

That is a complete editor: a tool bar with the three views, "Table...", "Wrap" and
"Outline", Ctrl+Shift+M to cycle the views, Ctrl+F / Ctrl+H in the source.

## The document is text, the file is yours

The widget holds text and knows no files. Opening is `setText()`, which leaves the
document unmodified; saving is `text()` and then `setModified(false)`. The source's
`QTextDocument` (`editor()->document()`) tells you about changes
(`modificationChanged()`). The one thing the widget wants to know about the file is its
folder, `setBaseDirectory()`: relative images and links resolve against it.

## What the application can add

**Its own tool bar.** Build the widget with `MarkdownToolBar::None` and put its actions
where you like: `sourceAction()`, `splitAction()`, `previewAction()`, `tableAction()`,
`lineWrapAction()`, `outlineAction()`. Or keep the built-in bar and add to it:
`editor.toolBar()->addAction(mySaveAction)`.

**What the preview shows.** A content provider builds the preview's content from the
source text; the default one leaves a YAML front matter (`---` … `---` at the top) out
of the rendering and adds nothing. Replace it to add chips above the document, a title
or link schemes of your own:

```cpp
editor.setContentProvider([](const QString& text, const QString& folder) {
    auto content = mde::defaultMarkdownContent(text, folder);
    content.linkSchemes = {{.scheme = QStringLiteral("issue"), .marker = QStringLiteral("# ")}};
    return content;
});
QObject::connect(&editor, &mde::MarkdownDocumentEditor::customLinkActivated, [](const QString& url) {
    // "issue:42" -- open it your way
});
```

**The view to start in.** The widget stores nothing. `viewModeChosen()` says the user
picked a view; store it and call `setViewMode()` on the next widget you build.

**Icons, font, line wrapping.** These come through the editor library:
`editor::setEditorIconProvider()` is asked for this library's icons too
(`markdownEditorIconNames()`: `note-source`, `note-split`, `preview`, `insert-table`,
`line-wrap`, `outline`), and `editor::EditorDefaults` holds the source's font and
wrapping. Without a provider the actions show their text alone.

**Another highlighter.** `setHighlighter()` replaces the built-in Markdown highlighter
with any `QSyntaxHighlighter`.

## Translations

The user-visible strings go through `tr()` in the context
`neko::markdown_editor::MarkdownDocumentEditor`, and the editor library's contexts for
its parts. Each library carries German, Bavarian and Italian translations, compiled in;
install both: `loadMarkdownEditorTranslation()` (`markdown_editor_translations.hpp`)
and `neko::editor::loadEditorTranslation()`.

## Tests

`tests/test_markdown_editor.cpp` is the library's whole test suite. It links this
library and Qt Test only, and uses the real preview:

```bash
QT_QPA_PLATFORM=offscreen ./build/test_markdown_editor
```

## Rules for changes here

- No `#include` from outside this directory except `neko/editor/`, `neko/markdown/`, Qt and the
  standard library. `test_markdown_editor` links the library alone, so a dependency on
  the application fails to link there.
- No `QSettings`, no files, no resource files but its own translations, no application names. The widget holds
  a text; what an application remembers or stores, it does itself.
- What a document's front matter means, and what a custom link refers to, is the
  application's: it comes in through the content provider.
- Nothing here goes into `neko/editor/` or `neko/markdown/` that would make one need the other:
  a project may take either alone.
