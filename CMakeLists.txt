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

cmake_minimum_required(VERSION 3.1)
# CMAKE_CXX_STANDARD needs v3.1, could be lowered with minor effort

project(treesheets)

if(NOT APPLE AND NOT WIN32)
    include(GNUInstallDirs)
endif()

# set(CMAKE_CXX_STANDARD 17)  # this doesn't work correctly on all CMake versions :(
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

find_package(wxWidgets REQUIRED aui adv core xml net)
include(${wxWidgets_USE_FILE})

file(GLOB sources src/*.cpp src/*.h
        lobster/external/flatbuffers/src/*.cpp
        lobster/src/builtins.cpp
        lobster/src/compiler.cpp
        lobster/src/disasm.cpp
        lobster/src/file.cpp
        lobster/src/lobsterreader.cpp
        lobster/src/platform.cpp
        lobster/src/vm.cpp
        lobster/src/vmdata.cpp
        lobster/src/vmlog.cpp)
include_directories(lobster/include lobster/src)
add_executable(treesheets ${sources})

target_link_libraries(treesheets PRIVATE ${wxWidgets_LIBRARIES})

install(TARGETS treesheets DESTINATION ${CMAKE_INSTALL_PREFIX})
install(DIRECTORY TS/docs DESTINATION ${CMAKE_INSTALL_PREFIX})
install(FILES TS/readme.html DESTINATION ${CMAKE_INSTALL_PREFIX})

install(DIRECTORY TS/examples DESTINATION ${CMAKE_INSTALL_PREFIX})
install(DIRECTORY TS/images DESTINATION ${CMAKE_INSTALL_PREFIX})
install(DIRECTORY TS/images DESTINATION ${CMAKE_INSTALL_PREFIX})
install(DIRECTORY TS/scripts DESTINATION ${CMAKE_INSTALL_PREFIX})
install(DIRECTORY TS/translations DESTINATION ${CMAKE_INSTALL_PREFIX})

if(NOT APPLE AND NOT WIN32)
    install(FILES TS/images/treesheets.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES TS/treesheets.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()
