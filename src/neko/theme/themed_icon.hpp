#pragma once

#include <QIcon>
#include <QString>

namespace neko::theme {

/// The UI icon called `name` in the application's icon set. The logical names are the
/// left-hand sides of the application's icon map ("new-note", "settings", ...), which
/// the CMake function neko_add_icon_set() compiles in under ":/neko/icons/<name>.svg"
/// (cmake/NekoIconSet.cmake). Code never names a file of a particular set, so the set
/// can be swapped at build time.
///
/// Use these instead of an emoji at the start of a label: an emoji is drawn by whatever
/// font the platform falls back to, whose size and line height differ per glyph and per
/// system, so labels in one row stop lining up.
///
/// The SVG is drawn at paint time in the application palette's ButtonText colour
/// (HighlightedText when selected, the disabled colour when disabled) -- every
/// `currentColor` in it is replaced -- so the icon follows a theme or light / dark
/// switch without being recreated, and it is sharp at any size and device-pixel ratio.
/// An unknown name gives a null QIcon and a warning, so the label shows its text alone
/// rather than a blank square.
///
/// UI thread: painting reads QGuiApplication::palette().
[[nodiscard]] QIcon themedIcon(const QString& name);

/// Whether the application's icon set has `name` -- for a provider that asks without
/// warning, or a test that the set has every name the code uses.
[[nodiscard]] bool hasThemedIcon(const QString& name);

} // namespace neko::theme
