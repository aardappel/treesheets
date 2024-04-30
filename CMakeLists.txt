# Sample build with makefile generator
#
#     cd <into-treesheets-source>
#     cmake . -B _build -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_INSTALL_PREFIX=installed \
#         -DCMAKE_PROGRAM_PATH=<path-to-wx-config> # not always needed
#     cmake --build _build --target install
#
# Sample build with IDE generator, like Xcode
#
#     cd <into-treesheets-source>
#     cmake -B _build -GXcode \
#         -DCMAKE_PROGRAM_PATH=<path-to-wx-config> # not always needed
#     cmake --build _build --config Release --target install
#
# Run program:
#
#     ./i/treesheets
#
# Should work on Windows, too. About helping FindwxWidgets to find wxWidgets
# consult: https://cmake.org/cmake/help/latest/module/FindwxWidgets.html

cmake_minimum_required(VERSION 3.25)

project(treesheets)

########## GLOBAL SETTINGS ################################

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

OPTION(WITH_CLANG_TIDY "Run clang-tidy linter" OFF)
if (WITH_CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY clang-tidy -checks=cppcoreguidelines-*,clang-analyzer-*,readability-*,performance-*,portability-*,concurrency-*,modernize-*)
endif()

########## LOBSTER COMPILE AND LINK SETTINGS ##############

file(
    GLOB lobster_sources
        lobster/external/flatbuffers/src/*.cpp
        lobster/src/builtins.cpp
        lobster/src/compiler.cpp
        lobster/src/disasm.cpp
        lobster/src/file.cpp
        lobster/src/lobsterreader.cpp
        lobster/src/platform.cpp
        lobster/src/vm.cpp
        lobster/src/vmdata.cpp
        lobster/src/vmlog.cpp
        lobster/src/tocpp.cpp
        lobster/src/tccbind.cpp
        lobster/external/libtcc/libtcc.c
)
add_library(lobster STATIC ${lobster_sources})
target_include_directories(lobster PUBLIC lobster/include lobster/src lobster/external/libtcc)

add_library(lobster-impl STATIC src/lobster_impl.cpp)
target_link_libraries(lobster-impl PRIVATE lobster)

########## TREESHEETS COMPILE AND LINK SETTINGS ###########

OPTION(TREESHEETS_WITH_STATIC_WXWIDGETS "Build wxWidgets along with TreeSheets and link TreeSheets against static wxWidgets library" OFF)

# If TREESHEETS_WITH_STATIC_WXWIDGETS is set, then it expects the wxWidgets source code
# (recursive git check-out with submodules) to be placed in lib/wxWidgets (see below, it can be changed to your liking).

if (TREESHEETS_WITH_STATIC_WXWIDGETS)
    set(wxBUILD_SHARED OFF)
    set(wxBUILD_INSTALL OFF CACHE BOOL "Avoid install/uninstall target for wxWidgets in static build" FORCE)
    add_subdirectory(lib/wxWidgets)
else()
    find_package(wxWidgets REQUIRED aui adv core xml net)
    include(${wxWidgets_USE_FILE})
endif()

add_executable(
    treesheets
    src/main.cpp
)

target_link_libraries(
    treesheets
    PRIVATE
    $<IF:$<BOOL:${TREESHEETS_WITH_STATIC_WXWIDGETS}>,
        wx::aui
        wx::adv
        wx::core
        wx::xml
        wx::net,
        ${wxWidgets_LIBRARIES}
    >
    lobster-impl
)

########## TREESHEETS INSTALLATION SETTINGS ###############

if(LINUX)
    OPTION(TREESHEETS_RELOCATABLE_INSTALLATION "Install data relative to the treesheets binary, instead of respecting the Filesystem Hierarchy Standard" OFF)
endif()

if(LINUX AND NOT TREESHEETS_RELOCATABLE_INSTALLATION)
    include(GNUInstallDirs)

    set(TREESHEETS_BINDIR ${CMAKE_INSTALL_BINDIR})
    set(TREESHEETS_DOCDIR ${CMAKE_INSTALL_DOCDIR})
    set(TREESHEETS_FULL_DOCDIR ${CMAKE_INSTALL_FULL_DOCDIR})
    set(TREESHEETS_PKGDATADIR ${CMAKE_INSTALL_DATADIR}/${CMAKE_PROJECT_NAME})
    set(TREESHEETS_FULL_PKGDATADIR ${CMAKE_INSTALL_FULL_DATADIR}/${CMAKE_PROJECT_NAME})

    # Convert relative to absolute paths because only absolute paths are looked up on Linux
    target_compile_definitions(treesheets PRIVATE "LOCALEDIR=L\"${CMAKE_INSTALL_FULL_LOCALEDIR}\"")
    target_compile_definitions(treesheets PRIVATE "TREESHEETS_DOCDIR=\"${TREESHEETS_FULL_DOCDIR}\"")
    target_compile_definitions(treesheets PRIVATE "TREESHEETS_DATADIR=\"${TREESHEETS_FULL_PKGDATADIR}\"")

    install(FILES linux/com.strlen.TreeSheets.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES linux/com.strlen.TreeSheets.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES linux/com.strlen.TreeSheets.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
else()
    set(TREESHEETS_BINDIR ${CMAKE_INSTALL_PREFIX})
    set(TREESHEETS_DOCDIR ${CMAKE_INSTALL_PREFIX})
    set(TREESHEETS_PKGDATADIR ${CMAKE_INSTALL_PREFIX})
endif()

install(TARGETS treesheets DESTINATION ${TREESHEETS_BINDIR})
install(DIRECTORY TS/docs DESTINATION ${TREESHEETS_DOCDIR})
install(FILES TS/readme.html DESTINATION ${TREESHEETS_DOCDIR})
install(DIRECTORY TS/examples DESTINATION ${TREESHEETS_DOCDIR})

install(DIRECTORY TS/images DESTINATION ${TREESHEETS_PKGDATADIR})
install(DIRECTORY TS/scripts DESTINATION ${TREESHEETS_PKGDATADIR})

# Install translations to correct platform-specific path.
# See: https://docs.wxwidgets.org/trunk/overview_i18n.html#overview_i18n_mofiles
file(
    GLOB locales
    LIST_DIRECTORIES true
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/TS/translations
    TS/translations/*/ts.mo
)
list(
    TRANSFORM locales
    REPLACE "/ts\\.mo$" ""
)
if(WIN32 OR TREESHEETS_RELOCATABLE_INSTALLATION)
    foreach(locale ${locales})
        install(
            FILES "TS/translations/${locale}/ts.mo"
            DESTINATION "${CMAKE_INSTALL_PREFIX}/translations/${locale}"
        )
    endforeach()
elseif(APPLE)
    foreach(locale ${locales})
        install(
            FILES "TS/translations/${locale}/ts.mo"
            DESTINATION "${CMAKE_INSTALL_PREFIX}/translations/${locale}.lproj"
        )
    endforeach()
else()
    # Falling back to GNU scheme
    foreach(locale ${locales})
        install(
            FILES "TS/translations/${locale}/ts.mo"
            DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/${locale}/LC_MESSAGES"
        )
    endforeach()
endif()
