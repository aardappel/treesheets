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

######### GIT SUBMODULES AUTOMATIC HANDLING ########
# Taken from https://cliutils.gitlab.io/modern-cmake/chapters/projects/submodule.html

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
# Update submodules as needed
    OPTION(GIT_WXWIDGETS_SUBMODULES "Check wxWidgets git submodules during build" ON)
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

######## X11 LINUX BUILD ISSUE ########

# This is the fix for this build error under linux with static wxWidgets:
# [ 60%] Linking CXX executable treesheets
# /usr/bin/ld: lib/wxWidgets/lib/libwx_gtk3u_core-3.3.a(utilsx11.cpp.o): undefined reference to symbol 'XGetWindowAttributes'
# /usr/bin/ld: /lib/x86_64-linux-gnu/libX11.so.6: error adding symbols: DSO missing from command line
# collect2: error: ld returned 1 exit status
# make[2]: *** [CMakeFiles/treesheets.dir/build.make:115: treesheets] Error 1
# make[1]: *** [CMakeFiles/Makefile2:449: CMakeFiles/treesheets.dir/all] Error 2
# make: *** [Makefile:136: all] Error 2

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11 REQUIRED) 
    target_link_libraries(treesheets PRIVATE ${X11_LIBRARIES})  
endif()

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
