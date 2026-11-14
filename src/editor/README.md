# nekogrid_editor — text editor controls for Qt Widgets

The text editor of [Neko Grid](../../README.md), as a small static library that other
Qt 6 projects can use as it is. It links Qt Core, Gui and Widgets and nothing else: no
settings, no icon files, no other part of Neko Grid. C++23, MIT license.

| Class | What it is |
|---|---|
| `CodeEditor` | A `QPlainTextEdit` with a line-number gutter, current-line highlight, column (box) selection (Alt + drag, Alt+Shift+arrows), a line-wrap toggle (Alt+Z), overwrite mode with a signal, and an undoable whole-text replace |
| `EditorFindBar` | Find / Replace for a `CodeEditor` or any `QPlainTextEdit`: Ctrl+F / Ctrl+H, F3, match case, whole word, regular expressions with `$1`, highlighted matches, undoable Replace All |
| `MarkdownEditor` | A `CodeEditor` with a GitHub emoji picker: `:` opens it, typing narrows it |
| `MarkdownHighlighter` | A dependency-free `QSyntaxHighlighter` for Markdown, light or dark |
| `MarkdownTableDialog` | A dialog that builds a Markdown table: columns, alignment, widths, empty rows |
| `EditorDefaults` | The font and line wrapping every editor starts with and follows |

The logic under the widgets is widget-free and usable by itself: `block_selection`
(column-selection arithmetic), `text_search` (`findAll`, `replacementFor`),
`emoji_shortcodes`, `markdown_table` (`buildMarkdownTable`). Each header documents its
class; that is the reference.

Everything is in the namespace `nekogrid::editor`.

## Using it

Copy this directory (or add the repository as a submodule) and add it to your build:

```cmake
add_subdirectory(third_party/editor)          # wherever this directory lives
target_link_libraries(myapp PRIVATE nekogrid_editor)
```

Headers are included with the directory's name, `#include "editor/code_editor.hpp"`; the
target adds the directory's parent to the include path. Keep the directory named
`editor`.

```cpp
#include "editor/code_editor.hpp"
#include "editor/editor_find_bar.hpp"

namespace ed = nekogrid::editor;

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

The user-visible strings go through `tr()` in the contexts
`nekogrid::editor::CodeEditor`, `nekogrid::editor::EditorFindBar` and
`nekogrid::editor::MarkdownTableDialog`. The library has no translation files of its
own; run `lupdate` over this directory with your project to translate them.

## Tests

`tests/test_editor.cpp` in the Neko Grid repository is the library's whole test suite.
It links this library and Qt Test only and includes nothing else from the repository,
so it can be taken along:

```bash
QT_QPA_PLATFORM=offscreen ./build/test_editor
```

## Rules for changes here

- No `#include` from outside this directory except Qt and the standard library.
  `test_editor` links the library alone, so a dependency on the application fails to
  link there.
- No `QSettings`, no resource files, no application names. What the library needs from
  an application comes in through `EditorDefaults` and the icon provider.
- `emoji_shortcodes_data.inc` is generated: `scripts/generate-emoji-shortcodes.py`.
