// The system's trusted root certificates, as each platform keeps them (see ants_net/tls.hpp). Platform code only: it hands the certificates over as bytes and reads no library header of the TLS code.
//   macOS    the anchor certificates of the Security framework (the roots that the system trusts), through the frameworks opened at run time (no link to them: see below)
//   Windows  the system stores "ROOT" (Trusted Root Certification Authorities) and "AuthRoot" (the third-party roots that the system adds itself)
//   others   the bundle file of the distribution, the first of the usual places that exists
#include "ants_net/tls.hpp"

#include <fstream>
#include <iterator>

#if defined(__APPLE__)
#include <dlfcn.h>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#endif

namespace ants::net {

namespace {

#if defined(_WIN32)
void read_store(const char* name, std::vector<RootBlob>& out) {
    HCERTSTORE store = CertOpenSystemStoreA(0, name);
    if (store == nullptr) return;
    for (PCCERT_CONTEXT cert = CertEnumCertificatesInStore(store, nullptr); cert != nullptr; cert = CertEnumCertificatesInStore(store, cert)) {
        if (cert->pbCertEncoded == nullptr || cert->cbCertEncoded == 0) continue;
        out.push_back(RootBlob{false, std::string(reinterpret_cast<const char*>(cert->pbCertEncoded), cert->cbCertEncoded)});
    }
    CertCloseStore(store, 0);
}
#elif !defined(__APPLE__)
bool read_file(const char* path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}
#endif

}  // namespace

std::vector<RootBlob> read_system_roots() {
    std::vector<RootBlob> out;
#if defined(__APPLE__)
    // The frameworks are opened when the roots are read, not linked: a program that links CoreFoundation has the variable __CF_USER_TEXT_ENCODING put into its environment (and into its children's),
    // which the game server and its tests, which never read the roots, must not have.
    void* security = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY | RTLD_LOCAL);
    void* core = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", RTLD_LAZY | RTLD_LOCAL);
    if (security != nullptr && core != nullptr) {
        const auto copy_anchors = reinterpret_cast<decltype(&SecTrustCopyAnchorCertificates)>(dlsym(security, "SecTrustCopyAnchorCertificates"));
        const auto copy_data = reinterpret_cast<decltype(&SecCertificateCopyData)>(dlsym(security, "SecCertificateCopyData"));
        const auto array_count = reinterpret_cast<decltype(&CFArrayGetCount)>(dlsym(core, "CFArrayGetCount"));
        const auto array_at = reinterpret_cast<decltype(&CFArrayGetValueAtIndex)>(dlsym(core, "CFArrayGetValueAtIndex"));
        const auto data_bytes = reinterpret_cast<decltype(&CFDataGetBytePtr)>(dlsym(core, "CFDataGetBytePtr"));
        const auto data_length = reinterpret_cast<decltype(&CFDataGetLength)>(dlsym(core, "CFDataGetLength"));
        const auto release = reinterpret_cast<decltype(&CFRelease)>(dlsym(core, "CFRelease"));
        if (copy_anchors && copy_data && array_count && array_at && data_bytes && data_length && release) {
            CFArrayRef anchors = nullptr;
            if (copy_anchors(&anchors) == errSecSuccess && anchors != nullptr) {
                const CFIndex count = array_count(anchors);
                for (CFIndex i = 0; i < count; ++i) {
                    SecCertificateRef cert = reinterpret_cast<SecCertificateRef>(const_cast<void*>(array_at(anchors, i)));
                    CFDataRef der = copy_data(cert);
                    if (der == nullptr) continue;
                    out.push_back(RootBlob{false, std::string(reinterpret_cast<const char*>(data_bytes(der)), static_cast<size_t>(data_length(der)))});
                    release(der);
                }
                release(anchors);
            }
        }
    }
#elif defined(_WIN32)
    read_store("ROOT", out);
    read_store("AuthRoot", out);
#else
    static const char* const kBundles[] = {
        "/etc/ssl/certs/ca-certificates.crt",       // Debian, Ubuntu, Gentoo
        "/etc/pki/tls/certs/ca-bundle.crt",         // Fedora, Red Hat
        "/etc/ssl/ca-bundle.pem",                   // openSUSE
        "/etc/pki/tls/cacert.pem",                  // other Red Hat family
        "/etc/ssl/cert.pem",                        // Alpine, the BSDs
        "/usr/local/etc/ssl/cert.pem",              // FreeBSD
    };
    for (const char* path : kBundles) {
        std::string text;
        if (read_file(path, text)) {
            out.push_back(RootBlob{true, std::move(text)});
            break;
        }
    }
#endif
    return out;
}

}  // namespace ants::net
