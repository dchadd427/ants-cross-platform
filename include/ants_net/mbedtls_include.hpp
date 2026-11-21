#pragma once

// The headers of Mbed TLS (third_party/mbedtls) that the encrypted link and its tests use, in one place, with the compiler's warnings off while they are read: they are third-party code, and
// the project builds with every warning an error. (GCC and clang already treat the library's include folder as a system folder; Visual C++ needs the pragma, since not every version takes the
// folder as an external one.) Native builds only.

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
