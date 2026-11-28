// The TLS client of the desktop game over Mbed TLS (see ants_net/tls.hpp for what it guarantees). The certificate check is the library's (the chain to a trusted root, the dates, the name), set to
// VERIFY_REQUIRED, with the name of the URL given to the library before the handshake; once the handshake is done the recorded verification result must be clean as well, so that a configuration
// slip could never make a link that was not checked an open one.
#include "ants_net/tls.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ants_net/mbedtls_include.hpp"

namespace ants::net {

namespace {

constexpr size_t kMaxHostChars = 253;               // a DNS name is at most 253 characters
constexpr size_t kMaxPendingBytes = 4u * 1024u * 1024u;   // encrypted bytes that wait for the socket: a peer that never reads must not make this grow without bound
constexpr size_t kRecordBytes = 16000;              // plaintext handed to the library at once (a TLS record holds 16384)

bool crypto_ready() {
    static const bool ok = psa_crypto_init() == PSA_SUCCESS;
    return ok;
}

// Where the roots come from: the platform, or the function that a test gave
std::function<std::vector<RootBlob>()>& roots_source() {
    static std::function<std::vector<RootBlob>()> source;
    return source;
}
std::function<bool(const std::vector<std::string>&)>& root_primer() {
    static std::function<bool(const std::vector<std::string>&)> primer;
    return primer;
}

// The parsed roots of the system's store, read once (and again after reload_system_roots(): a link that is being made keeps the chain that it started with, which it holds by shared pointer)
struct SystemChain {
    std::vector<RootBlob> blobs;
    mbedtls_x509_crt chain;
    size_t count{0};
    SystemChain() {
        mbedtls_x509_crt_init(&chain);
        blobs = roots_source() ? roots_source()() : read_system_roots();
        for (const RootBlob& b : blobs) add_blob(chain, b);
        for (const mbedtls_x509_crt* c = &chain; c != nullptr; c = c->next) {
            if (c->raw.p != nullptr) ++count;
        }
    }
    ~SystemChain() { mbedtls_x509_crt_free(&chain); }
    SystemChain(const SystemChain&) = delete;
    SystemChain& operator=(const SystemChain&) = delete;

    // (a blob that holds a certificate the library cannot read adds the others and skips that one)
    static void add_blob(mbedtls_x509_crt& into, const RootBlob& b) {
        if (b.pem) {
            std::string text = b.bytes;
            mbedtls_x509_crt_parse(&into, reinterpret_cast<const unsigned char*>(text.c_str()), text.size() + 1);    // (the terminating NUL counts for PEM)
        } else {
            mbedtls_x509_crt_parse_der(&into, reinterpret_cast<const unsigned char*>(b.bytes.data()), b.bytes.size());
        }
    }
};

std::shared_ptr<const SystemChain>& chain_slot() {
    static std::shared_ptr<const SystemChain> slot;
    return slot;
}

std::shared_ptr<const SystemChain> system_chain() {
    std::shared_ptr<const SystemChain>& slot = chain_slot();
    if (!slot) slot = std::make_shared<const SystemChain>();
    return slot;
}

// The suites that are offered: authenticated encryption only (TLS 1.3, and TLS 1.2 with AES-GCM or ChaCha20-Poly1305 over ECDHE). The library's own list holds suites with a CBC mode and a SHA-1 MAC, CCM_8, Camellia and ARIA
// that no server of the site needs; the usual web servers' defaults (TLS 1.3, ECDHE with GCM and ChaCha20) are all in this list. The list lives as long as the program (the configuration keeps a pointer to it).
const int kCipherSuites[] = {
    MBEDTLS_TLS1_3_AES_128_GCM_SHA256,
    MBEDTLS_TLS1_3_AES_256_GCM_SHA384,
    MBEDTLS_TLS1_3_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
    MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
    0,
};

std::string library_text(int code) {
    char buf[160];
    mbedtls_strerror(code, buf, sizeof(buf));
    return std::string(buf) + " (-0x" + [&]() {
        char hex[16];
        std::snprintf(hex, sizeof hex, "%04X", static_cast<unsigned>(-code));
        return std::string(hex);
    }() + ")";
}

// What a failed certificate check means to a player, from the library's flags
TlsFailure verify_kind(uint32_t flags) {
    if ((flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) != 0) return TlsFailure::WrongName;
    if ((flags & (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE)) != 0) return TlsFailure::Dates;
    if ((flags & (MBEDTLS_X509_BADCERT_NOT_TRUSTED | MBEDTLS_X509_BADCRL_NOT_TRUSTED)) != 0) return TlsFailure::Untrusted;
    return TlsFailure::NotVerified;
}

std::string verify_text(TlsFailure kind, const std::string& host) {
    switch (kind) {
        case TlsFailure::WrongName: return "The server's certificate is not for " + host + ", so the link is not safe.";
        case TlsFailure::Dates: return "The server's certificate has expired or is not valid yet (check the date and time of this computer).";
        case TlsFailure::Untrusted: return "The server's certificate is not trusted by this computer, so the link is not safe.";
        default: return "The server's certificate could not be verified, so the link is not safe.";
    }
}

}  // namespace

struct TlsClient::Impl {
    std::unique_ptr<TcpStream> stream;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt own_chain;                     // the roots of this link when it has roots of its own (the system's and the caller's extra ones); else the shared system chain is used
    std::shared_ptr<const SystemChain> shared_roots;    // the system's roots of this link when it uses them alone (kept alive for as long as the configuration points at them)
    std::vector<std::string> presented;             // the certificates of the server by depth (0: its own), recorded for the priming of the roots
    std::vector<uint8_t> pending;                   // plaintext that waits to be handed to the library (it waits when the socket is full)
    size_t inflight{0};                             // the length of a write that the library has not finished: the retry must repeat it
    std::string host;
    bool closed_sent{false};

    Impl() {
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_x509_crt_init(&own_chain);
    }
    ~Impl() {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_x509_crt_free(&own_chain);
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // Called by the library for every certificate of the chain as it judges it: the certificates are noted (never the verdict: the flags are left as the library set them, and the result is 0)
    static int note_certificate(void* ctx, mbedtls_x509_crt* crt, int depth, uint32_t* flags) {
        (void)flags;
        Impl* m = static_cast<Impl*>(ctx);
        if (crt != nullptr && crt->raw.p != nullptr && depth >= 0 && depth < 16) {
            if (m->presented.size() <= static_cast<size_t>(depth)) m->presented.resize(static_cast<size_t>(depth) + 1);
            m->presented[static_cast<size_t>(depth)].assign(reinterpret_cast<const char*>(crt->raw.p), crt->raw.len);
        }
        return 0;
    }
    static int bio_send(void* ctx, const unsigned char* buf, size_t len) {
        TcpStream* s = static_cast<Impl*>(ctx)->stream.get();
        const long n = s->send(buf, len);
        if (n < 0) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        if (n == 0) return MBEDTLS_ERR_SSL_WANT_WRITE;
        return static_cast<int>(n);
    }
    static int bio_recv(void* ctx, unsigned char* buf, size_t len) {
        TcpStream* s = static_cast<Impl*>(ctx)->stream.get();
        const long n = s->recv(buf, len);
        if (n > 0) return static_cast<int>(n);
        if (n == 0) return MBEDTLS_ERR_SSL_WANT_READ;
        return s->state() == TcpStream::State::Closed ? 0 : MBEDTLS_ERR_SSL_INTERNAL_ERROR;      // 0: the peer closed the socket in an orderly way
    }
};

TlsClient::TlsClient() = default;

TlsClient::~TlsClient() {
    if (impl_ != nullptr) close();
}

std::unique_ptr<TlsClient> TlsClient::start(std::unique_ptr<TcpStream> stream, const std::string& host, const TlsTrust& trust, std::string& why) {
    if (!stream) {
        why = "The server could not be reached.";
        return nullptr;
    }
    if (host.empty() || host.size() > kMaxHostChars || host.find('\0') != std::string::npos) {
        why = "The server's name is not usable for a secure link.";
        return nullptr;
    }
    if (!crypto_ready()) {
        why = "The security library could not start on this computer.";
        return nullptr;
    }
    std::unique_ptr<TlsClient> client(new TlsClient());
    client->impl_.reset(new Impl());
    Impl& m = *client->impl_;
    m.host = host;

    // the roots: the shared chain of the system's store when nothing is added, else a chain of this link's own
    const mbedtls_x509_crt* roots = nullptr;
    if (trust.extra_pem.empty() && trust.system_roots) {
        m.shared_roots = system_chain();
        if (m.shared_roots->count == 0) {
            why = "No trusted root certificates could be read on this computer, so a secure link cannot be made.";
            return nullptr;
        }
        roots = &m.shared_roots->chain;
    } else {
        if (trust.system_roots) {
            const std::shared_ptr<const SystemChain> system = system_chain();
            for (const RootBlob& b : system->blobs) SystemChain::add_blob(m.own_chain, b);
        }
        if (!trust.extra_pem.empty()) {
            std::string text = trust.extra_pem;
            if (mbedtls_x509_crt_parse(&m.own_chain, reinterpret_cast<const unsigned char*>(text.c_str()), text.size() + 1) != 0 && m.own_chain.raw.p == nullptr) {
                why = "The certificate that was given as trusted could not be read.";
                return nullptr;
            }
        }
        if (m.own_chain.raw.p == nullptr) {
            why = "No trusted root certificates could be read on this computer, so a secure link cannot be made.";
            return nullptr;
        }
        roots = &m.own_chain;
    }

    int rc = mbedtls_ssl_config_defaults(&m.conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) {
        why = "The security library could not be set up: " + library_text(rc);
        return nullptr;
    }
    mbedtls_ssl_conf_min_tls_version(&m.conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_authmode(&m.conf, MBEDTLS_SSL_VERIFY_REQUIRED);                 // the certificate is checked, always: a failed check fails the handshake
    mbedtls_ssl_conf_ca_chain(&m.conf, const_cast<mbedtls_x509_crt*>(roots), nullptr);
    mbedtls_ssl_conf_ciphersuites(&m.conf, kCipherSuites);
    mbedtls_ssl_conf_verify(&m.conf, Impl::note_certificate, &m);                    // (it only notes the chain, see there)
#if defined(MBEDTLS_SSL_SESSION_TICKETS)
    mbedtls_ssl_conf_session_tickets(&m.conf, MBEDTLS_SSL_SESSION_TICKETS_DISABLED);  // every link starts from nothing: nothing is kept that could be replayed
#endif
    rc = mbedtls_ssl_setup(&m.ssl, &m.conf);
    if (rc == 0) rc = mbedtls_ssl_set_hostname(&m.ssl, host.c_str());                // the name that the certificate must be for (and the name the server is asked for)
    if (rc != 0) {
        why = "The security library could not be set up: " + library_text(rc);
        return nullptr;
    }
    m.stream = std::move(stream);
    mbedtls_ssl_set_bio(&m.ssl, &m, Impl::bio_send, Impl::bio_recv, nullptr);
    return client;
}

void TlsClient::fail(TlsFailure kind, const std::string& why) {
    if (state_ == State::Failed) return;
    state_ = State::Failed;
    failure_ = kind;
    error_ = why;
    if (impl_ && impl_->stream) impl_->stream->close();
}

TlsClient::State TlsClient::advance() {
    if (state_ != State::Handshaking) {
        if (state_ == State::Open) flush();
        return state_;
    }
    Impl& m = *impl_;
    switch (m.stream->state()) {
        case TcpStream::State::Connecting: return state_;
        case TcpStream::State::Failed: fail(TlsFailure::Unreachable, "The server could not be reached."); return state_;
        case TcpStream::State::Closed: fail(TlsFailure::Protocol, "The server closed the connection before the secure link was made."); return state_;
        case TcpStream::State::Open: break;
    }
    const int rc = mbedtls_ssl_handshake(&m.ssl);
    if (rc == 0) {
        const uint32_t flags = mbedtls_ssl_get_verify_result(&m.ssl);
        if (flags != 0) {                                                            // (not reachable with VERIFY_REQUIRED; the second check makes a slip in the configuration harmless)
            const TlsFailure kind = verify_kind(flags);
            fail(kind, verify_text(kind, m.host));
            return state_;
        }
        state_ = State::Open;
        flush();
    } else if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
        if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
            const TlsFailure kind = verify_kind(mbedtls_ssl_get_verify_result(&m.ssl));
            fail(kind, verify_text(kind, m.host));
        } else if (m.stream->state() != TcpStream::State::Open) {
            fail(TlsFailure::Protocol, "The server closed the connection before the secure link was made.");
        } else {
            fail(TlsFailure::Protocol, "The secure link to the server could not be set up: " + library_text(rc));
        }
    }
    return state_;
}

long TlsClient::read(uint8_t* buffer, size_t size) {
    if (state_ != State::Open) return -1;
    if (size == 0) return 0;
    Impl& m = *impl_;
    flush();
    for (int rounds = 0; rounds < 4; ++rounds) {
        const int rc = mbedtls_ssl_read(&m.ssl, buffer, size);
        if (rc > 0) return rc;
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
#if defined(MBEDTLS_SSL_SESSION_TICKETS)
        if (rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;             // a ticket that nobody asked for: nothing to do with it, read on
#endif
        if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || rc == MBEDTLS_ERR_SSL_CONN_EOF) {
            state_ = State::Closed;
            m.stream->close();
            return -1;
        }
        fail(TlsFailure::Broken, "The secure link to the server broke: " + library_text(rc));
        return -1;
    }
    return 0;
}

long TlsClient::write(const uint8_t* data, size_t size) {
    if (state_ != State::Open) return -1;
    Impl& m = *impl_;
    if (m.pending.size() + size > kMaxPendingBytes) {                                // a peer that never reads
        fail(TlsFailure::Broken, "The server stopped reading, so the link was dropped.");
        return -1;
    }
    m.pending.insert(m.pending.end(), data, data + size);
    flush();
    return state_ == State::Open ? static_cast<long>(size) : -1;
}

size_t TlsClient::flush() {
    if (state_ != State::Open) return impl_ ? impl_->pending.size() : 0;
    Impl& m = *impl_;
    while (!m.pending.empty()) {
        const size_t len = m.inflight != 0 ? m.inflight : std::min(m.pending.size(), kRecordBytes);
        const int rc = mbedtls_ssl_write(&m.ssl, m.pending.data(), len);
        if (rc > 0) {
            m.pending.erase(m.pending.begin(), m.pending.begin() + rc);
            m.inflight = 0;
        } else if (rc == MBEDTLS_ERR_SSL_WANT_WRITE || rc == MBEDTLS_ERR_SSL_WANT_READ) {
            m.inflight = len;                                                        // (the library wants the same call again)
            break;
        } else {
            fail(TlsFailure::Broken, "The secure link to the server broke: " + library_text(rc));
            break;
        }
    }
    return m.pending.size();
}

void TlsClient::close() {
    if (!impl_) return;
    Impl& m = *impl_;
    if (state_ == State::Open && !m.closed_sent && m.stream && m.stream->state() == TcpStream::State::Open) {
        m.closed_sent = true;
        mbedtls_ssl_close_notify(&m.ssl);                                            // (best effort: a full socket or a peer that is gone is not waited for)
    }
    if (m.stream) m.stream->close();
    if (state_ != State::Failed) state_ = State::Closed;
}

std::string TlsClient::describe() const {
    if (!impl_ || state_ != State::Open) return std::string();
    const char* version = mbedtls_ssl_get_version(&impl_->ssl);
    const char* suite = mbedtls_ssl_get_ciphersuite(&impl_->ssl);
    return std::string(version != nullptr ? version : "?") + " " + (suite != nullptr ? suite : "?");
}

std::vector<std::string> TlsClient::presented_chain() const {
    std::vector<std::string> out;
    if (impl_) {
        for (const std::string& der : impl_->presented) {
            if (!der.empty()) out.push_back(der);
        }
    }
    return out;
}

bool prime_system_roots(const std::vector<std::string>& chain_der) {
    if (chain_der.empty()) return false;
    return root_primer() ? root_primer()(chain_der) : prime_platform_roots(chain_der);
}

void reload_system_roots() { chain_slot().reset(); }

void set_system_roots_source_for_test(std::function<std::vector<RootBlob>()> source) {
    roots_source() = std::move(source);
    reload_system_roots();
}

void set_root_primer_for_test(std::function<bool(const std::vector<std::string>&)> primer) { root_primer() = std::move(primer); }

size_t system_root_count() { return crypto_ready() ? system_chain()->count : 0; }

bool tls_random(uint8_t* out, size_t size) {
    if (size == 0) return true;
    return crypto_ready() && psa_generate_random(out, size) == PSA_SUCCESS;
}

}  // namespace ants::net
