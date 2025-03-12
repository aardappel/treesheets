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

project(TreeSheets
    DESCRIPTION "A free-form hierarchical data organizer"
    HOMEPAGE_URL "https://github.com/aardappel/treesheets")

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

if(CMAKE_COMPILER_IS_GNUCXX)
    # These warnings seem pretty broken in GCC, with lots of false positives.
    set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -Wno-array-bounds -Wno-stringop-overflow -Wno-maybe-uninitialized")
endif()

add_library(lobster STATIC ${lobster_sources})
target_include_directories(lobster PUBLIC lobster/include lobster/src lobster/external/libtcc)

add_library(lobster-impl STATIC src/lobster_impl.cpp)
target_link_libraries(lobster-impl PRIVATE lobster)

########## TREESHEETS COMPILE AND LINK SETTINGS ###########

OPTION(TREESHEETS_WITH_STATIC_WXWIDGETS "Build wxWidgets along with TreeSheets and link TreeSheets against static wxWidgets library" OFF)

######### GIT SUBMODULES AUTOMATIC HANDLING ########
# Taken from https://cliutils.gitlab.io/modern-cmake/chapters/projects/submodule.html

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
# Update submodules as needed
    OPTION(GIT_WXWIDGETS_SUBMODULES "Checkout wxWidgets git submodules during build" OFF)
    if(GIT_WXWIDGETS_SUBMODULES)
        message(STATUS "The Git wxWidgets submodules are currently being updated...")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --remote --recursive
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init --recursive failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
        if(NOT EXISTS "${PROJECT_SOURCE_DIR}/lib/wxWidgets/CMakeLists.txt")
            message(FATAL_ERROR "The submodules were not downloaded! GIT_WXWIDGETS_SUBMODULES failed.")
        endif()
        set(TREESHEETS_WITH_STATIC_WXWIDGETS ON)
    endif()
endif()

######## TREESHEETS WITH STATIC WXWIDGETS ########

# If TREESHEETS_WITH_STATIC_WXWIDGETS is set, then it expects the wxWidgets source code 
# to be placed in lib/wxWidgets (see below, it can be changed to your liking).

if (TREESHEETS_WITH_STATIC_WXWIDGETS)
    set(wxBUILD_SHARED OFF)
    set(wxBUILD_INSTALL OFF CACHE BOOL "Avoid install/uninstall target for wxWidgets in static build" FORCE)
    add_subdirectory(lib/wxWidgets)
else()
    find_package(wxWidgets REQUIRED aui adv core xml net)
    include(${wxWidgets_USE_FILE})
endif()

if(WIN32)
    add_library(StackWalker STATIC src/StackWalker/StackWalker.cpp src/StackWalker/StackWalkerHelpers.cpp)
    target_include_directories(StackWalker PUBLIC src/StackWalker)
endif()

if(APPLE)
    add_executable(
        TreeSheets
        MACOSX_BUNDLE
        src/main.cpp
        osx/App.icns
    )
    set_source_files_properties(osx/App.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(TreeSheets PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/osx/Info.plist"
    )
elseif(WIN32)
    add_executable(
        TreeSheets
        WIN32
        src/main.cpp
    )
else()
    add_executable(
        TreeSheets
        src/main.cpp
    )
endif()

if(WIN32)
    target_include_directories(TreeSheets PUBLIC lobster/src)
else()
    target_include_directories(TreeSheets PUBLIC lobster/src src/StackWalker)
endif()
target_precompile_headers(TreeSheets PUBLIC src/stdafx.h)

if(WIN32)
    target_link_libraries(
        TreeSheets
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
        StackWalker
    )
else()
    target_link_libraries(
        TreeSheets
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
endif()

########## TREESHEETS INSTALLATION SETTINGS ###############

if(LINUX)
    OPTION(TREESHEETS_RELOCATABLE_INSTALLATION "Install data relative to the TreeSheets binary, instead of respecting the Filesystem Hierarchy Standard" OFF)
endif()

if(LINUX AND NOT TREESHEETS_RELOCATABLE_INSTALLATION)
    include(GNUInstallDirs)

    set(TREESHEETS_BINDIR ${CMAKE_INSTALL_BINDIR})
    set(TREESHEETS_DOCDIR ${CMAKE_INSTALL_DOCDIR})
    set(TREESHEETS_FULL_DOCDIR ${CMAKE_INSTALL_FULL_DOCDIR})
    set(TREESHEETS_PKGDATADIR ${CMAKE_INSTALL_DATADIR}/${CMAKE_PROJECT_NAME})
    set(TREESHEETS_FULL_PKGDATADIR ${CMAKE_INSTALL_FULL_DATADIR}/${CMAKE_PROJECT_NAME})

    # Convert relative to absolute paths because only absolute paths are looked up on Linux
    target_compile_definitions(TreeSheets PRIVATE "LOCALEDIR=L\"${CMAKE_INSTALL_FULL_LOCALEDIR}\"")
    target_compile_definitions(TreeSheets PRIVATE "TREESHEETS_DOCDIR=\"${TREESHEETS_FULL_DOCDIR}\"")
    target_compile_definitions(TreeSheets PRIVATE "TREESHEETS_DATADIR=\"${TREESHEETS_FULL_PKGDATADIR}\"")

    install(FILES linux/com.strlen.TreeSheets.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES linux/com.strlen.TreeSheets.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES linux/com.strlen.TreeSheets.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
elseif(APPLE)
    # Paths must be relative to use with CPack
    set(TREESHEETS_BINDIR .)
    set(TREESHEETS_DOCDIR TreeSheets.app/Contents/Resources)
    set(TREESHEETS_PKGDATADIR TreeSheets.app/Contents/Resources)
else()
    set(TREESHEETS_BINDIR .)
    set(TREESHEETS_DOCDIR .)
    set(TREESHEETS_PKGDATADIR .)
endif()

install(TARGETS TreeSheets DESTINATION ${TREESHEETS_BINDIR})
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
            # Paths must be relative to use with CPack
            DESTINATION "translations/${locale}"
        )
    endforeach()
elseif(APPLE)
    foreach(locale ${locales})
        install(
            FILES "TS/translations/${locale}/ts.mo"
            # Paths must be relative to use with CPack
            DESTINATION "TreeSheets.app/Contents/Resources/translations/${locale}.lproj"
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

set(CPACK_PACKAGE_VENDOR "Wouter van Oortmerssen")
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS" "ZIP")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CPACK_PACKAGE_NAME})
    set(CPACK_NSIS_INSTALL_ROOT "$LOCALAPPDATA\\\\Programs")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY .)
    set(CPACK_NSIS_MANIFEST_DPI_AWARE 1)
else()
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "contrib/text")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Wouter van Oortmerssen")
    if(TREESHEETS_WITH_STATIC_WXWIDGETS)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "gtk3")
    else()
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "gtk3, wxwidgets3.2")
    endif()
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
endif()
include(CPack)
