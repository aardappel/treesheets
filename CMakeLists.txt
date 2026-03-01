cmake_minimum_required(VERSION 3.25)

### Project

set(TREESHEETS_VERSION "" CACHE STRING "Version string (e.g., 1.2.3). Leave empty for auto-timestamp.")

if("${TREESHEETS_VERSION}" STREQUAL "")
    string(TIMESTAMP timestamp "%y%m%d.%H%M" UTC)
    set(TREESHEETS_VERSION "${timestamp}")
    message(STATUS "Auto-generated version: ${TREESHEETS_VERSION}")
else()
    message(STATUS "User-defined version: ${TREESHEETS_VERSION}")
endif()

project(TreeSheets
    DESCRIPTION "A free-form hierarchical data organizer"
    HOMEPAGE_URL "https://github.com/aardappel/treesheets"
    VERSION "${TREESHEETS_VERSION}")

### Settings

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)
OPTION(ENABLE_LOBSTER "Enable Lobster scripting" ON)

## Compiler-specific

# Use statically-linked libraries with MSVC
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(wxBUILD_USE_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
    set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
endif(MSVC)

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
    GIT_TAG v3.2.10
    GIT_SHALLOW ON
    FIND_PACKAGE_ARGS 3.2.9 NAMES wxWidgets
)
FetchContent_MakeAvailable(wxwidgets)

if(ENABLE_LOBSTER)
    FetchContent_Declare(
        lobster
        GIT_REPOSITORY https://github.com/aardappel/lobster
        GIT_TAG v2026.1
        GIT_SHALLOW ON
    )
    FetchContent_MakeAvailable(lobster)
endif()

### Options

## Run clang-tidy linter

OPTION(WITH_CLANG_TIDY "Run clang-tidy linter" OFF)
if (WITH_CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY clang-tidy -checks=cppcoreguidelines-*,clang-analyzer-*,readability-*,performance-*,portability-*,concurrency-*,modernize-*)
endif()

### Libraries (lobster, lobster-impl)

## lobster (script interpreter)
if(ENABLE_LOBSTER)
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
        ${lobster_SOURCE_DIR}/dev/external/libtcc/libtcc.c)
    if(WIN32)
        target_sources(lobster PRIVATE
            ${lobster_SOURCE_DIR}/dev/include/StackWalker/StackWalker.cpp
            ${lobster_SOURCE_DIR}/dev/include/StackWalker/StackWalkerHelpers.cpp)
    endif(WIN32)
    target_include_directories(lobster PUBLIC
        ${lobster_SOURCE_DIR}/dev/src
        ${lobster_SOURCE_DIR}/dev/include
        ${lobster_SOURCE_DIR}/dev/external
        ${lobster_SOURCE_DIR}/dev/external/libtcc)

    ## lobster-impl (provider of TreeSheets functions in lobster)

    add_library(lobster-impl STATIC src/lobster_impl.cpp)
    target_link_libraries(lobster-impl PRIVATE lobster)
endif(ENABLE_LOBSTER)

### TreeSheets executable

add_executable(TreeSheets
    src/main.cpp)

target_compile_definitions(TreeSheets PRIVATE "PACKAGE_VERSION=\"${TREESHEETS_VERSION}\"")
if(ENABLE_LOBSTER)
    target_compile_definitions(TreeSheets PRIVATE "ENABLE_LOBSTER=1")
endif(ENABLE_LOBSTER)

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
    target_sources(TreeSheets PRIVATE
        platform/win/treesheets.rc)
    set_target_properties(TreeSheets PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

target_precompile_headers(TreeSheets PUBLIC src/stdafx.h)

## Link wxWidgets and lobster-impl into TreeSheets
if(ENABLE_LOBSTER)
    target_link_libraries(TreeSheets PRIVATE lobster-impl)
endif(ENABLE_LOBSTER)
target_link_libraries(TreeSheets PRIVATE wx::aui wx::adv wx::core wx::xml wx::net)

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

    # Adapt the AppStream metainfo to release version and date
    string(TIMESTAMP TREESHEETS_RELEASE_DATE "%Y-%m-%d")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/linux/com.strlen.TreeSheets.metainfo.xml.in"
        "${CMAKE_CURRENT_BINARY_DIR}/com.strlen.TreeSheets.metainfo.xml"
        @ONLY
    )

    install(FILES platform/linux/com.strlen.TreeSheets.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES platform/linux/com.strlen.TreeSheets.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES platform/linux/com.strlen.TreeSheets.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/com.strlen.TreeSheets.metainfo.xml" DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
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
if(ENABLE_LOBSTER)
    set(lobster_modules
        ${lobster_SOURCE_DIR}/modules/std.lobster
        ${lobster_SOURCE_DIR}/modules/stdtype.lobster
        ${lobster_SOURCE_DIR}/modules/vec.lobster
        ${lobster_SOURCE_DIR}/modules/color.lobster
    )
    install(FILES ${lobster_modules} DESTINATION ${TREESHEETS_PKGDATADIR}/scripts/modules)
endif(ENABLE_LOBSTER)

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
    if(CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64")
        set(CPACK_INNOSETUP_SETUP_ArchitecturesAllowed "arm64")
        set(ARCH_NAME "winarm64")
    else()
        set(CPACK_INNOSETUP_SETUP_ArchitecturesAllowed "x64compatible")
        set(ARCH_NAME "winx64")
    endif()
    set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${PROJECT_VERSION}-${ARCH_NAME}")
    set(CPACK_INNOSETUP_RUN_EXECUTABLES "TreeSheets")
    set(CPACK_INNOSETUP_PACKAGE_NAME ${CMAKE_PROJECT_NAME})
    set(CPACK_INNOSETUP_LANGUAGES
        "brazilianPortuguese" "english" "french" "german" 
        "italian" "korean" "russian"
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
