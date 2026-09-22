# Lobster scripting: interpreter library, TreeSheets bindings, script installation and the
# script reference target. Included from the top-level CMakeLists.txt when ENABLE_LOBSTER is ON,
# after the TreeSheets target and the TREESHEETS_*DIR install variables have been defined.

# Lobster gates some Windows code on _MSC_VER instead of _WIN32, so MinGW builds need a patch.
set(lobster_patch)
if(WIN32 AND NOT MSVC)
    find_program(PATCH_EXECUTABLE patch REQUIRED)
    set(lobster_patch PATCH_COMMAND
        ${PATCH_EXECUTABLE} -p1 -N -i ${CMAKE_CURRENT_LIST_DIR}/patches/lobster-mingw.patch)
endif()

FetchContent_Declare(
    lobster
    URL https://github.com/aardappel/lobster/archive/refs/tags/v2026.7.tar.gz
    URL_HASH SHA256=b19315a013106cd8611b34152fe48c0c45ef9f5bf89aab4b6df9b37951da3499
    ${lobster_patch}
)
FetchContent_MakeAvailable(lobster)

## lobster (script interpreter)

add_library(lobster STATIC
    ${lobster_SOURCE_DIR}/dev/external/flatbuffers/src/idl_gen_text.cpp
    ${lobster_SOURCE_DIR}/dev/external/flatbuffers/src/idl_parser.cpp
    ${lobster_SOURCE_DIR}/dev/external/flatbuffers/src/util.cpp
    ${lobster_SOURCE_DIR}/dev/src/builtindoc.cpp
    ${lobster_SOURCE_DIR}/dev/src/builtins.cpp
    ${lobster_SOURCE_DIR}/dev/src/compiler.cpp
    ${lobster_SOURCE_DIR}/dev/src/file.cpp
    ${lobster_SOURCE_DIR}/dev/src/pakfile.cpp
    ${lobster_SOURCE_DIR}/dev/src/lobsterreader.cpp
    ${lobster_SOURCE_DIR}/dev/src/platform.cpp
    ${lobster_SOURCE_DIR}/dev/src/simplex.cpp
    ${lobster_SOURCE_DIR}/dev/src/vm.cpp
    ${lobster_SOURCE_DIR}/dev/src/vmdata.cpp
    ${lobster_SOURCE_DIR}/dev/src/tccbind.cpp
    ${lobster_SOURCE_DIR}/dev/external/libtcc/libtcc.c)
if(MSVC)
    target_sources(lobster PRIVATE
        ${lobster_SOURCE_DIR}/dev/include/StackWalker/StackWalker.cpp
        ${lobster_SOURCE_DIR}/dev/include/StackWalker/StackWalkerHelpers.cpp)
endif()
target_include_directories(lobster PUBLIC
    ${lobster_SOURCE_DIR}/dev/src
    ${lobster_SOURCE_DIR}/dev/include
    ${lobster_SOURCE_DIR}/dev/external
    ${lobster_SOURCE_DIR}/dev/external/libtcc)
target_compile_definitions(lobster PRIVATE "LOBSTER_ENGINE=0")
if(WIN32 AND NOT MSVC)
    # Text-to-speech in platform.cpp needs the SAPI GUIDs, which MSVC resolves implicitly.
    target_link_libraries(lobster PUBLIC sapi)
endif()

## lobster-impl (provider of TreeSheets functions in lobster)

add_library(lobster-impl STATIC src/lobster_impl.cpp)
target_link_libraries(lobster-impl PUBLIC lobster)

target_link_libraries(TreeSheets PRIVATE lobster-impl)
target_compile_definitions(TreeSheets PRIVATE "ENABLE_LOBSTER=1")

## Installation

install(DIRECTORY TS/scripts DESTINATION ${TREESHEETS_PKGDATADIR})
install(FILES
    ${lobster_SOURCE_DIR}/modules/std.lobster
    ${lobster_SOURCE_DIR}/modules/stdtype.lobster
    ${lobster_SOURCE_DIR}/modules/vec.lobster
    ${lobster_SOURCE_DIR}/modules/color.lobster
    DESTINATION ${TREESHEETS_PKGDATADIR}/scripts/modules)

## Script reference

# Target to regenerate the Lobster script reference from the builtin functions registered in the
# TreeSheets executable (needs a display, as the application is started to dump the documentation):
#   cmake --build <builddir> --target update-script-reference
add_custom_target(
    update-script-reference
    COMMAND ${CMAKE_COMMAND}
        "-DTREESHEETS_EXE=$<TARGET_FILE:TreeSheets>"
        "-DSCRIPTS_SRC=${CMAKE_CURRENT_SOURCE_DIR}/TS/scripts"
        "-DOUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/TS/docs/script_reference.html"
        -P "${CMAKE_CURRENT_LIST_DIR}/UpdateScriptReference.cmake"
    DEPENDS TreeSheets
    COMMENT "Updating TS/docs/script_reference.html from the builtin function documentation"
    VERBATIM
)
