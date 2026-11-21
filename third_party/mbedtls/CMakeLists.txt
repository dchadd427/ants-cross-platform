# Mbed TLS (the TLS and X.509 library, Apache-2.0 option) as the static library ants_mbedtls. Native builds only: the desktop game's encrypted link to a game server
# (src/ants_net/tls.cpp) and the tests' own TLS server and certificates use it; the web build has the browser's TLS, and the game server leaves TLS to its proxy.
# The root project declares only C++, so C is enabled here.
#
# What is built: the TLS and X.509 sources of library/ and the cryptography sources of tf-psa-crypto/ (the PSA core, the builtin driver, platform, extras, utilities), every
# file as the release tarball has it (the generated files too: no Python or Perl is needed). Not built: net_sockets.c and timing.c (they are not vendored: the game has its own
# sockets and clock), the Everest and p256-m drivers (off in the default configuration, not vendored). The configuration is the release's default (include/mbedtls/mbedtls_config.h and
# tf-psa-crypto/include/psa/crypto_config.h, unchanged): the code that uses it (tls.cpp) sets TLS 1.2 as the lowest version and verifies the server's certificate always.
# The library is third-party code: its warnings are not ours (-w / /W0 after the project's flags, so -Werror / /WX have nothing to turn into errors).
enable_language(C)

file(GLOB ANTS_MBEDTLS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/library/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/core/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/drivers/builtin/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/platform/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/extras/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/utilities/*.c")

add_library(ants_mbedtls STATIC ${ANTS_MBEDTLS_SOURCES})
# The headers that a user of the library includes (<mbedtls/ssl.h>, <psa/crypto.h>): SYSTEM, so that their own warnings do not count either
target_include_directories(ants_mbedtls SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/drivers/builtin/include")
target_include_directories(ants_mbedtls PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/library"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/dispatch"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/extras"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/utilities"
    "${CMAKE_CURRENT_SOURCE_DIR}/tf-psa-crypto/drivers/builtin/src")
if(MSVC)
    target_compile_options(ants_mbedtls PRIVATE /W0)
else()
    target_compile_options(ants_mbedtls PRIVATE -w)
endif()
if(WIN32)
    target_link_libraries(ants_mbedtls PUBLIC bcrypt)       # the system's random numbers (the entropy source)
endif()
set_target_properties(ants_mbedtls PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99 C_EXTENSIONS ON)
