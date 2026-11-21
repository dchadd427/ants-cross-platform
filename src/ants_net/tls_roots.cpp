// The system's trusted root certificates, as each platform keeps them (see ants_net/tls.hpp). Platform code only: it hands the certificates over as bytes and reads no library header of the TLS code.
//   macOS    the anchor certificates of the Security framework (the roots that the system trusts), through the frameworks opened at run time (no link to them: see below)
//   Windows  the system stores "ROOT" (Trusted Root Certification Authorities) and "AuthRoot" (the third-party roots that the system adds itself)
//   others   the bundle file of the distribution, the first of the usual places that exists
#include "ants_net/tls.hpp"

#include <cstring>
#include <fstream>
#include <iterator>

#if defined(__APPLE__)
#include <dlfcn.h>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>

#include <set>
#endif

namespace ants::net {

namespace {

#if defined(_WIN32)
// The certificates of a system store, DER, in the order of the store
std::vector<std::string> read_store(const char* name) {
    std::vector<std::string> out;
    HCERTSTORE store = CertOpenSystemStoreA(0, name);
    if (store == nullptr) return out;
    for (PCCERT_CONTEXT cert = CertEnumCertificatesInStore(store, nullptr); cert != nullptr; cert = CertEnumCertificatesInStore(store, cert)) {
        if (cert->pbCertEncoded == nullptr || cert->cbCertEncoded == 0) continue;
        out.emplace_back(reinterpret_cast<const char*>(cert->pbCertEncoded), cert->cbCertEncoded);
    }
    CertCloseStore(store, 0);
    return out;
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
    // The roots that the system trusts, once each, without those that the user or an administrator has put in the store "Disallowed" (a certificate there is never to be trusted, whatever else says it is)
    const std::vector<std::string> disallowed = read_store("Disallowed");
    const std::set<std::string> refused(disallowed.begin(), disallowed.end());
    std::set<std::string> seen;
    for (const char* name : {"ROOT", "AuthRoot"}) {
        for (std::string& der : read_store(name)) {
            if (refused.count(der) != 0 || !seen.insert(der).second) continue;
            out.push_back(RootBlob{false, std::move(der)});
        }
    }
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

bool prime_platform_roots(const std::vector<std::string>& chain_der) {
#if defined(_WIN32)
    // Windows keeps few roots installed and installs the others when its own chain engine first needs one. The chain that the server presented is given to that engine once, in a store of memory
    // (the intermediates that the server sent), with no check of revocation: the engine finds the root that it trusts, fetches it if it has to and keeps it in the store "AuthRoot", where
    // read_system_roots() finds it the next time. The result of the engine is not looked at: whether the chain is trusted is for Mbed TLS to say, with the roots that the system then has.
    if (chain_der.empty()) return false;
    HCERTSTORE memory = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
    if (memory == nullptr) return false;
    PCCERT_CONTEXT leaf = nullptr;
    for (size_t i = 0; i < chain_der.size(); ++i) {
        PCCERT_CONTEXT added = nullptr;
        if (CertAddEncodedCertificateToStore(memory, X509_ASN_ENCODING, reinterpret_cast<const BYTE*>(chain_der[i].data()), static_cast<DWORD>(chain_der[i].size()), CERT_STORE_ADD_USE_EXISTING,
                                             i == 0 ? &added : nullptr) && i == 0) {
            leaf = added;
        }
    }
    bool asked = false;
    if (leaf != nullptr) {
        CERT_CHAIN_PARA parameters;
        std::memset(&parameters, 0, sizeof(parameters));
        parameters.cbSize = sizeof(parameters);
        PCCERT_CHAIN_CONTEXT engine = nullptr;
        asked = CertGetCertificateChain(nullptr, leaf, nullptr, memory, &parameters, 0, nullptr, &engine) != FALSE;
        if (engine != nullptr) CertFreeCertificateChain(engine);
        CertFreeCertificateContext(leaf);
    }
    CertCloseStore(memory, 0);
    return asked;
#else
    (void)chain_der;
    return false;                                       // (macOS and the systems with a bundle file have nothing to ask: the roots are what the file or the framework holds)
#endif
}

}  // namespace ants::net
