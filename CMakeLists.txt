# ---------------------------------------------------------------------------
# NekoControls: Qt Widgets controls of the Neko family, as three static libraries.
#
#   neko::editor           text editor controls (src/neko/editor)          Qt Widgets
#   neko::markdown         Markdown preview on Qt WebEngine (src/neko/markdown)
#   neko::markdown_editor  the two as one Markdown document editor (src/neko/markdown_editor)
#   neko::theme            themes, themed icons, responsive tool bar (src/neko/theme)
#
# and the CMake function neko_add_icon_set() (cmake/NekoIconSet.cmake), which compiles
# an application's icons in for neko::theme::themedIcon().
#
# Use it from another project:
#
#     include(FetchContent)
#     FetchContent_Declare(neko_controls URL ... URL_HASH SHA256=...)
#     FetchContent_MakeAvailable(neko_controls)
#     target_link_libraries(app PRIVATE neko::markdown_editor)
#
# or add_subdirectory(path/to/NekoControls). Headers are included as
# "neko/<library>/<name>.hpp". Every library links Qt only; none reads settings or
# loads anything the application has not handed it.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.24)

project(NekoControls
    VERSION 0.6.0
    DESCRIPTION "Qt Widgets controls of the Neko applications"
    LANGUAGES CXX
)

option(NEKO_CONTROLS_BUILD_TESTS "Build the NekoControls test suites" ${PROJECT_IS_TOP_LEVEL})

if(NOT TARGET Qt6::WebEngineWidgets OR NOT TARGET Qt6::Svg)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets WebEngineWidgets Svg)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/NekoIconSet.cmake)

add_subdirectory(src/neko/editor)
add_subdirectory(src/neko/markdown)
add_subdirectory(src/neko/markdown_editor)
add_subdirectory(src/neko/theme)

if(NEKO_CONTROLS_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()

    # Each suite links its library ALONE: that is what proves the library stands on
    # its own. All need a platform (QT_QPA_PLATFORM=offscreen will do); the Markdown
    # suites run the real page in Qt WebEngine.
    foreach(_neko_suite editor markdown markdown_editor theme)
        add_executable(test_${_neko_suite} tests/test_${_neko_suite}.cpp)
        set_target_properties(test_${_neko_suite} PROPERTIES
            AUTOMOC ON
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
        target_link_libraries(test_${_neko_suite} PRIVATE neko::${_neko_suite} Qt6::Test)
        if(MSVC)
            target_compile_options(test_${_neko_suite} PRIVATE /utf-8 /permissive- /W4)
        else()
            target_compile_options(test_${_neko_suite} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME test_${_neko_suite} COMMAND test_${_neko_suite})
    endforeach()

    # test_theme needs icons of its own to draw.
    neko_add_icon_set(test_theme MAP ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_theme_icons.txt)
endif()
