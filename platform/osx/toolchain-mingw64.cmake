# Toolchain file for cross-compilation with mingw-w64 from macOS to Windows, using Homebrew's
# mingw-w64 (brew install mingw-w64)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Specify the cross compiler
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Where is the target environment: Homebrew lives in /opt/homebrew on Apple silicon and in
# /usr/local on Intel Macs
foreach(prefix /opt/homebrew /usr/local)
    if(EXISTS ${prefix}/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32)
        set(CMAKE_FIND_ROOT_PATH ${prefix}/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32)
        break()
    endif()
endforeach()

# Search rules
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Force static linking of GCC runtime libraries
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static" CACHE INTERNAL "exe link flags")
set(CMAKE_SHARED_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static" CACHE INTERNAL "shared link flags")
set(CMAKE_MODULE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static" CACHE INTERNAL "module link flags")

# Hard overrides for TreeSheets specific options to ensure a smooth MinGW cross-compile
set(ENABLE_IPO OFF CACHE BOOL "Disable LTO to prevent multiple-definition errors" FORCE)
