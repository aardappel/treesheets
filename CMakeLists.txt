cmake_minimum_required(VERSION 3.25)

# The static wx libraries depend on each other, so they come up repeatedly on the link line.
# Let CMake drop the repeats where the linker doesn't need them, as Apple's ld warns about them.
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()

### Project

set(TREESHEETS_VERSION "" CACHE STRING "Version string (e.g., 1.2.3). Leave empty for auto-timestamp.")

if("${TREESHEETS_VERSION}" STREQUAL "")
    string(TIMESTAMP TREESHEETS_VERSION "%y%m%d.%H%M" UTC)
    message(STATUS "Auto-generated version: ${TREESHEETS_VERSION}")
else()
    message(STATUS "User-defined version: ${TREESHEETS_VERSION}")
endif()

project(TreeSheets
    DESCRIPTION "A free-form hierarchical data organizer"
    HOMEPAGE_URL "https://github.com/aardappel/treesheets"
    VERSION "${TREESHEETS_VERSION}")

### Options

option(ENABLE_LOBSTER "Enable Lobster scripting" ON)
option(ENABLE_WXPDFDOC "Enable direct PDF Export" OFF)
option(ENABLE_IPO "Enable interprocedural/link time optimization" ON)
option(ENABLE_CLANG_TIDY "Run clang-tidy linter" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(TREESHEETS_BUNDLE_WXWIDGETS "Always build wxWidgets from source as static libraries linked into TreeSheets, without installing them" OFF)
if(LINUX OR BSD)
    option(TREESHEETS_RELOCATABLE_INSTALLATION "Install data relative to the TreeSheets binary, instead of respecting the Filesystem Hierarchy Standard" OFF)
endif()

### Settings

set(CMAKE_CXX_STANDARD 20)
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT iporesult OUTPUT output)
    if(iporesult)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "IPO is not supported: ${output}")
    endif()
endif()

## Compiler-specific

# Use statically-linked libraries with MSVC
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(wxBUILD_USE_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
    set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
endif()

# Silence warnings in GCC that contain lots of false positives (also in the dependencies)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:CXX>:-Wno-array-bounds;-Wno-stringop-overflow;-Wno-maybe-uninitialized>")
endif()

### wxWidgets

# Exclude unused libraries
foreach(option WEBVIEW STC RIBBON PROPGRID RICHTEXT MEDIACTRL)
    set(wxUSE_${option} FALSE)
endforeach()
# Bundle wxWidgets statically into the executable, so none of its libraries or headers are installed
if(TREESHEETS_BUNDLE_WXWIDGETS)
    set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(wxBUILD_INSTALL OFF CACHE BOOL "" FORCE)
endif()
# wxWidgets only declares wxBUILD_SHARED when it's configured below, so declare it here with its
# default (shared, unless BUILD_SHARED_LIBS says otherwise) to see the value it will use.
if(DEFINED BUILD_SHARED_LIBS)
    set(wxBUILD_SHARED ${BUILD_SHARED_LIBS} CACHE BOOL "Build wx libraries as shared libs")
else()
    set(wxBUILD_SHARED ON CACHE BOOL "Build wx libraries as shared libs")
endif()
if(NOT wxBUILD_SHARED)
    set(wxUSE_HTML FALSE)
    set(wxUSE_WXHTML_HELP FALSE)
endif()

include(FetchContent)

# Use an installed wxWidgets if there is one, unless it's to be bundled
if(NOT TREESHEETS_BUNDLE_WXWIDGETS)
    set(wxwidgets_find_package_args FIND_PACKAGE_ARGS 3.3.2 NAMES wxWidgets)
endif()
FetchContent_Declare(
    wxwidgets
    URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.2/wxWidgets-3.3.2.tar.bz2
    URL_HASH SHA256=50a28cb668de47b0e006cd6ebed8cf4f76c1cac6116fb3c978c44478219103f2
    ${wxwidgets_find_package_args}
)
FetchContent_MakeAvailable(wxwidgets)

### TreeSheets executable

add_executable(TreeSheets src/main.cpp)

target_compile_definitions(TreeSheets PRIVATE "PACKAGE_VERSION=\"${TREESHEETS_VERSION}\"")
target_link_libraries(TreeSheets PRIVATE wx::aui wx::adv wx::core wx::xml wx::net)
target_precompile_headers(TreeSheets PRIVATE src/stdafx.h)

# With wxGTK 3, text is drawn with Pango directly where possible, to reuse the layouts of
# strings drawn before (see TextLayoutCache in src/wxtools.h). Optional: without the GTK 3
# development files, text is drawn through wx instead.
if(LINUX OR BSD)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
        if(GTK3_FOUND)
            target_link_libraries(TreeSheets PRIVATE PkgConfig::GTK3)
            target_compile_definitions(TreeSheets PRIVATE TREESHEETS_USE_PANGO)
        endif()
    endif()
endif()

if(MSVC)
    target_sources(TreeSheets PRIVATE src/stdafx.cpp)
    # Disable warnings with Microsoft Visual C++ compiler
    target_compile_options(TreeSheets PRIVATE
        /w34189  # Set warning 4189 (initialized but not referenced) to warning level 3
        /wd4244  # Disable warning 4244 (conversion from int to float)
        /wd4355  # Disable warning 4355 ('this' used in base initializer list)
        /wd4996  # Disable warning 4996 (deprecated functions like strncpy)
    )
endif()

if(ENABLE_CLANG_TIDY)
    set_target_properties(TreeSheets PROPERTIES CXX_CLANG_TIDY "clang-tidy")
endif()

# Suppressions for leaks reported in system libraries: platform/linux/lsan.supp
if(ENABLE_ASAN)
    if(MSVC)
        target_compile_options(TreeSheets PRIVATE /fsanitize=address)
    else()
        target_compile_options(TreeSheets PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(TreeSheets PRIVATE -fsanitize=address)
    endif()
endif()

if(APPLE)
    target_sources(TreeSheets PRIVATE src/macclipboard.mm)
    set_target_properties(TreeSheets PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright © 2025 Wouter van Oortmerssen and Tobias Predel. All rights reserved."
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.strlen.TreeSheets"
        MACOSX_BUNDLE_ICON_FILE "App.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platform/osx/Info.plist"
    )
    target_link_libraries(TreeSheets PRIVATE "-framework AppKit")
elseif(WIN32)
    target_sources(TreeSheets PRIVATE platform/win/treesheets.rc)
    set_target_properties(TreeSheets PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(TreeSheets PRIVATE imm32)
endif()

### Installation paths

if((LINUX OR BSD) AND NOT TREESHEETS_RELOCATABLE_INSTALLATION)
    include(GNUInstallDirs)

    set(TREESHEETS_BINDIR ${CMAKE_INSTALL_BINDIR})
    set(TREESHEETS_DOCDIR ${CMAKE_INSTALL_DOCDIR})
    set(TREESHEETS_FULL_DOCDIR ${CMAKE_INSTALL_FULL_DOCDIR})
    set(TREESHEETS_PKGDATADIR ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME})
    set(TREESHEETS_FULL_PKGDATADIR ${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME})

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

### Optional features

if(ENABLE_LOBSTER)
    include(cmake/Lobster.cmake)
endif()

if(ENABLE_WXPDFDOC)
    include(cmake/WxPdfDoc.cmake)
endif()

### Installation

install(TARGETS TreeSheets DESTINATION ${TREESHEETS_BINDIR})
install(DIRECTORY TS/docs DESTINATION ${TREESHEETS_DOCDIR})
file(GLOB treesheets_readme_files "TS/readme*.html")
install(FILES ${treesheets_readme_files} DESTINATION ${TREESHEETS_DOCDIR})
install(DIRECTORY TS/examples DESTINATION ${TREESHEETS_DOCDIR})
install(DIRECTORY TS/images DESTINATION ${TREESHEETS_PKGDATADIR})

if(APPLE)
    install(FILES platform/osx/App.icns DESTINATION TreeSheets.app/Contents/Resources)
endif()

include(cmake/Localization.cmake)

### Packaging

include(cmake/Packaging.cmake)
