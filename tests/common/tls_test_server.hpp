// A certificate maker and a TLS terminator for the tests of the encrypted link (ants_net/wss.hpp). Header only: everything is inline. It stands in for the site's proxy: it holds a certificate that a test
// authority issued, ends TLS on a loopback port and passes the decrypted bytes on to a plain game server door (a WsListener) or answers them itself with a script. The client under test trusts the
// test authority explicitly (TlsTrust::extra_pem, with the system's roots off): nothing in the product skips a check.
//
//   Ca          makes a P-256 key and a self-signed authority certificate (pem()), and issues leaf certificates for DNS names and IP addresses with any validity
//   Identity    a leaf's key and certificate chain, which the terminator serves
//   Terminator  listens on 127.0.0.1; one pass of pump() makes every link progress (nothing blocks, no thread: the test's loop calls it, as it calls the server's and the clients'). It keeps the bytes that
//               crossed the wire as they were (raw_in, raw_out), which is how a test shows that nothing readable travels in the clear.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "ants_net/mbedtls_include.hpp"
#include "ants_net/tcp.hpp"

namespace ants_test_tls {

inline void init() {
    static const bool ok = psa_crypto_init() == PSA_SUCCESS;
    if (!ok) throw std::runtime_error("psa_crypto_init failed");
}

inline std::string error_text(int code) {
    char buf[200];
    mbedtls_strerror(code, buf, sizeof(buf));
    return buf;
}

inline void check(int rc, const char* what) {
    if (rc != 0) throw std::runtime_error(std::string(what) + ": " + error_text(rc));
}

// "YYYYMMDDHHMMSS" (UTC) of now + `days` days
inline std::string stamp(int days) {
    std::time_t t = std::time(nullptr) + static_cast<std::time_t>(days) * 86400;
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y%m%d%H%M%S", &utc);
    return buf;
}

// The names that a leaf certificate is for
struct Names {
    std::vector<std::string> dns;
    std::vector<std::string> ips;           // "127.0.0.1" (any dotted IPv4) or "::1"
};

class Key {
public:
    Key() {
        init();
        mbedtls_pk_init(&pk_);
        psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
        psa_set_key_bits(&attr, 256);
        psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_HASH | PSA_KEY_USAGE_VERIFY_MESSAGE | PSA_KEY_USAGE_EXPORT);
        psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_ANY_HASH));
        mbedtls_svc_key_id_t id = MBEDTLS_SVC_KEY_ID_INIT;
        if (psa_generate_key(&attr, &id) != PSA_SUCCESS) throw std::runtime_error("psa_generate_key failed");
        const int rc = mbedtls_pk_copy_from_psa(id, &pk_);
        psa_destroy_key(id);
        check(rc, "mbedtls_pk_copy_from_psa");
    }
    ~Key() { mbedtls_pk_free(&pk_); }
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    mbedtls_pk_context* pk() { return &pk_; }

private:
    mbedtls_pk_context pk_;
};

// The value of the subjectAltName extension in DER: a SEQUENCE of dNSName [2] and iPAddress [7] entries
inline std::vector<uint8_t> san_value(const Names& names) {
    std::vector<uint8_t> body;
    for (const std::string& d : names.dns) {
        body.push_back(0x82);
        body.push_back(static_cast<uint8_t>(d.size()));
        body.insert(body.end(), d.begin(), d.end());
    }
    for (const std::string& ip : names.ips) {
        std::vector<uint8_t> raw;
        if (ip == "::1") {
            raw.assign(16, 0);
            raw[15] = 1;
        } else {
            unsigned a = 0, b = 0, c = 0, d = 0;
            if (std::sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) throw std::runtime_error("test certificates take dotted IPv4 addresses and ::1");
            raw = {static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c), static_cast<uint8_t>(d)};
        }
        body.push_back(0x87);
        body.push_back(static_cast<uint8_t>(raw.size()));
        body.insert(body.end(), raw.begin(), raw.end());
    }
    std::vector<uint8_t> out{0x30, static_cast<uint8_t>(body.size())};      // (the names of a test are short: the length fits one byte)
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

// A leaf's key and chain, which the terminator serves
struct Identity {
    Key key;
    mbedtls_x509_crt chain;
    Identity() { mbedtls_x509_crt_init(&chain); }
    ~Identity() { mbedtls_x509_crt_free(&chain); }
    Identity(const Identity&) = delete;
    Identity& operator=(const Identity&) = delete;
};

// One certificate in DER: for `subject_pk` (named `subject_dn`), signed with `issuer_pk` (named `issuer_dn`)
inline std::vector<uint8_t> make_certificate(mbedtls_pk_context* subject_pk, const std::string& subject_dn, mbedtls_pk_context* issuer_pk, const std::string& issuer_dn, const Names* names,
                                             bool is_ca, int from_days, int to_days) {
    mbedtls_x509write_cert crt;
    mbedtls_x509write_crt_init(&crt);
    std::vector<uint8_t> der;
    try {
        mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&crt, subject_pk);
        mbedtls_x509write_crt_set_issuer_key(&crt, issuer_pk);
        check(mbedtls_x509write_crt_set_subject_name(&crt, subject_dn.c_str()), "subject name");
        check(mbedtls_x509write_crt_set_issuer_name(&crt, issuer_dn.c_str()), "issuer name");
        uint8_t serial[8];
        if (psa_generate_random(serial, sizeof serial) != PSA_SUCCESS) throw std::runtime_error("no random numbers");
        serial[0] = static_cast<uint8_t>((serial[0] & 0x7Fu) | 0x01u);                          // positive, not zero
        check(mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof serial), "serial");
        check(mbedtls_x509write_crt_set_validity(&crt, stamp(from_days).c_str(), stamp(to_days).c_str()), "validity");
        check(mbedtls_x509write_crt_set_basic_constraints(&crt, is_ca ? 1 : 0, is_ca ? -1 : 0), "basic constraints");
        check(mbedtls_x509write_crt_set_key_usage(&crt, is_ca ? MBEDTLS_X509_KU_KEY_CERT_SIGN : MBEDTLS_X509_KU_DIGITAL_SIGNATURE), "key usage");
        if (names != nullptr) {
            const std::vector<uint8_t> san = san_value(*names);
            check(mbedtls_x509write_crt_set_extension(&crt, "\x55\x1D\x11", 3, 0, san.data(), san.size()), "subject alternative names");
        }
        der.resize(2048);
        const int n = mbedtls_x509write_crt_der(&crt, der.data(), der.size());
        if (n < 0) throw std::runtime_error("write the certificate: " + error_text(n));
        der.erase(der.begin(), der.end() - n);                                                  // (the library writes at the END of the buffer)
        der.resize(static_cast<size_t>(n));
    } catch (...) {
        mbedtls_x509write_crt_free(&crt);
        throw;
    }
    mbedtls_x509write_crt_free(&crt);
    return der;
}

inline std::string pem_of(const std::vector<uint8_t>& der) {
    static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64;
    for (size_t i = 0; i < der.size(); i += 3) {
        const uint32_t v = (uint32_t{der[i]} << 16) | (i + 1 < der.size() ? uint32_t{der[i + 1]} << 8 : 0u) | (i + 2 < der.size() ? uint32_t{der[i + 2]} : 0u);
        b64.push_back(kB64[(v >> 18) & 63u]);
        b64.push_back(kB64[(v >> 12) & 63u]);
        b64.push_back(i + 1 < der.size() ? kB64[(v >> 6) & 63u] : '=');
        b64.push_back(i + 2 < der.size() ? kB64[v & 63u] : '=');
    }
    std::string out = "-----BEGIN CERTIFICATE-----\n";
    for (size_t i = 0; i < b64.size(); i += 64) out += b64.substr(i, 64) + "\n";
    return out + "-----END CERTIFICATE-----\n";
}

class Ca {
public:
    explicit Ca(const std::string& common_name = "Ants Test CA") : name_("CN=" + common_name + ",O=Ants Tests") {
        pem_ = pem_of(make_certificate(key_.pk(), name_, key_.pk(), name_, nullptr, true, -1, 3650));
    }
    // The authority's certificate as a client is told to trust it
    const std::string& pem() const noexcept { return pem_; }
    // A leaf for `names`, valid from `from_days` to `to_days` after now (negative: in the past)
    std::unique_ptr<Identity> issue(const Names& names, int from_days = -1, int to_days = 30) {
        std::unique_ptr<Identity> id(new Identity());
        const std::string cn = names.dns.empty() ? std::string("ants-test") : names.dns[0];
        const std::vector<uint8_t> leaf = make_certificate(id->key.pk(), "CN=" + cn, key_.pk(), name_, &names, false, from_days, to_days);
        check(mbedtls_x509_crt_parse_der(&id->chain, leaf.data(), leaf.size()), "parse the leaf");
        return id;
    }

private:
    Key key_;
    std::string name_;
    std::string pem_;
};

// ---------------------------------------------------------------------------------------------------------------------------------

struct TerminatorOptions {
    uint16_t backend_port{0};                                   // bridge every link to 127.0.0.1:port (a plain WsListener); 0: answer with `script`
    std::function<std::string(std::string& inbox)> script;      // scripted: called when bytes arrive with everything that has arrived and was not taken (it may erase what it used); returns the bytes to send back
    bool use_tls{true};                                         // false: a server that does not speak TLS (the bytes are plain)
    mbedtls_ssl_protocol_version max_version{MBEDTLS_SSL_VERSION_TLS1_3};
    bool close_after_script{false};                             // scripted: close the link after the first answer
};

class Terminator {
public:
    // `identity` may be null when the options say use_tls = false
    Terminator(Identity* identity, TerminatorOptions options) : options_(std::move(options)) {
        init();
        listener_ = ants::net::TcpListener::listen(0, true);
        if (!listener_) throw std::runtime_error("the terminator cannot listen");
        mbedtls_ssl_config_init(&conf_);
        if (options_.use_tls) {
            check(mbedtls_ssl_config_defaults(&conf_, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT), "server defaults");
            check(mbedtls_ssl_conf_own_cert(&conf_, &identity->chain, identity->key.pk()), "own certificate");
            mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_NONE);
            mbedtls_ssl_conf_max_tls_version(&conf_, options_.max_version);
        }
    }
    ~Terminator() {
        links_.clear();
        mbedtls_ssl_config_free(&conf_);
    }
    Terminator(const Terminator&) = delete;
    Terminator& operator=(const Terminator&) = delete;

    uint16_t port() const noexcept { return listener_->port(); }

    // One pass over the listener and every link
    void pump() {
        while (auto s = listener_->accept_stream()) {
            ++accepted;
            std::unique_ptr<Link> l(new Link());
            l->owner = this;
            l->client = std::move(s);
            if (options_.use_tls) {
                if (mbedtls_ssl_setup(&l->ssl, &conf_) != 0) throw std::runtime_error("mbedtls_ssl_setup");
                mbedtls_ssl_set_bio(&l->ssl, l.get(), Link::send_cb, Link::recv_cb, nullptr);
            } else {
                l->handshaken = true;
            }
            if (options_.backend_port != 0) l->backend = ants::net::TcpStream::connect("127.0.0.1", options_.backend_port);
            links_.push_back(std::move(l));
        }
        for (std::unique_ptr<Link>& l : links_) step(*l);
        links_.erase(std::remove_if(links_.begin(), links_.end(), [](const std::unique_ptr<Link>& l) { return l->dead; }), links_.end());
    }
    // Every link is dropped without a goodbye, as a pulled cable does
    void cut_all() {
        for (std::unique_ptr<Link>& l : links_) {
            if (l->client) l->client->close();
            if (l->backend) l->backend->close();
            l->dead = true;
        }
        links_.clear();
    }
    size_t open_links() const { return links_.size(); }
    // What crossed the wire on the client's side, as it was (all links together, in the order they were read)
    const std::string& raw_in() const { return raw_in_; }
    const std::string& raw_out() const { return raw_out_; }
    // The protocol and cipher of the most recent handshake, as the library names them
    const std::string& last_cipher() const { return last_cipher_; }
    const std::string& last_failure() const { return last_failure_; }

    uint32_t accepted{0};
    uint32_t handshakes{0};
    uint32_t handshake_failures{0};
    uint64_t app_bytes_up{0};           // decrypted bytes that came from clients

private:
    struct Link {
        Terminator* owner{nullptr};
        std::unique_ptr<ants::net::TcpStream> client;
        std::unique_ptr<ants::net::TcpStream> backend;
        mbedtls_ssl_context ssl;
        bool handshaken{false};
        bool dead{false};
        bool close_after_flush{false};
        std::vector<uint8_t> up;                // decrypted client bytes waiting for the backend
        std::vector<uint8_t> down;              // bytes waiting to be encrypted to the client
        size_t inflight{0};
        std::string inbox;                      // scripted: what has arrived and was not taken
        Link() { mbedtls_ssl_init(&ssl); }
        ~Link() { mbedtls_ssl_free(&ssl); }
        Link(const Link&) = delete;
        Link& operator=(const Link&) = delete;

        static int send_cb(void* ctx, const unsigned char* buf, size_t len) {
            Link* l = static_cast<Link*>(ctx);
            const long n = l->client->send(buf, len);
            if (n < 0) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
            if (n == 0) return MBEDTLS_ERR_SSL_WANT_WRITE;
            l->owner->raw_out_.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
            return static_cast<int>(n);
        }
        static int recv_cb(void* ctx, unsigned char* buf, size_t len) {
            Link* l = static_cast<Link*>(ctx);
            const long n = l->client->recv(buf, len);
            if (n > 0) {
                l->owner->raw_in_.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
                return static_cast<int>(n);
            }
            if (n == 0) return MBEDTLS_ERR_SSL_WANT_READ;
            return l->client->state() == ants::net::TcpStream::State::Closed ? 0 : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        }
    };

    // application bytes from the client: >0 bytes, 0 nothing, -1 the link is over
    long read_app(Link& l, uint8_t* buf, size_t len) {
        if (!options_.use_tls) {
            const long n = l.client->recv(buf, len);
            if (n > 0) raw_in_.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
            return n;
        }
        const int rc = mbedtls_ssl_read(&l.ssl, buf, len);
        if (rc > 0) return rc;
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
        return -1;
    }
    // push l.down to the client: false when the link broke
    bool write_app(Link& l) {
        while (!l.down.empty()) {
            if (!options_.use_tls) {
                const long n = l.client->send(l.down.data(), l.down.size());
                if (n < 0) return false;
                if (n == 0) return true;
                raw_out_.append(reinterpret_cast<const char*>(l.down.data()), static_cast<size_t>(n));
                l.down.erase(l.down.begin(), l.down.begin() + n);
                continue;
            }
            const size_t len = l.inflight != 0 ? l.inflight : std::min<size_t>(l.down.size(), 16000);
            const int rc = mbedtls_ssl_write(&l.ssl, l.down.data(), len);
            if (rc > 0) {
                l.down.erase(l.down.begin(), l.down.begin() + rc);
                l.inflight = 0;
            } else if (rc == MBEDTLS_ERR_SSL_WANT_WRITE || rc == MBEDTLS_ERR_SSL_WANT_READ) {
                l.inflight = len;
                return true;
            } else {
                return false;
            }
        }
        return true;
    }
    void drop(Link& l) {
        l.dead = true;
        if (l.client) l.client->close();
        if (l.backend) l.backend->close();
    }
    void step(Link& l) {
        if (l.dead) return;
        if (!l.handshaken) {
            if (l.client->state() == ants::net::TcpStream::State::Failed || l.client->state() == ants::net::TcpStream::State::Closed) return drop(l);
            const int rc = mbedtls_ssl_handshake(&l.ssl);
            if (rc == 0) {
                l.handshaken = true;
                ++handshakes;
                const char* v = mbedtls_ssl_get_version(&l.ssl);
                const char* c = mbedtls_ssl_get_ciphersuite(&l.ssl);
                last_cipher_ = std::string(v != nullptr ? v : "?") + " " + (c != nullptr ? c : "?");
            } else if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
                ++handshake_failures;
                last_failure_ = error_text(rc);
                return drop(l);
            } else {
                return;
            }
        }
        uint8_t buf[16384];
        for (int rounds = 0; rounds < 8; ++rounds) {
            const long n = read_app(l, buf, sizeof buf);
            if (n < 0) {
                if (!l.down.empty()) write_app(l);
                return drop(l);
            }
            if (n == 0) break;
            app_bytes_up += static_cast<uint64_t>(n);
            if (options_.backend_port != 0) {
                l.up.insert(l.up.end(), buf, buf + n);
            } else {
                l.inbox.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
                if (options_.script) {
                    const std::string answer = options_.script(l.inbox);
                    l.down.insert(l.down.end(), answer.begin(), answer.end());
                    if (options_.close_after_script && !answer.empty()) l.close_after_flush = true;
                }
            }
        }
        if (l.backend) {
            while (!l.up.empty()) {
                const long n = l.backend->send(l.up.data(), l.up.size());
                if (n < 0) return drop(l);
                if (n == 0) break;
                l.up.erase(l.up.begin(), l.up.begin() + n);
            }
            for (int rounds = 0; rounds < 8; ++rounds) {
                const long n = l.backend->recv(buf, sizeof buf);
                if (n < 0) {
                    l.close_after_flush = true;
                    break;
                }
                if (n == 0) break;
                l.down.insert(l.down.end(), buf, buf + n);
            }
        }
        if (!write_app(l)) return drop(l);
        if (l.close_after_flush && l.down.empty()) {
            if (options_.use_tls) mbedtls_ssl_close_notify(&l.ssl);
            drop(l);
        }
    }

    TerminatorOptions options_;
    std::unique_ptr<ants::net::TcpListener> listener_;
    mbedtls_ssl_config conf_;
    std::vector<std::unique_ptr<Link>> links_;
    std::string raw_in_;
    std::string raw_out_;
    std::string last_cipher_;
    std::string last_failure_;
};

}  // namespace ants_test_tls
