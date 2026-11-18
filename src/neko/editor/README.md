# neko::editor — text editor controls for Qt Widgets

The text editor of Neko Grid, as a small static library that other Qt 6 projects can
use as it is. It links Qt Core, Gui and Widgets and nothing else: no settings, no icon
files, nothing of an application. C++23, MIT license. Part of
[NekoControls](../../../README.md).

| Class | What it is |
|---|---|
| `CodeEditor` | A `QPlainTextEdit` with a line-number gutter, current-line highlight, column (box) selection (Alt + drag, Alt+Shift+arrows), a line-wrap toggle (Alt+Z), overwrite mode with a signal, an undoable whole-text replace, and inline suggestions ("ghost text": Tab accepts, Ctrl+Right the next word, Esc dismisses) |
| `EditorFindBar` | Find / Replace for a `CodeEditor` or any `QPlainTextEdit`: Ctrl+F / Ctrl+H, F3, match case, whole word, regular expressions with `$1`, highlighted matches, undoable Replace All |
| `MarkdownEditor` | A `CodeEditor` with a GitHub emoji picker (`:` opens it, typing narrows it) and link completion: with a `MarkdownLinkTargetProvider` set, Ctrl+Space in `[text](` lists the files the application offers, and `#` after one its headings |
| `MarkdownHighlighter` | A dependency-free `QSyntaxHighlighter` for Markdown, light or dark |
| `MarkdownTableDialog` | A dialog that builds a Markdown table: columns, alignment, widths, empty rows |
| `EditorDefaults` | The font and line wrapping every editor starts with and follows |

The logic under the widgets is widget-free and usable by itself: `block_selection`
(column-selection arithmetic), `text_search` (`findAll`, `replacementFor`),
`emoji_shortcodes`, `markdown_link_target` (`markdownLinkTargetQueryAt`,
`matchMarkdownLinkTargets`), `markdown_table` (`buildMarkdownTable`). Each header
documents its class; that is the reference.

Everything is in the namespace `neko::editor`.

For a whole Markdown document editor -- this library's `MarkdownEditor` beside a
rendered preview, with an outline and a tool bar -- see
[`neko::markdown_editor`](../markdown_editor/README.md), which builds on this
library and on the [preview](../markdown/README.md).

## Using it

Add [NekoControls](../../../README.md) to your build -- with `FetchContent` or
`add_subdirectory` -- and link the target:

```cmake
include(FetchContent)
FetchContent_Declare(neko_controls
    URL      https://github.com/fhuber83/NekoControls/releases/download/v0.2.0/NekoControls-0.2.0.tar.gz
    URL_HASH SHA256=...)   # the exact line is in the release notes
FetchContent_MakeAvailable(neko_controls)
target_link_libraries(myapp PRIVATE neko::editor)
```

Headers are included as `#include "neko/editor/<name>.hpp"`.

```cpp
#include "neko/editor/code_editor.hpp"
#include "neko/editor/editor_find_bar.hpp"

namespace ed = neko::editor;

auto* pane = new QWidget;
auto* layout = new QVBoxLayout(pane);
auto* editor = new ed::CodeEditor(pane);
auto* findBar = new ed::EditorFindBar(
    editor, [editor](const QList<QTextEdit::ExtraSelection>& h) { editor->setSearchHighlights(h); }, pane);
layout->addWidget(findBar);            // starts hidden
layout->addWidget(editor, 1);
findBar->installShortcuts(pane);       // Ctrl+F, Ctrl+H, F3 while the focus is in `pane`
```

Syntax highlighting is not part of `CodeEditor`: attach any `QSyntaxHighlighter` to
`editor->document()`.

### Inline suggestions

`CodeEditor` shows text proposed by a completion engine (GitHub Copilot, a snippet
engine, ...) after the caret in the placeholder colour. The engine stays in the
application; the editor only shows, accepts and dismisses:

```cpp
editor->setInlineSuggestion(QStringLiteral("a + b;\n}"));   // at the caret
connect(editor, &ed::CodeEditor::inlineSuggestionAccepted, [](qsizetype length, bool complete) {
    // `length`: how much of the suggestion is now in the document, from its start
});
connect(editor, &ed::CodeEditor::inlineSuggestionDismissed, [] { /* ... */ });
```

The suggestion is not part of the document (no undo step, not modified, invisible to
highlighters and Find); the lines of a multi-line one push the lines below down. Tab
accepts it, Ctrl+Right its next word, each as one undo step; typing what it starts with
eats into it; Esc, other edits, caret moves, clicks and focus loss dismiss it. It is
drawn as the pre-edit area of the line's `QTextLayout`, so an input method's own pre-edit
text dismisses it.

## What the application provides

Both are optional and process-wide; set them up before the first editor is built.

**Font and line wrapping.** The library stores nothing. `EditorDefaults::instance()`
holds the font (the system's fixed-width font at 11 pt until set) and whether long lines
wrap (on until set). Load your own setting into it at start and set it again when the
user changes it; every open editor follows.

```cpp
ed::EditorDefaults::instance().setFont(myStoredFont);
ed::EditorDefaults::instance().setLineWrap(false);
```

**Icons.** The library ships no images. It asks for seven icons by name
(`editorIconNames()`): `line-wrap` for `CodeEditor::lineWrapAction()`, and
`find-previous`, `find-next`, `match-case`, `whole-word`, `regex`, `close` for the Find
bar. Without a provider — or where it returns a null icon — the action has its text
alone and the Find bar's buttons show short captions.

```cpp
ed::setEditorIconProvider([](const QString& name) { return QIcon(":/icons/" + name + ".svg"); });
```

Colours come from the widget's palette, so the controls follow the application's theme.

## Translations

The user-visible strings go through `tr()` in the contexts `neko::editor::CodeEditor`,
`neko::editor::EditorFindBar` and `neko::editor::MarkdownTableDialog`. The library
carries German, Bavarian and Italian translations (`translations/neko_editor_*.ts`,
compiled in); the application picks the language and installs them:

```cpp
#include "neko/editor/editor_translations.hpp"

static QTranslator editorTranslator;
if (ed::loadEditorTranslation(editorTranslator, QStringLiteral("de"))) {
    QCoreApplication::installTranslator(&editorTranslator);
}
```

## Tests

`tests/test_editor.cpp` is the library's whole test suite. It links this library and
Qt Test only:

```bash
QT_QPA_PLATFORM=offscreen ./build/test_editor
```

## Rules for changes here

- No `#include` from outside this directory except Qt and the standard library.
  `test_editor` links the library alone, so a dependency on the application fails to
  link there.
- No `QSettings`, no resource files but its own translations, no application names.
  What the library needs from an application comes in through `EditorDefaults` and the
  icon provider.
- `emoji_shortcodes_data.inc` is generated: `scripts/generate-emoji-shortcodes.py`.
