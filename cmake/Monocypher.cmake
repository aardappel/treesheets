# Monocypher (Argon2id key derivation, XChaCha20-Poly1305 encryption) for password protected
# documents, see src/encryption.h. Included from the top-level CMakeLists.txt after the TreeSheets
# target exists.

FetchContent_Declare(
    monocypher
    URL https://github.com/LoupVaillant/Monocypher/archive/refs/tags/4.0.3.tar.gz
    URL_HASH SHA256=a7cbae546fbdc489bca632c3747e1ceb8ca3d4bd39e2706a0916f28ccd280e50
)
FetchContent_MakeAvailable(monocypher)

add_library(monocypher STATIC ${monocypher_SOURCE_DIR}/src/monocypher.c)
target_include_directories(monocypher PUBLIC ${monocypher_SOURCE_DIR}/src)
target_link_libraries(TreeSheets PRIVATE monocypher)
# The random salts and nonces come from the OS: BCryptGenRandom on Windows, getentropy elsewhere.
if(WIN32)
    target_link_libraries(TreeSheets PRIVATE bcrypt)
endif()
