# ---------------------------------------------------------------------------
# nekogrid_editor: the text editor controls -- CodeEditor (gutter, column selection),
# EditorFindBar, MarkdownEditor (emoji picker), MarkdownHighlighter and
# MarkdownTableDialog, with the widget-free logic they rest on.
#
# Links Qt Core, Gui and Widgets ONLY, and includes nothing outside this directory:
# no src/core, no src/ui, no settings, no icon files. That is what lets another
# project take this directory as it is (see README.md):
#
#     add_subdirectory(path/to/editor)
#     target_link_libraries(app PRIVATE nekogrid_editor)
#
# In Neko Grid, tests/test_editor.cpp links this library alone, so an include of the
# application's code from here fails to link there.
#
# Headers are included as "editor/<name>.hpp": the include root is this directory's
# parent, whatever it is called where the directory lives.
# ---------------------------------------------------------------------------
if(NOT TARGET Qt6::Widgets)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
endif()

add_library(nekogrid_editor STATIC
    block_selection.hpp
    block_selection.cpp
    text_search.hpp
    text_search.cpp
    emoji_shortcodes.hpp
    emoji_shortcodes.cpp
    emoji_shortcodes_data.inc
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
)

# Its own settings rather than the including project's, so it builds the same in any.
set_target_properties(nekogrid_editor PROPERTIES
    AUTOMOC ON
    AUTORCC OFF
    AUTOUIC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_compile_features(nekogrid_editor PUBLIC cxx_std_23)

get_filename_component(_nekogrid_editor_include_root ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
target_include_directories(nekogrid_editor PUBLIC ${_nekogrid_editor_include_root})

target_link_libraries(nekogrid_editor PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

if(MSVC)
    # /utf-8: the sources hold non-ASCII literals (the emoji table, the Find bar's
    # captions); without it MSVC reads them in the ANSI code page.
    target_compile_options(nekogrid_editor PRIVATE /utf-8 /permissive- /W4)
else()
    target_compile_options(nekogrid_editor PRIVATE -Wall -Wextra -Wpedantic)
endif()
