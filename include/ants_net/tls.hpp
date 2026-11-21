#pragma once

// The TLS client of the desktop game (native builds only; Mbed TLS, third_party/mbedtls): the link to a game server that the WebSocket client of wss.hpp rides on. A browser has its own TLS, and
// the game server leaves TLS to the site's proxy, so only the desktop game needs this.
//
// What it guarantees. The server's certificate is ALWAYS verified, and there is no way to switch that off: the chain must lead to a root that the system trusts (or to a root that the caller
// names, see TlsTrust), must be valid now, and must be for the name that the caller asked for (the host name or the address of the URL, checked against the certificate's subject alternative names). A
// handshake whose certificate fails any of that fails, and so does a link whose certificate the library accepted but whose recorded verification result is not clean (a second check after the handshake). TLS 1.2 is the lowest version;
// session tickets and renegotiation are off. Nothing blocks: advance() makes progress whenever the owner polls, over a TcpStream that is itself non-blocking.
//
// Where the roots come from: the system's own store (the roots of the Security framework on macOS, the "ROOT" and "AuthRoot" stores on Windows, the bundle file of the distribution on Linux: tls_roots.cpp),
// read once per process. A computer where none can be read cannot make a TLS link at all (the error says so): there is no list of roots built into the game and no way around the check.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ants_net/tcp.hpp"

namespace ants::net {

/// Which certificate authorities a link trusts
struct TlsTrust {
    bool system_roots{true};                    // the roots of the system's store (always true in the game; the tests that use a CA of their own say false, so that no machine's store matters)
    std::string extra_pem;                      // more trusted roots, PEM (one or several certificates): a private server's CA, a test's CA. They add to the system's roots, they never replace the checks
};

/// One blob of root certificates as the system keeps it: PEM text of one or more certificates (a bundle file) or the DER bytes of one certificate
struct RootBlob {
    bool pem{false};
    std::string bytes;
};

/// The system's trusted roots, as the platform gives them (empty when it gives none). Platform code only (tls_roots.cpp); the TLS code reads them once.
std::vector<RootBlob> read_system_roots();

/// How many roots the system's store gives on this computer (the first call reads them; the number is what a link would trust, besides TlsTrust::extra_pem)
size_t system_root_count();

/// Random bytes from the system's strong generator (the keys and masks of the WebSocket handshake and frames); false when it cannot give them
bool tls_random(uint8_t* out, size_t size);

/// Why a TLS link failed, as a kind (error() has the words for a player)
enum class TlsFailure : uint8_t {
    None,
    Unreachable,        // the TCP connection was refused, reset or never made
    Untrusted,          // the certificate does not lead to a trusted root
    WrongName,          // the certificate is not for the host that was asked for
    Dates,              // the certificate has expired or is not valid yet
    NotVerified,        // the certificate failed in another way
    Protocol,           // the peer does not speak TLS, or the handshake broke in another way
    Broken              // the link broke after it was open
};

class TlsClient final {
public:
    enum class State : uint8_t { Handshaking, Open, Closed, Failed };

    /// Starts the handshake over `stream` (any state, usually still connecting), verifying the certificate for `host` (a DNS name or an IPv4 / IPv6 address, no brackets). nullptr with `why` (a sentence for a
    /// player) when the link cannot be set up at all: no root is trusted, the host is empty or too long, the library failed. The client owns the stream from then on.
    static std::unique_ptr<TlsClient> start(std::unique_ptr<TcpStream> stream, const std::string& host, const TlsTrust& trust, std::string& why);

    ~TlsClient();
    TlsClient(const TlsClient&) = delete;
    TlsClient& operator=(const TlsClient&) = delete;

    /// Makes progress and says where it stands: Handshaking until the certificate is verified and the keys are agreed, then Open; Closed after the peer's close or close(); Failed otherwise (error() says why)
    State advance();
    State state() const noexcept { return state_; }
    /// Reads decrypted bytes: the number (at most `size`), 0 when nothing is waiting, -1 when the link is closed or failed. Only when Open.
    long read(uint8_t* buffer, size_t size);
    /// Takes bytes to send: the number accepted (all of them unless the link is not open; the encrypted bytes leave as the socket takes them: flush() and advance() push them on), -1 when the link is not open
    long write(const uint8_t* data, size_t size);
    /// Pushes the bytes that wait to be sent; the number still waiting
    size_t flush();
    /// Sends the TLS goodbye (best effort, nothing is waited for) and closes the stream
    void close();
    /// Why the link failed, in words for a player (empty when it did not), and as a kind
    const std::string& error() const noexcept { return error_; }
    TlsFailure failure() const noexcept { return failure_; }
    /// The negotiated protocol and cipher suite as text (diagnostics and the tests): "TLSv1.3 TLS1-3-AES-256-GCM-SHA384"
    std::string describe() const;

private:
    struct Impl;
    TlsClient();
    void fail(TlsFailure kind, const std::string& why);

    std::unique_ptr<Impl> impl_;
    State state_{State::Handshaking};
    std::string error_;
    TlsFailure failure_{TlsFailure::None};
};

}  // namespace ants::net
