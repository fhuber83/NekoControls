# ---------------------------------------------------------------------------
# nekogrid_markdown_editor: a Markdown document editor -- MarkdownDocumentEditor, the
# source editor of nekogrid_editor beside the preview of nekogrid_markdown, with view
# modes, an outline of the headings (MarkdownOutline) and the tool bar actions.
#
# Links nekogrid_editor and nekogrid_markdown (and through them Qt Core, Gui, Widgets
# and WebEngineWidgets) ONLY, and includes nothing else: no src/core, no src/ui, no
# settings, no files. Another project takes the three directories as they are (see
# README.md):
#
#     add_subdirectory(path/to/markdown_editor)   # adds editor/ and markdown/ beside it
#     target_link_libraries(app PRIVATE nekogrid_markdown_editor)
#
# In Neko Grid, tests/test_markdown_editor.cpp links this library alone, so an include
# of the application's code from here fails to link there.
#
# Headers are included as "markdown_editor/<name>.hpp": the include root is this
# directory's parent, whatever it is called where the directory lives.
# ---------------------------------------------------------------------------

# The two libraries this one is made of. A project that added them itself is taken at
# its word; otherwise they are expected beside this directory, as in Neko Grid.
foreach(_nekogrid_sibling editor markdown)
    if(NOT TARGET nekogrid_${_nekogrid_sibling})
        if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../${_nekogrid_sibling}/CMakeLists.txt)
            message(FATAL_ERROR
                "nekogrid_markdown_editor needs nekogrid_${_nekogrid_sibling}: put its directory "
                "'${_nekogrid_sibling}' beside this one, or add it before this directory.")
        endif()
        add_subdirectory(
            ${CMAKE_CURRENT_SOURCE_DIR}/../${_nekogrid_sibling}
            ${CMAKE_CURRENT_BINARY_DIR}/../${_nekogrid_sibling}
        )
    endif()
endforeach()

add_library(nekogrid_markdown_editor STATIC
    markdown_structure.hpp
    markdown_structure.cpp
    markdown_outline.hpp
    markdown_outline.cpp
    markdown_document_editor.hpp
    markdown_document_editor.cpp
)

# Its own settings rather than the including project's, so it builds the same in any.
set_target_properties(nekogrid_markdown_editor PROPERTIES
    AUTOMOC ON
    AUTORCC OFF
    AUTOUIC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_compile_features(nekogrid_markdown_editor PUBLIC cxx_std_23)

get_filename_component(_nekogrid_markdown_editor_include_root ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
target_include_directories(nekogrid_markdown_editor PUBLIC ${_nekogrid_markdown_editor_include_root})

target_link_libraries(nekogrid_markdown_editor PUBLIC
    nekogrid_editor
    nekogrid_markdown
)

if(MSVC)
    target_compile_options(nekogrid_markdown_editor PRIVATE /utf-8 /permissive- /W4)
else()
    target_compile_options(nekogrid_markdown_editor PRIVATE -Wall -Wextra -Wpedantic)
endif()
