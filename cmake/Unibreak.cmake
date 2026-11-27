# libunibreak (Unicode line breaking, UAX #14, and East Asian Width) for wrapping cell text, see
# Text::GetLine in src/text.h. Included from the top-level CMakeLists.txt after the TreeSheets
# target exists.

FetchContent_Declare(
    libunibreak
    URL https://github.com/adah1972/libunibreak/releases/download/libunibreak_8_0_1/libunibreak-8.0.1.tar.gz
    URL_HASH SHA256=ac0dcc96c395bc86ff7f86f6b91d8b45bc8c39db775f3f1e73f947a11520737b
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
