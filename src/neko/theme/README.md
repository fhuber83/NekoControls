# neko::theme — themes, themed icons and responsive tool bars for Qt Widgets

The look of the Neko applications (Neko Grid, Neko Note) as a small static library. It
links Qt Core, Gui, Widgets and Svg and nothing else. C++23, MIT license. Part of
[NekoControls](../../../README.md).

| Piece | What it is |
|---|---|
| `ThemeManager` | Ten palettes — Light, Dark, High Contrast Dark / Light, Nord, Dracula, Solarized Dark / Light, Motif, System (follows the platform's light / dark) — every one checked for WCAG contrast; widget style plugins found and validated for this platform; the "restart recommended" notice; all of it remembered in `QSettings` |
| `populateThemeMenu()`, `populateStyleMenu()` | Fill a menu with the themes / styles as checkable entries that switch and follow changes |
| `themedIcon()` | An SVG icon drawn in the palette's colour at paint time, so it follows a theme switch and is sharp at any DPI |
| `ResponsiveToolBar` | A tool bar that drops captions (right to left, icon stays) before it hides any button when it gets narrow |
| `neko_add_icon_set()` | CMake: compiles an application's icon map into it — pinned Lucide by default |

Everything is in the namespace `neko::theme`. Each header documents its pieces.

## Using it

```cmake
FetchContent_MakeAvailable(neko_controls)   # see the top-level README
target_link_libraries(myapp PRIVATE neko::theme)
neko_add_icon_set(myapp MAP ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons.txt)
```

`resources/icons.txt` maps the names the code asks for to Lucide files:

```
# <logical name>=<Lucide file without .svg>
new-note=file-plus
settings=settings
```

The editor's and Markdown editor's own icons (`find-next`, `preview`, ...) are added
for you; your map can override them. Hand them to those libraries with
`neko::editor::setEditorIconProvider(&neko::theme::themedIcon)`.

At start-up, once the `QApplication` exists:

```cpp
#include "neko/theme/theme_manager.hpp"

namespace th = neko::theme;

auto& themes = th::ThemeManager::instance();
if (!themes.setStyle(th::ThemeManager::savedStyle())) {
    themes.setStyle(QStringLiteral("Fusion"));
}
themes.setThemeMode(th::ThemeManager::savedThemeMode());
```

and a View menu to choose:

```cpp
th::populateThemeMenu(viewMenu->addMenu(th::themedIcon("theme"), tr("&Theme")), this);
th::populateStyleMenu(viewMenu->addMenu(th::themedIcon("widget-style"), tr("Widget &Style")), this);
```

Tool bars:

```cpp
auto* bar = new th::ResponsiveToolBar(this);
bar->addLabeledAction(th::themedIcon("new-note"), tr("New Note"), this, &MainWindow::newNote);
bar->addLabeledAction(editor->previewAction());   // an action someone else owns
```

## What the application can set

- **Where the settings go**: `ThemeManager::setSettingsFactory()`; by default a plain
  `QSettings()` (the application's organization and name). Keys: `ui/theme`,
  `ui/style`, `ui/restart_notice_action`, `ui/suppress_restart_notice`.
- **The restarted process's environment**: `ThemeManager::setRestartEnvironmentHook()`,
  for an application with a single-instance lock that the new process must wait for.
- **The language**: German, Bavarian and Italian are compiled in;
  `loadThemeTranslation()` loads one into a `QTranslator` to install.

## Tests

`tests/test_theme.cpp` links this library alone, with icons of its own
(`tests/test_theme_icons.txt`).

## Rules for changes here

- No `#include` from outside this directory except Qt and the standard library.
- No application names, no settings file of its own: `QSettings` only through the
  factory.
- A new icon set: its `resources/icon_maps/controls-<set>.txt`, its pinned download in
  `cmake/NekoIconSet.cmake`, and its notice in `THIRD_PARTY_NOTICES.txt`.
