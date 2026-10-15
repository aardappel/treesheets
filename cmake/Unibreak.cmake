# libunibreak (Unicode line breaking, UAX #14, and East Asian Width) for wrapping cell text, see
# Text::GetLine in src/text.h. Included from the top-level CMakeLists.txt after the TreeSheets
# target exists.

FetchContent_Declare(
    libunibreak
    URL https://github.com/adah1972/libunibreak/releases/download/libunibreak_8_0/libunibreak-8.0.tar.gz
    URL_HASH SHA256=9c4fad6e517338a098373acc9f35579ae2c325e6446666fb9ac2666ba15ceba4
)
FetchContent_MakeAvailable(libunibreak)

set(UNIBREAK_SRC ${libunibreak_SOURCE_DIR}/src)
add_library(unibreak STATIC
    ${UNIBREAK_SRC}/unibreakbase.c
    ${UNIBREAK_SRC}/unibreakdef.c
    ${UNIBREAK_SRC}/linebreak.c
    ${UNIBREAK_SRC}/linebreakdata.c
    ${UNIBREAK_SRC}/linebreakdef.c
    ${UNIBREAK_SRC}/eastasianwidthdef.c
    ${UNIBREAK_SRC}/emojidef.c
)
target_include_directories(unibreak PUBLIC ${UNIBREAK_SRC})
target_link_libraries(TreeSheets PRIVATE unibreak)
