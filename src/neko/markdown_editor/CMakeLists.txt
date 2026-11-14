# ---------------------------------------------------------------------------
# neko_markdown_editor (alias neko::markdown_editor): a Markdown document editor --
# MarkdownDocumentEditor, the source editor of neko::editor beside the preview of
# neko::markdown, with view modes, an outline of the headings (MarkdownOutline) and
# the tool bar actions.
#
# Links neko::editor and neko::markdown (and through them Qt Core, Gui, Widgets and
# WebEngineWidgets) ONLY, and includes nothing else: no settings, no files, nothing of
# the application. tests/test_markdown_editor.cpp links this library alone.
#
# Headers are included as "neko/markdown_editor/<name>.hpp".
# ---------------------------------------------------------------------------

# The two libraries this one is made of. The top-level CMakeLists.txt of NekoControls
# adds them first.
foreach(_neko_sibling editor markdown)
    if(NOT TARGET neko_${_neko_sibling})
        message(FATAL_ERROR
            "neko_markdown_editor needs neko_${_neko_sibling}: add NekoControls through its "
            "top-level CMakeLists.txt, not this directory alone.")
    endif()
endforeach()

add_library(neko_markdown_editor STATIC
    markdown_structure.hpp
    markdown_structure.cpp
    markdown_outline.hpp
    markdown_outline.cpp
    markdown_document_editor.hpp
    markdown_document_editor.cpp
    markdown_editor_translations.hpp
    markdown_editor_translations.cpp
    neko_markdown_editor_translations.qrc
)
add_library(neko::markdown_editor ALIAS neko_markdown_editor)

# Its own settings rather than the including project's, so it builds the same in any.
set_target_properties(neko_markdown_editor PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_compile_features(neko_markdown_editor PUBLIC cxx_std_23)

cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH _neko_markdown_editor_include_root)
cmake_path(GET _neko_markdown_editor_include_root PARENT_PATH _neko_markdown_editor_include_root)
target_include_directories(neko_markdown_editor PUBLIC ${_neko_markdown_editor_include_root})

target_link_libraries(neko_markdown_editor PUBLIC
    neko_editor
    neko_markdown
)

if(MSVC)
    target_compile_options(neko_markdown_editor PRIVATE /utf-8 /permissive- /W4)
else()
    target_compile_options(neko_markdown_editor PRIVATE -Wall -Wextra -Wpedantic)
endif()
