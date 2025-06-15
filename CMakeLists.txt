cmake_minimum_required(VERSION 3.25)

### Project

string(TIMESTAMP TimeStamp "%y%m%d.%H%M" UTC)

project(TreeSheets
    DESCRIPTION "A free-form hierarchical data organizer"
    HOMEPAGE_URL "https://github.com/aardappel/treesheets"
    VERSION ${TimeStamp})

### Settings

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

## Compiler-specific

# Use statically-linked runtime library for MSVC
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Silence warnings in GCC that contain lots of false positives
if(CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -Wno-array-bounds -Wno-stringop-overflow -Wno-maybe-uninitialized")
endif()

### Options

## Run clang-tidy linter

OPTION(WITH_CLANG_TIDY "Run clang-tidy linter" OFF)
if (WITH_CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY clang-tidy -checks=cppcoreguidelines-*,clang-analyzer-*,readability-*,performance-*,portability-*,concurrency-*,modernize-*)
endif()

## Build with statically linked wxWidgets

OPTION(TREESHEETS_WITH_STATIC_WXWIDGETS "Build wxWidgets along with TreeSheets and link TreeSheets against static wxWidgets library" OFF)

## Build with statically linked wxWidgets that resides within a git submodule

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
        if(NOT EXISTS "${PROJECT_SOURCE_DIR}/thirdparty/wxWidgets/CMakeLists.txt")
            message(FATAL_ERROR "The submodules were not downloaded! GIT_WXWIDGETS_SUBMODULES failed.")
        endif()
        set(TREESHEETS_WITH_STATIC_WXWIDGETS ON)
    endif()
endif()

### Libraries (lobster, lobster-impl, StackWalker)

## lobster (script interpreter)

file(
    GLOB lobster_sources
        thirdparty/lobster/external/flatbuffers/src/*.cpp
        thirdparty/lobster/src/builtins.cpp
        thirdparty/lobster/src/compiler.cpp
        thirdparty/lobster/src/disasm.cpp
        thirdparty/lobster/src/file.cpp
        thirdparty/lobster/src/lobsterreader.cpp
        thirdparty/lobster/src/platform.cpp
        thirdparty/lobster/src/vm.cpp
        thirdparty/lobster/src/vmdata.cpp
        thirdparty/lobster/src/vmlog.cpp
        thirdparty/lobster/src/tocpp.cpp
        thirdparty/lobster/src/tccbind.cpp
        thirdparty/lobster/external/libtcc/libtcc.c
)

add_library(lobster STATIC ${lobster_sources})
target_include_directories(lobster PUBLIC thirdparty/lobster/include thirdparty/lobster/src thirdparty/lobster/external/libtcc)

## lobster-impl (provider of TreeSheets functions in lobster)

add_library(lobster-impl STATIC src/lobster_impl.cpp)
target_link_libraries(lobster-impl PRIVATE lobster)

## StackWalker (only on Windows)

if(WIN32)
    add_library(StackWalker STATIC thirdparty/StackWalker/StackWalker.cpp thirdparty/StackWalker/StackWalkerHelpers.cpp)
    target_include_directories(StackWalker PRIVATE thirdparty/StackWalker)
endif()

### TreeSheets executable

set(treesheets_sources
        src/main.cpp
        # The header files are included in order to make them appear in IDEs
        src/cell.h
        src/document.h
        src/evaluator.h
        src/grid.h
        src/app.h
        src/canvas.h
        src/events.h
        src/frame.h
        src/wxtools.h
        src/script_interface.h
        src/selection.h
        src/stdafx.h
        src/system.h
        src/text.h
        src/threadpool.h
        src/tools.h
)

if(APPLE)
    add_executable(
        TreeSheets
        MACOSX_BUNDLE
        ${treesheets_sources}
        platform/osx/App.icns
    )
    set_source_files_properties(platform/osx/App.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(TreeSheets PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/platform/osx/Info.plist"
    )
elseif(WIN32)
    add_executable(
        TreeSheets
        WIN32
        ${treesheets_sources}
        platform/win/icon.rc
    )
else()
    add_executable(
        TreeSheets
        ${treesheets_sources}
    )
endif()

target_precompile_headers(TreeSheets PUBLIC src/stdafx.h)

# If TREESHEETS_WITH_STATIC_WXWIDGETS is set, then it expects the wxWidgets source code 
# to be placed in thirdparty/wxWidgets (see below, it can be changed to your liking).

if(TREESHEETS_WITH_STATIC_WXWIDGETS)
    set(wxBUILD_SHARED OFF)
    set(wxBUILD_INSTALL OFF CACHE BOOL "Avoid install/uninstall target for wxWidgets in static build" FORCE)
    add_subdirectory(thirdparty/wxWidgets)
    set(TREESHEETS_LIBS wx::aui wx::adv wx::core wx::xml wx::net)
else()
    find_package(wxWidgets REQUIRED aui adv core xml net)
    include(${wxWidgets_USE_FILE})
    set(TREESHEETS_LIBS ${wxWidgets_LIBRARIES})
endif()

## Link wxWidgets, lobster-impl and StackWalker into TreeSheets

list(APPEND TREESHEETS_LIBS lobster-impl)
if(WIN32)
    list(APPEND TREESHEETS_LIBS StackWalker)
endif()
target_link_libraries(TreeSheets PRIVATE ${TREESHEETS_LIBS})

### Installation

## Platform specific installation paths

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

    install(FILES platform/linux/com.strlen.TreeSheets.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES platform/linux/com.strlen.TreeSheets.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES platform/linux/com.strlen.TreeSheets.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
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

## Installation

install(TARGETS TreeSheets DESTINATION ${TREESHEETS_BINDIR})
install(DIRECTORY TS/docs DESTINATION ${TREESHEETS_DOCDIR})
install(FILES TS/readme.html DESTINATION ${TREESHEETS_DOCDIR})
install(DIRECTORY TS/examples DESTINATION ${TREESHEETS_DOCDIR})

install(DIRECTORY TS/images DESTINATION ${TREESHEETS_PKGDATADIR})
install(DIRECTORY TS/scripts DESTINATION ${TREESHEETS_PKGDATADIR})

## Localization

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

### Packaging with CPack

set(CPACK_PACKAGE_VENDOR "Wouter van Oortmerssen")
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS" "ZIP")
    set(CPACK_NSIS_PACKAGE_NAME ${CMAKE_PROJECT_NAME})
    set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CMAKE_PROJECT_NAME})
    set(CPACK_NSIS_INSTALL_ROOT "$APPDATA")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY .)
    set(CPACK_NSIS_MANIFEST_DPI_AWARE 1)
    set(CPACK_NSIS_IGNORE_LICENSE_PAGE 1)
    string(APPEND CPACK_NSIS_DEFINES "\nRequestExecutionLevel user") # non-admin install
else()
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "contrib/text")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Wouter van Oortmerssen")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_EPOCH 1)
endif()
include(CPack)
