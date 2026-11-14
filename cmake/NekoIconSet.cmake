# ---------------------------------------------------------------------------
# neko_add_icon_set(<target> MAP <file> [SET lucide] [NO_CONTROL_ICONS])
#
# Compiles an application's UI icons into <target>, for neko::theme::themedIcon().
# The code asks for logical names only ("new-note"); <file> maps each to a file of the
# icon set, one `<logical name>=<file name without .svg>` per line (other lines are
# ignored). The set is downloaded once, pinned by SHA-256; only the mapped files are
# compiled in, as ":/neko/icons/<logical name>.svg".
#
# The icons NekoControls' own widgets ask for (resources/icon_maps/controls-<set>.txt)
# come first, so the editor's and Markdown editor's actions have theirs; the
# application's map may override any of them. NO_CONTROL_ICONS leaves them out.
#
# <target> may be a static library: Qt links the resources into whatever links it, and
# they register themselves -- no Q_INIT_RESOURCE needed.
#
# Sets:  lucide  Lucide 1.61.0 -- ISC (and MIT for the icons it took from Feather). An
#                application that ships it owes its notice (THIRD_PARTY_NOTICES.txt).
# ---------------------------------------------------------------------------
include_guard(GLOBAL)
include(FetchContent)

set(_NEKO_ICON_SET_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "NekoControls source root, for neko_add_icon_set")

function(neko_add_icon_set target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "NO_CONTROL_ICONS" "MAP;SET" "")
    if(NOT arg_MAP)
        message(FATAL_ERROR "neko_add_icon_set(${target}): MAP <file> is required")
    endif()
    if(NOT arg_SET)
        set(arg_SET lucide)
    endif()

    if(arg_SET STREQUAL "lucide")
        FetchContent_Declare(
            neko_icons_lucide
            URL      https://github.com/lucide-icons/lucide/releases/download/1.61.0/lucide-icons-1.61.0.zip
            URL_HASH SHA256=d4cfe5949e0cf202ec558c3fe2cc6c8373371c1cb8643fac2f7c68ce4153ff4b
        )
        FetchContent_MakeAvailable(neko_icons_lucide)
        set(_source_dir ${neko_icons_lucide_SOURCE_DIR})
    else()
        message(FATAL_ERROR "neko_add_icon_set(${target}): unknown SET '${arg_SET}' (known: lucide)")
    endif()
    # CMake drops an archive's single top-level folder; accept the SVGs either way.
    if(EXISTS ${_source_dir}/icons)
        set(_source_dir ${_source_dir}/icons)
    endif()

    set(_maps "")
    if(NOT arg_NO_CONTROL_ICONS)
        list(APPEND _maps "${_NEKO_ICON_SET_DIR}/resources/icon_maps/controls-${arg_SET}.txt")
    endif()
    get_filename_component(_app_map "${arg_MAP}" ABSOLUTE)
    list(APPEND _maps "${_app_map}")

    # Each logical name becomes a file of its own in the build tree (two names may share
    # one source file), later maps overriding earlier ones.
    set(_out_dir ${CMAKE_CURRENT_BINARY_DIR}/neko_icons_${target})
    file(REMOVE_RECURSE ${_out_dir})
    file(MAKE_DIRECTORY ${_out_dir})
    set(_names "")
    foreach(_map IN LISTS _maps)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_map})
        file(STRINGS ${_map} _lines REGEX "^[a-z0-9-]+=[A-Za-z0-9_-]+$")
        foreach(_line IN LISTS _lines)
            string(REGEX REPLACE "^([^=]+)=(.+)$" "\\1" _logical "${_line}")
            string(REGEX REPLACE "^([^=]+)=(.+)$" "\\2" _file "${_line}")
            if(NOT EXISTS ${_source_dir}/${_file}.svg)
                message(FATAL_ERROR "Icon set ${arg_SET} has no ${_file}.svg (for '${_logical}' in ${_map})")
            endif()
            file(COPY_FILE ${_source_dir}/${_file}.svg ${_out_dir}/${_logical}.svg ONLY_IF_DIFFERENT)
            list(APPEND _names ${_logical})
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _names)
    list(TRANSFORM _names PREPEND "${_out_dir}/")
    list(TRANSFORM _names APPEND ".svg")

    qt_add_resources(${target} "neko_icons_${target}"
        PREFIX /neko/icons
        BASE ${_out_dir}
        FILES ${_names}
    )
endfunction()
