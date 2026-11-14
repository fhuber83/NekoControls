# ---------------------------------------------------------------------------
# neko_theme (alias neko::theme): the look of a Neko application -- ThemeManager
# (palettes from Light to Solarized, widget style plugins, the restart notice),
# themedIcon() (SVG icons drawn in the palette's colours) and ResponsiveToolBar (drops
# captions before buttons when it gets narrow), and the View menus that pick a theme
# and a style (populateThemeMenu, populateStyleMenu). The icons themselves come from the
# application, through neko_add_icon_set() (cmake/NekoIconSet.cmake).
#
# Links Qt Core, Gui, Widgets and Svg ONLY. tests/test_theme.cpp links this library
# alone.
#
# Headers are included as "neko/theme/<name>.hpp".
# ---------------------------------------------------------------------------
if(NOT TARGET Qt6::Svg)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
endif()

add_library(neko_theme STATIC
    theme_manager.hpp
    theme_manager.cpp
    themed_icon.hpp
    themed_icon.cpp
    responsive_tool_bar.hpp
    responsive_tool_bar.cpp
    theme_menus.hpp
    theme_menus.cpp
    theme_translations.hpp
    theme_translations.cpp
    neko_theme_translations.qrc
)
add_library(neko::theme ALIAS neko_theme)

# Its own settings rather than the including project's, so it builds the same in any.
set_target_properties(neko_theme PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_compile_features(neko_theme PUBLIC cxx_std_23)

cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH _neko_theme_include_root)
cmake_path(GET _neko_theme_include_root PARENT_PATH _neko_theme_include_root)
target_include_directories(neko_theme PUBLIC ${_neko_theme_include_root})

target_link_libraries(neko_theme PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
)

if(MSVC)
    target_compile_options(neko_theme PRIVATE /utf-8 /permissive- /W4)
else()
    target_compile_options(neko_theme PRIVATE -Wall -Wextra -Wpedantic)
endif()
