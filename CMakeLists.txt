cmake_minimum_required(VERSION 3.25)

### Project

if(NOT TREESHEETS_VERSION)
    string(TIMESTAMP timestamp "%y%m%d.%H%M" UTC)
    set(TREESHEETS_VERSION "${timestamp}")
endif()

project(TreeSheets
    DESCRIPTION "A free-form hierarchical data organizer"
    HOMEPAGE_URL "https://github.com/aardappel/treesheets"
    VERSION "${TREESHEETS_VERSION}")

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

### Thirdparty dependencies

include(FetchContent)
FetchContent_Declare(
    wxwidgets
    GIT_REPOSITORY https://github.com/wxWidgets/wxWidgets
    GIT_TAG v3.2.8
    FIND_PACKAGE_ARGS 3.2.8 NAMES wxWidgets
)
FetchContent_Declare(
    lobster
    GIT_REPOSITORY https://github.com/aardappel/lobster
    GIT_TAG v2025.3
)
FetchContent_MakeAvailable(wxwidgets lobster)

### Options

## Run clang-tidy linter

OPTION(WITH_CLANG_TIDY "Run clang-tidy linter" OFF)
if (WITH_CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY clang-tidy -checks=cppcoreguidelines-*,clang-analyzer-*,readability-*,performance-*,portability-*,concurrency-*,modernize-*)
endif()

### Libraries (lobster, lobster-impl, StackWalker)

## lobster (script interpreter)

add_library(lobster STATIC
    ${lobster_SOURCE_DIR}/dev/external/flatbuffers/src/idl_gen_text.cpp
    ${lobster_SOURCE_DIR}/dev/external/flatbuffers/src/idl_parser.cpp
    ${lobster_SOURCE_DIR}/dev/external/flatbuffers/src/util.cpp
    ${lobster_SOURCE_DIR}/dev/src/builtins.cpp
    ${lobster_SOURCE_DIR}/dev/src/compiler.cpp
    ${lobster_SOURCE_DIR}/dev/src/file.cpp
    ${lobster_SOURCE_DIR}/dev/src/lobsterreader.cpp
    ${lobster_SOURCE_DIR}/dev/src/platform.cpp
    ${lobster_SOURCE_DIR}/dev/src/vm.cpp
    ${lobster_SOURCE_DIR}/dev/src/vmdata.cpp
    ${lobster_SOURCE_DIR}/dev/src/tccbind.cpp
    ${lobster_SOURCE_DIR}/dev/external/libtcc/libtcc.c
)
target_include_directories(lobster PRIVATE
    ${lobster_SOURCE_DIR}/dev/src
    ${lobster_SOURCE_DIR}/dev/include
    ${lobster_SOURCE_DIR}/dev/external
    ${lobster_SOURCE_DIR}/dev/external/libtcc)

## lobster-impl (provider of TreeSheets functions in lobster)

add_library(lobster-impl STATIC src/lobster_impl.cpp)
target_include_directories(lobster-impl PRIVATE 
    ${lobster_SOURCE_DIR}/dev/src
    ${lobster_SOURCE_DIR}/dev/include)
target_link_libraries(lobster-impl PRIVATE lobster)

## StackWalker (only on Windows)

if(WIN32)
    add_library(StackWalker STATIC 
        thirdparty/StackWalker/StackWalker.cpp
        thirdparty/StackWalker/StackWalkerHelpers.cpp)
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
    src/tsapp.h
    src/tscanvas.h
    src/events.h
    src/tsframe.h
    src/wxtools.h
    src/script_interface.h
    src/selection.h
    src/stdafx.h
    src/system.h
    src/text.h
    src/threadpool.h
    src/tools.h
)

if(WIN32)
    list(APPEND treesheets_sources platform/win/icon.rc)
endif()

add_executable(TreeSheets ${treesheets_sources})
target_compile_definitions(TreeSheets PRIVATE "PACKAGE_VERSION=\"${CMAKE_PROJECT_VERSION}\"")

if(APPLE)
    set_target_properties(TreeSheets PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "${CMAKE_PROJECT_NAME}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright © 2025 Wouter van Oortmerssen and Tobias Predel. All rights reserved."
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.strlen.TreeSheets"
        MACOSX_BUNDLE_ICON_FILE "App.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/platform/osx/Info.plist"
    )
elseif(WIN32)
    set_target_properties(TreeSheets PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

target_precompile_headers(TreeSheets PUBLIC src/stdafx.h)

## Link wxWidgets, lobster-impl and StackWalker into TreeSheets
set(TREESHEETS_LIBS wx::aui wx::adv wx::core wx::xml wx::net lobster-impl)
if(WIN32)
    list(APPEND TREESHEETS_LIBS StackWalker)
endif()
target_link_libraries(TreeSheets PRIVATE ${TREESHEETS_LIBS})

### Installation

## Platform specific installation paths

if(LINUX OR BSD)
    OPTION(TREESHEETS_RELOCATABLE_INSTALLATION "Install data relative to the TreeSheets binary, instead of respecting the Filesystem Hierarchy Standard" OFF)
endif()

if((LINUX OR BSD) AND NOT TREESHEETS_RELOCATABLE_INSTALLATION)
    include(GNUInstallDirs)

    set(TREESHEETS_BINDIR ${CMAKE_INSTALL_BINDIR})
    set(TREESHEETS_DOCDIR ${CMAKE_INSTALL_DOCDIR})
    set(TREESHEETS_FULL_DOCDIR ${CMAKE_INSTALL_FULL_DOCDIR})
    set(TREESHEETS_PKGDATADIR ${CMAKE_INSTALL_DATADIR}/${CMAKE_PROJECT_NAME})
    set(TREESHEETS_FULL_PKGDATADIR ${CMAKE_INSTALL_FULL_DATADIR}/${CMAKE_PROJECT_NAME})

    # Convert relative to absolute paths because only absolute paths are looked up on Linux (and BSD)
    target_compile_definitions(TreeSheets PRIVATE
        "LOCALEDIR=L\"${CMAKE_INSTALL_FULL_LOCALEDIR}\""
        "TREESHEETS_DOCDIR=\"${TREESHEETS_FULL_DOCDIR}\""
        "TREESHEETS_DATADIR=\"${TREESHEETS_FULL_PKGDATADIR}\""
    )

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
file(GLOB treesheets_readme_files "TS/readme*.html")
install(FILES ${treesheets_readme_files} DESTINATION ${TREESHEETS_DOCDIR})
install(DIRECTORY TS/examples DESTINATION ${TREESHEETS_DOCDIR})

install(DIRECTORY TS/images DESTINATION ${TREESHEETS_PKGDATADIR})
install(DIRECTORY TS/scripts DESTINATION ${TREESHEETS_PKGDATADIR})
set(lobster_modules
    ${lobster_SOURCE_DIR}/modules/std.lobster
    ${lobster_SOURCE_DIR}/modules/stdtype.lobster
    ${lobster_SOURCE_DIR}/modules/vec.lobster
    ${lobster_SOURCE_DIR}/modules/color.lobster
)
install(FILES ${lobster_modules} DESTINATION ${TREESHEETS_PKGDATADIR}/scripts/modules)

## Apple icon set
if(APPLE)
    install(
        FILES "platform/osx/App.icns"
        DESTINATION "TreeSheets.app/Contents/Resources"
    )
endif()

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
    set(CPACK_GENERATOR "INNOSETUP" "ZIP")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CMAKE_PROJECT_NAME})
    set(CPACK_PACKAGE_EXECUTABLES "TreeSheets" "TreeSheets")
    set(CPACK_INNOSETUP_RUN_EXECUTABLES "TreeSheets")
    set(CPACK_INNOSETUP_PACKAGE_NAME ${CMAKE_PROJECT_NAME})
    set(CPACK_INNOSETUP_LANGUAGES
        "brazilianPortuguese"
        "english"
        "french"
        "german"
        "italian"
        "korean"
        "russian"
    )
    set(CPACK_INNOSETUP_SETUP_PrivilegesRequired "lowest")
    set(CPACK_INNOSETUP_IGNORE_README_PAGE ON)
    set(CPACK_INNOSETUP_IGNORE_LICENSE_PAGE ON)
    set(CPACK_INNOSETUP_USE_MODERN_WIZARD ON)
else()
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "contrib/text")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Wouter van Oortmerssen")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_EPOCH 2)
endif()
include(CPack)
