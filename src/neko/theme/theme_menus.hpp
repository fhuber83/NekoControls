#pragma once

class QMenu;
class QWidget;

namespace neko::theme {

/// Fills `menu` with one checkable entry per theme (ThemeManager::themeModes(), "System"
/// after a separator). Choosing one switches ThemeManager and shows its restart notice
/// (ThemeManager::promptRestartNotice, parented to `dialogParent`); the check follows
/// ThemeManager::themeChanged, from wherever the theme changes. The application creates
/// the menu -- title, icon, place -- and this fills it.
///
/// UI thread only.
void populateThemeMenu(QMenu* menu, QWidget* dialogParent);

/// The same for the widget styles this platform can show
/// (ThemeManager::availableStyles()).
void populateStyleMenu(QMenu* menu, QWidget* dialogParent);

} // namespace neko::theme
