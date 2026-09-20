# Direct PDF export via wxPDFDocument (and its bundled woff2 and zint dependencies). Included from
# the top-level CMakeLists.txt when ENABLE_WXPDFDOC is ON, after the TreeSheets target exists.

FetchContent_Declare(
    wxpdfdoc
    URL https://github.com/utelle/wxpdfdoc/archive/refs/tags/v1.4.0.tar.gz
    URL_HASH SHA256=f651471dfe24b49947434e5930ee550ba40c496c194a64050892ee1799357c97
)
FetchContent_MakeAvailable(wxpdfdoc)

set(WOFF2_DIR ${wxpdfdoc_SOURCE_DIR}/thirdparty/woff2)
add_library(wxpdfdoc_woff2 STATIC
    ${WOFF2_DIR}/brotli/common/constants.c
    ${WOFF2_DIR}/brotli/common/context.c
    ${WOFF2_DIR}/brotli/common/dictionary.c
    ${WOFF2_DIR}/brotli/common/platform.c
    ${WOFF2_DIR}/brotli/common/shared_dictionary.c
    ${WOFF2_DIR}/brotli/common/transform.c
    ${WOFF2_DIR}/brotli/dec/bit_reader.c
    ${WOFF2_DIR}/brotli/dec/decode.c
    ${WOFF2_DIR}/brotli/dec/huffman.c
    ${WOFF2_DIR}/brotli/dec/state.c
    ${WOFF2_DIR}/src/table_tags.cc
    ${WOFF2_DIR}/src/variable_length.cc
    ${WOFF2_DIR}/src/woff2_common.cc
    ${WOFF2_DIR}/src/woff2_dec.cc
    ${WOFF2_DIR}/src/woff2_out.cc
)
target_include_directories(wxpdfdoc_woff2 PUBLIC
    ${WOFF2_DIR}/include
    ${WOFF2_DIR}/src
    ${WOFF2_DIR}/brotli/include
)

set(ZINT_DIR ${wxpdfdoc_SOURCE_DIR}/thirdparty/zint/backend)
file(GLOB ZINT_SRCS CONFIGURE_DEPENDS ${ZINT_DIR}/*.c)
add_library(wxpdfdoc_zint STATIC ${ZINT_SRCS})
target_compile_definitions(wxpdfdoc_zint PRIVATE ZINT_NO_PNG _LIB)
target_include_directories(wxpdfdoc_zint PUBLIC ${ZINT_DIR} ${ZINT_DIR}/fonts)

file(GLOB WXPDFDOC_SRCS CONFIGURE_DEPENDS
    ${wxpdfdoc_SOURCE_DIR}/src/*.cpp
    ${wxpdfdoc_SOURCE_DIR}/src/crypto/*.cpp
    ${wxpdfdoc_SOURCE_DIR}/src/woff/*.cpp
)
add_library(wxpdfdoc STATIC ${WXPDFDOC_SRCS})
target_include_directories(wxpdfdoc PUBLIC ${wxpdfdoc_SOURCE_DIR}/include)
target_compile_definitions(wxpdfdoc PUBLIC WXUSINGLIB_PDFDOC)
target_link_libraries(wxpdfdoc
    PUBLIC  wx::core wx::base wx::xml
    PRIVATE wxpdfdoc_woff2 wxpdfdoc_zint
)
if(LINUX OR BSD)
    find_package(Fontconfig REQUIRED)
    target_link_libraries(wxpdfdoc PRIVATE Fontconfig::Fontconfig)
endif()

target_link_libraries(TreeSheets PRIVATE wxpdfdoc)
target_compile_definitions(TreeSheets PRIVATE "ENABLE_WXPDFDOC=1")
