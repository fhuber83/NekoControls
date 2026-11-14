# ---------------------------------------------------------------------------
# nekogrid_markdown: a Markdown preview widget on Qt WebEngine -- MarkdownPreview and
# the hardened MarkdownPage under it -- with its page (page/) and the JavaScript
# libraries the page renders with compiled in. Nothing is loaded from the network at
# run time; these pinned, checksummed downloads are the only copies it uses.
#
# Links Qt Core, Gui, Widgets and WebEngineWidgets ONLY, and includes nothing outside
# this directory, so another project can take the directory as it is (see README.md):
#
#     add_subdirectory(path/to/markdown)
#     target_link_libraries(app PRIVATE nekogrid_markdown)
#
# In Neko Grid, tests/test_markdown.cpp links this library alone, so an include of the
# application's code from here fails to link there.
#
# Headers are included as "markdown/<name>.hpp": the include root is this directory's
# parent, whatever it is called where the directory lives.
# ---------------------------------------------------------------------------
if(NOT TARGET Qt6::WebEngineWidgets)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets WebEngineWidgets)
endif()

# The page's libraries, their single-file browser builds. All MIT except MathJax
# (Apache-2.0); an application that ships this library ships them and owes their
# notices (README.md lists them). markdown-it 15 ships no browser build, hence the
# maintained 14.x line.
#
# Neko Grid's emoji picker (src/editor) lists the shortcodes of the markdown-it-emoji
# pinned here: after changing that pin, run scripts/generate-emoji-shortcodes.py.
include(FetchContent)
FetchContent_Declare(
    nekogrid_markdown_it
    URL      https://cdn.jsdelivr.net/npm/markdown-it@14.3.3/dist/markdown-it.min.js
    URL_HASH SHA256=494ba53db1aac9f676317dda7062bd9545b5f0bbe3f12b0ab1ab4f813825cfb0
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_Declare(
    nekogrid_markdown_it_sub
    URL      https://cdn.jsdelivr.net/npm/markdown-it-sub@2.0.0/dist/markdown-it-sub.min.js
    URL_HASH SHA256=fffa57742043f3012ee57a4d5b337f04222a459e7a8d0892a9755b5c6b16bca8
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_Declare(
    nekogrid_markdown_it_sup
    URL      https://cdn.jsdelivr.net/npm/markdown-it-sup@2.0.0/dist/markdown-it-sup.min.js
    URL_HASH SHA256=6c81de84d54484f1a37c58277db9cc0a92623253159e360ae99246ff08203236
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_Declare(
    nekogrid_markdown_it_footnote
    URL      https://cdn.jsdelivr.net/npm/markdown-it-footnote@4.0.0/dist/markdown-it-footnote.min.js
    URL_HASH SHA256=d6fee58a3b56c5742fa18f3e01f1d317cc99975683ebd39c9195cb2aff0c2e42
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_Declare(
    nekogrid_markdown_it_deflist
    URL      https://cdn.jsdelivr.net/npm/markdown-it-deflist@3.0.0/dist/markdown-it-deflist.min.js
    URL_HASH SHA256=ab393099a725e532d63bf877b4c6a5c9c57b87bf8d8d467e5c3a8d8ced33692e
    DOWNLOAD_NO_EXTRACT TRUE
)
# The note editor's emoji picker lists this build's shortcodes: after changing the pin,
# run scripts/generate-emoji-shortcodes.py (src/editor/emoji_shortcodes_data.inc).
FetchContent_Declare(
    nekogrid_markdown_it_emoji
    URL      https://cdn.jsdelivr.net/npm/markdown-it-emoji@3.0.0/dist/markdown-it-emoji.min.js
    URL_HASH SHA256=84486c9d5a51b28aa90c5a3cb2625473dbde2415e2e106f8f40ea617de66dae0
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_Declare(
    nekogrid_katex
    URL      https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.js
    URL_HASH SHA256=e6bfe5deebd4c7ccd272055bab63bd3ab2c73b907b6e6a22d352740a81381fd4
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_Declare(
    nekogrid_mermaid
    URL      https://cdn.jsdelivr.net/npm/mermaid@11.17.2/dist/mermaid.min.js
    URL_HASH SHA256=581ed7d74bd9048d0e3a91363927d72ef22942d7722546b27f7cc29e35390eb8
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_Declare(
    nekogrid_mathjax
    URL      https://cdn.jsdelivr.net/npm/mathjax@3.2.2/es5/tex-svg.js
    URL_HASH SHA256=d4295dc33744836935c1399feece5159577b34c5c8ffb9f1c6324cd82e03a882
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(
    nekogrid_markdown_it
    nekogrid_markdown_it_sub
    nekogrid_markdown_it_sup
    nekogrid_markdown_it_footnote
    nekogrid_markdown_it_deflist
    nekogrid_markdown_it_emoji
    nekogrid_katex
    nekogrid_mermaid
    nekogrid_mathjax
)
set(NEKOGRID_MARKDOWN_IT_JS ${nekogrid_markdown_it_SOURCE_DIR}/markdown-it.min.js)
set(NEKOGRID_MARKDOWN_IT_SUB_JS ${nekogrid_markdown_it_sub_SOURCE_DIR}/markdown-it-sub.min.js)
set(NEKOGRID_MARKDOWN_IT_SUP_JS ${nekogrid_markdown_it_sup_SOURCE_DIR}/markdown-it-sup.min.js)
set(NEKOGRID_MARKDOWN_IT_FOOTNOTE_JS ${nekogrid_markdown_it_footnote_SOURCE_DIR}/markdown-it-footnote.min.js)
set(NEKOGRID_MARKDOWN_IT_DEFLIST_JS ${nekogrid_markdown_it_deflist_SOURCE_DIR}/markdown-it-deflist.min.js)
set(NEKOGRID_MARKDOWN_IT_EMOJI_JS ${nekogrid_markdown_it_emoji_SOURCE_DIR}/markdown-it-emoji.min.js)
set(NEKOGRID_KATEX_JS ${nekogrid_katex_SOURCE_DIR}/katex.min.js)
set(NEKOGRID_MERMAID_JS ${nekogrid_mermaid_SOURCE_DIR}/mermaid.min.js)
set(NEKOGRID_MATHJAX_JS ${nekogrid_mathjax_SOURCE_DIR}/tex-svg.js)
set(NEKOGRID_MARKDOWN_IT_JS ${nekogrid_markdown_it_SOURCE_DIR}/markdown-it.min.js)
set(NEKOGRID_MARKDOWN_IT_SUB_JS ${nekogrid_markdown_it_sub_SOURCE_DIR}/markdown-it-sub.min.js)
set(NEKOGRID_MARKDOWN_IT_SUP_JS ${nekogrid_markdown_it_sup_SOURCE_DIR}/markdown-it-sup.min.js)
set(NEKOGRID_MARKDOWN_IT_FOOTNOTE_JS ${nekogrid_markdown_it_footnote_SOURCE_DIR}/markdown-it-footnote.min.js)
set(NEKOGRID_MARKDOWN_IT_DEFLIST_JS ${nekogrid_markdown_it_deflist_SOURCE_DIR}/markdown-it-deflist.min.js)
set(NEKOGRID_MARKDOWN_IT_EMOJI_JS ${nekogrid_markdown_it_emoji_SOURCE_DIR}/markdown-it-emoji.min.js)
set(NEKOGRID_KATEX_JS ${nekogrid_katex_SOURCE_DIR}/katex.min.js)
set(NEKOGRID_MERMAID_JS ${nekogrid_mermaid_SOURCE_DIR}/mermaid.min.js)
set(NEKOGRID_MATHJAX_JS ${nekogrid_mathjax_SOURCE_DIR}/tex-svg.js)
set(NEKOGRID_MARKDOWN_PAGE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/page)
# The resource file names the downloads by their place in the build tree, so it is
# generated; AUTORCC compiles it into the library (":/markdown/...").
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/markdown_preview.qrc.in
    ${CMAKE_CURRENT_BINARY_DIR}/markdown_preview.qrc
    @ONLY
)

add_library(nekogrid_markdown STATIC
    markdown_preview.hpp
    markdown_preview.cpp
    markdown_page.hpp
    markdown_page.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/markdown_preview.qrc
)

# Its own settings rather than the including project's, so it builds the same in any.
set_target_properties(nekogrid_markdown PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_compile_features(nekogrid_markdown PUBLIC cxx_std_23)

get_filename_component(_nekogrid_markdown_include_root ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
target_include_directories(nekogrid_markdown PUBLIC ${_nekogrid_markdown_include_root})

target_link_libraries(nekogrid_markdown PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::WebEngineWidgets
)

if(MSVC)
    target_compile_options(nekogrid_markdown PRIVATE /utf-8 /permissive- /W4)
else()
    target_compile_options(nekogrid_markdown PRIVATE -Wall -Wextra -Wpedantic)
endif()
