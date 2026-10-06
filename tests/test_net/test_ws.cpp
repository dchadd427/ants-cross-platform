// Tests of the WebSocket server transport (ants_net/ws.hpp): SHA-1, base64 and the accept key against published vectors; the frame codec (byte vectors of
// RFC 6455, round trips of every length class with random masks fed in every chunking, fragmentation with interleaved control frames, every protocol
// error, lengths judged before anything is allocated, close frames, 200000 random and mutated streams that never crash, hang or allocate more than the
// limit); the HTTP upgrade (the accepted variants, every refusal, a request fed one byte at a time, the 8 KB limit, fuzzing); and the listener and the
// connections over real sockets on the loopback interface with a raw test client that has its own, independent frame code (messages in both directions
// in order, ping and pong, the close handshake, protocol errors answered with their close status, a peer that never reads, slow, oversized, refused and
// abandoned handshakes, the limit of pending handshakes, 1000 random messages echoed through the Connection interface).
#include "ants_net/protocol.hpp"
#include "ants_net/transport.hpp"
#include "ants_net/ws.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#endif

using namespace ants::net;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond)                                                                                        \
    do {                                                                                                         \
        ++g_assert_count;                                                                                        \
        if (!(cond)) {                                                                                           \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures;                                                                                   \
            return;                                                                                              \
        }                                                                                                        \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

// ------------------------------------------------------------------------------------------------
// Allocation tracking: inside a TrackScope the largest single allocation is recorded, so that the tests can prove that no length that a peer declares is ever
// allocated for. (Replacing the global operator new is what the language provides for this; outside a scope the replacement only counts nothing.)

namespace {
bool g_track_on = false;
size_t g_track_max = 0;

void* tracked_alloc(size_t n) {
    if (g_track_on && n > g_track_max) g_track_max = n;
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}

struct TrackScope {
    bool before;
    TrackScope() : before(g_track_on) { g_track_on = true; }
    ~TrackScope() { g_track_on = before; }
};
}  // namespace

void* operator new(std::size_t n) { return tracked_alloc(n); }
void* operator new[](std::size_t n) { return tracked_alloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

// ------------------------------------------------------------------------------------------------
// Helpers

struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

using Bytes = std::vector<uint8_t>;
using Mask = std::array<uint8_t, 4>;

Bytes blob(size_t n, uint8_t seed) {
    Bytes v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(seed + i * 31u);
    return v;
}

Bytes bytes_of(const std::string& s) { return Bytes(s.begin(), s.end()); }

Mask random_mask(Lcg& rng) {
    Mask m;
    for (uint8_t& b : m) b = static_cast<uint8_t>(rng.next());
    return m;
}

std::string hex(const std::array<uint8_t, 20>& d) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (uint8_t b : d) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 15u]);
    }
    return out;
}

std::string sha1_hex(const std::string& s) { return hex(ws_sha1(reinterpret_cast<const uint8_t*>(s.data()), s.size())); }

constexpr const char* kRfcKey = "dGhlIHNhbXBsZSBub25jZQ==";
constexpr const char* kRfcAccept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

// What a parser produced
struct Ev {
    WsFrameParser::Result kind{WsFrameParser::Result::NeedMore};
    uint16_t status{0};
    Bytes payload;
    bool operator==(const Ev& o) const { return kind == o.kind && status == o.status && payload == o.payload; }
};

struct RunResult {
    std::vector<Ev> events;     // up to and including the Close or Error
    bool well_behaved{true};    // every event consumed bytes, the buffer stayed within one frame, an error stays an error
    size_t max_buffered{0};
};

// Feeds `bytes` in chunks of 1 .. chunk_max bytes (0 = all at once) and collects every event. A Close or an Error ends the run.
RunResult run_parser(WsFrameParser& parser, const Bytes& bytes, size_t chunk_max, Lcg& rng, size_t max_message = kMaxMessageBytes) {
    RunResult r;
    size_t pos = 0;
    bool first = true;
    while ((pos < bytes.size() || first) && r.well_behaved) {
        first = false;
        size_t n = chunk_max == 0 ? bytes.size() - pos : 1 + rng.below(static_cast<uint32_t>(chunk_max));
        n = std::min(n, bytes.size() - pos);
        {
            TrackScope track;
            parser.feed(bytes.data() + pos, n);
        }
        pos += n;
        for (;;) {
            Ev e;
            const size_t before = parser.buffered();
            {
                TrackScope track;
                e.kind = parser.next(e.payload, e.status);
            }
            if (e.kind == WsFrameParser::Result::NeedMore) {
                r.max_buffered = std::max(r.max_buffered, parser.buffered());
                if (parser.buffered() > max_message + 14) r.well_behaved = false;           // more than one frame is being held
                break;
            }
            if (e.kind == WsFrameParser::Result::Error) {
                r.events.push_back(e);
                // an error stays: the same answer again, nothing buffered, nothing more taken in
                Ev again;
                if (parser.next(again.payload, again.status) != WsFrameParser::Result::Error || again.status != e.status) r.well_behaved = false;
                const uint8_t more[3] = {1, 2, 3};
                parser.feed(more, 3);
                if (parser.buffered() != 0 || !parser.finished()) r.well_behaved = false;
                return r;
            }
            if (parser.buffered() >= before) r.well_behaved = false;                            // an event must consume something
            r.events.push_back(e);
            if (e.kind == WsFrameParser::Result::Close) {
                if (!parser.finished() || parser.buffered() != 0) r.well_behaved = false;
                return r;
            }
        }
    }
    return r;
}

RunResult run_whole(const Bytes& bytes, bool from_client = true, size_t max_message = kMaxMessageBytes) {
    WsFrameParser p(from_client, max_message);
    Lcg rng(1);
    return run_parser(p, bytes, 0, rng, max_message);
}

RunResult run_bytewise(const Bytes& bytes, bool from_client = true, size_t max_message = kMaxMessageBytes) {
    WsFrameParser p(from_client, max_message);
    Lcg rng(1);
    return run_parser(p, bytes, 1, rng, max_message);
}

void append(Bytes& to, const Bytes& more) { to.insert(to.end(), more.begin(), more.end()); }

Bytes client_frame(WsOpcode op, const Bytes& payload, Lcg& rng, bool fin = true) {
    const Mask m = random_mask(rng);
    return ws_encode_frame(op, payload, fin, &m);
}

// The only event of a run, or nothing
bool single_event(const RunResult& r, WsFrameParser::Result kind, Ev& out) {
    if (r.events.size() != 1 || r.events[0].kind != kind) return false;
    out = r.events[0];
    return true;
}

bool is_error(const Bytes& bytes, uint16_t status, bool from_client = true, size_t max_message = kMaxMessageBytes) {
    for (int mode = 0; mode < 2; ++mode) {
        const RunResult r = mode == 0 ? run_whole(bytes, from_client, max_message) : run_bytewise(bytes, from_client, max_message);
        if (!r.well_behaved || r.events.empty()) return false;
        const Ev& last = r.events.back();
        if (last.kind != WsFrameParser::Result::Error || last.status != status) return false;
    }
    return true;
}

// ------------------------------------------------------------------------------------------------
// Handshake helpers

std::vector<std::string> base_headers() {
    return {"Host: play.example.org", "Upgrade: websocket", "Connection: Upgrade", std::string("Sec-WebSocket-Key: ") + kRfcKey, "Sec-WebSocket-Version: 13"};
}

// `name` (lower case) removed from the headers, `replacement` (a whole "Name: value" line, may be empty) put in its place
std::vector<std::string> replace_header(std::vector<std::string> headers, const std::string& name, const std::string& replacement) {
    std::vector<std::string> out;
    bool placed = false;
    for (const std::string& h : headers) {
        std::string lower = h.substr(0, h.find(':'));
        for (char& c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        if (lower != name) {
            out.push_back(h);
        } else if (!placed && !replacement.empty()) {
            out.push_back(replacement);
            placed = true;
        }
    }
    if (!placed && !replacement.empty()) out.push_back(replacement);
    return out;
}

std::string make_request(const std::vector<std::string>& headers, const std::string& request_line = "GET /play HTTP/1.1") {
    std::string r = request_line + "\r\n";
    for (const std::string& h : headers) r += h + "\r\n";
    return r + "\r\n";
}

WsHandshakeResult handshake(const std::string& request, const WsServerOptions& options = WsServerOptions()) { return ws_parse_handshake(request, options); }

bool refused_with(const std::string& request, int status, const WsServerOptions& options = WsServerOptions()) {
    const WsHandshakeResult r = handshake(request, options);
    return r.status == WsHandshakeResult::Status::Rejected && r.http_status == status && r.response.find("HTTP/1.1 " + std::to_string(status) + " ") == 0 &&
           r.response.find("Connection: close\r\n") != std::string::npos && r.response.size() >= 4 && r.response.compare(r.response.size() - 4, 4, "\r\n\r\n") == 0;
}

bool accepted(const std::string& request, const WsServerOptions& options = WsServerOptions()) {
    return handshake(request, options).status == WsHandshakeResult::Status::Accepted;
}

// ------------------------------------------------------------------------------------------------
// Sockets: a plain non-blocking client with its own frame code (written from the RFC, not from ws.cpp), so that the server is tested against an
// independent implementation

#ifdef _WIN32
using sock_t = SOCKET;
using io_len_t = int;
constexpr sock_t kBadSock = INVALID_SOCKET;
void sock_init() {
    static WSADATA data;
    static const bool once = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    (void)once;
}
void sock_close(sock_t s) { closesocket(s); }
bool sock_would_block() { return WSAGetLastError() == WSAEWOULDBLOCK; }
void sock_nonblocking(sock_t s) {
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
}
#else
using sock_t = int;
using io_len_t = size_t;
constexpr sock_t kBadSock = -1;
void sock_init() {}
void sock_close(sock_t s) { ::close(s); }
bool sock_would_block() { return errno == EWOULDBLOCK || errno == EAGAIN; }
void sock_nonblocking(sock_t s) {
    const int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
}
#endif

#ifdef MSG_NOSIGNAL
constexpr int kTestSendFlags = MSG_NOSIGNAL;
#else
constexpr int kTestSendFlags = 0;
#endif

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Frame {
    bool fin{false};
    uint8_t op{0};
    uint8_t rsv{0};
    bool masked{false};
    Bytes payload;
};

class RawClient {
public:
    RawClient() = default;
    RawClient(const RawClient&) = delete;
    RawClient& operator=(const RawClient&) = delete;
    ~RawClient() { close(); }

    bool connect(uint16_t port, int receive_buffer = 0) {
        sock_init();
        s_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (s_ == kBadSock) return false;
        if (receive_buffer > 0) setsockopt(s_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&receive_buffer), sizeof(receive_buffer));
#ifdef SO_NOSIGPIPE
        int one = 1;
        setsockopt(s_, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&one), sizeof(one));
#endif
        sockaddr_in a;
        std::memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(s_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) return false;
        sock_nonblocking(s_);
        return true;
    }
    void close() {
        if (s_ != kBadSock) sock_close(s_);
        s_ = kBadSock;
    }
    // Writes everything (the socket buffers of the loopback hold what the tests send before the server has read any of it)
    bool send_bytes(const Bytes& b) {
        size_t sent = 0;
        for (int spins = 0; sent < b.size() && spins < 5000; ++spins) {
            const auto n = ::send(s_, reinterpret_cast<const char*>(b.data() + sent), static_cast<io_len_t>(b.size() - sent), kTestSendFlags);
            if (n > 0) {
                sent += static_cast<size_t>(n);
                spins = 0;
            } else if (n < 0 && sock_would_block()) {
                sleep_ms(1);
            } else {
                return false;
            }
        }
        return sent == b.size();
    }
    bool send_text(const std::string& s) { return send_bytes(bytes_of(s)); }

    // Reads what is there
    void pull() {
        if (s_ == kBadSock) return;
        uint8_t buf[16384];
        for (int rounds = 0; rounds < 64; ++rounds) {
            const auto n = ::recv(s_, reinterpret_cast<char*>(buf), static_cast<io_len_t>(sizeof(buf)), 0);
            if (n > 0) {
                in_.insert(in_.end(), buf, buf + n);
            } else if (n == 0) {
                eof_ = true;
                break;
            } else if (sock_would_block()) {
                break;
            } else {
                eof_ = true;                                        // a reset ends the connection as well
                break;
            }
        }
    }
    // Whether the kernel holds bytes that nobody has read (a socket that is closed with such bytes resets the link)
    bool unread() const {
        uint8_t b = 0;
        return s_ != kBadSock && ::recv(s_, reinterpret_cast<char*>(&b), 1, MSG_PEEK) == 1;
    }
    bool eof() const { return eof_; }
    size_t waiting() const { return in_.size(); }
    // What is left of the received bytes, as text, taken off (the body of an answer, after take_head)
    std::string take_text() {
        std::string text(in_.begin(), in_.end());
        in_.clear();
        return text;
    }

    // The HTTP response head (through the blank line), taken off the front
    bool take_head(std::string& head) {
        const std::string text(in_.begin(), in_.end());
        const size_t end = text.find("\r\n\r\n");
        if (end == std::string::npos) return false;
        head = text.substr(0, end + 4);
        in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(end + 4));
        return true;
    }

    // One frame of the server (which must not mask), taken off the front
    bool take_frame(Frame& f) {
        if (in_.size() < 2) return false;
        const uint8_t b0 = in_[0];
        const uint8_t b1 = in_[1];
        size_t len = b1 & 0x7Fu;
        size_t off = 2;
        if (len == 126) {
            if (in_.size() < 4) return false;
            len = (static_cast<size_t>(in_[2]) << 8) | in_[3];
            off = 4;
        } else if (len == 127) {
            if (in_.size() < 10) return false;
            len = 0;
            for (size_t i = 0; i < 8; ++i) len = (len << 8) | in_[2 + i];
            off = 10;
        }
        if ((b1 & 0x80u) != 0) off += 4;
        if (in_.size() < off + len) return false;
        f.fin = (b0 & 0x80u) != 0;
        f.rsv = b0 & 0x70u;
        f.op = b0 & 0x0Fu;
        f.masked = (b1 & 0x80u) != 0;
        f.payload.assign(in_.begin() + static_cast<std::ptrdiff_t>(off), in_.begin() + static_cast<std::ptrdiff_t>(off + len));
        in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(off + len));
        return true;
    }

    // A masked frame built by hand: first byte (FIN, RSV, opcode), the length in its shortest form, a random key
    Bytes build_frame(uint8_t first_byte, const Bytes& payload) {
        Bytes f;
        f.push_back(first_byte);
        const size_t n = payload.size();
        if (n < 126) {
            f.push_back(static_cast<uint8_t>(0x80u | n));
        } else if (n < 65536) {
            f.push_back(0x80u | 126u);
            f.push_back(static_cast<uint8_t>(n >> 8));
            f.push_back(static_cast<uint8_t>(n & 0xFFu));
        } else {
            f.push_back(0x80u | 127u);
            for (int shift = 56; shift >= 0; shift -= 8) f.push_back(static_cast<uint8_t>(static_cast<uint64_t>(n) >> shift));
        }
        uint8_t key[4];
        for (uint8_t& k : key) k = static_cast<uint8_t>(rng_.next());
        f.insert(f.end(), key, key + 4);
        for (size_t i = 0; i < n; ++i) f.push_back(static_cast<uint8_t>(payload[i] ^ key[i % 4]));
        return f;
    }
    bool send_frame(uint8_t first_byte, const Bytes& payload) { return send_bytes(build_frame(first_byte, payload)); }
    bool send_raw(const Bytes& bytes) { return send_bytes(bytes); }

private:
    sock_t s_{kBadSock};
    Bytes in_;
    bool eof_{false};
    Lcg rng_{12345};
};

std::string upgrade_request(const std::string& extra_headers = "", const std::string& path = "/play", const std::string& key = kRfcKey,
                            const std::string& version = "13") {
    return "GET " + path + " HTTP/1.1\r\nHost: play.example.org\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
           "\r\nSec-WebSocket-Version: " + version + "\r\n" + extra_headers + "\r\n";
}

// A listener and the connections it has handed out; wait() keeps accepting while it waits
struct Rig {
    std::unique_ptr<WsListener> listener;
    std::vector<std::unique_ptr<WsConnection>> conns;

    bool start(const WsServerOptions& options = WsServerOptions()) {
        listener = WsListener::listen(0, true, options);
        return listener != nullptr;
    }
    void step() {
        for (;;) {
            std::unique_ptr<WsConnection> c = listener->accept();
            if (!c) break;
            conns.push_back(std::move(c));
        }
    }
    // Real time, not rounds: a busy machine gets the whole budget. The loopback usually has the data within a few microseconds, so the first rounds only
    // yield; after that the waiting sleeps.
    bool wait(const std::function<bool()>& done, int max_ms = 3000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(max_ms);
        for (int round = 0;; ++round) {
            step();
            if (done()) return true;
            if (std::chrono::steady_clock::now() >= deadline) return false;
            if (round < 200) {
                std::this_thread::yield();
            } else {
                sleep_ms(1);
            }
        }
    }
};

// Connects `client`, sends the upgrade request and waits for the answer head; returns the head ("" on failure). The server side is in rig.conns.back()
// when the answer was a 101.
std::string connect_and_ask(Rig& rig, RawClient& client, const std::string& request, int receive_buffer = 0) {
    if (!client.connect(rig.listener->port(), receive_buffer)) return "";
    if (!client.send_text(request)) return "";
    std::string head;
    rig.wait([&]() {
        client.pull();
        return client.take_head(head);
    });
    return head;
}

// A finished WebSocket: client and the server side of it
struct Link {
    RawClient client;
    WsConnection* server{nullptr};
};

bool open_link(Rig& rig, Link& link, const std::string& extra_headers = "", int receive_buffer = 0) {
    const size_t before = rig.conns.size();
    const std::string head = connect_and_ask(rig, link.client, upgrade_request(extra_headers), receive_buffer);
    if (head.find("HTTP/1.1 101 Switching Protocols\r\n") != 0 || head.find(std::string("Sec-WebSocket-Accept: ") + kRfcAccept + "\r\n") == std::string::npos) return false;
    if (!rig.wait([&]() { return rig.conns.size() > before; })) return false;
    link.server = rig.conns.back().get();
    return true;
}

// Polls the server side (collecting what arrives) until `done` or the time is up
bool poll_server(Rig& rig, WsConnection& conn, std::vector<Bytes>& got, const std::function<bool()>& done, int max_ms = 3000) {
    return rig.wait(
        [&]() {
            Bytes m;
            while (conn.poll(m)) got.push_back(m);
            return done();
        },
        max_ms);
}

// Reads frames from the client's socket until `want(frame)` matches one, collecting everything else
bool read_frame(Rig& rig, RawClient& client, Frame& out, int max_ms = 3000) {
    return rig.wait(
        [&]() {
            client.pull();
            return client.take_frame(out);
        },
        max_ms);
}

// A Connection built on the raw client: with it the same code can drive a WsConnection and its peer through the interface the game uses
class ClientConnection final : public Connection {
public:
    explicit ClientConnection(RawClient& c) : c_(c) {}
    bool send(const Bytes& m) override { return m.size() <= kMaxMessageBytes && c_.send_frame(0x82, m); }
    bool poll(Bytes& m) override {
        c_.pull();
        Frame f;
        while (c_.take_frame(f)) {
            if (f.op == 0x2 && f.fin) {
                m = f.payload;
                return true;
            }
        }
        return false;
    }
    State state() const override { return c_.eof() ? State::Closed : State::Open; }
    void close() override { c_.close(); }

private:
    RawClient& c_;
};

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port: WebSocket server transport (RFC 6455)\n"
                 "=======================================================\n";

    // ============================================================================================
    // Primitives

    TEST_CASE("W1.1 SHA-1 gives the published digests (block borders included); base64 round-trips and refuses everything but its own output") {
        ASSERT_EQ(sha1_hex(""), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
        ASSERT_EQ(sha1_hex("abc"), "a9993e364706816aba3e25717850c26c9cd0d89d");
        ASSERT_EQ(sha1_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
        struct {
            size_t n;
            const char* digest;
        } const vectors[] = {{1, "86f7e437faa5a7fce15d1ddcb9eaeaea377667b8"},   {55, "c1c8bbdc22796e28c0e15163d20899b65621d65a"},
                        {56, "c2db330f6083854c99d4b5bfb6e8f29f201be699"},  {57, "f08f24908d682555111be7ff6f004e78283d989a"},
                        {63, "03f09f5b158a7a8cdad920bddc29b81c18a551f5"},  {64, "0098ba824b5c16427bd7a1122a5a442a25ec644d"},
                        {65, "11655326c708d70319be2610e8a57d9a5b959d3b"},  {119, "ee971065aaa017e0632a8ca6c77bb3bf8b1dfc56"},
                        {120, "f34c1488385346a55709ba056ddd08280dd4c6d6"}, {128, "ad5b3fdbcb526778c2839d2f151ea753995e26a0"},
                        {1000000, "34aa973cd4c4daa4f61eeb2bdbad27316534016f"}};
        for (const auto& v : vectors) ASSERT_EQ(sha1_hex(std::string(v.n, 'a')), v.digest);

        const char* const plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
        const char* const coded[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};      // RFC 4648 section 10
        for (size_t i = 0; i < 7; ++i) {
            const std::string p = plain[i];
            ASSERT_EQ(ws_base64_encode(reinterpret_cast<const uint8_t*>(p.data()), p.size()), coded[i]);
            Bytes back;
            ASSERT_TRUE(ws_base64_decode(coded[i], back));
            ASSERT_TRUE(back == bytes_of(p));
        }
        Lcg rng(7);
        for (int i = 0; i < 500; ++i) {
            const Bytes data = blob(rng.below(100), static_cast<uint8_t>(rng.next()));
            const std::string text = ws_base64_encode(data.data(), data.size());
            Bytes back;
            ASSERT_TRUE(ws_base64_decode(text, back));
            ASSERT_TRUE(back == data);
        }
        // not what the encoder writes: wrong length, padding in the wrong place, characters outside the alphabet, unused bits that are not zero
        for (const char* bad : {"Zg=", "Zg", "Z", "Z===", "====", "Zg=a", "=Zg=", "Zh==", "Zm9=", "Zm9v Zg==", "Zm-v", "Zm_v", "Zg==Zg==", "Zm9vY===", "Zm9v\n",
                                "Zm9v\r\n", " Zm9v", "Zm9\xC3\xA9"}) {
            Bytes out{1, 2, 3};
            ASSERT_FALSE(ws_base64_decode(bad, out));
        }
    } TEST_END();

    TEST_CASE("W1.2 The accept key of RFC 6455 section 1.3 (and a second published pair)") {
        ASSERT_EQ(ws_accept_key("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
        ASSERT_EQ(ws_accept_key("x3JJHMbDL1EzLkh9GBhXDw=="), "HSmrc0sMlYUkAGmm5OPpG2HaGWk=");
        ASSERT_EQ(ws_accept_key("").size(), 28u);                    // whatever the input, 20 bytes of digest in base64
    } TEST_END();

    // ============================================================================================
    // Frame codec

    TEST_CASE("W1.3 Frames are encoded byte for byte as in RFC 6455 section 5.7, and the length takes its shortest form") {
        const Bytes hello = bytes_of("Hello");
        ASSERT_TRUE(ws_encode_frame(WsOpcode::Binary, hello) == (Bytes{0x82, 0x05, 'H', 'e', 'l', 'l', 'o'}));
        const Mask key = {0x37, 0xfa, 0x21, 0x3d};
        ASSERT_TRUE(ws_encode_frame(WsOpcode::Binary, hello, true, &key) == (Bytes{0x82, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58}));
        ASSERT_TRUE(ws_encode_frame(WsOpcode::Ping, hello) == (Bytes{0x89, 0x05, 'H', 'e', 'l', 'l', 'o'}));
        ASSERT_TRUE(ws_encode_frame(WsOpcode::Pong, hello, true, &key) == (Bytes{0x8a, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58}));
        ASSERT_TRUE(ws_encode_frame(WsOpcode::Binary, bytes_of("Hel"), false) == (Bytes{0x02, 0x03, 'H', 'e', 'l'}));
        ASSERT_TRUE(ws_encode_frame(WsOpcode::Continuation, bytes_of("lo"), true) == (Bytes{0x80, 0x02, 'l', 'o'}));
        ASSERT_TRUE(ws_encode_frame(WsOpcode::Binary, Bytes()) == (Bytes{0x82, 0x00}));
        ASSERT_TRUE(ws_close_payload(1000, "bye") == (Bytes{0x03, 0xE8, 'b', 'y', 'e'}));
        ASSERT_TRUE(ws_encode_frame(WsOpcode::Close, ws_close_payload(1002)) == (Bytes{0x88, 0x02, 0x03, 0xEA}));

        struct {
            size_t payload;
            Bytes header;       // the bytes before the payload of an unmasked frame
        } const shapes[] = {{0, {0x82, 0x00}},
                            {125, {0x82, 0x7D}},
                            {126, {0x82, 0x7E, 0x00, 0x7E}},
                            {256, {0x82, 0x7E, 0x01, 0x00}},
                            {65535, {0x82, 0x7E, 0xFF, 0xFF}},
                            {65536, {0x82, 0x7F, 0, 0, 0, 0, 0, 1, 0, 0}},
                            {70000, {0x82, 0x7F, 0, 0, 0, 0, 0, 1, 0x11, 0x70}}};
        for (const auto& s : shapes) {
            const Bytes payload = blob(s.payload, 5);
            const Bytes frame = ws_encode_frame(WsOpcode::Binary, payload);
            ASSERT_EQ(frame.size(), s.header.size() + s.payload);
            ASSERT_TRUE(Bytes(frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(s.header.size())) == s.header);
            ASSERT_TRUE(Bytes(frame.begin() + static_cast<std::ptrdiff_t>(s.header.size()), frame.end()) == payload);
            // masked: the length bytes carry the mask bit, the key follows them, the payload is XORed
            const Mask m = {0x01, 0x02, 0x03, 0x04};
            const Bytes masked = ws_encode_frame(WsOpcode::Binary, payload, true, &m);
            ASSERT_EQ(masked.size(), frame.size() + 4);
            ASSERT_EQ(masked[1] & 0x80, 0x80);
            ASSERT_EQ(masked[1] & 0x7F, frame[1]);
            const size_t body = s.header.size() + 4;
            for (size_t i = 0; i < 4; ++i) ASSERT_EQ(masked[s.header.size() + i], m[i]);
            for (size_t i = 0; i < s.payload; i += 997) ASSERT_EQ(masked[body + i], static_cast<uint8_t>(payload[i] ^ m[i % 4]));
        }

        // and the parser reads the RFC's own examples
        Ev e;
        ASSERT_TRUE(single_event(run_whole(Bytes{0x82, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58}), WsFrameParser::Result::Message, e));
        ASSERT_TRUE(e.payload == hello);
        ASSERT_TRUE(single_event(run_whole(Bytes{0x8a, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58}), WsFrameParser::Result::Pong, e));
        ASSERT_TRUE(e.payload == hello);
        ASSERT_TRUE(single_event(run_whole(Bytes{0x89, 0x05, 'H', 'e', 'l', 'l', 'o'}, false), WsFrameParser::Result::Ping, e));    // from a server: unmasked
        ASSERT_TRUE(e.payload == hello);
    } TEST_END();

    TEST_CASE("W1.4 Every length class round-trips with random masks, fed whole, one byte at a time and in random chunks (a server's unmasked frames as well)") {
        Lcg rng(42);
        const size_t sizes[] = {0, 1, 2, 3, 124, 125, 126, 127, 255, 256, 4096, 65535, 65536};
        for (size_t size : sizes) {
            for (int round = 0; round < 4; ++round) {
                const Bytes payload = blob(size, static_cast<uint8_t>(rng.next()));
                const Bytes frame = client_frame(WsOpcode::Binary, payload, rng);
                const Bytes unmasked = ws_encode_frame(WsOpcode::Binary, payload);
                for (int mode = 0; mode < 4; ++mode) {
                    RunResult r;
                    if (mode == 0) {
                        r = run_whole(frame);
                    } else if (mode == 1) {
                        r = run_bytewise(frame);
                    } else {
                        WsFrameParser parser(true);
                        r = run_parser(parser, frame, mode == 2 ? 7u : 3000u, rng);
                    }
                    ASSERT_TRUE(r.well_behaved);
                    ASSERT_EQ(r.events.size(), 1u);
                    ASSERT_TRUE(r.events[0].kind == WsFrameParser::Result::Message);
                    ASSERT_TRUE(r.events[0].payload == payload);
                }
                Ev e;
                ASSERT_TRUE(single_event(run_whole(unmasked, false), WsFrameParser::Result::Message, e));
                ASSERT_TRUE(e.payload == payload);
                ASSERT_TRUE(single_event(run_bytewise(unmasked, false), WsFrameParser::Result::Message, e));
                ASSERT_TRUE(e.payload == payload);
            }
        }
        // 70000 bytes are over the message limit: refused as soon as the header is in, before the payload is fed (and nothing of that size is allocated)
        {
            const Bytes payload = blob(70000, 9);
            const Bytes frame = client_frame(WsOpcode::Binary, payload, rng);
            WsFrameParser parser(true);
            parser.feed(frame.data(), 14);
            Bytes out;
            uint16_t status = 0;
            ASSERT_TRUE(parser.next(out, status) == WsFrameParser::Result::Error);
            ASSERT_EQ(status, kWsCloseMessageTooBig);
            ASSERT_TRUE(is_error(frame, kWsCloseMessageTooBig));
            // with a larger limit the same frame is a message
            Ev e;
            ASSERT_TRUE(single_event(run_whole(frame, true, 1u << 20), WsFrameParser::Result::Message, e));
            ASSERT_TRUE(e.payload == payload);
            ASSERT_TRUE(single_event(run_bytewise(frame, true, 1u << 20), WsFrameParser::Result::Message, e));
            ASSERT_TRUE(e.payload == payload);
        }
        // several frames in one chunk come out one by one, in order
        {
            Bytes stream;
            std::vector<Bytes> sent;
            for (int i = 0; i < 50; ++i) {
                sent.push_back(blob(rng.below(300), static_cast<uint8_t>(i)));
                append(stream, client_frame(WsOpcode::Binary, sent.back(), rng));
            }
            const RunResult r = run_whole(stream);
            ASSERT_TRUE(r.well_behaved);
            ASSERT_EQ(r.events.size(), sent.size());
            for (size_t i = 0; i < sent.size(); ++i) ASSERT_TRUE(r.events[i].payload == sent[i]);
        }
    } TEST_END();

    TEST_CASE("W1.5 Fragmented messages are reassembled, with pings and pongs between the fragments, in every chunking") {
        Lcg rng(99);
        for (int round = 0; round < 300; ++round) {
            const size_t total = rng.below(5) == 0 ? rng.below(20) : rng.below(6000);
            const Bytes whole = blob(total, static_cast<uint8_t>(round));
            const uint32_t pieces = 2 + rng.below(6);
            Bytes stream;
            std::vector<Ev> expected;
            size_t at = 0;
            for (uint32_t k = 0; k < pieces; ++k) {
                const bool last = k + 1 == pieces;
                const size_t take = last ? total - at : (rng.below(4) == 0 ? 0 : rng.below(static_cast<uint32_t>(total - at) + 1));
                const Bytes part(whole.begin() + static_cast<std::ptrdiff_t>(at), whole.begin() + static_cast<std::ptrdiff_t>(at + take));
                at += take;
                append(stream, client_frame(k == 0 ? WsOpcode::Binary : WsOpcode::Continuation, part, rng, last));
                if (!last && rng.below(2) == 0) {                   // a control frame in the middle of the message
                    Ev c;
                    c.kind = rng.below(2) == 0 ? WsFrameParser::Result::Ping : WsFrameParser::Result::Pong;
                    c.payload = blob(rng.below(126), static_cast<uint8_t>(k));
                    append(stream, client_frame(c.kind == WsFrameParser::Result::Ping ? WsOpcode::Ping : WsOpcode::Pong, c.payload, rng));
                    expected.push_back(c);
                }
            }
            Ev m;
            m.kind = WsFrameParser::Result::Message;
            m.payload = whole;
            expected.push_back(m);
            const RunResult a = run_whole(stream);
            const RunResult b = run_bytewise(stream);
            WsFrameParser chunked(true);
            const RunResult c = run_parser(chunked, stream, 13, rng);
            ASSERT_TRUE(a.well_behaved && b.well_behaved && c.well_behaved);
            ASSERT_TRUE(a.events == expected);
            ASSERT_TRUE(b.events == expected);
            ASSERT_TRUE(c.events == expected);
        }
        // a message of exactly the limit in three fragments is fine, one byte more is refused at the header of the fragment that overflows
        {
            Lcg r2(5);
            Bytes ok;
            append(ok, client_frame(WsOpcode::Binary, blob(30000, 1), r2, false));
            append(ok, client_frame(WsOpcode::Continuation, blob(30000, 2), r2, false));
            append(ok, client_frame(WsOpcode::Continuation, blob(kMaxMessageBytes - 60000, 3), r2, true));
            const RunResult a = run_whole(ok);
            ASSERT_TRUE(a.well_behaved);
            ASSERT_EQ(a.events.size(), 1u);
            ASSERT_EQ(a.events[0].payload.size(), kMaxMessageBytes);
            Bytes over;
            append(over, client_frame(WsOpcode::Binary, blob(30000, 1), r2, false));
            append(over, client_frame(WsOpcode::Continuation, blob(30000, 2), r2, false));
            append(over, client_frame(WsOpcode::Continuation, blob(kMaxMessageBytes - 60000 + 1, 3), r2, true));
            ASSERT_TRUE(is_error(over, kWsCloseMessageTooBig));
            // the refusal comes with the header: the last fragment's payload is never needed
            Bytes cut(over.begin(), over.end() - static_cast<std::ptrdiff_t>(kMaxMessageBytes - 60000));
            ASSERT_TRUE(is_error(cut, kWsCloseMessageTooBig));
        }
    } TEST_END();

    TEST_CASE("W1.6 Every protocol error is answered with its close status, however the bytes arrive; an error stays an error") {
        Lcg rng(11);
        const Mask k = {1, 2, 3, 4};
        auto frame_bytes = [&](uint8_t b0, uint8_t len_byte, const Bytes& more) {
            Bytes f = {b0, len_byte};
            append(f, more);
            return f;
        };
        // an unmasked frame from a client
        ASSERT_TRUE(is_error(Bytes{0x82, 0x01, 0xAA}, kWsCloseProtocolError));
        ASSERT_TRUE(is_error(Bytes{0x82, 0x00}, kWsCloseProtocolError));
        ASSERT_TRUE(is_error(Bytes{0x89, 0x00}, kWsCloseProtocolError));
        ASSERT_TRUE(is_error(Bytes{0x88, 0x00}, kWsCloseProtocolError));
        // a masked frame from a server
        ASSERT_TRUE(is_error(client_frame(WsOpcode::Binary, bytes_of("x"), rng), kWsCloseProtocolError, false));
        ASSERT_TRUE(is_error(client_frame(WsOpcode::Ping, Bytes(), rng), kWsCloseProtocolError, false));
        // reserved bits (no extension was negotiated), on data and on control frames
        const uint8_t rsv_bits[] = {0x40, 0x20, 0x10, 0x70};
        for (uint8_t rsv : rsv_bits) {
            ASSERT_TRUE(is_error(frame_bytes(static_cast<uint8_t>(0x82 | rsv), 0x80, Bytes(k.begin(), k.end())), kWsCloseProtocolError));
            ASSERT_TRUE(is_error(frame_bytes(static_cast<uint8_t>(0x89 | rsv), 0x80, Bytes(k.begin(), k.end())), kWsCloseProtocolError));
        }
        // text is refused, whole or as the start of a fragmented message
        ASSERT_TRUE(is_error(frame_bytes(0x81, 0x80, Bytes(k.begin(), k.end())), kWsCloseUnsupportedData));
        ASSERT_TRUE(is_error(frame_bytes(0x01, 0x80, Bytes(k.begin(), k.end())), kWsCloseUnsupportedData));
        ASSERT_TRUE(is_error(frame_bytes(0x81, 0x85, Bytes{1, 2, 3, 4, 'h', 'e', 'l', 'l', 'o'}), kWsCloseUnsupportedData));
        // reserved opcodes: 3 - 7 for data, 0xB - 0xF for control
        for (uint8_t op = 3; op <= 7; ++op) ASSERT_TRUE(is_error(frame_bytes(static_cast<uint8_t>(0x80 | op), 0x80, Bytes(k.begin(), k.end())), kWsCloseProtocolError));
        for (uint8_t op = 0x0B; op <= 0x0F; ++op) ASSERT_TRUE(is_error(frame_bytes(static_cast<uint8_t>(0x80 | op), 0x80, Bytes(k.begin(), k.end())), kWsCloseProtocolError));
        // control frames are never fragmented ...
        ASSERT_TRUE(is_error(frame_bytes(0x09, 0x80, Bytes(k.begin(), k.end())), kWsCloseProtocolError));
        ASSERT_TRUE(is_error(frame_bytes(0x0A, 0x80, Bytes(k.begin(), k.end())), kWsCloseProtocolError));
        ASSERT_TRUE(is_error(frame_bytes(0x08, 0x80, Bytes(k.begin(), k.end())), kWsCloseProtocolError));
        // ... and carry at most 125 bytes (126 and 127 are the length codes of longer frames)
        ASSERT_TRUE(is_error(frame_bytes(0x89, 0xFE, Bytes{0x00, 0x7E, 1, 2, 3, 4}), kWsCloseProtocolError));
        ASSERT_TRUE(is_error(frame_bytes(0x88, 0xFE, Bytes{0x00, 0x7E, 1, 2, 3, 4}), kWsCloseProtocolError));
        ASSERT_TRUE(is_error(frame_bytes(0x8A, 0xFF, Bytes{0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 3, 4}), kWsCloseProtocolError));
        {
            Ev e;
            ASSERT_TRUE(single_event(run_whole(client_frame(WsOpcode::Ping, blob(125, 3), rng)), WsFrameParser::Result::Ping, e));       // 125 is the most
            ASSERT_EQ(e.payload.size(), 125u);
        }
        // a 64-bit length with the top bit set; lengths that are not in their shortest form
        ASSERT_TRUE(is_error(frame_bytes(0x82, 0xFF, Bytes{0x80, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4}), kWsCloseProtocolError));
        ASSERT_TRUE(is_error(frame_bytes(0x82, 0xFF, Bytes{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 1, 2, 3, 4}), kWsCloseProtocolError));
        ASSERT_TRUE(is_error(frame_bytes(0x82, 0xFE, Bytes{0x00, 0x05, 1, 2, 3, 4}), kWsCloseProtocolError));              // 5 in 16 bits
        ASSERT_TRUE(is_error(frame_bytes(0x82, 0xFE, Bytes{0x00, 0x7D, 1, 2, 3, 4}), kWsCloseProtocolError));              // 125 in 16 bits
        ASSERT_TRUE(is_error(frame_bytes(0x82, 0xFF, Bytes{0, 0, 0, 0, 0, 0, 0, 100, 1, 2, 3, 4}), kWsCloseProtocolError));     // 100 in 64 bits
        ASSERT_TRUE(is_error(frame_bytes(0x82, 0xFF, Bytes{0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 1, 2, 3, 4}), kWsCloseProtocolError));  // 65535 in 64 bits
        // a continuation of nothing, and a new message inside an unfinished one
        ASSERT_TRUE(is_error(frame_bytes(0x80, 0x80, Bytes(k.begin(), k.end())), kWsCloseProtocolError));
        {
            Bytes s;
            append(s, client_frame(WsOpcode::Binary, bytes_of("ab"), rng, false));
            append(s, client_frame(WsOpcode::Binary, bytes_of("cd"), rng, true));
            ASSERT_TRUE(is_error(s, kWsCloseProtocolError));
            Bytes t;
            append(t, client_frame(WsOpcode::Binary, bytes_of("ab"), rng, true));
            append(t, client_frame(WsOpcode::Continuation, bytes_of("cd"), rng, true));     // the message was complete
            ASSERT_TRUE(is_error(t, kWsCloseProtocolError));
            Bytes u;
            append(u, client_frame(WsOpcode::Binary, bytes_of("ab"), rng, false));
            append(u, client_frame(WsOpcode::Text, bytes_of("cd"), rng, true));
            ASSERT_TRUE(is_error(u, kWsCloseUnsupportedData));
        }
        // what came before an error is still delivered, in order
        {
            Bytes s;
            append(s, client_frame(WsOpcode::Binary, bytes_of("one"), rng));
            append(s, client_frame(WsOpcode::Ping, bytes_of("p"), rng));
            append(s, Bytes{0x82, 0x00});                                                   // unmasked
            const RunResult r = run_bytewise(s);
            ASSERT_TRUE(r.well_behaved);
            ASSERT_EQ(r.events.size(), 3u);
            ASSERT_TRUE(r.events[0].kind == WsFrameParser::Result::Message && r.events[0].payload == bytes_of("one"));
            ASSERT_TRUE(r.events[1].kind == WsFrameParser::Result::Ping);
            ASSERT_TRUE(r.events[2].kind == WsFrameParser::Result::Error && r.events[2].status == kWsCloseProtocolError);
        }
        // a message that is cut short is never delivered
        {
            const Bytes whole = client_frame(WsOpcode::Binary, blob(500, 1), rng);
            for (size_t cut : {size_t{0}, size_t{1}, size_t{2}, size_t{5}, size_t{6}, size_t{100}, whole.size() - 1}) {
                const RunResult r = run_bytewise(Bytes(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(cut)));
                ASSERT_TRUE(r.well_behaved);
                ASSERT_TRUE(r.events.empty());
            }
        }
    } TEST_END();

    TEST_CASE("W1.7 A declared length is judged before anything is allocated or buffered for it") {
        Lcg rng(21);
        const uint64_t lengths[] = {65537u, 100000u, 1u << 20, 1u << 31, (1ull << 32) - 1, 1ull << 32, 1ull << 40, 1ull << 62, (1ull << 63) - 1};
        for (uint64_t len : lengths) {
            Bytes header = {0x82, 0xFF};
            for (int shift = 56; shift >= 0; shift -= 8) header.push_back(static_cast<uint8_t>(len >> shift));
            header.insert(header.end(), {9, 9, 9, 9});                                       // the mask; no payload follows
            g_track_max = 0;
            ASSERT_TRUE(is_error(header, kWsCloseMessageTooBig));
            ASSERT_TRUE(g_track_max < 1024);                                                 // nothing near the declared size was allocated
        }
        {
            Bytes header = {0x82, 0xFF};                                                     // 2^64 - 1: the top bit is set
            for (int i = 0; i < 8; ++i) header.push_back(0xFF);
            header.insert(header.end(), {9, 9, 9, 9});
            g_track_max = 0;
            ASSERT_TRUE(is_error(header, kWsCloseProtocolError));
            ASSERT_TRUE(g_track_max < 1024);
        }
        // exactly one message fits, one byte more does not: at the header, not at the end of the payload
        {
            Bytes ok = {0x82, 0xFF, 0, 0, 0, 0, 0, 1, 0, 0, 1, 2, 3, 4};                     // 65536 bytes announced; the payload is still to come
            WsFrameParser p(true);
            p.feed(ok.data(), ok.size());
            Bytes out;
            uint16_t status = 0;
            ASSERT_TRUE(p.next(out, status) == WsFrameParser::Result::NeedMore);
            ASSERT_TRUE(is_error(Bytes{0x82, 0xFF, 0, 0, 0, 0, 0, 1, 0, 1, 1, 2, 3, 4}, kWsCloseMessageTooBig));
            ASSERT_TRUE(is_error(Bytes{0x82, 0xFE, 0x00, 0x7E, 1, 2, 3, 4}, kWsCloseMessageTooBig, true, 100));    // 126 bytes with a limit of 100
        }
        // a small limit is honoured
        {
            Lcg r2(1);
            Ev e;
            ASSERT_TRUE(single_event(run_whole(client_frame(WsOpcode::Binary, blob(10, 1), r2), true, 10), WsFrameParser::Result::Message, e));
            ASSERT_TRUE(is_error(client_frame(WsOpcode::Binary, blob(11, 1), r2), kWsCloseMessageTooBig, true, 10));
        }
        // a stream of nothing but fragments never holds more than the limit: the 65th KB is refused
        {
            Bytes s;
            Lcg r2(2);
            for (int i = 0; i < 70; ++i) append(s, client_frame(i == 0 ? WsOpcode::Binary : WsOpcode::Continuation, blob(1024, 1), r2, false));
            g_track_max = 0;
            ASSERT_TRUE(is_error(s, kWsCloseMessageTooBig));
            ASSERT_TRUE(g_track_max <= 4 * kMaxMessageBytes);
        }
    } TEST_END();

    TEST_CASE("W1.8 Close frames: no status or a valid one with an UTF-8 reason; anything else is a protocol error, and nothing is read after a close") {
        Lcg rng(31);
        Ev e;
        // nothing at all
        ASSERT_TRUE(single_event(run_whole(client_frame(WsOpcode::Close, Bytes(), rng)), WsFrameParser::Result::Close, e));
        ASSERT_EQ(e.status, kWsCloseNoStatus);
        ASSERT_TRUE(e.payload.empty());
        // a status and a reason
        ASSERT_TRUE(single_event(run_bytewise(client_frame(WsOpcode::Close, ws_close_payload(1001, "going away \xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"), rng)),
                                 WsFrameParser::Result::Close, e));
        ASSERT_EQ(e.status, 1001);
        ASSERT_TRUE(e.payload == bytes_of("going away \xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"));
        // the longest close frame: 125 bytes
        ASSERT_TRUE(single_event(run_whole(client_frame(WsOpcode::Close, ws_close_payload(1000, std::string(123, 'a')), rng)), WsFrameParser::Result::Close, e));
        ASSERT_EQ(e.payload.size(), 123u);
        // statuses that may be sent
        const uint16_t good_codes[] = {1000, 1001, 1002, 1003, 1007, 1008, 1009, 1010, 1011, 1012, 1013, 1014, 3000, 3999, 4000, 4999};
        for (uint16_t code : good_codes) {
            ASSERT_TRUE(ws_valid_close_status(code));
            ASSERT_TRUE(single_event(run_whole(client_frame(WsOpcode::Close, ws_close_payload(code), rng)), WsFrameParser::Result::Close, e));
            ASSERT_EQ(e.status, code);
        }
        // statuses that may not (below 1000, the reserved 1004 - 1006 and 1015, the unassigned ranges)
        const uint16_t bad_codes[] = {0, 1, 999, 1004, 1005, 1006, 1015, 1016, 2000, 2999, 5000, 65535};
        for (uint16_t code : bad_codes) {
            ASSERT_FALSE(ws_valid_close_status(code));
            ASSERT_TRUE(is_error(client_frame(WsOpcode::Close, ws_close_payload(code), rng), kWsCloseProtocolError));
        }
        // half a status
        ASSERT_TRUE(is_error(client_frame(WsOpcode::Close, Bytes{0x03}, rng), kWsCloseProtocolError));
        // a reason that is not UTF-8
        const char* const bad_reasons[] = {"\xFF", "\xC0\x80", "\xED\xA0\x80", "\xE2\x82", "\xF4\x90\x80\x80", "ok\x80", "\xC3"};
        for (const char* bad : bad_reasons) {
            ASSERT_TRUE(is_error(client_frame(WsOpcode::Close, ws_close_payload(1000, bad), rng), kWsCloseInvalidPayload));
        }
        // nothing is read after a close: the frames behind it are ignored
        {
            Bytes s = client_frame(WsOpcode::Close, ws_close_payload(1000), rng);
            append(s, client_frame(WsOpcode::Binary, bytes_of("late"), rng));
            append(s, Bytes{0x82, 0x00});                                                    // not even a violation behind it is looked at
            const RunResult r = run_bytewise(s);
            ASSERT_TRUE(r.well_behaved);
            ASSERT_EQ(r.events.size(), 1u);
            ASSERT_TRUE(r.events[0].kind == WsFrameParser::Result::Close);
        }
        // a close in the middle of a fragmented message drops the message
        {
            Bytes s;
            append(s, client_frame(WsOpcode::Binary, bytes_of("half"), rng, false));
            append(s, client_frame(WsOpcode::Close, ws_close_payload(1000), rng));
            const RunResult r = run_whole(s);
            ASSERT_EQ(r.events.size(), 1u);
            ASSERT_TRUE(r.events[0].kind == WsFrameParser::Result::Close);
        }
        // UTF-8 itself
        ASSERT_TRUE(ws_valid_utf8(nullptr, 0));
        ASSERT_TRUE(ws_valid_utf8(reinterpret_cast<const uint8_t*>("\x7F"), 1));
        const char* const good[] = {"\xC2\x80", "\xDF\xBF", "\xE0\xA0\x80", "\xEF\xBF\xBF", "\xED\x9F\xBF", "\xEE\x80\x80", "\xF0\x90\x80\x80", "\xF4\x8F\xBF\xBF"};
        for (const char* g : good) ASSERT_TRUE(ws_valid_utf8(reinterpret_cast<const uint8_t*>(g), std::strlen(g)));
        const char* const bad[] = {"\x80", "\xBF", "\xC0\xAF", "\xC1\xBF", "\xE0\x80\x80", "\xE0\x9F\xBF", "\xED\xA0\x80", "\xED\xBF\xBF", "\xF0\x80\x80\x80",
                                   "\xF0\x8F\xBF\xBF", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xF8\x88\x80\x80\x80", "\xFE", "\xC2", "\xE2\x82", "\xF0\x9F\x98"};
        for (const char* b : bad) ASSERT_FALSE(ws_valid_utf8(reinterpret_cast<const uint8_t*>(b), std::strlen(b)));
    } TEST_END();

    TEST_CASE("W1.9 200000 random, mutated and valid streams never crash, hang or allocate more than the limit, and no chunking changes what a stream means") {
        Lcg rng(2024);
        auto pick_size = [&]() -> size_t {
            const uint32_t d = rng.below(400);
            if (d == 0) return 65536 - rng.below(3);                  // the edge of the limit
            if (d < 20) return rng.below(2000);
            return rng.below(200);
        };
        // A valid stream with the events it must produce
        auto build = [&](bool from_client, std::vector<Ev>& expected) {
            Bytes s;
            expected.clear();
            const Mask* nomask = nullptr;
            auto put = [&](WsOpcode op, const Bytes& payload, bool fin) {
                if (from_client) {
                    const Mask m = random_mask(rng);
                    append(s, ws_encode_frame(op, payload, fin, &m));
                } else {
                    append(s, ws_encode_frame(op, payload, fin, nomask));
                }
            };
            const uint32_t items = 1 + rng.below(6);
            for (uint32_t i = 0; i < items; ++i) {
                const uint32_t kind = rng.below(10);
                if (kind < 5) {
                    Ev e;
                    e.kind = WsFrameParser::Result::Message;
                    e.payload = blob(pick_size(), static_cast<uint8_t>(rng.next()));
                    put(WsOpcode::Binary, e.payload, true);
                    expected.push_back(e);
                } else if (kind < 7) {
                    const Bytes whole = blob(pick_size(), static_cast<uint8_t>(rng.next()));
                    const uint32_t pieces = 2 + rng.below(4);
                    size_t at = 0;
                    for (uint32_t k = 0; k < pieces; ++k) {
                        const bool last = k + 1 == pieces;
                        const size_t take = last ? whole.size() - at : rng.below(static_cast<uint32_t>(whole.size() - at) + 1);
                        put(k == 0 ? WsOpcode::Binary : WsOpcode::Continuation, Bytes(whole.begin() + static_cast<std::ptrdiff_t>(at), whole.begin() + static_cast<std::ptrdiff_t>(at + take)), last);
                        at += take;
                        if (!last && rng.below(3) == 0) {
                            Ev c;
                            c.kind = rng.below(2) == 0 ? WsFrameParser::Result::Ping : WsFrameParser::Result::Pong;
                            c.payload = blob(rng.below(126), static_cast<uint8_t>(k));
                            put(c.kind == WsFrameParser::Result::Ping ? WsOpcode::Ping : WsOpcode::Pong, c.payload, true);
                            expected.push_back(c);
                        }
                    }
                    Ev m;
                    m.kind = WsFrameParser::Result::Message;
                    m.payload = whole;
                    expected.push_back(m);
                } else if (kind < 9) {
                    Ev c;
                    c.kind = rng.below(2) == 0 ? WsFrameParser::Result::Ping : WsFrameParser::Result::Pong;
                    c.payload = blob(rng.below(126), static_cast<uint8_t>(i));
                    put(c.kind == WsFrameParser::Result::Ping ? WsOpcode::Ping : WsOpcode::Pong, c.payload, true);
                    expected.push_back(c);
                } else {
                    Ev c;
                    c.kind = WsFrameParser::Result::Close;
                    c.status = rng.below(2) == 0 ? uint16_t{1000} : static_cast<uint16_t>(3000 + rng.below(2000));
                    c.payload = bytes_of(rng.below(2) == 0 ? "" : "bye \xC3\xA9");
                    put(WsOpcode::Close, ws_close_payload(c.status, std::string(c.payload.begin(), c.payload.end())), true);
                    expected.push_back(c);
                    break;
                }
            }
            return s;
        };
        auto mutate = [&](Bytes s) {
            const uint32_t edits = 1 + rng.below(3);
            for (uint32_t e = 0; e < edits; ++e) {
                if (s.empty()) {
                    s.push_back(static_cast<uint8_t>(rng.next()));
                    continue;
                }
                const size_t at = rng.below(static_cast<uint32_t>(s.size()));
                switch (rng.below(7)) {
                    case 0: s[at] = static_cast<uint8_t>(s[at] ^ (1u + rng.below(255))); break;                                      // a changed byte
                    case 1: s.resize(at); break;                                                                                     // cut short
                    case 2: s.insert(s.begin() + static_cast<std::ptrdiff_t>(at), static_cast<uint8_t>(rng.next())); break;          // an extra byte
                    case 3: s.erase(s.begin() + static_cast<std::ptrdiff_t>(at)); break;                                             // a missing byte
                    case 4: s[at] = 0xFF; if (at + 1 < s.size()) s[at + 1] = 0xFF; break;                                            // all ones (lengths)
                    case 5: s[at] = static_cast<uint8_t>(1u << rng.below(8)); break;                                                 // a single bit
                    default: std::swap(s[at], s[rng.below(static_cast<uint32_t>(s.size()))]); break;
                }
            }
            return s;
        };
        size_t errors = 0;
        size_t closes = 0;
        size_t messages = 0;
        g_track_max = 0;
        const int kStreams = 200000;
        for (int i = 0; i < kStreams; ++i) {
            const bool from_client = rng.below(8) != 0;
            Bytes stream;
            std::vector<Ev> expected;
            bool valid = false;
            switch (i % 4) {
                case 0:                                                                  // plain noise
                    stream = blob(rng.below(80), static_cast<uint8_t>(rng.next()));
                    for (uint8_t& b : stream) b = static_cast<uint8_t>(rng.next());
                    break;
                case 1:                                                                  // a valid stream
                    stream = build(from_client, expected);
                    valid = true;
                    break;
                case 2:                                                                  // a valid stream, damaged
                    stream = mutate(build(from_client, expected));
                    break;
                default: {                                                               // header-like bytes: every combination of flags, lengths and masks
                    stream.push_back(static_cast<uint8_t>(rng.next()));
                    stream.push_back(static_cast<uint8_t>(rng.next() | (rng.below(2) != 0 ? 0x80u : 0u)));
                    const uint32_t extra = rng.below(40);
                    for (uint32_t k = 0; k < extra; ++k) stream.push_back(static_cast<uint8_t>(rng.below(4) == 0 ? 0 : rng.next()));
                    break;
                }
            }
            WsFrameParser whole(from_client);
            WsFrameParser pieces(from_client);
            const RunResult a = run_parser(whole, stream, 0, rng);
            const size_t chunk = i % 16 == 0 ? size_t{1} : static_cast<size_t>(2 + i % 29);
            const RunResult b = run_parser(pieces, stream, chunk, rng);
            ASSERT_TRUE(a.well_behaved);
            ASSERT_TRUE(b.well_behaved);
            ASSERT_TRUE(a.events == b.events);                                           // however the bytes are cut, the same events
            if (valid) {
                ASSERT_TRUE(a.events == expected);
            }
            for (const Ev& e : a.events) {
                if (e.kind == WsFrameParser::Result::Message) {
                    ++messages;
                    ASSERT_TRUE(e.payload.size() <= kMaxMessageBytes);
                } else if (e.kind == WsFrameParser::Result::Ping || e.kind == WsFrameParser::Result::Pong) {
                    ASSERT_TRUE(e.payload.size() <= kWsMaxControlBytes);
                } else if (e.kind == WsFrameParser::Result::Close) {
                    ++closes;
                    ASSERT_TRUE(ws_valid_close_status(e.status) || e.status == kWsCloseNoStatus);
                } else if (e.kind == WsFrameParser::Result::Error) {
                    ++errors;
                    ASSERT_TRUE(e.status == kWsCloseProtocolError || e.status == kWsCloseUnsupportedData || e.status == kWsCloseInvalidPayload ||
                                e.status == kWsCloseMessageTooBig);
                }
            }
        }
        // the damage did something, and the valid streams did their job (a test of nothing would pass as well)
        ASSERT_TRUE(errors > 20000);
        ASSERT_TRUE(closes > 1000);
        ASSERT_TRUE(messages > 50000);
        ASSERT_TRUE(g_track_max <= 4 * kMaxMessageBytes);                                // never more than a message (vectors may double) at a time
    } TEST_END();

    // ============================================================================================
    // Handshake

    TEST_CASE("W1.10 A proper upgrade request is answered with the 101 and the accept key, whatever the case, order and company of its headers") {
        const WsHandshakeResult r = handshake(make_request(base_headers()));
        ASSERT_TRUE(r.status == WsHandshakeResult::Status::Accepted);
        ASSERT_EQ(r.http_status, 101);
        ASSERT_EQ(r.response, std::string("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ") + kRfcAccept + "\r\n\r\n");
        ASSERT_FALSE(r.subprotocol);
        ASSERT_EQ(r.consumed, make_request(base_headers()).size());

        // the headers' names and the tokens' case do not matter, nor do blanks around the values, nor the order, nor other headers, nor a query
        ASSERT_TRUE(accepted(make_request({"sec-websocket-version:13", "SEC-WEBSOCKET-KEY:   dGhlIHNhbXBsZSBub25jZQ==  ", "connection: UPGRADE", "UPGRADE: WebSocket",
                                           "host: x", "User-Agent: test", "X-Whatever:", "Accept-Language: en"})));
        ASSERT_TRUE(accepted(make_request(base_headers(), "GET /a/b/c?room=1&x=%20 HTTP/1.1")));
        ASSERT_TRUE(accepted(make_request(base_headers(), "GET / HTTP/1.1")));
        // browsers send "keep-alive, Upgrade"; a proxy may add its own tokens; the header may be repeated
        ASSERT_TRUE(accepted(make_request(replace_header(base_headers(), "connection", "Connection: keep-alive, Upgrade"))));
        ASSERT_TRUE(accepted(make_request(replace_header(base_headers(), "connection", "Connection: Upgrade,close"))));
        ASSERT_TRUE(accepted(make_request(replace_header(base_headers(), "connection", "Connection: keep-alive\r\nConnection: upgrade"))));
        ASSERT_TRUE(accepted(make_request(replace_header(base_headers(), "upgrade", "Upgrade: h2c, websocket"))));
        // a key from a real browser
        ASSERT_EQ(handshake(make_request(replace_header(base_headers(), "sec-websocket-key", "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw=="))).response,
                  "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: HSmrc0sMlYUkAGmm5OPpG2HaGWk=\r\n\r\n");
        // Content-Length: 0 is no body
        ASSERT_TRUE(accepted(make_request(replace_header(base_headers(), "content-length", "Content-Length: 0"))));
        // the subprotocol "ants" is echoed when offered, and only then
        {
            const WsHandshakeResult a = handshake(make_request(replace_header(base_headers(), "sec-websocket-protocol", "Sec-WebSocket-Protocol: ants")));
            ASSERT_TRUE(a.subprotocol);
            ASSERT_TRUE(a.response.find("Sec-WebSocket-Protocol: ants\r\n") != std::string::npos);
            const WsHandshakeResult b = handshake(make_request(replace_header(base_headers(), "sec-websocket-protocol", "Sec-WebSocket-Protocol: chat, ants , v2")));
            ASSERT_TRUE(b.subprotocol);
            const WsHandshakeResult c = handshake(make_request(replace_header(base_headers(), "sec-websocket-protocol", "Sec-WebSocket-Protocol: chat, superchat")));
            ASSERT_TRUE(c.status == WsHandshakeResult::Status::Accepted);
            ASSERT_FALSE(c.subprotocol);
            ASSERT_TRUE(c.response.find("Sec-WebSocket-Protocol") == std::string::npos);
            ASSERT_TRUE(accepted(make_request(base_headers())) && handshake(make_request(base_headers())).response.find("Sec-WebSocket-Protocol") == std::string::npos);
            // extensions are never agreed to
            ASSERT_TRUE(handshake(make_request(replace_header(base_headers(), "sec-websocket-extensions", "Sec-WebSocket-Extensions: permessage-deflate"))).response.find(
                            "Extensions") == std::string::npos);
        }
        // the origin allow-list: empty = any; a listed origin passes (case aside); a request without Origin passes (only browsers send it)
        {
            WsServerOptions o;
            ASSERT_TRUE(accepted(make_request(replace_header(base_headers(), "origin", "Origin: https://anything.example.net")), o));
            o.allowed_origins = {"https://play.example.org", "http://localhost:8080"};
            ASSERT_TRUE(accepted(make_request(replace_header(base_headers(), "origin", "Origin: https://play.example.org")), o));
            ASSERT_TRUE(accepted(make_request(replace_header(base_headers(), "origin", "Origin: HTTP://LOCALHOST:8080")), o));
            ASSERT_TRUE(accepted(make_request(base_headers()), o));
        }
        // the required path: the query is not part of it
        {
            WsServerOptions o;
            o.path = "/ws";
            ASSERT_TRUE(accepted(make_request(base_headers(), "GET /ws HTTP/1.1"), o));
            ASSERT_TRUE(accepted(make_request(base_headers(), "GET /ws?room=7 HTTP/1.1"), o));
            ASSERT_TRUE(accepted(make_request(base_headers(), "GET /ws#frag HTTP/1.1"), o));
        }
    } TEST_END();

    TEST_CASE("W1.11 Everything that is not a proper upgrade is refused with the right HTTP error") {
        const auto bad_key = [](const std::string& key) { return make_request(replace_header(base_headers(), "sec-websocket-key", "Sec-WebSocket-Key: " + key)); };
        // method and version of the request line
        ASSERT_TRUE(refused_with(make_request(base_headers(), "POST /play HTTP/1.1"), 405));
        ASSERT_TRUE(handshake(make_request(base_headers(), "POST /play HTTP/1.1")).response.find("Allow: GET\r\n") != std::string::npos);
        ASSERT_TRUE(refused_with(make_request(base_headers(), "get /play HTTP/1.1"), 405));
        ASSERT_TRUE(refused_with(make_request(base_headers(), "HEAD /play HTTP/1.1"), 405));
        ASSERT_TRUE(refused_with(make_request(base_headers(), "GET /play HTTP/1.0"), 400));
        ASSERT_TRUE(refused_with(make_request(base_headers(), "GET /play HTTP/2.0"), 400));
        ASSERT_TRUE(refused_with(make_request(base_headers(), "GET /play"), 400));
        ASSERT_TRUE(refused_with(make_request(base_headers(), "GET /play HTTP/1.1 extra"), 400));
        ASSERT_TRUE(refused_with(make_request(base_headers(), "GET  /play HTTP/1.1"), 400));
        ASSERT_TRUE(refused_with(make_request(base_headers(), "GET HTTP/1.1"), 400));
        ASSERT_TRUE(refused_with(make_request(base_headers(), ""), 400));
        ASSERT_TRUE(refused_with(make_request(base_headers(), "GET http://play.example.org/ HTTP/1.1"), 400));      // absolute form: a proxy sends the path
        ASSERT_TRUE(refused_with(make_request(base_headers(), "GET play HTTP/1.1"), 400));
        ASSERT_TRUE(refused_with(make_request(base_headers(), "G\x01T /play HTTP/1.1"), 400));
        // Host
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "host", "")), 400));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "host", "Host:")), 400));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "host", "Host: a\r\nHost: b")), 400));
        // Upgrade: a plain GET is told to upgrade (426); what is named must contain the token websocket
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "upgrade", "")), 426));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "upgrade", "Upgrade: h2c")), 426));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "upgrade", "Upgrade: websockets")), 426));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "upgrade", "Upgrade:")), 426));
        ASSERT_TRUE(handshake(make_request(replace_header(base_headers(), "upgrade", ""))).response.find("Upgrade: websocket\r\n") != std::string::npos);
        // Connection must contain the token Upgrade
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "connection", "")), 400));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "connection", "Connection: keep-alive")), 400));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "connection", "Connection: Upgrades")), 400));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "connection", "Connection: close")), 400));
        // the version: 13 or nothing (and the answer names the one that is spoken)
        for (const char* v : {"8", "12", "14", "0", "", "013", "13 13", "13,8", "thirteen", "13.0"}) {
            ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "sec-websocket-version", std::string("Sec-WebSocket-Version: ") + v)), 426));
        }
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "sec-websocket-version", "")), 426));
        ASSERT_TRUE(handshake(make_request(replace_header(base_headers(), "sec-websocket-version", "Sec-WebSocket-Version: 8"))).response.find("Sec-WebSocket-Version: 13\r\n") !=
                    std::string::npos);
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "sec-websocket-version", "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Version: 13")), 400));
        // the key: 16 bytes of base64, once
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "sec-websocket-key", "")), 400));
        ASSERT_TRUE(refused_with(bad_key(""), 400));
        ASSERT_TRUE(refused_with(bad_key("dGhlIHNhbXBsZQ=="), 400));                                                 // 10 bytes
        {
            const Bytes fifteen = blob(15, 1);
            const Bytes seventeen = blob(17, 1);
            const Bytes thirty_two = blob(32, 1);
            ASSERT_TRUE(refused_with(bad_key(ws_base64_encode(fifteen.data(), fifteen.size())), 400));
            ASSERT_TRUE(refused_with(bad_key(ws_base64_encode(seventeen.data(), seventeen.size())), 400));
            ASSERT_TRUE(refused_with(bad_key(ws_base64_encode(thirty_two.data(), thirty_two.size())), 400));
            const Bytes sixteen = blob(16, 77);
            ASSERT_TRUE(accepted(bad_key(ws_base64_encode(sixteen.data(), sixteen.size()))));                        // the same helper with 16 bytes: fine
        }
        ASSERT_TRUE(refused_with(bad_key("!!!!!!!!!!!!!!!!!!!!!!=="), 400));
        ASSERT_TRUE(refused_with(bad_key("dGhlIHNhbXBsZSBub25jZQ=A"), 400));                                          // padding where a digit belongs
        ASSERT_TRUE(refused_with(bad_key("dGhlIHNhbXBsZSBub25jZR=="), 400));                                          // unused bits not zero
        ASSERT_TRUE(refused_with(bad_key("dGhlIHNhbXBsZSBub25jZQ"), 400));                                            // padding missing
        ASSERT_TRUE(refused_with(bad_key("dGhlIHNhbXBsZSBub25jZQ==="), 400));
        ASSERT_TRUE(refused_with(bad_key("dGhlIHNhbXBsZSBub25jZQ-="), 400));
        ASSERT_TRUE(refused_with(bad_key("dGhl IHNhbXBsZSBub25jZQ=="), 400));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "sec-websocket-key", std::string("Sec-WebSocket-Key: ") + kRfcKey + "\r\nSec-WebSocket-Key: " + kRfcKey)), 400));
        // framing of the request itself: CRLF only, no control characters, no folding, no malformed header lines
        {
            std::string lf = make_request(base_headers());
            for (size_t at = lf.find("\r\n"); at != std::string::npos; at = lf.find("\r\n")) lf.replace(at, 2, "\n");
            ASSERT_TRUE(refused_with(lf, 400));
            std::string mixed = make_request(base_headers());
            mixed.replace(mixed.find("\r\n"), 2, "\n");                                                             // one bare LF
            ASSERT_TRUE(refused_with(mixed, 400));
            std::string cr = make_request(base_headers());
            cr.replace(cr.find("\r\n"), 2, "\r");                                                                  // a lone CR
            ASSERT_TRUE(refused_with(cr, 400));
            std::string cr2 = make_request(base_headers());
            cr2.insert(cr2.find("\r\n"), "\r");                                                                    // CR CR LF
            ASSERT_TRUE(refused_with(cr2, 400));
        }
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "x", std::string("X-A: b") + '\0' + "c")), 400));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "x", std::string("X-A: b") + '\x01')), 400));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "x", std::string("X-A: b") + '\x7F')), 400));
        ASSERT_TRUE(accepted(make_request(replace_header(base_headers(), "x", "X-A: tab\tinside"))));                  // a tab is fine inside a value
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "x", "X-No-Colon")), 400));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "x", ": no name")), 400));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "x", "X-A : space before the colon")), 400));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "x", "X (A): not a token")), 400));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "x", "X-A: one\r\n folded")), 400));       // obsolete line folding
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "x", "X-A: one\r\n\tfolded")), 400));
        // a request with a body is no handshake
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "content-length", "Content-Length: 5")), 400));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "content-length", "Content-Length: abc")), 400));
        ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "transfer-encoding", "Transfer-Encoding: chunked")), 400));
        // the origin allow-list: a page of another site gets 403, before anything else is looked at
        {
            WsServerOptions o;
            o.allowed_origins = {"https://play.example.org"};
            for (const char* origin : {"https://evil.example.net", "https://play.example.org.evil.example.net", "http://play.example.org", "null", "",
                                       "https://play.example.org/", "https://play.example.org:8443", "play.example.org"}) {
                ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "origin", std::string("Origin: ") + origin)), 403, o));
            }
            ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "origin", "Origin: https://play.example.org\r\nOrigin: https://play.example.org")), 403, o));
            ASSERT_TRUE(refused_with(make_request(replace_header(replace_header(base_headers(), "upgrade", ""), "origin", "Origin: https://evil.example.net")), 403, o));
            // without the list the same requests are fine
            ASSERT_TRUE(accepted(make_request(replace_header(base_headers(), "origin", "Origin: https://evil.example.net"))));
        }
        // the required path: exactly that, the query aside; decided before the headers
        {
            WsServerOptions o;
            o.path = "/ws";
            for (const char* path : {"/", "/ws/", "/WS", "/w", "/wss", "/ws2", "/x/ws", "/%77s", "/ws/x", "//ws"}) {
                ASSERT_TRUE(refused_with(make_request(base_headers(), std::string("GET ") + path + " HTTP/1.1"), 404, o));
            }
            ASSERT_TRUE(refused_with(make_request(replace_header(base_headers(), "upgrade", ""), "GET /other HTTP/1.1"), 404, o));
        }
        // the number of header lines is limited
        {
            std::vector<std::string> h = base_headers();
            for (int i = 0; i < 58; ++i) h.push_back("X-N" + std::to_string(i) + ": v");                              // 63 headers
            ASSERT_TRUE(accepted(make_request(h)));
            h.push_back("X-Last: v");                                                                                // 64: the most
            ASSERT_TRUE(accepted(make_request(h)));
            h.push_back("X-Over: v");
            ASSERT_TRUE(refused_with(make_request(h), 431));
        }
    } TEST_END();

    TEST_CASE("W1.12 A request that is not complete is waited for, however slowly it comes; 8 KB is the limit; what follows the blank line is left alone") {
        const std::string req = make_request(base_headers());
        for (size_t n = 0; n < req.size(); ++n) {                                      // every shorter prefix: not yet
            const WsHandshakeResult r = handshake(req.substr(0, n));
            ASSERT_TRUE(r.status == WsHandshakeResult::Status::NeedMore);
            ASSERT_TRUE(r.response.empty());
        }
        ASSERT_TRUE(handshake(req).status == WsHandshakeResult::Status::Accepted);

        // a request that is already wrong is refused at the first sign (a bare LF, a control character), not after the whole 8 KB
        {
            const std::string bad = "GET /play HTTP/1.1\nHost: x\r\n\r\n";
            ASSERT_TRUE(refused_with(bad.substr(0, 20), 400));                         // up to and including the bare LF
            ASSERT_TRUE(refused_with("GET /pla\x02", 400));
            ASSERT_TRUE(handshake("GET /play HTTP/1.1\r").status == WsHandshakeResult::Status::NeedMore);      // a CR at the end may still get its LF
        }

        // the limit: a request of 8192 bytes (through the blank line) is fine, 8193 is not
        const auto padded = [](size_t total) {
            std::vector<std::string> h = base_headers();
            const size_t fixed = make_request(h).size() + std::string("X-Pad: ").size() + 2;
            h.push_back("X-Pad: " + std::string(total - fixed, 'a'));
            return make_request(h);
        };
        ASSERT_EQ(padded(8192).size(), kWsMaxRequestBytes);
        ASSERT_TRUE(accepted(padded(8192)));
        ASSERT_TRUE(refused_with(padded(8193), 431));
        ASSERT_TRUE(refused_with(padded(20000), 431));
        ASSERT_TRUE(handshake(padded(8192).substr(0, 8191)).status == WsHandshakeResult::Status::NeedMore);   // not complete, and may still fit
        ASSERT_TRUE(refused_with(padded(8200).substr(0, 8192), 431));                                          // no blank line in 8 KB: it cannot fit any more
        ASSERT_TRUE(refused_with(std::string(9000, 'a'), 431));
        ASSERT_TRUE(handshake(std::string(8191, 'a')).status == WsHandshakeResult::Status::NeedMore);

        // what follows the blank line belongs to the frames
        {
            const std::string frames = std::string("\x82\x80", 2) + "abcd";
            const WsHandshakeResult r = handshake(req + frames);
            ASSERT_TRUE(r.status == WsHandshakeResult::Status::Accepted);
            ASSERT_EQ(r.consumed, req.size());
            const WsHandshakeResult refused = handshake(make_request(base_headers(), "POST / HTTP/1.1") + frames);
            ASSERT_TRUE(refused.status == WsHandshakeResult::Status::Rejected);
            ASSERT_EQ(refused.consumed, make_request(base_headers(), "POST / HTTP/1.1").size());
        }
    } TEST_END();

    TEST_CASE("W1.13 20000 mutated handshake requests are judged without a crash; what is accepted and refused is always well formed") {
        Lcg rng(77);
        const std::string good = make_request(replace_header(base_headers(), "sec-websocket-protocol", "Sec-WebSocket-Protocol: chat, ants"), "GET /play HTTP/1.1");
        WsServerOptions picky;
        picky.allowed_origins = {"https://play.example.org"};
        picky.path = "/play";
        const std::string alphabet = ": \r\n,-\t\x01\x7F\x80/?=HhGgUuWw0123";
        size_t accepted_n = 0;
        size_t refused_n = 0;
        size_t more_n = 0;
        for (int i = 0; i < 20000; ++i) {
            std::string r = good;
            if (i % 5 == 4) r.insert(r.find("Sec-WebSocket-Key"), "Origin: https://play.example.org\r\n");
            const uint32_t edits = i % 7 == 0 ? 0 : 1 + rng.below(4);
            for (uint32_t e = 0; e < edits && !r.empty(); ++e) {
                const size_t at = rng.below(static_cast<uint32_t>(r.size()));
                switch (rng.below(6)) {
                    case 0: r[at] = static_cast<char>(rng.next()); break;
                    case 1: r[at] = alphabet[rng.below(static_cast<uint32_t>(alphabet.size()))]; break;
                    case 2: r.erase(at, 1 + rng.below(8)); break;
                    case 3: r.insert(at, 1, alphabet[rng.below(static_cast<uint32_t>(alphabet.size()))]); break;
                    case 4: r.resize(at); break;
                    default: r.insert(at, r.substr(rng.below(static_cast<uint32_t>(r.size())), rng.below(40))); break;
                }
            }
            const WsHandshakeResult res = handshake(r, i % 2 == 0 ? WsServerOptions() : picky);
            switch (res.status) {
                case WsHandshakeResult::Status::Accepted:
                    ++accepted_n;
                    ASSERT_EQ(res.http_status, 101);
                    ASSERT_TRUE(res.response.find("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ") == 0);
                    ASSERT_TRUE(res.response.size() >= 4 && res.response.compare(res.response.size() - 4, 4, "\r\n\r\n") == 0);
                    ASSERT_TRUE(res.consumed >= 4 && res.consumed <= r.size() && r.compare(res.consumed - 4, 4, "\r\n\r\n") == 0);
                    ASSERT_TRUE(r.find("\r\n\r\n") == res.consumed - 4);                  // the first blank line ends the request
                    break;
                case WsHandshakeResult::Status::Rejected:
                    ++refused_n;
                    ASSERT_TRUE(res.http_status == 400 || res.http_status == 403 || res.http_status == 404 || res.http_status == 405 || res.http_status == 426 ||
                                res.http_status == 431);
                    ASSERT_TRUE(res.response.find("HTTP/1.1 " + std::to_string(res.http_status) + " ") == 0);
                    ASSERT_TRUE(res.response.find("Connection: close\r\n") != std::string::npos);
                    ASSERT_TRUE(res.response.find("101") == std::string::npos);
                    break;
                case WsHandshakeResult::Status::NeedMore:
                    ++more_n;
                    ASSERT_TRUE(res.response.empty());
                    ASSERT_TRUE(r.find("\r\n\r\n") == std::string::npos);
                    break;
                case WsHandshakeResult::Status::Answered:
                    ASSERT_TRUE(false);                                                   // (these options have no status path)
                    break;
            }
        }
        ASSERT_TRUE(accepted_n > 1000);
        ASSERT_TRUE(refused_n > 5000);
        ASSERT_TRUE(more_n > 100);
    } TEST_END();

    // ============================================================================================
    // Real sockets

    TEST_CASE("W1.14 Over a real socket: the upgrade, then binary messages of every size in both directions, in order, one frame each") {
        Rig rig;
        ASSERT_TRUE(rig.start());
        ASSERT_TRUE(rig.listener->port() != 0);
        ASSERT_TRUE(WsListener::listen(rig.listener->port(), true) == nullptr);                    // the port is taken
        Link link;
        ASSERT_TRUE(open_link(rig, link));
        WsConnection& srv = *link.server;
        ASSERT_TRUE(srv.is_open());
        ASSERT_TRUE(srv.peer().find("127.0.0.1:") == 0);

        std::vector<Bytes> got;
        const size_t sizes[] = {0, 1, 2, 100, 125, 126, 127, 4095, 4096, 4097, 16384, 60000, kMaxMessageBytes};
        for (size_t n : sizes) {
            const Bytes m = blob(n, static_cast<uint8_t>(n));
            got.clear();
            ASSERT_TRUE(link.client.send_frame(0x82, m));
            ASSERT_TRUE(poll_server(rig, srv, got, [&]() { return !got.empty(); }));
            ASSERT_EQ(got.size(), 1u);
            ASSERT_TRUE(got[0] == m);
            ASSERT_TRUE(srv.send(m));
            Frame f;
            ASSERT_TRUE(read_frame(rig, link.client, f));
            ASSERT_TRUE(f.fin && f.op == 0x2 && f.rsv == 0 && !f.masked);                              // one binary frame, whole, unmasked
            ASSERT_TRUE(f.payload == m);
        }
        ASSERT_FALSE(srv.send(blob(kMaxMessageBytes + 1, 1)));                                         // over the limit: refused, connection intact
        ASSERT_TRUE(srv.is_open());

        // bursts of small messages keep their boundaries and their order in both directions
        Lcg rng(3);
        for (int burst = 0; burst < 20; ++burst) {
            std::vector<Bytes> sent;
            for (int i = 0; i < 500; ++i) {
                sent.push_back(blob(1 + rng.below(100), static_cast<uint8_t>(burst * 500 + i)));
                ASSERT_TRUE(link.client.send_frame(0x82, sent.back()));
            }
            got.clear();
            ASSERT_TRUE(poll_server(rig, srv, got, [&]() { return got.size() >= sent.size(); }));
            ASSERT_TRUE(got == sent);
            for (const Bytes& m : sent) ASSERT_TRUE(srv.send(m));
            for (const Bytes& m : sent) {
                Frame f;
                ASSERT_TRUE(read_frame(rig, link.client, f));
                ASSERT_TRUE(f.op == 0x2 && f.payload == m);
            }
        }
        // thousands of tiny messages at once: the connection takes them as the game polls (it stops reading while a pile is waiting) and loses none
        {
            Bytes burst;
            for (uint32_t i = 0; i < 10000; ++i) append(burst, link.client.build_frame(0x82, Bytes{static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8)}));
            ASSERT_TRUE(link.client.send_bytes(burst));
            got.clear();
            ASSERT_TRUE(poll_server(rig, srv, got, [&]() { return got.size() >= 10000; }));
            ASSERT_EQ(got.size(), 10000u);
            for (uint32_t i = 0; i < 10000; ++i) ASSERT_TRUE(got[i] == (Bytes{static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8)}));
        }
    } TEST_END();

    TEST_CASE("W1.15 Pings: the peer's ping is answered with a pong at once, a quiet connection is pinged and stays open; pings can be switched off") {
        ASSERT_EQ(WsServerOptions().ping_interval_ms, 20000u);                                         // the proxies' default timeout is 60 s
        ASSERT_EQ(WsServerOptions().handshake_timeout_ms, 5000u);
        {
            WsServerOptions o;
            o.ping_interval_ms = 50;
            Rig rig;
            ASSERT_TRUE(rig.start(o));
            Link link;
            ASSERT_TRUE(open_link(rig, link));
            WsConnection& srv = *link.server;
            // a ping with data between two messages: the messages arrive in order, the pong carries the same data
            ASSERT_TRUE(link.client.send_frame(0x82, bytes_of("one")));
            ASSERT_TRUE(link.client.send_frame(0x89, bytes_of("abc")));
            ASSERT_TRUE(link.client.send_frame(0x82, bytes_of("two")));
            std::vector<Bytes> got;
            ASSERT_TRUE(poll_server(rig, srv, got, [&]() { return got.size() == 2; }));
            ASSERT_TRUE(got[0] == bytes_of("one") && got[1] == bytes_of("two"));
            bool pong = false;
            for (int guard = 0; guard < 50 && !pong; ++guard) {
                Frame f;
                ASSERT_TRUE(read_frame(rig, link.client, f));
                if (f.op == 0xA) {
                    pong = true;
                    ASSERT_TRUE(f.fin && f.payload == bytes_of("abc"));
                } else {
                    ASSERT_EQ(f.op, 0x9);                                                              // nothing but the server's own pings may come before it
                }
            }
            ASSERT_TRUE(pong);
            // a ping of 125 bytes, an empty ping, and an unsolicited pong (ignored, no answer)
            ASSERT_TRUE(link.client.send_frame(0x89, blob(125, 1)));
            ASSERT_TRUE(link.client.send_frame(0x89, Bytes()));
            ASSERT_TRUE(link.client.send_frame(0x8A, bytes_of("unsolicited")));
            bool big = false;
            bool empty = false;
            int answers = 0;
            rig.wait([&]() {
                Bytes d;
                while (srv.poll(d)) {}
                link.client.pull();
                Frame f;
                while (link.client.take_frame(f)) {
                    if (f.op != 0xA) continue;
                    ++answers;
                    big = big || f.payload == blob(125, 1);
                    empty = empty || f.payload.empty();
                }
                return big && empty;
            });
            ASSERT_TRUE(big && empty);
            ASSERT_TRUE(srv.is_open());
            // the server pings a connection that sends nothing, and the pong keeps it open
            int pings = 0;
            bool well_formed = true;
            rig.wait(
                [&]() {
                    Bytes d;
                    while (srv.poll(d)) {}
                    link.client.pull();
                    Frame f;
                    while (link.client.take_frame(f)) {
                        if (f.op != 0x9) continue;
                        ++pings;
                        well_formed = well_formed && f.fin && f.payload.empty() && !f.masked;
                        link.client.send_frame(0x8A, f.payload);
                    }
                    return pings >= 3;
                },
                5000);
            ASSERT_TRUE(pings >= 3);
            ASSERT_TRUE(well_formed);
            ASSERT_TRUE(srv.is_open());
            got.clear();
            ASSERT_TRUE(srv.send(bytes_of("still here")));
            ASSERT_TRUE(link.client.send_frame(0x82, bytes_of("me too")));
            ASSERT_TRUE(poll_server(rig, srv, got, [&]() { return !got.empty(); }));
            ASSERT_TRUE(got[0] == bytes_of("me too"));
            (void)answers;
        }
        {
            // with the pings off a silent connection stays silent (and is not closed by this class either)
            WsServerOptions o;
            o.ping_interval_ms = 0;
            Rig rig;
            ASSERT_TRUE(rig.start(o));
            Link link;
            ASSERT_TRUE(open_link(rig, link));
            for (int i = 0; i < 300; ++i) {
                Bytes d;
                ASSERT_FALSE(link.server->poll(d));
                link.client.pull();
                sleep_ms(1);
            }
            ASSERT_EQ(link.client.waiting(), 0u);
            ASSERT_TRUE(link.server->is_open());
            ASSERT_FALSE(link.client.eof());
        }
    } TEST_END();

    TEST_CASE("W1.16 The close handshake: the peer's status is echoed and the socket closed; what came before the close is still delivered") {
        // the client says goodbye with a status and a reason
        {
            Rig rig;
            ASSERT_TRUE(rig.start());
            Link link;
            ASSERT_TRUE(open_link(rig, link));
            ASSERT_TRUE(link.client.send_frame(0x88, ws_close_payload(1001, "bye")));
            std::vector<Bytes> got;
            ASSERT_TRUE(poll_server(rig, *link.server, got, [&]() { return link.server->state() == Connection::State::Closed; }));
            ASSERT_TRUE(got.empty());
            Frame f;
            ASSERT_TRUE(read_frame(rig, link.client, f));
            ASSERT_TRUE(f.op == 0x8 && f.fin && f.payload.size() >= 2);
            ASSERT_EQ((f.payload[0] << 8) | f.payload[1], 1001);                                       // the peer's status, echoed
            ASSERT_TRUE(rig.wait([&]() {
                link.client.pull();
                return link.client.eof();
            }));
            ASSERT_FALSE(link.server->send(bytes_of("x")));
            ASSERT_EQ(link.server->state(), Connection::State::Closed);
            ASSERT_FALSE(link.server->is_open());
        }
        // a close without a status is answered without one
        {
            Rig rig;
            ASSERT_TRUE(rig.start());
            Link link;
            ASSERT_TRUE(open_link(rig, link));
            ASSERT_TRUE(link.client.send_frame(0x88, Bytes()));
            std::vector<Bytes> got;
            ASSERT_TRUE(poll_server(rig, *link.server, got, [&]() { return link.server->state() == Connection::State::Closed; }));
            Frame f;
            ASSERT_TRUE(read_frame(rig, link.client, f));
            ASSERT_TRUE(f.op == 0x8 && f.payload.empty());
        }
        // the game closes: a close frame with 1000, then the socket
        {
            Rig rig;
            ASSERT_TRUE(rig.start());
            Link link;
            ASSERT_TRUE(open_link(rig, link));
            ASSERT_TRUE(link.server->send(bytes_of("last words")));
            link.server->close();
            ASSERT_EQ(link.server->state(), Connection::State::Closed);
            ASSERT_FALSE(link.server->send(bytes_of("x")));
            Frame f;
            ASSERT_TRUE(read_frame(rig, link.client, f));
            ASSERT_TRUE(f.op == 0x2 && f.payload == bytes_of("last words"));                           // what was queued before the close goes first
            ASSERT_TRUE(read_frame(rig, link.client, f));
            ASSERT_TRUE(f.op == 0x8 && f.payload.size() == 2 && f.payload[0] == 0x03 && f.payload[1] == 0xE8);
            ASSERT_TRUE(rig.wait([&]() {
                link.client.pull();
                return link.client.eof();
            }));
            link.server->close();                                                                      // closing twice is harmless
            ASSERT_EQ(link.server->state(), Connection::State::Closed);
        }
        // messages that arrived before the close frame can still be polled after the connection is Closed
        {
            Rig rig;
            ASSERT_TRUE(rig.start());
            Link link;
            ASSERT_TRUE(open_link(rig, link));
            Bytes all = link.client.build_frame(0x82, bytes_of("first"));
            append(all, link.client.build_frame(0x82, bytes_of("second")));
            append(all, link.client.build_frame(0x88, ws_close_payload(1000)));
            ASSERT_TRUE(link.client.send_bytes(all));
            Bytes m;
            ASSERT_TRUE(rig.wait([&]() { return link.server->poll(m); }));
            ASSERT_TRUE(m == bytes_of("first"));
            ASSERT_EQ(link.server->state(), Connection::State::Closed);
            ASSERT_TRUE(link.server->poll(m));
            ASSERT_TRUE(m == bytes_of("second"));
            ASSERT_FALSE(link.server->poll(m));
        }
        // the socket closed without a close frame is Closed too (not Failed), after what was sent
        {
            Rig rig;
            ASSERT_TRUE(rig.start());
            Link link;
            ASSERT_TRUE(open_link(rig, link));
            ASSERT_TRUE(link.client.send_frame(0x82, bytes_of("parting")));
            link.client.close();
            std::vector<Bytes> got;
            ASSERT_TRUE(poll_server(rig, *link.server, got, [&]() { return link.server->state() == Connection::State::Closed; }));
            ASSERT_TRUE(got.size() == 1 && got[0] == bytes_of("parting"));
        }
        // a half frame and then the close of the socket delivers nothing
        {
            Rig rig;
            ASSERT_TRUE(rig.start());
            Link link;
            ASSERT_TRUE(open_link(rig, link));
            Bytes frame = link.client.build_frame(0x82, blob(1000, 1));
            frame.resize(500);
            ASSERT_TRUE(link.client.send_bytes(frame));
            link.client.close();
            std::vector<Bytes> got;
            ASSERT_TRUE(poll_server(rig, *link.server, got, [&]() { return link.server->state() == Connection::State::Closed; }));
            ASSERT_TRUE(got.empty());
        }
        // a connection that is dropped says goodbye
        {
            Rig rig;
            ASSERT_TRUE(rig.start());
            Link link;
            ASSERT_TRUE(open_link(rig, link));
            link.server = nullptr;
            rig.conns.clear();
            Frame f;
            ASSERT_TRUE(read_frame(rig, link.client, f));
            ASSERT_TRUE(f.op == 0x8 && f.payload.size() == 2 && f.payload[1] == 0xE8);
        }
    } TEST_END();

    TEST_CASE("W1.17 A peer that breaks the protocol gets the close status of its fault and the connection Failed; what came before is delivered") {
        struct Case {
            const char* what;
            uint8_t first_byte;         // of the frame the test builds (masked) ...
            Bytes payload;
            Bytes raw;                  // ... or, when not empty, these bytes exactly
            uint16_t status;
        };
        const Case cases[] = {
            {"an unmasked frame", 0, {}, {0x82, 0x02, 'h', 'i'}, 1002},
            {"text", 0x81, bytes_of("hello"), {}, 1003},
            {"reserved bit RSV1", 0xC2, bytes_of("x"), {}, 1002},
            {"a reserved opcode", 0x83, bytes_of("x"), {}, 1002},
            {"a continuation of nothing", 0x80, bytes_of("x"), {}, 1002},
            {"a fragmented ping", 0x09, bytes_of("x"), {}, 1002},
            {"a ping of 126 bytes", 0x89, blob(126, 1), {}, 1002},
            {"a message over the limit (header only)", 0, {}, {0x82, 0xFF, 0, 0, 0, 0, 0, 1, 0, 1, 1, 2, 3, 4}, 1009},
            {"a length of 2^63", 0, {}, {0x82, 0xFF, 0x80, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4}, 1002},
            {"a close of one byte", 0x88, Bytes{0x03}, {}, 1002},
            {"a close with a forbidden status", 0x88, ws_close_payload(1005), {}, 1002},
            {"a close reason that is not UTF-8", 0x88, ws_close_payload(1000, "\xFF\xFE"), {}, 1007},
        };
        for (const Case& c : cases) {
            Rig rig;
            ASSERT_TRUE(rig.start());
            Link link;
            ASSERT_TRUE(open_link(rig, link));
            Bytes wire = link.client.build_frame(0x82, bytes_of("before"));                           // a good message first
            append(wire, c.raw.empty() ? link.client.build_frame(c.first_byte, c.payload) : c.raw);
            ASSERT_TRUE(link.client.send_bytes(wire));
            std::vector<Bytes> got;
            ASSERT_TRUE(poll_server(rig, *link.server, got, [&]() { return link.server->state() == Connection::State::Failed; }));
            ASSERT_TRUE(got.size() == 1 && got[0] == bytes_of("before"));
            Frame f;
            ASSERT_TRUE(read_frame(rig, link.client, f));
            ASSERT_TRUE(f.op == 0x8 && f.fin && f.payload.size() == 2);
            ASSERT_EQ((f.payload[0] << 8) | f.payload[1], c.status);
            ASSERT_TRUE(rig.wait([&]() {
                link.client.pull();
                return link.client.eof();
            }));
            ASSERT_FALSE(link.server->send(bytes_of("x")));
            ASSERT_FALSE(link.server->is_open());
        }
    } TEST_END();

    TEST_CASE("W1.18 A peer that never reads fails the connection once 1 MB is waiting; a slow reader that does read loses nothing") {
        // never reads
        {
            Rig rig;
            ASSERT_TRUE(rig.start());
            Link link;
            ASSERT_TRUE(open_link(rig, link, "", 4096));
            WsConnection& srv = *link.server;
            const Bytes m = blob(60000, 1);
            size_t accepted_messages = 0;
            for (int i = 0; i < 4000 && srv.is_open(); ++i) {
                if (srv.send(m)) ++accepted_messages;
                ASSERT_TRUE(srv.backlog() <= kWsMaxBacklogBytes + kMaxMessageBytes + 16);              // the queue is bounded at every moment
            }
            ASSERT_EQ(srv.state(), Connection::State::Failed);
            ASSERT_TRUE(accepted_messages >= 16);                                                      // 1 MB is 16 messages of this size: a short stall is allowed
            ASSERT_TRUE(accepted_messages < 4000);
            ASSERT_EQ(srv.backlog(), 0u);
            ASSERT_FALSE(srv.send(bytes_of("x")));
            Bytes d;
            ASSERT_FALSE(srv.poll(d));
        }
        // reads slowly: 12 messages (720 KB) wait, none is lost, all come in order
        {
            Rig rig;
            ASSERT_TRUE(rig.start());
            Link link;
            ASSERT_TRUE(open_link(rig, link, "", 8192));
            WsConnection& srv = *link.server;
            std::vector<Bytes> sent;
            for (int i = 0; i < 12; ++i) {
                sent.push_back(blob(60000, static_cast<uint8_t>(i)));
                ASSERT_TRUE(srv.send(sent.back()));
            }
            ASSERT_TRUE(srv.is_open());
            size_t next = 0;
            ASSERT_TRUE(rig.wait(
                [&]() {
                    Bytes d;
                    srv.poll(d);                                                                       // keeps writing what is waiting
                    link.client.pull();
                    Frame f;
                    while (next < sent.size() && link.client.take_frame(f)) {
                        if (f.op != 0x2 || f.payload != sent[next]) return true;                      // a mismatch ends the wait (checked below)
                        ++next;
                    }
                    return next == sent.size();
                },
                8000));
            ASSERT_EQ(next, sent.size());
            ASSERT_TRUE(srv.is_open());
            ASSERT_EQ(srv.backlog(), 0u);
        }
        // never reads, and PINGS the server about half a million times: every ping makes the server queue a 2-byte pong. The cost of queueing is linear in the
        // number of frames (appending used to copy the whole backlog again and again: this took several seconds of the server's time, now some milliseconds)
        {
            Rig rig;
            ASSERT_TRUE(rig.start());
            Link link;
            ASSERT_TRUE(open_link(rig, link, "", 4096));
            WsConnection& srv = *link.server;
            Bytes burst;
            for (int i = 0; i < 2000; ++i) {
                const Bytes ping = link.client.build_frame(0x89, Bytes());
                burst.insert(burst.end(), ping.begin(), ping.end());
            }
            double server_seconds = 0;
            size_t pings = 0;
            for (int round = 0; round < 10000 && srv.is_open(); ++round) {                          // (until the connection fails: a host with big socket buffers takes more pings first)
                if (!link.client.send_raw(burst)) break;
                pings += 2000;
                const auto started = std::chrono::steady_clock::now();
                for (int k = 0; k < 8; ++k) {
                    Bytes d;
                    srv.poll(d);
                }
                server_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            }
            std::cout << "[" << pings << " pings, the server needed " << server_seconds << " s] ";
            ASSERT_TRUE(pings >= 400000);
            ASSERT_EQ(srv.state(), Connection::State::Failed);                                         // 1 MB of pongs were waiting
            ASSERT_TRUE(server_seconds < 1.5);
        }
    } TEST_END();

    TEST_CASE("W1.19 Handshakes over real sockets: refusals are answered with their HTTP error and the socket closed, nothing reaches the game") {
        struct Refusal {
            std::string request;
            int status;
            const char* header;     // a header the answer must carry ("" = none)
            WsServerOptions options;
        };
        std::vector<Refusal> refusals;
        refusals.push_back({upgrade_request("", "/play", kRfcKey, "8"), 426, "Sec-WebSocket-Version: 13", WsServerOptions()});
        refusals.push_back({"POST /play HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n", 405, "Allow: GET", WsServerOptions()});
        refusals.push_back({"GET /play HTTP/1.1\r\nHost: x\r\n\r\n", 426, "Upgrade: websocket", WsServerOptions()});
        refusals.push_back({upgrade_request("", "/play", "not a key"), 400, "", WsServerOptions()});
        {
            WsServerOptions o;
            o.allowed_origins = {"https://play.example.org"};
            refusals.push_back({upgrade_request("Origin: https://evil.example.net\r\n"), 403, "", o});
        }
        {
            WsServerOptions o;
            o.path = "/ws";
            refusals.push_back({upgrade_request("", "/play"), 404, "", o});
        }
        refusals.push_back({"GET /play HTTP/1.1\nHost: x\n\n", 400, "", WsServerOptions()});      // bare line feeds never even reach a blank line the HTTP way
        for (const Refusal& r : refusals) {
            Rig rig;
            ASSERT_TRUE(rig.start(r.options));
            RawClient client;
            const std::string head = connect_and_ask(rig, client, r.request);
            ASSERT_TRUE(head.find("HTTP/1.1 " + std::to_string(r.status) + " ") == 0);
            ASSERT_TRUE(head.find("Connection: close\r\n") != std::string::npos);
            if (r.header[0] != '\0') ASSERT_TRUE(head.find(std::string(r.header) + "\r\n") != std::string::npos);
            ASSERT_TRUE(rig.wait([&]() {
                client.pull();
                return client.eof();
            }));
            ASSERT_TRUE(rig.conns.empty());
            ASSERT_EQ(rig.listener->pending(), 0u);
        }
        // a request far over 8 KB, or one that never ends a line, gets 431 (and the answer arrives although most of the request was never read)
        for (int kind = 0; kind < 2; ++kind) {
            Rig rig;
            ASSERT_TRUE(rig.start());
            RawClient client;
            const std::string big = kind == 0 ? "GET /play HTTP/1.1\r\nHost: x\r\nX-Pad: " + std::string(40000, 'a') + "\r\n\r\n" : "GET /" + std::string(40000, 'a');
            const std::string head = connect_and_ask(rig, client, big);
            ASSERT_TRUE(head.find("HTTP/1.1 431 ") == 0);
            ASSERT_TRUE(rig.wait([&]() {
                client.pull();
                return client.eof();
            }));
            ASSERT_TRUE(rig.conns.empty());
        }
    } TEST_END();

    TEST_CASE("W1.20 Handshakes over real sockets: a stalled one is answered 408 and dropped, an abandoned one is forgotten, a slow one does not hold the others up") {
        // stalled: half a request, then nothing
        {
            WsServerOptions o;
            o.handshake_timeout_ms = 150;
            Rig rig;
            ASSERT_TRUE(rig.start(o));
            RawClient client;
            // the clock starts before the connect: the listener counts from the moment it takes the socket in, which is later, so that the answer cannot come
            // sooner than the timeout after `start` however busy the machine is
            const auto start = std::chrono::steady_clock::now();
            ASSERT_TRUE(client.connect(rig.listener->port()));
            ASSERT_TRUE(client.send_text("GET /play HTTP/1.1\r\nHost: play.exa"));
            ASSERT_TRUE(rig.wait([&]() { return rig.listener->pending() == 1; }));
            std::string head;
            ASSERT_TRUE(rig.wait([&]() {
                client.pull();
                return client.take_head(head);
            }));
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            ASSERT_TRUE(head.find("HTTP/1.1 408 ") == 0);
            ASSERT_TRUE(elapsed >= 140 && elapsed < 3000);                                             // not before the timeout (150 ms), and not never
            ASSERT_TRUE(rig.wait([&]() {
                client.pull();
                return client.eof();
            }));
            ASSERT_EQ(rig.listener->pending(), 0u);
            ASSERT_TRUE(rig.conns.empty());
        }
        // a client that connects and leaves without a word
        {
            Rig rig;
            ASSERT_TRUE(rig.start());
            RawClient client;
            ASSERT_TRUE(client.connect(rig.listener->port()));
            ASSERT_TRUE(rig.wait([&]() { return rig.listener->pending() == 1; }));
            client.close();
            ASSERT_TRUE(rig.wait([&]() { return rig.listener->pending() == 0; }));
            ASSERT_TRUE(rig.conns.empty());
        }
        // a request that comes one byte at a time is a connection only when its last byte is in
        {
            Rig rig;
            ASSERT_TRUE(rig.start());
            RawClient client;
            ASSERT_TRUE(client.connect(rig.listener->port()));
            const std::string request = upgrade_request();
            for (size_t i = 0; i < request.size(); ++i) {
                ASSERT_TRUE(client.send_text(std::string(1, request[i])));
                rig.step();
                if (i + 1 < request.size()) ASSERT_TRUE(rig.conns.empty());
            }
            ASSERT_TRUE(rig.wait([&]() { return rig.conns.size() == 1; }));
            std::string head;
            ASSERT_TRUE(rig.wait([&]() {
                client.pull();
                return client.take_head(head);
            }));
            ASSERT_TRUE(head.find("HTTP/1.1 101 ") == 0);
        }
        // three clients at once: the slow one (half a request) does not hold up the other two, which are told apart by what they send
        {
            Rig rig;
            ASSERT_TRUE(rig.start());
            RawClient a;
            RawClient b;
            RawClient c;
            ASSERT_TRUE(a.connect(rig.listener->port()));
            ASSERT_TRUE(b.connect(rig.listener->port()));
            ASSERT_TRUE(c.connect(rig.listener->port()));
            const std::string request = upgrade_request();
            ASSERT_TRUE(a.send_text(request.substr(0, 40)));
            ASSERT_TRUE(b.send_text(request));
            ASSERT_TRUE(c.send_text(request));
            ASSERT_TRUE(rig.wait([&]() { return rig.conns.size() == 2; }));
            for (int i = 0; i < 30; ++i) {                                                             // a is still pending
                rig.step();
                sleep_ms(1);
            }
            ASSERT_EQ(rig.conns.size(), 2u);
            ASSERT_EQ(rig.listener->pending(), 1u);
            ASSERT_TRUE(a.send_text(request.substr(40)));
            ASSERT_TRUE(rig.wait([&]() { return rig.conns.size() == 3; }));
            ASSERT_TRUE(a.send_frame(0x82, bytes_of("from a")));
            ASSERT_TRUE(b.send_frame(0x82, bytes_of("from b")));
            ASSERT_TRUE(c.send_frame(0x82, bytes_of("from c")));
            std::vector<std::string> seen;
            ASSERT_TRUE(rig.wait([&]() {
                for (auto& conn : rig.conns) {
                    Bytes m;
                    while (conn->poll(m)) seen.push_back(std::string(m.begin(), m.end()));
                }
                return seen.size() == 3;
            }));
            std::sort(seen.begin(), seen.end());
            ASSERT_TRUE(seen == (std::vector<std::string>{"from a", "from b", "from c"}));
        }
        // a request and the first frames in one write: the message is there as soon as the connection is
        {
            Rig rig;
            ASSERT_TRUE(rig.start());
            RawClient client;
            ASSERT_TRUE(client.connect(rig.listener->port()));
            Bytes wire = bytes_of(upgrade_request());
            append(wire, client.build_frame(0x82, bytes_of("early")));
            append(wire, client.build_frame(0x89, bytes_of("ping")));
            ASSERT_TRUE(client.send_bytes(wire));
            ASSERT_TRUE(rig.wait([&]() { return rig.conns.size() == 1; }));
            Bytes m;
            ASSERT_TRUE(rig.conns[0]->poll(m));
            ASSERT_TRUE(m == bytes_of("early"));
            std::string head;
            Frame f;
            ASSERT_TRUE(rig.wait([&]() {
                client.pull();
                if (head.empty()) client.take_head(head);
                return !head.empty() && client.take_frame(f);
            }));
            ASSERT_TRUE(head.find("HTTP/1.1 101 ") == 0);
            ASSERT_TRUE(f.op == 0xA && f.payload == bytes_of("ping"));                                  // the pong follows the 101
        }
    } TEST_END();

    TEST_CASE("W1.21 The listener limits the handshakes in progress, closes what it holds when it goes, and honours the origin, the path and the subprotocol") {
        // at most max_pending sockets are held; the others are closed at once, the held ones still work
        {
            WsServerOptions o;
            o.max_pending = 3;
            Rig rig;
            ASSERT_TRUE(rig.start(o));
            RawClient clients[6];
            for (RawClient& c : clients) ASSERT_TRUE(c.connect(rig.listener->port()));
            ASSERT_TRUE(rig.wait([&]() {
                for (int i = 3; i < 6; ++i) clients[i].pull();
                return clients[3].eof() && clients[4].eof() && clients[5].eof();
            }));
            for (int i = 0; i < 3; ++i) {
                clients[i].pull();
                ASSERT_FALSE(clients[i].eof());
            }
            ASSERT_EQ(rig.listener->pending(), 3u);
            ASSERT_TRUE(clients[0].send_text(upgrade_request()));
            std::string head;
            ASSERT_TRUE(rig.wait([&]() {
                clients[0].pull();
                return clients[0].take_head(head);
            }));
            ASSERT_TRUE(head.find("HTTP/1.1 101 ") == 0);
            ASSERT_EQ(rig.conns.size(), 1u);
            // and a place is free again
            RawClient again;
            ASSERT_TRUE(again.connect(rig.listener->port()));
            ASSERT_TRUE(again.send_text(upgrade_request()));
            ASSERT_TRUE(rig.wait([&]() { return rig.conns.size() == 2; }));
        }
        // pending sockets are closed when the listener goes
        {
            Rig rig;
            ASSERT_TRUE(rig.start());
            RawClient client;
            ASSERT_TRUE(client.connect(rig.listener->port()));
            ASSERT_TRUE(rig.wait([&]() { return rig.listener->pending() == 1; }));
            rig.listener.reset();
            bool eof = false;
            for (int i = 0; i < 2000 && !eof; ++i) {
                client.pull();
                eof = client.eof();
                if (!eof) sleep_ms(1);
            }
            ASSERT_TRUE(eof);
        }
        // origin, path and subprotocol on the wire
        {
            WsServerOptions o;
            o.allowed_origins = {"https://play.example.org"};
            o.path = "/ws";
            Rig rig;
            ASSERT_TRUE(rig.start(o));
            RawClient listed;
            std::string head = connect_and_ask(rig, listed, upgrade_request("Origin: https://play.example.org\r\nSec-WebSocket-Protocol: chat, ants\r\n", "/ws?room=1"));
            ASSERT_TRUE(head.find("HTTP/1.1 101 ") == 0);
            ASSERT_TRUE(head.find("Sec-WebSocket-Protocol: ants\r\n") != std::string::npos);
            RawClient foreign;
            head = connect_and_ask(rig, foreign, upgrade_request("Origin: https://evil.example.net\r\n", "/ws"));
            ASSERT_TRUE(head.find("HTTP/1.1 403 ") == 0);
            RawClient plain;                                                                            // a native client: no Origin header
            head = connect_and_ask(rig, plain, upgrade_request("", "/ws"));
            ASSERT_TRUE(head.find("HTTP/1.1 101 ") == 0);
            ASSERT_TRUE(head.find("Sec-WebSocket-Protocol") == std::string::npos);
            RawClient elsewhere;
            head = connect_and_ask(rig, elsewhere, upgrade_request("", "/other"));
            ASSERT_TRUE(head.find("HTTP/1.1 404 ") == 0);
            ASSERT_EQ(rig.conns.size(), 2u);
        }
    } TEST_END();

    TEST_CASE("W1.22 1000 messages of random sizes are echoed through the Connection interface, in both directions, byte for byte") {
        Rig rig;
        ASSERT_TRUE(rig.start());
        Link link;
        ASSERT_TRUE(open_link(rig, link));
        ClientConnection client(link.client);
        std::unique_ptr<Connection> server(std::move(rig.conns.back()));                              // the game sees nothing but the interface
        rig.conns.pop_back();
        Connection& a = client;
        Connection& b = *server;
        ASSERT_TRUE(a.is_open() && b.is_open());
        Lcg rng(5150);
        size_t echoed = 0;
        size_t bytes = 0;
        size_t biggest = 0;
        // `first` sends, `second` receives and sends the same message back, `first` receives it
        const auto echo = [&](Connection& first, Connection& second, const Bytes& m) {
            if (!first.send(m)) return false;
            Bytes at_second;
            if (!rig.wait([&]() { return second.poll(at_second); }) || at_second != m) return false;
            if (!second.send(at_second)) return false;
            Bytes back;
            if (!rig.wait([&]() { return first.poll(back); }) || back != m) return false;
            return true;
        };
        for (int i = 0; i < 1000; ++i) {
            const uint32_t d = rng.below(100);
            const size_t size = d < 55 ? rng.below(101) : d < 85 ? 100 + rng.below(2900) : d < 98 ? 3000 + rng.below(27000) : d < 99 ? 30000 + rng.below(35537) : kMaxMessageBytes;
            const Bytes m = blob(size, static_cast<uint8_t>(rng.next()));
            const bool ok = i % 3 == 0 ? echo(b, a, m) : echo(a, b, m);
            ASSERT_TRUE(ok);
            ++echoed;
            bytes += size;
            biggest = std::max(biggest, size);
        }
        ASSERT_EQ(echoed, 1000u);
        ASSERT_TRUE(biggest > 30000);
        ASSERT_TRUE(bytes > 1000000);
        // nothing is left over, and the interface says what the sockets do
        Bytes none;
        ASSERT_FALSE(a.poll(none));
        ASSERT_FALSE(b.poll(none));
        ASSERT_FALSE(b.send(blob(kMaxMessageBytes + 1, 0)));
        ASSERT_TRUE(b.is_open());
        b.close();
        ASSERT_EQ(b.state(), Connection::State::Closed);
        ASSERT_FALSE(b.send(none));
    } TEST_END();

    TEST_CASE("W1.23 A burst of 64 connections that come while the server is not looking are all waiting for it when it looks (the listener's backlog was 32)") {
        Rig rig;
        ASSERT_TRUE(rig.start());                                                 // (the listener holds 64 handshakes at the most: max_pending)
        constexpr size_t kBurst = 64;
        std::vector<sock_t> socks;
        for (size_t i = 0; i < kBurst; ++i) {
            const sock_t s = ::socket(AF_INET, SOCK_STREAM, 0);
            ASSERT_TRUE(s != kBadSock);
            sock_nonblocking(s);
            sockaddr_in a;
            std::memset(&a, 0, sizeof(a));
            a.sin_family = AF_INET;
            a.sin_port = htons(rig.listener->port());
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            (void)::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));        // (begun, not waited for: the answer to a burst comes from the operating system)
            socks.push_back(s);
        }
        sleep_ms(300);                                                            // a slow pass of the server's loop: the handshakes are finished, nobody has accepted
        for (int i = 0; i < 20; ++i) rig.step();                                  // (a call takes in at most 16 sockets)
        const size_t held = rig.listener->pending();
        for (const sock_t s : socks) sock_close(s);
        ASSERT_EQ(held, kBurst);                                                  // all of them were waiting (with a backlog of 32, 32 were, on macOS)
    } TEST_END();

    TEST_CASE("W1.24 The status path (the server's /busy): a plain GET of exactly that path is answered 200 with the status text, JSON and no-store, whatever the other options say; nothing else is, and over a real socket the answer arrives whole, the socket closes, nothing reaches the game and an upgrade still works") {
        const std::string body = "{\"matches\":2,\"players\":5}";
        WsServerOptions o;
        o.status_path = "/busy";
        int calls = 0;
        o.status_body = [&calls, &body]() {
            ++calls;
            return body;
        };
        const std::string get = "GET /busy HTTP/1.1\r\nHost: play.example.org\r\n\r\n";
        {
            const WsHandshakeResult r = handshake(get, o);
            ASSERT_TRUE(r.status == WsHandshakeResult::Status::Answered && r.http_status == 200 && r.consumed == get.size());
            ASSERT_TRUE(r.response.find("HTTP/1.1 200 OK\r\n") == 0);
            ASSERT_TRUE(r.response.find("Content-Type: application/json\r\n") != std::string::npos);
            ASSERT_TRUE(r.response.find("Cache-Control: no-store\r\n") != std::string::npos);
            ASSERT_TRUE(r.response.find("X-Content-Type-Options: nosniff\r\n") != std::string::npos);
            ASSERT_TRUE(r.response.find("Content-Length: " + std::to_string(body.size()) + "\r\n") != std::string::npos);
            ASSERT_TRUE(r.response.find("Connection: close\r\n") != std::string::npos);
            ASSERT_TRUE(r.response.size() > body.size() && r.response.compare(r.response.size() - body.size(), body.size(), body) == 0);
            ASSERT_EQ(calls, 1);
        }
        calls = 0;                                                                          // not a status request: the usual answers, and the text is never asked for
        ASSERT_TRUE(refused_with("GET /busy?x=1 HTTP/1.1\r\nHost: x\r\n\r\n", 426, o));          // no query: a path with a query is no status request
        ASSERT_TRUE(refused_with("GET /busy/ HTTP/1.1\r\nHost: x\r\n\r\n", 426, o));
        ASSERT_TRUE(refused_with("GET /Busy HTTP/1.1\r\nHost: x\r\n\r\n", 426, o));
        ASSERT_TRUE(refused_with("GET /other HTTP/1.1\r\nHost: x\r\n\r\n", 426, o));
        ASSERT_TRUE(refused_with("POST /busy HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n", 405, o));
        ASSERT_TRUE(refused_with("HEAD /busy HTTP/1.1\r\nHost: x\r\n\r\n", 405, o));
        ASSERT_TRUE(refused_with("PUT /busy HTTP/1.1\r\nHost: x\r\n\r\n", 405, o));
        ASSERT_TRUE(refused_with("GET /busy HTTP/1.1\r\n\r\n", 400, o));                           // no Host
        ASSERT_TRUE(refused_with("GET /busy HTTP/1.1\r\nHost: x\r\nHost: y\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("GET /busy HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhello", 400, o));
        ASSERT_TRUE(refused_with("GET /busy HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("GET /busy HTTP/1.0\r\nHost: x\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("GET /busy HTTP/1.1\nHost: x\n\n", 400, o));
        ASSERT_EQ(calls, 0);
        ASSERT_TRUE(handshake("GET /busy HTTP/1.1\r\nHost: x\r\n", o).status == WsHandshakeResult::Status::NeedMore);   // an incomplete request is waited for
        ASSERT_EQ(calls, 0);
        ASSERT_TRUE(accepted(upgrade_request("", "/busy"), o));                             // with an Upgrade header it is the usual handshake: the status path takes nothing from the door
        ASSERT_EQ(calls, 0);
        {
            WsServerOptions none = o;                                                       // no path, or no function: no status
            none.status_path.clear();
            ASSERT_TRUE(refused_with(get, 426, none));
            WsServerOptions nofn = o;
            nofn.status_body = nullptr;
            ASSERT_TRUE(refused_with(get, 426, nofn));
            WsServerOptions strict = o;                                                     // the path, origin and the other options of the door do not matter to it
            strict.path = "/ws";
            strict.allowed_origins = {"https://play.example.org"};
            const WsHandshakeResult r = handshake("GET /busy HTTP/1.1\r\nHost: x\r\nOrigin: https://evil.example.net\r\n\r\n", strict);
            ASSERT_TRUE(r.status == WsHandshakeResult::Status::Answered);
            ASSERT_TRUE(refused_with(upgrade_request("Origin: https://evil.example.net\r\n", "/ws"), 403, strict));     // (and the door itself is as strict as before)
        }
        {
            WsServerOptions big = o;                                                        // a text that is too long is a 500, never a long answer; the longest allowed goes out
            big.status_body = []() { return std::string(kWsMaxStatusBytes + 1, 'x'); };
            ASSERT_TRUE(refused_with(get, 500, big));
            WsServerOptions edge = o;
            edge.status_body = []() { return std::string(kWsMaxStatusBytes, 'x'); };
            ASSERT_TRUE(handshake(get, edge).status == WsHandshakeResult::Status::Answered);
        }
        {
            Rig rig;                                                                        // over a real socket
            ASSERT_TRUE(rig.start(o));
            RawClient client;
            const std::string head = connect_and_ask(rig, client, get);
            ASSERT_TRUE(head.find("HTTP/1.1 200 OK\r\n") == 0);
            ASSERT_TRUE(rig.wait([&]() {
                client.pull();
                return client.eof();
            }));
            ASSERT_EQ(client.take_text(), body);
            ASSERT_TRUE(rig.conns.empty());                                                 // nothing reached the game
            ASSERT_EQ(rig.listener->pending(), 0u);
            RawClient socket;                                                               // and the door still opens for a game
            const std::string upgrade = connect_and_ask(rig, socket, upgrade_request());
            ASSERT_TRUE(upgrade.find("HTTP/1.1 101 ") == 0);
            ASSERT_TRUE(rig.wait([&]() { return rig.conns.size() == 1; }));
        }
        {
            Rig rig;                                                                        // set_status after the listener exists (the server does it once its rooms exist)
            ASSERT_TRUE(rig.start());
            RawClient before;
            ASSERT_TRUE(connect_and_ask(rig, before, get).find("HTTP/1.1 426 ") == 0);
            rig.listener->set_status("/busy", [&body]() { return body; });
            RawClient after;
            ASSERT_TRUE(connect_and_ask(rig, after, get).find("HTTP/1.1 200 OK\r\n") == 0);
        }
    } TEST_END();

    TEST_CASE("W1.25 More status paths and the post path (the server's /stats and POST /stats/local): a GET of each listed path is answered like /busy; a POST of exactly the post path with no body is answered 204 and counted ONCE by the listener (never by the parser); a query, a body, an upgrade, another path, another method, a missing Host or a long request are refused, and over a real socket nothing reaches the game") {
        int busy_calls = 0;
        int stats_calls = 0;
        int more_calls = 0;
        int posts = 0;
        WsServerOptions o;
        o.status_path = "/busy";
        o.status_body = [&busy_calls]() { ++busy_calls; return std::string("{\"matches\":0,\"players\":0}"); };
        o.more_status.emplace_back("/stats", [&stats_calls]() { ++stats_calls; return std::string("{\"online\":1}"); });
        o.more_status.emplace_back("/more", [&more_calls]() { ++more_calls; return std::string("{\"more\":true}"); });
        o.post_path = "/stats/local";
        o.post_action = [&posts]() { ++posts; };
        const std::string post = "POST /stats/local HTTP/1.1\r\nHost: play.example.org\r\n\r\n";
        // ---- a GET of every listed path is a status answer, each calls its own function once; /busy is what it was ----
        {
            const WsHandshakeResult r = handshake("GET /stats HTTP/1.1\r\nHost: x\r\n\r\n", o);
            ASSERT_TRUE(r.status == WsHandshakeResult::Status::Answered && r.http_status == 200 && !r.posted);
            ASSERT_TRUE(r.response.find("Cache-Control: no-store\r\n") != std::string::npos && r.response.find("Content-Type: application/json\r\n") != std::string::npos);
            ASSERT_TRUE(r.response.compare(r.response.size() - 12, 12, "{\"online\":1}") == 0);
            ASSERT_TRUE(stats_calls == 1 && busy_calls == 0 && more_calls == 0);
            ASSERT_TRUE(handshake("GET /more HTTP/1.1\r\nHost: x\r\n\r\n", o).response.find("{\"more\":true}") != std::string::npos);
            ASSERT_TRUE(handshake("GET /busy HTTP/1.1\r\nHost: x\r\n\r\n", o).response.find("{\"matches\":0,\"players\":0}") != std::string::npos);
            ASSERT_TRUE(stats_calls == 1 && busy_calls == 1 && more_calls == 1);
        }
        // the same rules as /busy: no query, no other case, no trailing slash, no body, one Host, and an upgrade is the usual handshake; nothing asks for the text
        stats_calls = busy_calls = more_calls = 0;
        ASSERT_TRUE(refused_with("GET /stats?x=1 HTTP/1.1\r\nHost: x\r\n\r\n", 426, o));
        ASSERT_TRUE(refused_with("GET /stats/ HTTP/1.1\r\nHost: x\r\n\r\n", 426, o));
        ASSERT_TRUE(refused_with("GET /Stats HTTP/1.1\r\nHost: x\r\n\r\n", 426, o));
        ASSERT_TRUE(refused_with("GET /stat HTTP/1.1\r\nHost: x\r\n\r\n", 426, o));
        ASSERT_TRUE(refused_with("GET /stats HTTP/1.1\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("GET /stats HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhello", 400, o));
        ASSERT_TRUE(refused_with("GET /stats HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("POST /stats HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n", 405, o));          // (a POST of a status path: 405, Allow: GET)
        ASSERT_TRUE(handshake("POST /stats HTTP/1.1\r\nHost: x\r\n\r\n", o).response.find("Allow: GET\r\n") != std::string::npos);
        ASSERT_TRUE(accepted(upgrade_request("", "/stats"), o));
        ASSERT_TRUE(stats_calls == 0 && busy_calls == 0 && more_calls == 0 && posts == 0);
        {
            WsServerOptions blank = o;                                                       // an empty path or no function in the list is no status
            blank.more_status.emplace_back("", []() { return std::string("{}"); });
            blank.more_status.emplace_back("/nofn", nullptr);
            ASSERT_TRUE(refused_with("GET / HTTP/1.1\r\nHost: x\r\n\r\n", 426, blank));
            ASSERT_TRUE(refused_with("GET /nofn HTTP/1.1\r\nHost: x\r\n\r\n", 426, blank));
        }
        // ---- the POST: 204, nothing to read, the parser itself counts nothing ----
        {
            const WsHandshakeResult r = handshake(post, o);
            ASSERT_TRUE(r.status == WsHandshakeResult::Status::Answered && r.http_status == 204 && r.posted && r.consumed == post.size());
            ASSERT_TRUE(r.response.find("HTTP/1.1 204 No Content\r\n") == 0 && r.response.find("Connection: close\r\n") != std::string::npos);
            ASSERT_TRUE(r.response.find("Cache-Control: no-store\r\n") != std::string::npos && r.response.find("Content-Length") == std::string::npos);
            ASSERT_TRUE(r.response.size() >= 4 && r.response.compare(r.response.size() - 4, 4, "\r\n\r\n") == 0);                // (no body after the head)
            ASSERT_EQ(posts, 0);                                                              // (the listener runs the action: the parser is pure)
            ASSERT_TRUE(handshake("POST /stats/local HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n", o).posted);
            ASSERT_TRUE(handshake("POST /stats/local HTTP/1.1\r\nHost: x\r\nContent-Length:   0  \r\n\r\n", o).posted);
            ASSERT_TRUE(handshake("POST /stats/local HTTP/1.1\r\nHost: x\r\nContent-Type: text/plain\r\nAccept: */*\r\nOrigin: https://play.example.org\r\n\r\n", o).posted);   // (what a browser adds)
            const WsHandshakeResult tail = handshake(post + "POST /stats/local HTTP/1.1\r\n", o);                                          // (whatever follows is not read)
            ASSERT_TRUE(tail.posted && tail.consumed == post.size());
            ASSERT_TRUE(handshake("POST /stats/local HTTP/1.1\r\nHost: x\r\n", o).status == WsHandshakeResult::Status::NeedMore);       // an incomplete request is waited for
            ASSERT_EQ(posts, 0);
        }
        // ---- what the post path refuses: a body, a query, an upgrade, no or two Hosts, the wrong version, another path, another method, a long request ----
        ASSERT_TRUE(refused_with("POST /stats/local HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhello", 400, o));
        ASSERT_TRUE(refused_with("POST /stats/local HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\n\r\nx", 400, o));
        ASSERT_TRUE(refused_with("POST /stats/local HTTP/1.1\r\nHost: x\r\nContent-Length: 00\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("POST /stats/local HTTP/1.1\r\nHost: x\r\nContent-Length: 0, 0\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("POST /stats/local HTTP/1.1\r\nHost: x\r\nContent-Length: -1\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("POST /stats/local HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("POST /stats/local HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\nTransfer-Encoding: chunked\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("POST /stats/local HTTP/1.1\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("POST /stats/local HTTP/1.1\r\nHost:\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("POST /stats/local HTTP/1.1\r\nHost: x\r\nHost: y\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("POST /stats/local HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("POST /stats/local HTTP/1.1\r\nHost: x\r\nUpgrade: h2c\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("POST /stats/local HTTP/1.0\r\nHost: x\r\n\r\n", 400, o));
        ASSERT_TRUE(refused_with("POST /stats/local HTTP/1.1\nHost: x\n\n", 400, o));
        ASSERT_TRUE(refused_with("POST  /stats/local HTTP/1.1\r\nHost: x\r\n\r\n", 400, o));
        for (const char* other : {"/stats/local?x=1", "/stats/local?", "/stats/local#a", "/stats/local/", "/stats/localx", "/stats/Local", "/stats/loca", "/stats", "/busy", "/", "/ws", "/stats/local/x", "//stats/local"}) {
            const std::string request = std::string("POST ") + other + " HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n";
            ASSERT_TRUE(refused_with(request, 405, o));                                       // (a POST of any other path: 405, and it names GET: nothing but the status paths take a request)
            ASSERT_TRUE(handshake(request, o).response.find("Allow: GET\r\n") != std::string::npos);
        }
        for (const char* method : {"GET", "HEAD", "PUT", "DELETE", "PATCH", "OPTIONS", "TRACE", "CONNECT", "post", "Post", "PROPFIND"}) {
            const std::string request = std::string(method) + " /stats/local HTTP/1.1\r\nHost: x\r\n\r\n";
            const WsHandshakeResult r = handshake(request, o);
            ASSERT_TRUE(r.status == WsHandshakeResult::Status::Rejected && r.http_status == 405 && !r.posted);                           // (a GET of the post path too: it is no upgrade)
            ASSERT_TRUE(r.response.find("Allow: POST\r\n") != std::string::npos && r.response.find("Allow: GET") == std::string::npos);
        }
        ASSERT_TRUE(accepted(upgrade_request("", "/stats/local"), o));                        // the upgrade of a game is the door's own business, on any path as before
        {
            const std::string path = "/" + std::string(7000, 'a');                              // a long path that fits in the 8 KB of a request: not the post path
            ASSERT_TRUE(refused_with("POST " + path + " HTTP/1.1\r\nHost: x\r\n\r\n", 405, o));
            ASSERT_TRUE(refused_with("POST /stats/local HTTP/1.1\r\nHost: x\r\nX-Pad: " + std::string(9000, 'b') + "\r\n\r\n", 431, o));            // too long a request: refused whole
            ASSERT_TRUE(refused_with("POST /" + std::string(9000, 'a') + " HTTP/1.1\r\nHost: x\r\n\r\n", 431, o));
            ASSERT_TRUE(handshake("POST /stats/local HTTP/1.1\r\nHost: x\r\nX-Pad: " + std::string(kWsMaxRequestBytes, 'b'), o).http_status == 431);   // (no blank line within the limit: refused before it comes)
        }
        ASSERT_EQ(posts, 0);
        {
            WsServerOptions off = o;                                                          // no function or no path: no post path, and the POST is any other POST
            off.post_action = nullptr;
            ASSERT_TRUE(refused_with(post, 405, off));
            ASSERT_TRUE(handshake(post, off).response.find("Allow: GET\r\n") != std::string::npos);
            off = o;
            off.post_path.clear();
            ASSERT_TRUE(refused_with(post, 405, off));
            ASSERT_TRUE(refused_with("GET /stats/local HTTP/1.1\r\nHost: x\r\n\r\n", 426, off));
            ASSERT_TRUE(refused_with("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n", 405, off));      // (an empty path never matches "/")
        }
        // ---- over a real socket ----
        {
            Rig rig;
            ASSERT_TRUE(rig.start(o));
            RawClient client;
            const std::string head = connect_and_ask(rig, client, post);
            ASSERT_TRUE(head.find("HTTP/1.1 204 No Content\r\n") == 0);
            ASSERT_TRUE(rig.wait([&]() {
                client.pull();
                return client.eof();
            }));                                                                              // the socket is closed after the answer
            ASSERT_TRUE(client.take_text().empty());                                          // (and the answer has no body)
            ASSERT_EQ(posts, 1);
            ASSERT_TRUE(rig.conns.empty());                                                   // nothing reached the game
            ASSERT_EQ(rig.listener->pending(), 0u);
            for (int i = 0; i < 9; ++i) {                                                     // ten in all: each counted once
                RawClient more;
                ASSERT_TRUE(connect_and_ask(rig, more, post).find("HTTP/1.1 204 ") == 0);
            }
            ASSERT_EQ(posts, 10);
            RawClient slow;                                                                   // a client that sends one byte at a time is counted once, when its request is whole
            ASSERT_TRUE(slow.connect(rig.listener->port()));
            for (size_t i = 0; i + 1 < post.size(); ++i) {
                ASSERT_TRUE(slow.send_text(std::string(1, post[i])));
                for (int k = 0; k < 3; ++k) rig.step();
                ASSERT_EQ(posts, 10);
            }
            ASSERT_TRUE(slow.send_text(std::string(1, post.back())));
            std::string slow_head;
            ASSERT_TRUE(rig.wait([&]() {
                slow.pull();
                return slow.take_head(slow_head);
            }));
            ASSERT_TRUE(slow_head.find("HTTP/1.1 204 ") == 0);
            ASSERT_EQ(posts, 11);
            RawClient body;                                                                   // a body: refused, not counted, the socket still closes cleanly
            ASSERT_TRUE(connect_and_ask(rig, body, "POST /stats/local HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhello").find("HTTP/1.1 400 ") == 0);
            RawClient query;
            ASSERT_TRUE(connect_and_ask(rig, query, "POST /stats/local?x=1 HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n").find("HTTP/1.1 405 ") == 0);
            RawClient get;
            ASSERT_TRUE(connect_and_ask(rig, get, "GET /stats/local HTTP/1.1\r\nHost: x\r\n\r\n").find("HTTP/1.1 405 ") == 0);
            RawClient stats;                                                                  // and a status path of the list answers beside it
            ASSERT_TRUE(connect_and_ask(rig, stats, "GET /stats HTTP/1.1\r\nHost: x\r\n\r\n").find("HTTP/1.1 200 OK\r\n") == 0);
            ASSERT_EQ(posts, 11);
            ASSERT_TRUE(rig.conns.empty());
            RawClient socket;                                                                 // the door still opens for a game
            ASSERT_TRUE(connect_and_ask(rig, socket, upgrade_request()).find("HTTP/1.1 101 ") == 0);
            ASSERT_TRUE(rig.wait([&]() { return rig.conns.size() == 1; }));
        }
        {
            posts = 0;
            Rig rig;                                                                          // set_post and add_status after the listener exists (the server does it once its rooms and its statistics exist)
            ASSERT_TRUE(rig.start());
            RawClient before;
            ASSERT_TRUE(connect_and_ask(rig, before, post).find("HTTP/1.1 405 ") == 0);
            RawClient before_get;
            ASSERT_TRUE(connect_and_ask(rig, before_get, "GET /stats HTTP/1.1\r\nHost: x\r\n\r\n").find("HTTP/1.1 426 ") == 0);
            rig.listener->set_post("/stats/local", [&posts]() { ++posts; });
            rig.listener->add_status("/stats", []() { return std::string("{\"ok\":true}"); });
            RawClient after;
            ASSERT_TRUE(connect_and_ask(rig, after, post).find("HTTP/1.1 204 ") == 0);
            ASSERT_EQ(posts, 1);
            RawClient after_get;
            ASSERT_TRUE(connect_and_ask(rig, after_get, "GET /stats HTTP/1.1\r\nHost: x\r\n\r\n").find("HTTP/1.1 200 OK\r\n") == 0);
        }
    } TEST_END();

    TEST_CASE("W1.26 What a peer sent just before it reset the link (it closed with bytes of ours unread) is delivered before the connection ends, whether the connection reads or writes first and whether a close frame follows it: a Leave is not lost to the reset") {
        for (const bool close_frame : {false, true}) {
            for (const bool write_first : {false, true}) {
                Rig rig;
                ASSERT_TRUE(rig.start());
                Link link;
                ASSERT_TRUE(open_link(rig, link));
                WsConnection& srv = *link.server;
                ASSERT_TRUE(srv.send(bytes_of("unread")));
                ASSERT_TRUE(rig.wait([&]() { return link.client.unread(); }));                         // (the client never reads it: where the kernel answers a close with unread bytes by a reset, the link ends in one)
                Bytes last = link.client.build_frame(0x82, bytes_of("LEAVE"));
                if (close_frame) append(last, link.client.build_frame(0x88, ws_close_payload(1001)));   // (a browser that quits says goodbye after its message)
                ASSERT_TRUE(link.client.send_bytes(last));                                              // (in one write: what is not yet sent goes with the reset)
                link.client.close();
                if (write_first) srv.send(bytes_of("turn"));                                            // (a room sends a turn every 50 ms, whether the peer reads or not: the write comes before the read)
                std::vector<Bytes> got;
                ASSERT_TRUE(poll_server(rig, srv, got, [&]() { return !srv.is_open(); }));               // (the reset ends the connection)
#ifdef __linux__
                ASSERT_TRUE(got.size() == 1 && got[0] == bytes_of("LEAVE"));                            // (macOS and Windows may drop what came before a reset in their kernels: nothing is asked of them here)
                ASSERT_EQ(srv.state(), close_frame ? Connection::State::Closed : Connection::State::Failed);   // (a close frame among what was read ends it Closed, a reset alone Failed)
#endif
            }
        }
    } TEST_END();

    std::cout << "\n=======================================================\n";
    std::cout << " WebSocket transport: " << g_test_count << " test cases, " << g_assert_count << " assertions, " << g_test_failures << " failures\n";
    std::cout << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
