# ---------------------------------------------------------------------------
# neko_editor (alias neko::editor): the text editor controls -- CodeEditor (gutter,
# column selection), EditorFindBar, MarkdownEditor (emoji picker), MarkdownHighlighter
# and MarkdownTableDialog, with the widget-free logic they rest on.
#
# Links Qt Core, Gui and Widgets ONLY, and includes nothing outside this directory:
# no settings, no icon files, nothing of the application. tests/test_editor.cpp links
# this library alone.
#
# Headers are included as "neko/editor/<name>.hpp".
# ---------------------------------------------------------------------------
if(NOT TARGET Qt6::Widgets)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
endif()

add_library(neko_editor STATIC
    block_selection.hpp
    block_selection.cpp
    text_search.hpp
    text_search.cpp
    emoji_shortcodes.hpp
    emoji_shortcodes.cpp
    emoji_shortcodes_data.inc
    markdown_link_target.hpp
    markdown_link_target.cpp
    markdown_table.hpp
    markdown_table.cpp
    editor_defaults.hpp
    editor_defaults.cpp
    editor_icons.hpp
    editor_icons.cpp
    code_editor.hpp
    code_editor.cpp
    editor_find_bar.hpp
    editor_find_bar.cpp
    markdown_editor.hpp
    markdown_editor.cpp
    markdown_highlighter.hpp
    markdown_highlighter.cpp
    markdown_table_dialog.hpp
    markdown_table_dialog.cpp
    editor_translations.hpp
    editor_translations.cpp
    neko_editor_translations.qrc
)
add_library(neko::editor ALIAS neko_editor)

# Its own settings rather than the including project's, so it builds the same in any.
set_target_properties(neko_editor PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_compile_features(neko_editor PUBLIC cxx_std_23)

cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH _neko_editor_include_root)
cmake_path(GET _neko_editor_include_root PARENT_PATH _neko_editor_include_root)
target_include_directories(neko_editor PUBLIC ${_neko_editor_include_root})

target_link_libraries(neko_editor PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

if(MSVC)
    # /utf-8: the sources hold non-ASCII literals (the emoji table, the Find bar's
    # captions); without it MSVC reads them in the ANSI code page.
    target_compile_options(neko_editor PRIVATE /utf-8 /permissive- /W4)
else()
    target_compile_options(neko_editor PRIVATE -Wall -Wextra -Wpedantic)
endif()
