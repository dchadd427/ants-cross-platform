// Tests of the control interface primitives (ants_ctl): the strict JSON library (valid and invalid documents of the JSONTestSuite kind, the limits, the
// typed accessors, the serializer, round trips of random values, 200,000 random and mutated documents checked against an independent recognizer) and the
// HTTP server over real loopback sockets with a raw client (the secret, every status the server can answer itself, a request in single bytes, pipelining,
// clients that vanish, the connection limit, response safety, the clock). Nothing here needs the game's simulation or assets.
#include "ants_ctl/http.hpp"
#include "ants_ctl/json.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
using iolen_t = int;
static constexpr sock_t kBadSock = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using sock_t = int;
using iolen_t = size_t;
static constexpr sock_t kBadSock = -1;
#endif

using namespace ants::ctl;

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
#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
// The same with a text that says which input failed (the documents of a table, the iteration of a fuzz loop)
#define ASSERT_MSG(cond, msg) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n    " << (msg) << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

namespace {

// ---- helpers shared by the JSON and the HTTP tests ----

template <size_t N>
std::string lit(const char (&a)[N]) {                  // a string literal with its embedded NULs
    return std::string(a, N - 1);
}

std::string shown(const std::string& s) {              // a text with its unprintable bytes made visible (for failure messages)
    std::string out;
    for (const char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= 0x20 && u < 0x7F) {
            out.push_back(c);
        } else {
            char b[8];
            std::snprintf(b, sizeof(b), "\\x%02x", u);
            out += b;
        }
        if (out.size() > 200) {
            out += "...";
            break;
        }
    }
    return out;
}

struct Rng {                                            // xorshift64*, a fixed seed gives the same run every time
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ull;
    }
    uint32_t below(uint32_t n) { return static_cast<uint32_t>((next() >> 11) % n); }
    bool chance(uint32_t percent) { return below(100) < percent; }
};

bool parses(const std::string& text) {
    JsonValue v;
    std::string e;
    return parse_json(text, v, &e);
}

JsonValue parse_ok(const std::string& text) {
    JsonValue v;
    std::string e;
    if (!parse_json(text, v, &e)) return JsonValue::make_string("PARSE FAILED: " + e);
    return v;
}

std::string error_of(const std::string& text, const JsonLimits& limits = {}) {
    JsonValue v;
    std::string e;
    return parse_json(text, v, &e, limits) ? std::string() : e;
}

std::string nested_arrays(size_t depth) { return std::string(depth, '[') + std::string(depth, ']'); }

std::string nested_objects(size_t depth) {
    std::string s;
    for (size_t i = 0; i < depth; ++i) s += "{\"a\":";
    s += "1";
    s += std::string(depth, '}');
    return s;
}

// ---- an independent recognizer of JSON text (an explicit stack and a state machine instead of recursion; no values are built) for the differential
// fuzz. It knows grammar, UTF-8, escapes and the depth / number-length limits, not repeated keys and not doubles that overflow.

bool recognizer_utf8(const std::string& s, size_t& i) {                 // one non-ASCII character at s[i]
    const unsigned char b = static_cast<unsigned char>(s[i]);
    size_t n = 0;
    uint32_t cp = 0;
    uint32_t min = 0;
    if ((b & 0xE0) == 0xC0) { n = 2; cp = b & 0x1Fu; min = 0x80; }
    else if ((b & 0xF0) == 0xE0) { n = 3; cp = b & 0x0Fu; min = 0x800; }
    else if ((b & 0xF8) == 0xF0) { n = 4; cp = b & 0x07u; min = 0x10000; }
    else return false;
    if (i + n > s.size()) return false;
    for (size_t k = 1; k < n; ++k) {
        const unsigned char c = static_cast<unsigned char>(s[i + k]);
        if ((c & 0xC0) != 0x80) return false;
        cp = (cp << 6) | (c & 0x3Fu);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    i += n;
    return true;
}

bool recognizer_string(const std::string& s, size_t& i) {                // s[i] is the opening quote
    ++i;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"') {
            ++i;
            return true;
        }
        if (c < 0x20) return false;
        if (c == '\\') {
            if (i + 1 >= s.size()) return false;
            const char e = s[i + 1];
            if (e == 'u') {
                const auto hex4 = [&](size_t at, uint32_t& v) {
                    if (at + 4 > s.size()) return false;
                    v = 0;
                    for (size_t k = 0; k < 4; ++k) {
                        const char h = s[at + k];
                        if (!std::isxdigit(static_cast<unsigned char>(h))) return false;
                        v = v * 16 + static_cast<uint32_t>(std::isdigit(static_cast<unsigned char>(h)) ? h - '0' : (h | 0x20) - 'a' + 10);
                    }
                    return true;
                };
                uint32_t v = 0;
                if (!hex4(i + 2, v)) return false;
                i += 6;
                if (v >= 0xDC00 && v <= 0xDFFF) return false;
                if (v >= 0xD800 && v <= 0xDBFF) {
                    uint32_t lo = 0;
                    if (i + 1 >= s.size() || s[i] != '\\' || s[i + 1] != 'u' || !hex4(i + 2, lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                    i += 6;
                }
            } else if (std::strchr("\"\\/bfnrt", e) != nullptr && e != '\0') {
                i += 2;
            } else {
                return false;
            }
        } else if (c < 0x80) {
            ++i;
        } else if (!recognizer_utf8(s, i)) {
            return false;
        }
    }
    return false;
}

bool recognizer_number(const std::string& s, size_t& i) {
    const size_t start = i;
    const auto digit = [&](size_t at) { return at < s.size() && s[at] >= '0' && s[at] <= '9'; };
    if (s[i] == '-') ++i;
    if (!digit(i)) return false;
    if (s[i] == '0') {
        ++i;
        if (digit(i)) return false;
    } else {
        while (digit(i)) ++i;
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        if (!digit(i)) return false;
        while (digit(i)) ++i;
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        if (!digit(i)) return false;
        while (digit(i)) ++i;
    }
    return i - start <= 128;
}

bool recognizes(const std::string& s, size_t max_depth = 16) {
    enum class St { Value, ValueOrEnd, KeyOrEnd, Key, Colon, After };
    std::vector<char> stack;
    St st = St::Value;
    size_t i = 0;
    const auto ws = [&]() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    };
    if (s.size() > 64 * 1024) return false;
    for (;;) {
        ws();
        if (i >= s.size()) return st == St::After && stack.empty();
        const char c = s[i];
        switch (st) {
            case St::Value:
            case St::ValueOrEnd:
                if (st == St::ValueOrEnd && c == ']') {
                    stack.pop_back();
                    ++i;
                    st = St::After;
                } else if (c == '[' || c == '{') {
                    if (stack.size() >= max_depth) return false;
                    stack.push_back(c);
                    ++i;
                    st = c == '[' ? St::ValueOrEnd : St::KeyOrEnd;
                } else if (c == '"') {
                    if (!recognizer_string(s, i)) return false;
                    st = St::After;
                } else if (c == '-' || (c >= '0' && c <= '9')) {
                    if (!recognizer_number(s, i)) return false;
                    st = St::After;
                } else {
                    const char* word = c == 't' ? "true" : c == 'f' ? "false" : c == 'n' ? "null" : nullptr;
                    if (word == nullptr || s.compare(i, std::strlen(word), word) != 0) return false;
                    i += std::strlen(word);
                    st = St::After;
                }
                break;
            case St::KeyOrEnd:
                if (c == '}') {
                    stack.pop_back();
                    ++i;
                    st = St::After;
                    break;
                }
                [[fallthrough]];
            case St::Key:
                if (c != '"' || !recognizer_string(s, i)) return false;
                st = St::Colon;
                break;
            case St::Colon:
                if (c != ':') return false;
                ++i;
                st = St::Value;
                break;
            case St::After:
                if (stack.empty()) return false;                       // data after the top-level value
                if (c == ',') {
                    ++i;
                    st = stack.back() == '[' ? St::Value : St::Key;
                } else if ((c == ']' && stack.back() == '[') || (c == '}' && stack.back() == '{')) {
                    stack.pop_back();
                    ++i;
                } else {
                    return false;
                }
                break;
        }
    }
}

// ---- random JSON values ----

std::string random_string(Rng& r) {
    std::string s;
    const uint32_t n = r.below(12);
    for (uint32_t i = 0; i < n; ++i) {
        switch (r.below(8)) {
            case 0: s.push_back(static_cast<char>(r.below(0x20))); break;                 // a control character (NUL included)
            case 1: s.push_back("\"\\/\b\f\n\r\t"[r.below(8)]); break;                    // what the escapes are for (the 8th is the terminator: NUL)
            case 2: s.push_back(static_cast<char>(0x7F)); break;
            case 3: {                                                                      // a two-byte character
                const uint32_t cp = 0x80 + r.below(0x780);
                s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                break;
            }
            case 4: {                                                                      // three bytes, never a surrogate
                uint32_t cp = 0x800 + r.below(0xF800);
                if (cp >= 0xD800 && cp <= 0xDFFF) cp += 0x800;
                s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                break;
            }
            case 5: {                                                                      // four bytes
                const uint32_t cp = 0x10000 + r.below(0x100000);
                s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                break;
            }
            default: s.push_back(static_cast<char>('a' + r.below(26))); break;
        }
    }
    return s;
}

double random_double(Rng& r) {
    switch (r.below(6)) {
        case 0: return 0.0;
        case 1: return -0.0;
        case 2: return static_cast<double>(static_cast<int32_t>(r.next())) / 1000.0;      // a "nice" decimal
        case 3: {
            const double specials[] = {0.1, 0.5, 1.0, 100.0, 1e21, 1e-7, 123456789012345680000.0, 5e-324, 1.7976931348623157e308, -2.2250738585072014e-308,
                                       9007199254740993.0, 0.30000000000000004};
            return specials[r.below(sizeof(specials) / sizeof(specials[0]))];
        }
        default:
            for (;;) {                                                                    // any bit pattern that is a finite number
                const uint64_t bits = r.next();
                double d;
                std::memcpy(&d, &bits, sizeof(d));
                if (std::isfinite(d)) return d;
            }
    }
}

JsonValue random_value(Rng& r, int depth) {
    const uint32_t kind = r.below(depth >= 5 ? 5 : 7);
    switch (kind) {
        case 0: return JsonValue::make_null();
        case 1: return JsonValue::make_bool(r.chance(50));
        case 2: {
            switch (r.below(4)) {
                case 0: return JsonValue::make_int(std::numeric_limits<int64_t>::min());
                case 1: return JsonValue::make_int(std::numeric_limits<int64_t>::max());
                case 2: return JsonValue::make_int(static_cast<int64_t>(r.below(2000)) - 1000);
                default: return JsonValue::make_int(static_cast<int64_t>(r.next()));
            }
        }
        case 3: return JsonValue::make_double(random_double(r));
        case 4: return JsonValue::make_string(random_string(r));
        case 5: {
            JsonValue a = JsonValue::make_array();
            const uint32_t n = r.below(6);
            for (uint32_t i = 0; i < n; ++i) a.push_back(random_value(r, depth + 1));
            return a;
        }
        default: {
            JsonValue o = JsonValue::make_object();
            const uint32_t n = r.below(6);
            for (uint32_t i = 0; i < n; ++i) o.set(random_string(r), random_value(r, depth + 1));   // set() keeps the keys distinct
            return o;
        }
    }
}

// ---- sockets: a raw client for the HTTP tests ----

#ifdef _WIN32
struct WsaGuard {
    WsaGuard() {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    }
    ~WsaGuard() { WSACleanup(); }
};
void sock_close(sock_t s) { closesocket(s); }
bool sock_would_block() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
void sock_close(sock_t s) { ::close(s); }
bool sock_would_block() { return errno == EWOULDBLOCK || errno == EAGAIN; }
#endif

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

bool sock_nonblocking(sock_t s) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

class Client {
public:
    Client() = default;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&& o) noexcept : fd_(o.fd_), receive_buffer_(o.receive_buffer_) { o.fd_ = kBadSock; }
    ~Client() { close(); }

    // Connects to 127.0.0.1:port (the connect itself is blocking: on the loopback interface it completes in the kernel at once)
    bool connect_to(uint16_t port, const char* address = "127.0.0.1") {
        close();
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ == kBadSock) return false;
        if (receive_buffer_ > 0) {                                                       // before the connect: the window is agreed in the handshake
            setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&receive_buffer_), sizeof(receive_buffer_));
        }
        sockaddr_in a;
        std::memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        inet_pton(AF_INET, address, &a.sin_addr);
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
            close();
            return false;
        }
#ifdef SO_NOSIGPIPE
        int one = 1;
        setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&one), sizeof(one));
#endif
        return sock_nonblocking(fd_);
    }

    // Sends everything (the socket is non-blocking: it waits a little when the buffer is full). The count of bytes that went out.
    size_t send_all(const std::string& data) {
        size_t sent = 0;
        int stalls = 0;
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif
        while (sent < data.size() && fd_ != kBadSock && stalls < 2000) {
            const auto n = ::send(fd_, data.data() + sent, static_cast<iolen_t>(data.size() - sent), flags);
            if (n > 0) {
                sent += static_cast<size_t>(n);
                stalls = 0;
            } else if (n < 0 && sock_would_block()) {
                ++stalls;
                sleep_ms(1);
            } else {
                break;
            }
        }
        return sent;
    }

    // Appends what has arrived: > 0 bytes read, 0 the server closed in an orderly way, -1 nothing yet, -2 reset or failed
    int recv_some(std::string& out) {
        if (fd_ == kBadSock) return -2;
        char buf[8192];
        const auto n = ::recv(fd_, buf, static_cast<iolen_t>(sizeof(buf)), 0);
        if (n > 0) {
            out.append(buf, static_cast<size_t>(n));
            return 1;
        }
        if (n == 0) return 0;
        return sock_would_block() ? -1 : -2;
    }

    void shutdown_write() {
        if (fd_ != kBadSock) ::shutdown(fd_, 1);
    }

    // Closes with a reset instead of an orderly FIN
    void abort_connection() {
        if (fd_ == kBadSock) return;
        linger l;
        l.l_onoff = 1;
        l.l_linger = 0;
        setsockopt(fd_, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&l), sizeof(l));
        close();
    }

    void close() {
        if (fd_ != kBadSock) {
            sock_close(fd_);
            fd_ = kBadSock;
        }
    }
    bool valid() const { return fd_ != kBadSock; }
    // A kernel receive buffer of `bytes` from the next connect_to on (a client that never reads then fills it at once; 0 keeps the system's)
    void set_receive_buffer(int bytes) { receive_buffer_ = bytes; }

private:
    sock_t fd_{kBadSock};
    int receive_buffer_{0};
};

struct Reply {
    bool complete{false};          // the connection ended in an orderly way (end of stream after the answer)
    bool reset{false};             // the connection was reset or failed
    int status{0};
    std::string reason;
    std::map<std::string, std::string> headers;   // lower case names
    std::string body;
    std::string raw;
    int responses{0};              // how many "HTTP/1.1" status lines the stream held

    const std::string* header(const std::string& name) const {
        const auto it = headers.find(name);
        return it == headers.end() ? nullptr : &it->second;
    }
};

Reply parse_reply(const std::string& raw) {
    Reply r;
    r.raw = raw;
    for (size_t at = raw.find("HTTP/1.1 "); at != std::string::npos; at = raw.find("HTTP/1.1 ", at + 1)) ++r.responses;
    const size_t head_end = raw.find("\r\n\r\n");
    if (raw.compare(0, 9, "HTTP/1.1 ") != 0 || head_end == std::string::npos) return r;
    size_t line_end = raw.find("\r\n");
    r.status = std::atoi(raw.c_str() + 9);
    r.reason = raw.substr(13, line_end - 13);
    size_t pos = line_end + 2;
    while (pos < head_end + 2) {
        const size_t eol = raw.find("\r\n", pos);
        const std::string line = raw.substr(pos, eol - pos);
        pos = eol + 2;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        size_t v = colon + 1;
        while (v < line.size() && line[v] == ' ') ++v;
        r.headers[name] = line.substr(v);
    }
    r.body = raw.substr(head_end + 4);
    return r;
}

// A server, a handler that records what it is asked, and a clock that only the test moves
struct Rig {
    static constexpr const char* kSecret = "test-secret-0123456789";

    std::unique_ptr<HttpServer> server;
    uint32_t now{1000};
    int calls{0};
    std::vector<HttpRequest> seen;
    std::function<HttpResponse(const HttpRequest&)> handler;

    explicit Rig(bool public_door = false) {
        server = public_door ? HttpServer::listen_public(0, true) : HttpServer::listen(0, kSecret, true);
        handler = [this](const HttpRequest& r) {
            ++calls;
            seen.push_back(r);
            HttpResponse resp;
            resp.body = "{\"handled\":true}";
            return resp;
        };
    }
    bool ok() const { return server != nullptr; }
    uint16_t port() const { return server->port(); }
    void pump() { server->update(now, handler); }
    void pump_n(int n) {
        for (int i = 0; i < n; ++i) {
            pump();
            sleep_ms(1);
        }
    }
    bool connect(Client& c) { return c.connect_to(port()); }

    // The kernel hands a connect() to accept(), and a close() to recv(), a moment after the call returns (usually at once, but not on a loaded
    // machine), so a test that waits for such an event pumps until it has happened instead of pumping a fixed number of times.
    // Pumps until the server holds exactly `n` connections; false after ~2 s of wall time.
    bool wait_count(size_t n, int max_ms = 2000) {
        for (int i = 0; i < max_ms; ++i) {
            pump();
            if (server->connection_count() == n) return true;
            sleep_ms(1);
        }
        return false;
    }
    // Pumps until the handler has been called `n` times in all; false after ~2 s.
    bool wait_calls(int n, int max_ms = 2000) {
        for (int i = 0; i < max_ms; ++i) {
            pump();
            if (calls >= n) return true;
            sleep_ms(1);
        }
        return false;
    }
    // Connects when the server holds no other connection, and returns when it has accepted this one: so the clock value that the test has set
    // is the time of the accept (the tests of the 5 second timeout move the clock right after connecting).
    bool connect_accepted(Client& c) {
        return wait_count(0) && connect(c) && wait_count(1);
    }

    // Pumps the server and reads from `c` until the server has ended the connection (or ~2 s of wall time have passed)
    Reply read_reply(Client& c, int max_ms = 2000) {
        std::string raw;
        Reply r;
        for (int i = 0; i < max_ms; ++i) {
            pump();
            int rc;
            while ((rc = c.recv_some(raw)) > 0) {}
            if (rc == 0) {
                r.complete = true;
                break;
            }
            if (rc == -2) {
                r.reset = true;
                break;
            }
            sleep_ms(1);
        }
        const bool complete = r.complete;
        const bool reset = r.reset;
        r = parse_reply(raw);
        r.complete = complete;
        r.reset = reset;
        return r;
    }

    // One whole exchange on a fresh connection
    Reply exchange(const std::string& request) {
        Client c;
        Reply none;
        if (!connect(c)) return none;
        c.send_all(request);
        return read_reply(c);
    }

    // Pumps for `ms` of wall time (the virtual clock stays) and returns what `c` received meanwhile
    std::string quiet_for(Client& c, int ms, int* last_rc = nullptr) {
        std::string raw;
        int rc = -1;
        for (int i = 0; i < ms; ++i) {
            pump();
            int r;
            while ((r = c.recv_some(raw)) > 0) {}
            rc = r;
            if (r != -1) break;
            sleep_ms(1);
        }
        if (last_rc != nullptr) *last_rc = rc;
        return raw;
    }
};

std::string auth_line() { return std::string("Authorization: Bearer ") + Rig::kSecret + "\r\n"; }

// A request with the secret (unless `with_secret` is false); the body gets its Content-Length
std::string make_request(const std::string& method, const std::string& target, const std::string& body = "", const std::string& extra = "",
                         bool with_secret = true, bool with_length = true) {
    std::string s = method + " " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
    if (with_secret) s += auth_line();
    s += extra;
    if (with_length && (!body.empty() || method == "POST")) s += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    s += "\r\n";
    s += body;
    return s;
}

std::string body_pattern(size_t n) {
    std::string s(n, 'x');
    for (size_t i = 0; i < n; ++i) s[i] = static_cast<char>('a' + (i * 7 + i / 251) % 26);
    return s;
}

// The address of a network interface of this machine other than the loopback (empty when there is none or the platform does not tell)
std::string other_local_address() {
#ifndef _WIN32
    ifaddrs* list = nullptr;
    std::string found;
    if (getifaddrs(&list) != 0) return found;
    for (const ifaddrs* a = list; a != nullptr && found.empty(); a = a->ifa_next) {
        if (a->ifa_addr == nullptr || a->ifa_addr->sa_family != AF_INET) continue;
        const auto* in = reinterpret_cast<const sockaddr_in*>(a->ifa_addr);
        const uint32_t host = ntohl(in->sin_addr.s_addr);
        if ((host >> 24) == 127u || (host >> 16) == 0xA9FEu) continue;      // loopback, link-local
        char buf[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf));
        found = buf;
    }
    freeifaddrs(list);
    return found;
#else
    return std::string();
#endif
}

}  // namespace

int main() {
#ifdef _WIN32
    WsaGuard wsa;
#endif
    std::cout << "=======================================================\n ANTS CONTROL INTERFACE TESTS (JSON, HTTP)\n"
              << "=======================================================\n";

    // ============================================================================================
    // JSON: documents that must parse
    // ============================================================================================

    TEST_CASE("CTL1.1 JSON: valid documents parse, serialize and parse again to an equal value") {
        const std::vector<std::string> valid = {
            "null", "true", "false", "0", "-0", "1", "-1", "123456789", "1.5", "-1.5e3", "1E+2", "1e-2", "0.0", "0e0", "0E0", "0.1e1", "1e5",
            "\"\"", "\"a\"", "[]", "{}", " [ ] ", "\t\r\n[1 , 2]\n", "[null,true,false,0,\"x\",[],{}]", "{\"a\":1,\"b\":[1,2,{\"c\":null}]}",
            "{\"a\":1,\"A\":2}", "{\"\":1}", "{\"a\":{\"a\":1}}", "[[],[]]", "[{},{}]", "[[[[]]]]", "{\"a\":[{\"a\":[]}]}",
            "\"\\u0041\"", "\"\\ud83d\\ude00\"", "\"\\uD83D\\uDE00\"", "\"\\u0000\"", "\"\\/\"", "\"\\b\\f\\n\\r\\t\\\"\\\\\"", "\"\\u00e9\\u00E9\"",
            "\"\\uffff\"", "\"\\ue000\"", "\"\\ud7ff\"", "\"\\udbff\\udfff\"", "\"\\ud800\\udc00\"", "[\"\\u0022\"]", "{\"\\u0061\":1}",
            "1e308", "-1e308", "5e-324", "1e-400", "-1e-400", "2.2250738585072014e-308", "1.7976931348623157e308", "0.000000000000000000000000000001",
            "9223372036854775807", "-9223372036854775808", "9223372036854775808", "-9223372036854775809", "18446744073709551615",
            "123456789012345678901234567890", "[1,2,3,4,5,6,7,8,9,10]", "{\"k1\":1,\"k2\":2,\"k3\":3}",
            lit("\"\x7f\""), lit("\"\xC2\x80\""), lit("\"\xDF\xBF\""), lit("\"\xE0\xA0\x80\""), lit("\"\xED\x9F\xBF\""), lit("\"\xEE\x80\x80\""),
            lit("\"\xEF\xBF\xBD\""), lit("\"\xEF\xBF\xBF\""), lit("\"\xF0\x90\x80\x80\""), lit("\"\xF4\x8F\xBF\xBF\""), lit("\"\xF0\x9F\x98\x80\""),
            lit("\"caf\xC3\xA9\""), lit("{\"\xE2\x82\xAC\":\"\xE2\x82\xAC\"}"), lit("\"\xEF\xBB\xBF\""),                    // a BOM inside a string is just text
        };
        for (const std::string& text : valid) {
            JsonValue v;
            std::string error;
            ASSERT_MSG(parse_json(text, v, &error), "should parse: " + shown(text) + "  (" + error + ")");
            ASSERT_MSG(error.empty(), "an empty error on success: " + shown(text));
            const std::string out = to_json(v);
            JsonValue again;
            ASSERT_MSG(parse_json(out, again, &error), "serialized form should parse: " + shown(out));
            ASSERT_MSG(again == v, "round trip changed the value: " + shown(text) + " -> " + shown(out));
            ASSERT_MSG(to_json(again) == out, "serialization is not stable: " + shown(out));
            ASSERT_MSG(recognizes(text), "the independent recognizer disagrees: " + shown(text));
        }
    } TEST_END();

    TEST_CASE("CTL1.2 JSON: documents that must be refused (JSONTestSuite style)") {
        const std::vector<std::string> invalid = {
            // empty and broken literals
            "", " ", "\n", "nul", "nulL", "NULL", "Null", "True", "FALSE", "tru", "fals", "truee", "nullx", "null null", "n", "t", "f",
            // structure
            "[", "]", "{", "}", "[1,]", "[,1]", "[,]", "[1 2]", "[1,,2]", "[1}", "{]", "{\"a\":1,}", "{,}", "{\"a\"}", "{\"a\":}", "{\"a\" 1}", "{\"a\":1 \"b\":2}",
            "{a:1}", "{'a':1}", "['a']", "{1:2}", "{null:1}", "{\"a\":1,,\"b\":2}", "{\"a\"::1}", "[\"a\":1]", "{\"a\",1}", "[1,2", "{\"a\":[1,2}", "[[]", "[]]", "{}}",
            "[1][2]", "{} {}", "1 2", "[] x", "\"a\" \"b\"", "[\"a\" \"b\"]", "{\"a\":1}}", "[1,2,3]]", "{\"a\":1}x",
            // numbers
            "01", "-01", "00", "-00", "1.", "-1.", ".5", "-.5", "1.e2", "1e", "1e+", "1e-", "1E", "+1", "+0", "- 1", "--1", "-", "0x10", "1_000", "Infinity", "-Infinity", "NaN",
            "-NaN", "1e1.5", "0.1.2", "1..2", "1e2e3", "1.5.", "1,5", "0e", "0.e1", "1 e5", "1e 5", "\xD9\xA1\xD9\xA2\xD9\xA3", "1\xCE\xBC", "0b1", "1f", "1d", "1L", "inf", "nan", "1e999", "-1e999",
            "[1e999]", "{\"a\":1e400}", "[01]", "[1.]", "[.5]", "[-]", "[+1]",
            // strings
            "\"abc", "\"", "\"a\nb\"", "\"a\tb\"", "\"a\rb\"", lit("\"\x01\""), lit("\"\x1f\""), lit("\"a\0b\""), "\"\\x41\"", "\"\\a\"", "\"\\'\"", "\"\\", "\"\\\"",
            "\"\\u\"", "\"\\u1\"", "\"\\u12\"", "\"\\u123\"", "\"\\u12G4\"", "\"\\u+123\"", "\"\\u-123\"", "\"\\u 123\"", "\"\\U0041\"", "\"\\0\"", "\"\\1\"", "\"\\ \"", "\"\\/\\\"",
            "'abc'", "abc", "\"a\"b\"",
            // surrogates
            "\"\\ud800\"", "\"\\udbff\"", "\"\\udc00\"", "\"\\udfff\"", "\"\\ud800\\u0041\"", "\"\\ud800\\ud800\"", "\"\\ud800\\udbff\"", "\"\\udc00\\ud800\"", "\"\\udc00\\udc00\"",
            "\"\\ud800x\"", "\"\\ud800\\\"", "\"\\ud800\\n\"", "\"\\ud800\\u\"", "\"\\ud800\\u00\"", "\"\\ud83d\\ude0\"", "\"\\ud83d \\ude00\"", "[\"\\ud800\"]", "{\"\\udc00\":1}",
            // UTF-8
            lit("\"\x80\""), lit("\"\xBF\""), lit("\"\xC0\x80\""), lit("\"\xC1\xBF\""), lit("\"\xC2\""), lit("\"\xC2\x20\""), lit("\"\xC2\xC2\""), lit("\"\xE0\x80\x80\""),
            lit("\"\xE0\x9F\xBF\""), lit("\"\xE2\x82\""), lit("\"\xE2\x82\x20\""), lit("\"\xED\xA0\x80\""), lit("\"\xED\xBF\xBF\""), lit("\"\xED\xAF\xBF\xED\xB0\x80\""),
            lit("\"\xF0\x80\x80\x80\""), lit("\"\xF0\x8F\xBF\xBF\""), lit("\"\xF0\x9F\x98\""), lit("\"\xF4\x90\x80\x80\""), lit("\"\xF5\x80\x80\x80\""), lit("\"\xF7\xBF\xBF\xBF\""),
            lit("\"\xF8\x88\x80\x80\x80\""), lit("\"\xFC\x84\x80\x80\x80\x80\""), lit("\"\xFE\""), lit("\"\xFF\""), lit("{\"\xFF\":1}"), lit("{\"\xC0\xAF\":1}"),
            lit("\"\xE2\x28\xA1\""), lit("\"\xF0\x28\x8C\xBC\""),
            // outside a string: UTF-8 is not white space or a value
            lit("\xEF\xBB\xBF[]"), lit("\xEF\xBB\xBF" "1"), lit("\xC2\xA0[]"), lit("[]\xC2\xA0"), lit("\xE2\x80\xA8[]"), lit("[\xE2\x80\x83" "1]"), lit("\xFF"), lit("\x80"),
            // control characters and other white space
            lit("[1]\0"), lit("\0"), lit("[\0]"), lit("[1,\0" "2]"), lit("\x01"), "\f[]", "\v[]", lit("\x0b" "1"), lit("[1]\x1a"), lit("\x7f"), "\x85[]",
            // comments and extensions
            "// c\n1", "/* c */ 1", "[1,/*c*/2]", "# comment", "[1] // x", "[1] /* x */", "{\"a\":1 /* c */}", "[1,\n// c\n2]", "{\"a\":1}//", "[0x1]", "[1,2,]", "{\"a\":+1}", "[undefined]", "[NaN]",
            "[Infinity]", "[-Infinity]", "[.1]", "['a']", "{\"a\":'b'}", "[`a`]", "[a]", "[\"a\",]", "{\"a\":\"b\",}", "(1)", "<1>", "1;", "1,", ",1",
            // repeated keys
            "{\"a\":1,\"a\":2}", "{\"a\":1,\"a\":1}", "{\"a\":1,\"b\":2,\"a\":3}", "{\"a\":1,\"\\u0061\":2}", "{\"\\u0061\":1,\"a\":2}", lit("{\"\\u0000\":1,\"\\u0000\":2}"),
            "{\"a\":{\"x\":1,\"x\":2}}", "[{\"a\":1,\"a\":1}]", "{\"a\":[{\"b\":1,\"b\":2}]}", "{\"\":1,\"\":2}", lit("{\"\xC3\xA9\":1,\"\\u00e9\":2}"), "{\"a\\n\":1,\"a\\n\":2}",
            "{\"k1\":1,\"k2\":2,\"k3\":3,\"k4\":4,\"k5\":5,\"k6\":6,\"k7\":7,\"k8\":8,\"k9\":9,\"k10\":10,\"k11\":11,\"k12\":12,\"k13\":13,\"k14\":14,\"k15\":15,\"k16\":16,\"k17\":17,\"k5\":0}",
            "{\"k1\":1,\"k2\":2,\"k3\":3,\"k4\":4,\"k5\":5,\"k6\":6,\"k7\":7,\"k8\":8,\"k9\":9,\"k10\":10,\"k11\":11,\"k12\":12,\"k13\":13,\"k14\":14,\"k15\":15,\"k16\":16,\"k16\":0}",
        };
        for (const std::string& text : invalid) {
            JsonValue v = JsonValue::make_bool(true);
            std::string error;
            ASSERT_MSG(!parse_json(text, v, &error), "should be refused: " + shown(text));
            ASSERT_MSG(v.is_null(), "out is left null after a refusal: " + shown(text));
            ASSERT_MSG(!error.empty() && error.find("byte") != std::string::npos, "an error text with a position for: " + shown(text) + "  (" + error + ")");
            ASSERT_FALSE(parse_json(text, v, nullptr));                       // without an error pointer
        }
    } TEST_END();

    TEST_CASE("CTL1.3 JSON: error messages say what was wrong") {
        ASSERT_TRUE(error_of("{\"a\":1,\"a\":2}").find("duplicate key") != std::string::npos);
        ASSERT_TRUE(error_of("[1,]").find("unexpected character") != std::string::npos);
        ASSERT_TRUE(error_of("01").find("leading zero") != std::string::npos);
        ASSERT_TRUE(error_of("\"\\ud800\"").find("surrogate") != std::string::npos);
        ASSERT_TRUE(error_of(lit("\"\xFF\"")).find("UTF-8") != std::string::npos);
        ASSERT_TRUE(error_of("\"a\nb\"").find("control character") != std::string::npos);
        ASSERT_TRUE(error_of("[1] 2").find("after the value") != std::string::npos);
        ASSERT_TRUE(error_of("1e999").find("out of range") != std::string::npos);
        ASSERT_TRUE(error_of("").find("empty") != std::string::npos);
        ASSERT_TRUE(error_of("{\"a\":1,\"a\":2}").find("at byte 7") != std::string::npos);       // the position of the second "a"
        ASSERT_TRUE(error_of("[1,x]").find("at byte 3") != std::string::npos);
    } TEST_END();

    // ============================================================================================
    // JSON: limits
    // ============================================================================================

    TEST_CASE("CTL1.4 JSON limits: depth 16 passes, 17 does not (arrays, objects, mixed), and a hostile depth cannot overflow the stack") {
        ASSERT_TRUE(parses(nested_arrays(16)));
        ASSERT_FALSE(parses(nested_arrays(17)));
        ASSERT_TRUE(parses(nested_objects(16)));
        ASSERT_FALSE(parses(nested_objects(17)));
        ASSERT_TRUE(parses("[[[[[[[[[[[[[[[{\"a\":1}]]]]]]]]]]]]]]]"));         // 15 arrays and an object: 16
        ASSERT_FALSE(parses("[[[[[[[[[[[[[[[[{\"a\":1}]]]]]]]]]]]]]]]]"));      // 16 and an object: 17
        ASSERT_TRUE(error_of(nested_arrays(17)).find("too deep") != std::string::npos);
        ASSERT_TRUE(parses("1"));                                                // a scalar is depth 0
        ASSERT_TRUE(parses("[1]"));
        // a document of the largest size made of nothing but opening brackets
        ASSERT_FALSE(parses(std::string(64 * 1024, '[')));
        ASSERT_FALSE(parses(std::string(64 * 1024, '{')));
        ASSERT_FALSE(parses("{\"a\":" + std::string(60000, '[')));
        // JsonLimits can lower or raise the depth, but never above 128
        JsonLimits shallow;
        shallow.max_depth = 2;
        ASSERT_TRUE(error_of("[[1]]", shallow).empty());
        ASSERT_FALSE(error_of("[[[1]]]", shallow).empty());
        JsonLimits zero;
        zero.max_depth = 0;
        ASSERT_TRUE(error_of("1", zero).empty());
        ASSERT_FALSE(error_of("[]", zero).empty());
        JsonLimits deep;
        deep.max_depth = 100000;
        ASSERT_TRUE(error_of(nested_arrays(128), deep).empty());
        ASSERT_FALSE(error_of(nested_arrays(129), deep).empty());
        deep.max_bytes = 10 * 1024 * 1024;
        ASSERT_FALSE(error_of(std::string(1000000, '['), deep).empty());
    } TEST_END();

    TEST_CASE("CTL1.5 JSON limits: 64 KB for the document") {
        const std::string head = "\"";
        std::string exact = head + std::string(64 * 1024 - 2, 'a') + "\"";
        ASSERT_EQ(exact.size(), 64u * 1024u);
        ASSERT_TRUE(parses(exact));
        std::string over = head + std::string(64 * 1024 - 1, 'a') + "\"";
        ASSERT_FALSE(parses(over));
        ASSERT_TRUE(error_of(over).find("too large") != std::string::npos);
        // white space counts too
        ASSERT_TRUE(parses("1" + std::string(64 * 1024 - 1, ' ')));
        ASSERT_FALSE(parses("1" + std::string(64 * 1024, ' ')));
        JsonLimits tiny;
        tiny.max_bytes = 8;
        ASSERT_TRUE(error_of("[1,2,3]", tiny).empty());
        ASSERT_FALSE(error_of("[1,2,3,4]", tiny).empty());
        JsonValue v;
        ASSERT_FALSE(parse_json(over, v, nullptr));
        ASSERT_TRUE(v.is_null());
    } TEST_END();

    TEST_CASE("CTL1.6 JSON limits: 4096 elements per container, 1024 bytes per key, 128 characters per number") {
        const auto array_of = [](size_t n) {
            std::string s = "[";
            for (size_t i = 0; i < n; ++i) s += (i ? ",1" : "1");
            return s + "]";
        };
        const auto object_of = [](size_t n) {
            std::string s = "{";
            for (size_t i = 0; i < n; ++i) s += (i ? ",\"k" : "\"k") + std::to_string(i) + "\":1";
            return s + "}";
        };
        ASSERT_TRUE(parses(array_of(4096)));
        ASSERT_FALSE(parses(array_of(4097)));
        ASSERT_TRUE(parses(object_of(4096)));
        ASSERT_FALSE(parses(object_of(4097)));
        ASSERT_TRUE(error_of(array_of(4097)).find("too many") != std::string::npos);
        JsonValue big = parse_ok(object_of(4096));
        ASSERT_EQ(big.size(), 4096u);
        ASSERT_EQ(big.get("k4095").as_int_or(0), 1);
        ASSERT_EQ(big.key_at(4095), "k4095");
        // 4096 elements in each of several nested containers is fine as long as every container is within its own limit
        ASSERT_TRUE(parses("[" + array_of(4096) + "," + array_of(4096) + "]"));
        JsonLimits few;
        few.max_elements = 2;
        ASSERT_TRUE(error_of("[1,2]", few).empty());
        ASSERT_FALSE(error_of("[1,2,3]", few).empty());
        ASSERT_TRUE(error_of("{\"a\":1,\"b\":2}", few).empty());
        ASSERT_FALSE(error_of("{\"a\":1,\"b\":2,\"c\":3}", few).empty());
        JsonLimits none;
        none.max_elements = 0;
        ASSERT_TRUE(error_of("[]", none).empty());
        ASSERT_TRUE(error_of("{}", none).empty());
        ASSERT_FALSE(error_of("[1]", none).empty());

        ASSERT_TRUE(parses("{\"" + std::string(1024, 'k') + "\":1}"));
        ASSERT_FALSE(parses("{\"" + std::string(1025, 'k') + "\":1}"));
        ASSERT_TRUE(error_of("{\"" + std::string(1025, 'k') + "\":1}").find("key too long") != std::string::npos);
        std::string accents;
        for (int i = 0; i < 512; ++i) accents += "\\u00e9";                       // 512 characters of two bytes: 1024 bytes after unescaping
        ASSERT_TRUE(parses("{\"" + accents + "\":1}"));
        ASSERT_FALSE(parses("{\"" + accents + "\\u00e9\":1}"));
        ASSERT_TRUE(parses("[\"" + std::string(5000, 'v') + "\"]"));              // a value may be longer than a key
        JsonLimits short_keys;
        short_keys.max_key_chars = 3;
        ASSERT_TRUE(error_of("{\"abc\":1}", short_keys).empty());
        ASSERT_FALSE(error_of("{\"abcd\":1}", short_keys).empty());

        ASSERT_TRUE(parses("0." + std::string(125, '0') + "1"));                  // 128 characters
        ASSERT_FALSE(parses("0." + std::string(126, '0') + "1"));                 // 129
        ASSERT_TRUE(error_of(std::string(200, '9')).find("too long") != std::string::npos);
        ASSERT_FALSE(parses(std::string(64 * 1024, '1')));
        ASSERT_FALSE(parses("1e" + std::string(5000, '9')));
    } TEST_END();

    TEST_CASE("CTL1.7 JSON limits: nothing a hostile document can do takes long or crashes") {
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<std::string> nasty;
        nasty.push_back(std::string(64 * 1024, '['));
        nasty.push_back(std::string(64 * 1024, '{'));
        nasty.push_back("[" + std::string(64 * 1024 - 2, ','));
        nasty.push_back("\"" + std::string(64 * 1024 - 2, '\\') + "\"");
        nasty.push_back("\"" + std::string(32000, '\\') + "u\"");
        nasty.push_back(std::string(64 * 1024, '1'));
        nasty.push_back("[" + std::string(30000, '1') + ",1]");
        nasty.push_back("0." + std::string(64 * 1024 - 3, '9'));
        nasty.push_back("-" + std::string(64 * 1024 - 2, '0'));
        nasty.push_back("{" + std::string(64 * 1024 - 2, '"') + "}");
        {
            std::string s = "{";                                                       // 4096 members, the last one repeats the first: the hash set is used
            for (int i = 0; i < 4096; ++i) s += "\"key" + std::to_string(i) + "\":0,";
            nasty.push_back(s + "\"key0\":0}");
        }
        {
            std::string s = "[";
            for (int i = 0; i < 6000; ++i) s += "[],";                                  // 6000 elements: over the limit after 4096
            nasty.push_back(s + "[]]");
        }
        {
            std::string s;                                                              // many short strings with every kind of escape
            s += "[";
            for (int i = 0; i < 8000; ++i) s += "\"\\u00e9\\ud83d\\ude00\",";
            nasty.push_back(s + "1]");
        }
        for (int round = 0; round < 5; ++round) {
            for (const std::string& n : nasty) {
                JsonValue v;
                std::string e;
                parse_json(n, v, &e);
            }
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        ASSERT_MSG(ms < 10000, "slow: " + std::to_string(ms) + " ms");
    } TEST_END();

    // ============================================================================================
    // JSON: kinds of numbers, accessors
    // ============================================================================================

    TEST_CASE("CTL1.8 JSON numbers keep their kind: integers (int64) and doubles are apart") {
        JsonValue v = parse_ok("1");
        ASSERT_TRUE(v.is_int());
        ASSERT_EQ(v.as_int_or(-1), 1);
        v = parse_ok("1.0");
        ASSERT_TRUE(v.is_double());
        ASSERT_TRUE(v.as_double_or(0) == 1.0);
        ASSERT_EQ(v.as_int_or(-1), -1);                                         // a double is not an integer, even when it is a whole number
        v = parse_ok("1e2");
        ASSERT_TRUE(v.is_double());
        ASSERT_TRUE(v.as_double_or(0) == 100.0);
        v = parse_ok("-0");
        ASSERT_TRUE(v.is_int());
        ASSERT_EQ(v.as_int_or(-1), 0);
        v = parse_ok("-0.0");
        ASSERT_TRUE(v.is_double());
        ASSERT_TRUE(std::signbit(v.as_double_or(1.0)));
        v = parse_ok("9223372036854775807");
        ASSERT_TRUE(v.is_int());
        ASSERT_EQ(v.as_int_or(0), std::numeric_limits<int64_t>::max());
        v = parse_ok("-9223372036854775808");
        ASSERT_TRUE(v.is_int());
        ASSERT_EQ(v.as_int_or(0), std::numeric_limits<int64_t>::min());
        v = parse_ok("9223372036854775808");                                    // one more than int64 holds: a double, so an int lookup falls back
        ASSERT_TRUE(v.is_double());
        ASSERT_TRUE(v.as_double_or(0) == 9223372036854775808.0);
        ASSERT_EQ(v.as_int_or(-7), -7);
        v = parse_ok("-9223372036854775809");
        ASSERT_TRUE(v.is_double());
        v = parse_ok("123456789012345678901234567890");
        ASSERT_TRUE(v.is_double());
        ASSERT_TRUE(v.as_double_or(0) == 123456789012345678901234567890.0);
        v = parse_ok("0000000000000000000000");                                 // leading zeros are an error even here
        ASSERT_TRUE(v.is_string());                                             // (parse_ok returns the failure as a string)
        v = parse_ok("1e-400");
        ASSERT_TRUE(v.is_double());
        ASSERT_TRUE(v.as_double_or(1.0) == 0.0);
        v = parse_ok("5e-324");
        ASSERT_TRUE(v.is_double());
        ASSERT_TRUE(v.as_double_or(0) == 5e-324);
        v = parse_ok("1.7976931348623157e308");
        ASSERT_TRUE(v.as_double_or(0) == 1.7976931348623157e308);
        v = parse_ok("0.1");
        ASSERT_TRUE(v.as_double_or(0) == 0.1);
        v = parse_ok("[1, 1.0, 1e0, -1, 10, 1E1]");
        ASSERT_TRUE(v.at(0).is_int() && v.at(1).is_double() && v.at(2).is_double() && v.at(3).is_int() && v.at(4).is_int() && v.at(5).is_double());
        ASSERT_FALSE(v.at(0) == v.at(1));                                       // 1 and 1.0 are different values
    } TEST_END();

    TEST_CASE("CTL1.9 JSON accessors never throw: wrong kinds give fallbacks, lookups give null") {
        const JsonValue doc = parse_ok("{\"room\":{\"seats\":4,\"name\":\"Anthill\",\"open\":true,\"ratio\":0.5,\"tags\":[\"a\",\"b\"],\"none\":null},\"n\":7}");
        ASSERT_TRUE(doc.is_object());
        ASSERT_EQ(doc.size(), 2u);
        ASSERT_EQ(doc.get("room").get("seats").as_int_or(0), 4);
        ASSERT_EQ(doc.get("room").get("name").as_string_or("x"), "Anthill");
        ASSERT_EQ(doc.get("room").get("name").str(), "Anthill");
        ASSERT_TRUE(doc.get("room").get("open").as_bool_or(false));
        ASSERT_TRUE(doc.get("room").get("ratio").as_double_or(0) == 0.5);
        ASSERT_TRUE(doc.get("n").as_double_or(0) == 7.0);                       // an integer reads as a double
        ASSERT_EQ(doc.get("room").get("tags").size(), 2u);
        ASSERT_EQ(doc.get("room").get("tags").at(1).as_string_or(""), "b");
        // missing members and wrong kinds
        ASSERT_TRUE(doc.get("missing").is_null());
        ASSERT_TRUE(doc.get("missing").get("deeper").get("still").is_null());
        ASSERT_EQ(doc.get("missing").as_int_or(42), 42);
        ASSERT_EQ(doc.get("room").get("name").as_int_or(5), 5);
        ASSERT_EQ(doc.get("room").get("seats").as_string_or("fb"), "fb");
        ASSERT_EQ(doc.get("room").get("seats").as_string_or(), "");
        ASSERT_TRUE(doc.get("room").get("seats").as_bool_or(true));
        ASSERT_FALSE(doc.get("room").get("seats").as_bool_or(false));
        ASSERT_EQ(doc.get("room").get("seats").str(), "");
        ASSERT_TRUE(doc.get("room").get("tags").get("x").is_null());            // a key on an array
        ASSERT_TRUE(doc.get("room").get("seats").at(0).is_null());              // an index on a number
        ASSERT_TRUE(doc.get("room").get("tags").at(2).is_null());               // out of range
        ASSERT_TRUE(doc.get("room").get("tags").at(static_cast<size_t>(-1)).is_null());
        ASSERT_EQ(doc.get("room").get("seats").size(), 0u);
        ASSERT_EQ(doc.key_at(0), "room");
        ASSERT_EQ(doc.key_at(1), "n");
        ASSERT_EQ(doc.key_at(2), "");
        ASSERT_EQ(doc.get("room").get("tags").key_at(0), "");
        // find tells a missing member from a null one
        ASSERT_TRUE(doc.get("room").find("none") != nullptr);
        ASSERT_TRUE(doc.get("room").find("none")->is_null());
        ASSERT_TRUE(doc.get("room").find("nope") == nullptr);
        ASSERT_TRUE(doc.find("seats") == nullptr);                              // only direct members
        ASSERT_TRUE(doc.get("room").has("seats"));
        ASSERT_FALSE(doc.get("room").has("Seats"));                             // keys are case sensitive
        ASSERT_TRUE(parse_ok("[1]").find("a") == nullptr);
        ASSERT_TRUE(JsonValue().find("a") == nullptr);
        ASSERT_TRUE(JsonValue().is_null());
        ASSERT_EQ(JsonValue().as_int_or(9), 9);
        // items() and keys() walk a container in order
        ASSERT_EQ(doc.keys().size(), doc.items().size());
        ASSERT_EQ(doc.keys()[0], "room");
        // the elements of an object keep the insertion order of the document
        const JsonValue ordered = parse_ok("{\"z\":1,\"a\":2,\"m\":3}");
        ASSERT_EQ(ordered.key_at(0), "z");
        ASSERT_EQ(ordered.key_at(1), "a");
        ASSERT_EQ(ordered.key_at(2), "m");
        ASSERT_EQ(to_json(ordered), "{\"z\":1,\"a\":2,\"m\":3}");
    } TEST_END();

    TEST_CASE("CTL1.10 JSON building: push_back and set check the kind, set keeps keys distinct and in place") {
        JsonValue arr = JsonValue::make_array();
        ASSERT_TRUE(arr.push_back(JsonValue::make_int(1)));
        ASSERT_TRUE(arr.push_back(JsonValue::make_string("two")));
        ASSERT_FALSE(arr.set("k", JsonValue::make_null()));
        ASSERT_EQ(to_json(arr), "[1,\"two\"]");
        JsonValue obj = JsonValue::make_object();
        ASSERT_TRUE(obj.set("b", JsonValue::make_int(1)));
        ASSERT_TRUE(obj.set("a", JsonValue::make_int(2)));
        ASSERT_TRUE(obj.set("b", JsonValue::make_int(3)));                      // replaces, keeps its place
        ASSERT_EQ(to_json(obj), "{\"b\":3,\"a\":2}");
        ASSERT_EQ(obj.size(), 2u);
        ASSERT_FALSE(obj.push_back(JsonValue::make_null()));
        ASSERT_EQ(obj.size(), 2u);
        JsonValue scalar = JsonValue::make_int(5);
        ASSERT_FALSE(scalar.push_back(JsonValue::make_int(1)));
        ASSERT_FALSE(scalar.set("a", JsonValue::make_int(1)));
        ASSERT_EQ(to_json(scalar), "5");
        JsonValue* b = obj.find("b");
        ASSERT_TRUE(b != nullptr);
        *b = JsonValue::make_string("changed");
        ASSERT_EQ(to_json(obj), "{\"b\":\"changed\",\"a\":2}");
        // make_*_from
        ASSERT_EQ(to_json(JsonValue::make_array_from({JsonValue::make_bool(true), JsonValue::make_null()})), "[true,null]");
        ASSERT_EQ(to_json(JsonValue::make_object_from({"x", "y", "z"}, {JsonValue::make_int(1), JsonValue::make_int(2)})), "{\"x\":1,\"y\":2}");   // the longer is cut
        ASSERT_EQ(to_json(JsonValue::make_object_from({"x"}, {JsonValue::make_int(1), JsonValue::make_int(2)})), "{\"x\":1}");
        // equality
        ASSERT_TRUE(parse_ok("{\"a\":[1,2.5,\"x\",null,true]}") == parse_ok(" { \"a\" : [ 1 , 2.5 , \"x\" , null , true ] } "));
        ASSERT_FALSE(parse_ok("{\"a\":1,\"b\":2}") == parse_ok("{\"b\":2,\"a\":1}"));         // members compare in order
        ASSERT_FALSE(parse_ok("[1,2]") == parse_ok("[1,2,3]"));
        ASSERT_TRUE(parse_ok("[1,2]") != parse_ok("[2,1]"));
        ASSERT_FALSE(parse_ok("0.0") == parse_ok("-0.0"));
        ASSERT_FALSE(JsonValue::make_string("1") == JsonValue::make_int(1));
        ASSERT_TRUE(JsonValue() == JsonValue::make_null());
    } TEST_END();

    // ============================================================================================
    // JSON: the serializer
    // ============================================================================================

    TEST_CASE("CTL1.11 JSON serializer: compact, deterministic, escapes exactly what must be escaped") {
        ASSERT_EQ(to_json(JsonValue()), "null");
        ASSERT_EQ(to_json(JsonValue::make_bool(true)), "true");
        ASSERT_EQ(to_json(JsonValue::make_bool(false)), "false");
        ASSERT_EQ(to_json(JsonValue::make_int(0)), "0");
        ASSERT_EQ(to_json(JsonValue::make_int(-12)), "-12");
        ASSERT_EQ(to_json(JsonValue::make_int(std::numeric_limits<int64_t>::min())), "-9223372036854775808");
        ASSERT_EQ(to_json(JsonValue::make_int(std::numeric_limits<int64_t>::max())), "9223372036854775807");
        ASSERT_EQ(to_json(JsonValue::make_string("")), "\"\"");
        ASSERT_EQ(to_json(JsonValue::make_string("plain /slash/ and DEL \x7f")), "\"plain /slash/ and DEL \x7f\"");      // / and DEL stay as they are
        ASSERT_EQ(to_json(JsonValue::make_string(lit("q\"b\\n\n r\r t\t bs\b ff\f nul\0 u\x01 us\x1f"))),
                  "\"q\\\"b\\\\n\\n r\\r t\\t bs\\b ff\\f nul\\u0000 u\\u0001 us\\u001f\"");
        ASSERT_EQ(to_json(JsonValue::make_string("caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80")), "\"caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80\"");     // UTF-8 goes out as it is
        ASSERT_EQ(to_json(JsonValue::make_string("\xE2\x80\xA8")), "\"\xE2\x80\xA8\"");                                                        // U+2028 too
        // invalid UTF-8 is replaced, so that what comes out is valid JSON; every bad byte is one U+FFFD
        ASSERT_EQ(to_json(JsonValue::make_string("a\xFF" "b")), "\"a\xEF\xBF\xBD" "b\"");
        ASSERT_EQ(to_json(JsonValue::make_string("\xE2\x82")), "\"\xEF\xBF\xBD\xEF\xBF\xBD\"");
        ASSERT_EQ(to_json(JsonValue::make_string("\xED\xA0\x80")), "\"\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\"");
        ASSERT_EQ(to_json(JsonValue::make_string("\xC0\x80")), "\"\xEF\xBF\xBD\xEF\xBF\xBD\"");
        ASSERT_TRUE(parses(to_json(JsonValue::make_string("\xFF\xFE\x80\xC3"))));
        // containers: no white space, insertion order, empty ones
        ASSERT_EQ(to_json(JsonValue::make_array()), "[]");
        ASSERT_EQ(to_json(JsonValue::make_object()), "{}");
        ASSERT_EQ(to_json(parse_ok(" {\"b\" : [ 1 , { \"c\" : null } ] , \"a\" : \"d\" , \"e\" : { } , \"f\" : [ ] } ")), "{\"b\":[1,{\"c\":null}],\"a\":\"d\",\"e\":{},\"f\":[]}");
        JsonValue keyed = JsonValue::make_object();
        keyed.set("quote\"key\n", JsonValue::make_int(1));
        ASSERT_EQ(to_json(keyed), "{\"quote\\\"key\\n\":1}");                    // keys are escaped like strings
        // the same value always gives the same bytes
        const JsonValue v = parse_ok("{\"a\":[1,2,{\"b\":0.1}],\"c\":\"d\"}");
        ASSERT_EQ(to_json(v), to_json(v));
        ASSERT_EQ(to_json(v), "{\"a\":[1,2,{\"b\":0.1}],\"c\":\"d\"}");
    } TEST_END();

    TEST_CASE("CTL1.12 JSON serializer: doubles parse back to the same bits and stay doubles") {
        ASSERT_EQ(to_json(JsonValue::make_double(0.5)), "0.5");
        ASSERT_EQ(to_json(JsonValue::make_double(1.0)), "1.0");
        ASSERT_EQ(to_json(JsonValue::make_double(100.0)), "100.0");
        ASSERT_EQ(to_json(JsonValue::make_double(-0.0)), "-0.0");
        ASSERT_EQ(to_json(JsonValue::make_double(0.0)), "0.0");
        ASSERT_EQ(to_json(JsonValue::make_double(0.1)), "0.1");
        ASSERT_EQ(to_json(JsonValue::make_double(-2.5)), "-2.5");
        ASSERT_EQ(to_json(JsonValue::make_double(0.30000000000000004)), "0.30000000000000004");
        ASSERT_TRUE(to_json(JsonValue::make_double(1e21)).find('e') != std::string::npos);
        ASSERT_TRUE(to_json(JsonValue::make_double(1e-7)).find('e') != std::string::npos);
        // no JSON form: null
        ASSERT_EQ(to_json(JsonValue::make_double(std::numeric_limits<double>::quiet_NaN())), "null");
        ASSERT_EQ(to_json(JsonValue::make_double(std::numeric_limits<double>::infinity())), "null");
        ASSERT_EQ(to_json(JsonValue::make_double(-std::numeric_limits<double>::infinity())), "null");
        ASSERT_EQ(to_json(parse_ok("[1.0,1e0,1E2,-0.0,0.5e1]")), "[1.0,1.0,100.0,-0.0,5.0]");
        Rng r(0xD0D0);
        for (int i = 0; i < 20000; ++i) {
            const double d = random_double(r);
            const JsonValue v = JsonValue::make_double(d);
            const std::string text = to_json(v);
            JsonValue back;
            std::string e;
            ASSERT_MSG(parse_json(text, back, &e), "double text should parse: " + text);
            ASSERT_MSG(back.is_double(), "a double stays a double: " + text);
            ASSERT_MSG(back == v, "double changed in a round trip: " + text);
        }
    } TEST_END();

    TEST_CASE("CTL1.13 JSON serializer: nesting beyond 128 (only a program can build it) is cut, not a crash") {
        JsonValue v = JsonValue::make_int(1);
        for (int i = 0; i < 300; ++i) {
            JsonValue a = JsonValue::make_array();
            a.push_back(std::move(v));
            v = std::move(a);
        }
        const std::string text = to_json(v);
        ASSERT_TRUE(text.find("null") != std::string::npos);
        ASSERT_EQ(std::count(text.begin(), text.end(), '['), 128);
        ASSERT_TRUE(parses(text) == false);                                     // too deep for the parser's own limit, as it should be
        JsonValue obj = JsonValue::make_int(1);
        for (int i = 0; i < 300; ++i) {
            JsonValue o = JsonValue::make_object();
            o.set("a", std::move(obj));
            obj = std::move(o);
        }
        ASSERT_TRUE(to_json(obj).find("null") != std::string::npos);
        JsonLimits deep;
        deep.max_depth = 128;
        JsonValue back;
        ASSERT_TRUE(parse_json(nested_arrays(128), back, nullptr, deep));
        ASSERT_EQ(to_json(back), nested_arrays(128));                           // exactly 128 levels still print in full
    } TEST_END();

    TEST_CASE("CTL1.14 JSON: is_valid_utf8") {
        ASSERT_TRUE(is_valid_utf8(""));
        ASSERT_TRUE(is_valid_utf8("plain ascii"));
        ASSERT_TRUE(is_valid_utf8("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"));
        ASSERT_TRUE(is_valid_utf8("\xF4\x8F\xBF\xBF"));
        ASSERT_TRUE(is_valid_utf8(lit("with\0nul")));
        ASSERT_FALSE(is_valid_utf8("\x80"));
        ASSERT_FALSE(is_valid_utf8("\xC0\xAF"));
        ASSERT_FALSE(is_valid_utf8("\xED\xA0\x80"));
        ASSERT_FALSE(is_valid_utf8("\xF4\x90\x80\x80"));
        ASSERT_FALSE(is_valid_utf8("\xE2\x82"));
        ASSERT_FALSE(is_valid_utf8("ok\xFF"));
        // every code point round trips through the encoder of the parser and is valid; neighbours of the surrogate range included
        for (uint32_t cp = 0; cp <= 0x10FFFF; cp += (cp < 0x1000 ? 1 : 97)) {
            if (cp >= 0xD800 && cp <= 0xDFFF) continue;
            char buf[16];
            if (cp < 0x10000) std::snprintf(buf, sizeof(buf), "\"\\u%04x\"", cp);
            else std::snprintf(buf, sizeof(buf), "\"\\u%04x\\u%04x\"", 0xD800 + ((cp - 0x10000) >> 10), 0xDC00 + ((cp - 0x10000) & 0x3FF));
            JsonValue v;
            ASSERT_MSG(parse_json(buf, v, nullptr), std::string("escape should parse: ") + buf);
            ASSERT_MSG(is_valid_utf8(v.str()), std::string("decoded text is valid UTF-8: ") + buf);
            ASSERT_MSG(!v.str().empty(), buf);
        }
    } TEST_END();

    TEST_CASE("CTL1.14b JSON: numbers do not depend on the decimal point of the C locale") {
        const char* previous = std::setlocale(LC_NUMERIC, nullptr);
        const std::string saved = previous != nullptr ? previous : "C";
        bool comma = false;
        for (const char* name : {"de_DE.UTF-8", "de_DE", "fr_FR.UTF-8", "fr_FR", "de_DE.utf8", "fr_FR.utf8"}) {
            if (std::setlocale(LC_NUMERIC, name) != nullptr && std::localeconv() != nullptr && std::strcmp(std::localeconv()->decimal_point, ",") == 0) {
                comma = true;
                break;
            }
        }
        if (!comma) {
            std::setlocale(LC_NUMERIC, saved.c_str());
            std::cout << "(no locale with a decimal comma here, skipped) " << std::flush;
            return;
        }
        JsonValue v;
        const bool parsed = parse_json("[1.5,-0.25,1e2,0.1]", v, nullptr);
        const std::string out = to_json(JsonValue::make_double(1234.5));
        const std::string out2 = to_json(parse_ok("[0.1,2.5e-3]"));
        std::setlocale(LC_NUMERIC, saved.c_str());
        ASSERT_TRUE(parsed);
        ASSERT_TRUE(v.at(0).as_double_or(0) == 1.5);
        ASSERT_TRUE(v.at(1).as_double_or(0) == -0.25);
        ASSERT_TRUE(v.at(3).as_double_or(0) == 0.1);
        ASSERT_EQ(out, "1234.5");
        ASSERT_EQ(out2, "[0.1,0.0025]");
    } TEST_END();

    // ============================================================================================
    // JSON: round trips and fuzzing
    // ============================================================================================

    TEST_CASE("CTL1.15 JSON: 30,000 random values survive to_json and parse_json bit for bit") {
        Rng r(0xC0FFEE);
        for (int i = 0; i < 30000; ++i) {
            const JsonValue v = random_value(r, 0);
            const std::string text = to_json(v);
            JsonValue back;
            std::string e;
            ASSERT_MSG(parse_json(text, back, &e), "serialized value should parse: " + shown(text) + "  (" + e + ")");
            ASSERT_MSG(back == v, "round trip changed the value: " + shown(text));
            ASSERT_MSG(to_json(back) == text, "serialization is not stable: " + shown(text));
            ASSERT_MSG(is_valid_utf8(text), "the text is valid UTF-8: " + shown(text));
            ASSERT_MSG(recognizes(text), "the independent recognizer refuses it: " + shown(text));
        }
    } TEST_END();

    TEST_CASE("CTL1.16 JSON fuzz: 200,000 random and mutated documents never crash or hang, and agree with an independent recognizer") {
        const std::vector<std::string> corpus = {
            "null", "true", "[]", "{}", "0", "-1.5e+10", "\"a\\u00e9\\ud83d\\ude00\\n\"", "[1,2,3]", "{\"a\":1}", "{\"a\":[1,{\"b\":null}],\"c\":\"d\"}",
            "[[[[1]]]]", "{\"k\":{\"k\":{\"k\":{}}}}", "[1.0,2e5,-0,0.5E-3,123456789012345678901234567890]", "\"\\\\\\/\\b\\f\\n\\r\\t\\\"\"",
            "{\"name\":\"Anthill\",\"seats\":4,\"open\":true,\"tags\":[\"a\",\"b\"],\"meta\":{\"x\":1.5,\"y\":null}}", lit("\"\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\""),
            " [ 1 , { \"a\" : [ ] } , \"b\" ] ", "[\"\\ud800\\udc00\",\"\\uffff\"]", "{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8,\"i\":9,\"j\":10,\"k\":11,\"l\":12,\"m\":13,\"n\":14,\"o\":15,\"p\":16,\"q\":17,\"r\":18}",
        };
        const std::string alphabet = "{}[]\",:0123456789.eE+-truefalsn\\u/ \t\n\r\x7f";
        Rng r(0xFEED5EED);
        int accepted = 0;
        int refused_but_grammatical = 0;
        size_t slowest_us = 0;
        for (int i = 0; i < 200000; ++i) {
            std::string doc;
            const uint32_t mode = r.below(10);
            if (mode < 3) {                                                     // random bytes, mostly JSON punctuation
                const uint32_t n = r.below(48);
                for (uint32_t k = 0; k < n; ++k) doc.push_back(r.chance(75) ? alphabet[r.below(static_cast<uint32_t>(alphabet.size()))] : static_cast<char>(r.below(256)));
            } else if (mode < 5) {                                              // a random valid value, serialized, then maybe damaged
                doc = to_json(random_value(r, 2));
                if (r.chance(50) && !doc.empty()) doc[r.below(static_cast<uint32_t>(doc.size()))] = alphabet[r.below(static_cast<uint32_t>(alphabet.size()))];
            } else {                                                            // a corpus document with a few mutations
                doc = corpus[r.below(static_cast<uint32_t>(corpus.size()))];
                const uint32_t edits = 1 + r.below(4);
                for (uint32_t e = 0; e < edits; ++e) {
                    const uint32_t op = r.below(7);
                    const uint32_t at = doc.empty() ? 0 : r.below(static_cast<uint32_t>(doc.size()));
                    switch (op) {
                        case 0: if (!doc.empty()) doc[at] = static_cast<char>(static_cast<unsigned char>(doc[at]) ^ (1u << r.below(8))); break;                // flip a bit
                        case 1: doc.insert(doc.begin() + at, alphabet[r.below(static_cast<uint32_t>(alphabet.size()))]); break;   // insert punctuation
                        case 2: doc.insert(doc.begin() + at, static_cast<char>(r.below(256))); break;                              // insert a random byte
                        case 3: if (!doc.empty()) doc.erase(at, 1 + r.below(4)); break;                                             // delete
                        case 4: if (!doc.empty()) doc.insert(at, doc.substr(at, 1 + r.below(8))); break;                            // repeat a piece
                        case 5: doc.resize(at); break;                                                                              // truncate
                        default: doc += corpus[r.below(static_cast<uint32_t>(corpus.size()))]; break;                               // concatenate
                    }
                }
            }
            const auto t0 = std::chrono::steady_clock::now();
            JsonValue v;
            std::string error;
            const bool ok = parse_json(doc, v, &error);
            const auto us = static_cast<size_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
            slowest_us = std::max(slowest_us, us);
            const bool grammatical = recognizes(doc);
            if (ok) {
                ++accepted;
                ASSERT_MSG(grammatical, "accepted but the independent recognizer refuses: " + shown(doc));
                ASSERT_MSG(error.empty(), "error text on success: " + shown(doc));
                const std::string out = to_json(v);
                JsonValue back;
                ASSERT_MSG(parse_json(out, back, nullptr), "serialized form of an accepted document fails: " + shown(doc) + " -> " + shown(out));
                ASSERT_MSG(back == v, "round trip of an accepted document changed it: " + shown(doc) + " -> " + shown(out));
                ASSERT_MSG(to_json(back) == out, "serialization not stable: " + shown(out));
            } else {
                ASSERT_MSG(v.is_null(), "out must be null after a refusal: " + shown(doc));
                ASSERT_MSG(!error.empty(), "an error text: " + shown(doc));
                if (grammatical) {                                              // only what the recognizer cannot know may refuse a grammatical text
                    ++refused_but_grammatical;
                    ASSERT_MSG(error.find("duplicate key") != std::string::npos || error.find("out of range") != std::string::npos,
                               "refused a grammatical document for the wrong reason: " + shown(doc) + "  (" + error + ")");
                }
            }
        }
        std::cout << "(accepted " << accepted << ", grammatical but refused " << refused_but_grammatical << ", slowest " << slowest_us << " us) " << std::flush;
        ASSERT_TRUE(accepted > 5000);                                           // the generator reaches deep into the grammar
        ASSERT_TRUE(refused_but_grammatical > 0);                               // and into the duplicate keys
        ASSERT_MSG(slowest_us < 2000000, "the slowest parse took " + std::to_string(slowest_us) + " us");
    } TEST_END();

    // ============================================================================================
    // HTTP: the server over real loopback sockets
    // ============================================================================================

    TEST_CASE("CTL2.1 HTTP listen: a secret is required, the port is exclusive and the loopback address only") {
        ASSERT_TRUE(HttpServer::listen(0, "") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, "has space") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, " ") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, "tab\tsecret") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, "line\nbreak") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, "line\rbreak") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, std::string("nul\0byte", 8)) == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, "del\x7f") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, "caf\xC3\xA9") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, "", false) == nullptr);                // an empty secret also refuses a server open to the network
        auto server = HttpServer::listen(0, "Ab-._~+/=:;!@#$%^&*()[]{}<>?,|`'\"\\x", true);
        ASSERT_TRUE(server != nullptr);                                           // every visible ASCII character may be part of a secret
        ASSERT_TRUE(server->port() != 0);
        ASSERT_EQ(server->connection_count(), 0u);
        // a second server on the same port does not start
        ASSERT_TRUE(HttpServer::listen(server->port(), "another-secret", true) == nullptr);
        const uint16_t port = server->port();
        server.reset();                                                            // and the port is free again when the first one is gone
        auto again = HttpServer::listen(port, "another-secret", true);
        ASSERT_TRUE(again != nullptr);
        ASSERT_EQ(again->port(), port);
    } TEST_END();

    TEST_CASE("CTL2.2 HTTP listen: only the loopback address answers (a connection to the machine's other address is refused)") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        Client c;
        ASSERT_TRUE(rig.connect(c));                                              // 127.0.0.1 works
        const std::string other = other_local_address();
        if (other.empty()) {
            std::cout << "(no other network interface here, skipped) " << std::flush;
            return;
        }
        Client remote;                                                            // (not `far`: that is an empty macro in the Windows headers)
        ASSERT_FALSE(remote.connect_to(rig.port(), other.c_str()));
    } TEST_END();

    TEST_CASE("CTL2.3 HTTP /healthz: answered by the server without a secret, the handler never sees it") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        Reply r = rig.exchange("GET /healthz HTTP/1.1\r\nHost: x\r\n\r\n");
        ASSERT_TRUE(r.complete);
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(r.reason, "OK");
        ASSERT_EQ(r.body, "{\"ok\":true}");
        ASSERT_TRUE(r.header("content-type") != nullptr && *r.header("content-type") == "application/json");
        ASSERT_TRUE(r.header("content-length") != nullptr && *r.header("content-length") == std::to_string(r.body.size()));
        ASSERT_TRUE(r.header("cache-control") != nullptr && *r.header("cache-control") == "no-store");
        ASSERT_TRUE(r.header("connection") != nullptr && *r.header("connection") == "close");
        ASSERT_TRUE(r.header("www-authenticate") == nullptr);
        ASSERT_EQ(rig.calls, 0);
        // a wrong secret does not matter for it, nor does a query or HTTP/1.0, or a body
        r = rig.exchange("GET /healthz HTTP/1.1\r\nAuthorization: Bearer nonsense\r\n\r\n");
        ASSERT_EQ(r.status, 200);
        r = rig.exchange("GET /healthz?probe=1&x=2 HTTP/1.1\r\n\r\n");
        ASSERT_EQ(r.status, 200);
        r = rig.exchange("GET /healthz HTTP/1.0\r\n\r\n");
        ASSERT_EQ(r.status, 200);
        r = rig.exchange("GET /healthz HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(r.body, "{\"ok\":true}");
        ASSERT_EQ(rig.calls, 0);
        // only that exact request is open: other methods, other paths, other spellings need the secret
        for (const char* request : {"POST /healthz HTTP/1.1\r\nContent-Length: 0\r\n\r\n", "DELETE /healthz HTTP/1.1\r\n\r\n", "GET /healthz/ HTTP/1.1\r\n\r\n",
                                    "GET /Healthz HTTP/1.1\r\n\r\n", "GET /healthz2 HTTP/1.1\r\n\r\n", "GET /health HTTP/1.1\r\n\r\n", "GET //healthz HTTP/1.1\r\n\r\n",
                                    "GET /healthz%2F HTTP/1.1\r\n\r\n", "GET /rooms HTTP/1.1\r\n\r\n", "GET / HTTP/1.1\r\n\r\n"}) {
            r = rig.exchange(request);
            ASSERT_MSG(r.status == 401, std::string("needs the secret: ") + shown(request));
        }
        ASSERT_EQ(rig.calls, 0);
        // with the secret, /healthz is still the server's own answer for GET; the other methods go to the handler
        r = rig.exchange(make_request("GET", "/healthz"));
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(r.body, "{\"ok\":true}");
        ASSERT_EQ(rig.calls, 0);
        r = rig.exchange(make_request("DELETE", "/healthz"));
        ASSERT_EQ(rig.calls, 1);
    } TEST_END();

    TEST_CASE("CTL2.4 HTTP: a call with the right secret reaches the handler with method, path, query, lower-cased headers and body") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        rig.handler = [&](const HttpRequest& req) {
            ++rig.calls;
            rig.seen.push_back(req);
            HttpResponse resp;
            resp.status = 201;
            resp.content_type = "text/plain; charset=utf-8";
            resp.body = "made " + req.path;
            return resp;
        };
        Reply r = rig.exchange(make_request("POST", "/rooms", "{\"seats\":4,\"map\":\"A B\"}", "Content-Type: application/json\r\nX-Custom-Header:   Some Value  \r\n"));
        ASSERT_TRUE(r.complete);
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(r.reason, "Created");
        ASSERT_EQ(r.body, "made /rooms");
        ASSERT_TRUE(r.header("content-type") != nullptr && *r.header("content-type") == "text/plain; charset=utf-8");
        ASSERT_TRUE(r.header("content-length") != nullptr && *r.header("content-length") == "11");
        ASSERT_TRUE(r.header("cache-control") != nullptr && *r.header("cache-control") == "no-store");
        ASSERT_EQ(rig.calls, 1);
        const HttpRequest& q = rig.seen[0];
        ASSERT_EQ(q.method, "POST");
        ASSERT_EQ(q.path, "/rooms");
        ASSERT_EQ(q.query, "");
        ASSERT_EQ(q.body, "{\"seats\":4,\"map\":\"A B\"}");
        ASSERT_TRUE(q.header("content-type") != nullptr && *q.header("content-type") == "application/json");
        ASSERT_TRUE(q.header("x-custom-header") != nullptr && *q.header("x-custom-header") == "Some Value");      // lower-cased name, trimmed value
        ASSERT_TRUE(q.header("host") != nullptr && *q.header("host") == "127.0.0.1");
        ASSERT_TRUE(q.header("authorization") != nullptr);
        ASSERT_TRUE(q.header("X-Custom-Header") == nullptr);                    // names are stored in lower case only
        ASSERT_EQ(q.headers.count("content-length"), 1u);
        ASSERT_EQ(q.header("content-length") != nullptr ? *q.header("content-length") : "", std::to_string(q.body.size()));

        r = rig.exchange(make_request("GET", "/rooms/ABCD?x=1&y=%20z"));
        ASSERT_EQ(rig.calls, 2);
        ASSERT_EQ(rig.seen[1].method, "GET");
        ASSERT_EQ(rig.seen[1].path, "/rooms/ABCD");
        ASSERT_EQ(rig.seen[1].query, "x=1&y=%20z");                             // not decoded
        ASSERT_EQ(rig.seen[1].body, "");
        r = rig.exchange(make_request("DELETE", "/rooms/ABCD"));
        ASSERT_EQ(rig.calls, 3);
        ASSERT_EQ(rig.seen[2].method, "DELETE");
        ASSERT_EQ(rig.seen[2].path, "/rooms/ABCD");
        ASSERT_EQ(r.status, 201);

        // edges of the target
        r = rig.exchange(make_request("GET", "/"));
        ASSERT_EQ(rig.seen.back().path, "/");
        r = rig.exchange(make_request("GET", "/?"));
        ASSERT_EQ(rig.seen.back().path, "/");
        ASSERT_EQ(rig.seen.back().query, "");
        r = rig.exchange(make_request("GET", "/a?b?c=d"));
        ASSERT_EQ(rig.seen.back().path, "/a");
        ASSERT_EQ(rig.seen.back().query, "b?c=d");                              // only the first ? splits
        r = rig.exchange(make_request("GET", "/a%2Fb/%00?q=%0d%0a"));
        ASSERT_EQ(rig.seen.back().path, "/a%2Fb/%00");                          // nothing is decoded
        ASSERT_EQ(rig.seen.back().query, "q=%0d%0a");
        r = rig.exchange(make_request("GET", "/../../etc/passwd"));
        ASSERT_EQ(rig.seen.back().path, "/../../etc/passwd");                   // not normalized: the router decides
        // the scheme is case-insensitive, and so are the header names; repeated ordinary headers are joined
        r = rig.exchange("GET /a HTTP/1.1\r\nauthorization: bearer " + std::string(Rig::kSecret) + "\r\nX-Multi: one\r\nx-multi: two\r\n\r\n");
        ASSERT_EQ(r.status, 201);
        ASSERT_TRUE(rig.seen.back().header("x-multi") != nullptr && *rig.seen.back().header("x-multi") == "one, two");
        r = rig.exchange("GET /a HTTP/1.1\r\nAUTHORIZATION: BEARER   " + std::string(Rig::kSecret) + "\r\n\r\n");     // several spaces before the token
        ASSERT_EQ(r.status, 201);
        r = rig.exchange("GET /a HTTP/1.1\r\nAuthorization:Bearer " + std::string(Rig::kSecret) + "\r\n\r\n");            // no space after the colon
        ASSERT_EQ(r.status, 201);
        r = rig.exchange("GET /a HTTP/1.1\r\nAuthorization: \t Bearer " + std::string(Rig::kSecret) + " \t \r\n\r\n");    // white space around the value
        ASSERT_EQ(r.status, 201);
        r = rig.exchange(make_request("GET", "/a").replace(0, 15, "GET /a HTTP/1.0"));                                        // HTTP/1.0
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(rig.seen.back().method, "GET");
        // a body of 64 KB with every byte value
        std::string all(HttpServer::kMaxBodyBytes, 'x');
        for (size_t i = 0; i < all.size(); ++i) all[i] = static_cast<char>(i * 131 + (i >> 8));
        r = rig.exchange(make_request("POST", "/rooms", all));
        ASSERT_EQ(r.status, 201);
        ASSERT_TRUE(rig.seen.back().body == all);
    } TEST_END();

    TEST_CASE("CTL2.5 HTTP auth: a missing, wrong, shortened or lengthened secret gets 401 and never reaches the handler") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        const std::string secret = Rig::kSecret;
        const std::vector<std::pair<std::string, std::string>> tries = {
            {"no header", ""},
            {"empty value", "Authorization:\r\n"},
            {"scheme only", "Authorization: Bearer\r\n"},
            {"scheme and spaces", "Authorization: Bearer    \r\n"},
            {"wrong secret", "Authorization: Bearer wrong\r\n"},
            {"first half", "Authorization: Bearer " + secret.substr(0, secret.size() / 2) + "\r\n"},
            {"one character short", "Authorization: Bearer " + secret.substr(0, secret.size() - 1) + "\r\n"},
            {"one character only", "Authorization: Bearer " + secret.substr(0, 1) + "\r\n"},
            {"missing first character", "Authorization: Bearer " + secret.substr(1) + "\r\n"},
            {"one character longer", "Authorization: Bearer " + secret + "x\r\n"},
            {"twice the secret", "Authorization: Bearer " + secret + secret + "\r\n"},
            {"secret and a space and more", "Authorization: Bearer " + secret + " more\r\n"},
            {"a high byte after the secret", "Authorization: Bearer " + secret + "\x80\r\n"},
            {"last character changed", "Authorization: Bearer " + secret.substr(0, secret.size() - 1) + "X\r\n"},
            {"first character changed", "Authorization: Bearer X" + secret.substr(1) + "\r\n"},
            {"upper case secret", "Authorization: Bearer " + std::string(secret.size(), 'T') + "\r\n"},
            {"swapped case", "Authorization: Bearer TEST-SECRET-0123456789\r\n"},
            {"basic scheme", "Authorization: Basic " + secret + "\r\n"},
            {"token scheme", "Authorization: Token " + secret + "\r\n"},
            {"no scheme", "Authorization: " + secret + "\r\n"},
            {"scheme glued to the secret", "Authorization: Bearer" + secret + "\r\n"},
            {"scheme in a tab", "Authorization: Bearer\t" + secret + "\r\n"},
            {"quoted secret", "Authorization: Bearer \"" + secret + "\"\r\n"},
            {"colon scheme", "Authorization: Bearer: " + secret + "\r\n"},
            {"secret in another header", "X-Authorization: Bearer " + secret + "\r\n"},
            {"secret in a cookie", "Cookie: Bearer " + secret + "\r\n"},
            {"secret as a query-like header", "X-Api-Key: " + secret + "\r\n"},
            {"proxy header", "Proxy-Authorization: Bearer " + secret + "\r\n"},
        };
        for (const auto& t : tries) {
            const Reply r = rig.exchange("GET /rooms/ABCD HTTP/1.1\r\nHost: x\r\n" + t.second + "\r\n");
            ASSERT_MSG(r.complete && r.status == 401, "should be 401: " + t.first + " (got " + std::to_string(r.status) + ")");
            ASSERT_MSG(r.header("www-authenticate") != nullptr && *r.header("www-authenticate") == "Bearer", "WWW-Authenticate: " + t.first);
            ASSERT_MSG(r.body == "{\"error\":\"unauthorized\"}", "body: " + t.first + " " + r.body);
            ASSERT_MSG(r.header("content-type") != nullptr && *r.header("content-type") == "application/json", t.first);
            ASSERT_MSG(r.header("content-length") != nullptr && *r.header("content-length") == std::to_string(r.body.size()), t.first);
            ASSERT_MSG(r.header("cache-control") != nullptr && *r.header("cache-control") == "no-store", t.first);
            ASSERT_MSG(rig.calls == 0, "the handler must not be called: " + t.first);
        }
        // a NUL (or any control character) in the head is a malformed request: 400, before the secret is even looked at
        Reply nul = rig.exchange(std::string("GET /rooms/ABCD HTTP/1.1\r\nAuthorization: Bearer ") + secret + std::string(1, '\0') + "\r\n\r\n");
        ASSERT_EQ(nul.status, 400);
        ASSERT_EQ(rig.calls, 0);
        // the same with POST and a body, DELETE, an unknown method and a framing the server would otherwise refuse: the answer is 401 first
        Reply r = rig.exchange(make_request("POST", "/rooms", "{\"a\":1}", "", false));
        ASSERT_EQ(r.status, 401);
        r = rig.exchange(make_request("DELETE", "/rooms/ABCD", "", "", false));
        ASSERT_EQ(r.status, 401);
        r = rig.exchange(make_request("PUT", "/rooms/ABCD", "", "", false));
        ASSERT_EQ(r.status, 401);
        r = rig.exchange(make_request("POST", "/rooms", "", "", false, false));                                      // no Content-Length either
        ASSERT_EQ(r.status, 401);
        r = rig.exchange("POST /rooms HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n");
        ASSERT_EQ(r.status, 401);
        r = rig.exchange("POST /rooms HTTP/1.1\r\nContent-Length: 99999999\r\n\r\n");
        ASSERT_EQ(r.status, 401);
        ASSERT_EQ(rig.calls, 0);
        // and with the right secret the same server answers
        r = rig.exchange(make_request("GET", "/rooms/ABCD"));
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.calls, 1);
        // two Authorization headers are ambiguous: refused, even when both are right
        r = rig.exchange("GET /a HTTP/1.1\r\n" + auth_line() + auth_line() + "\r\n");
        ASSERT_EQ(r.status, 400);
        r = rig.exchange("GET /a HTTP/1.1\r\nAuthorization: Bearer wrong\r\n" + auth_line() + "\r\n");
        ASSERT_EQ(r.status, 400);
        ASSERT_EQ(rig.calls, 1);
    } TEST_END();

    TEST_CASE("CTL2.6 HTTP auth: constant_time_equal compares correctly") {
        ASSERT_TRUE(constant_time_equal("", ""));
        ASSERT_TRUE(constant_time_equal("a", "a"));
        ASSERT_TRUE(constant_time_equal("secret", "secret"));
        ASSERT_FALSE(constant_time_equal("secret", "secreT"));
        ASSERT_FALSE(constant_time_equal("secret", "secre"));
        ASSERT_FALSE(constant_time_equal("secre", "secret"));
        ASSERT_FALSE(constant_time_equal("secret", ""));
        ASSERT_FALSE(constant_time_equal("", "secret"));
        ASSERT_FALSE(constant_time_equal(std::string_view("ab\0", 3), std::string_view("ab", 2)));     // a trailing NUL is a difference
        ASSERT_FALSE(constant_time_equal(std::string_view("ab", 2), std::string_view("ab\0", 3)));
        ASSERT_TRUE(constant_time_equal(std::string_view("a\0b", 3), std::string_view("a\0b", 3)));
        ASSERT_FALSE(constant_time_equal(std::string_view("a\0b", 3), std::string_view("a\0c", 3)));
        const std::string base(64, 'k');
        for (size_t i = 0; i < base.size(); ++i) {
            for (int bit = 0; bit < 8; ++bit) {
                std::string other = base;
                other[i] = static_cast<char>(other[i] ^ (1 << bit));
                ASSERT_FALSE(constant_time_equal(base, other));
                ASSERT_FALSE(constant_time_equal(other, base));
            }
        }
        ASSERT_TRUE(constant_time_equal(base, std::string(64, 'k')));
        Rng r(77);
        for (int i = 0; i < 20000; ++i) {
            const std::string a = random_string(r);
            const std::string b = r.chance(50) ? a : random_string(r);
            ASSERT_EQ(constant_time_equal(a, b), a == b);
        }
    } TEST_END();

    TEST_CASE("CTL2.7 HTTP: other methods get 405 (with Allow), a bad method token gets 400, the handler is not called") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        for (const char* method : {"PUT", "PATCH", "HEAD", "OPTIONS", "TRACE", "CONNECT", "FOO", "get", "Get", "POSTX", "GETS", "LINK", "PROPFIND", "A"}) {
            const Reply r = rig.exchange(make_request(method, "/rooms/ABCD"));
            ASSERT_MSG(r.complete && r.status == 405, std::string("405 for ") + method + " (got " + std::to_string(r.status) + ")");
            ASSERT_MSG(r.header("allow") != nullptr && *r.header("allow") == "GET, POST, DELETE", method);
            ASSERT_MSG(r.body == "{\"error\":\"method not allowed\"}", method);
        }
        ASSERT_EQ(rig.calls, 0);
        for (const char* method : {"G(T", "GE T", "G\"T", "G,T", "G;T", "G/T", "G@T", "G[T", "G{T", "G<T", "G=T"}) {
            const Reply r = rig.exchange(std::string(method) + " /a HTTP/1.1\r\n" + auth_line() + "\r\n");
            ASSERT_MSG(r.status == 400, std::string("400 for the method token ") + method + " (got " + std::to_string(r.status) + ")");
        }
        const Reply long_method = rig.exchange(std::string(40, 'A') + " /a HTTP/1.1\r\n" + auth_line() + "\r\n");
        ASSERT_EQ(long_method.status, 400);
        ASSERT_EQ(rig.calls, 0);
        // the three methods that are allowed are
        ASSERT_EQ(rig.exchange(make_request("GET", "/a")).status, 200);
        ASSERT_EQ(rig.exchange(make_request("POST", "/a", "{}")).status, 200);
        ASSERT_EQ(rig.exchange(make_request("DELETE", "/a")).status, 200);
        ASSERT_EQ(rig.calls, 3);
    } TEST_END();

    TEST_CASE("CTL2.8 HTTP bodies: Content-Length only (411), at most 64 KB (413), the length must be a plain number (400)") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        // 411
        Reply r = rig.exchange(make_request("POST", "/rooms", "", "", true, false));
        ASSERT_EQ(r.status, 411);
        ASSERT_EQ(r.reason, "Length Required");
        ASSERT_TRUE(r.body.find("\"error\"") != std::string::npos);
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Transfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n");
        ASSERT_EQ(r.status, 411);
        r = rig.exchange("GET /rooms HTTP/1.1\r\n" + auth_line() + "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
        ASSERT_EQ(r.status, 411);
        r = rig.exchange("DELETE /rooms/A HTTP/1.1\r\n" + auth_line() + "Transfer-Encoding: identity\r\n\r\n");
        ASSERT_EQ(r.status, 411);
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "transfer-encoding: gzip, chunked\r\n\r\n");
        ASSERT_EQ(r.status, 411);
        ASSERT_EQ(rig.calls, 0);
        // 413: answered on the head, the body does not have to be sent (or read)
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 65537\r\n\r\n");
        ASSERT_EQ(r.status, 413);
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 99999999999999999999999\r\n\r\n");
        ASSERT_EQ(r.status, 413);
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 4294967296\r\n\r\n");
        ASSERT_EQ(r.status, 413);
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 18446744073709551616\r\n\r\n");
        ASSERT_EQ(r.status, 413);
        r = rig.exchange("GET /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 70000\r\n\r\n");
        ASSERT_EQ(r.status, 413);
        r = rig.exchange(make_request("POST", "/rooms", std::string(65537, 'a')));                       // the whole body is sent anyway
        ASSERT_TRUE(r.complete);
        ASSERT_EQ(r.status, 413);
        r = rig.exchange(make_request("POST", "/rooms", std::string(1000000, 'a')));
        ASSERT_EQ(r.status, 413);
        ASSERT_EQ(rig.calls, 0);
        // exactly the limit passes; leading zeros are fine
        r = rig.exchange(make_request("POST", "/rooms", std::string(65536, 'a')));
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.calls, 1);
        ASSERT_EQ(rig.seen.back().body.size(), 65536u);
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 000000000000000000000003\r\n\r\nabc");
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.seen.back().body, "abc");
        // Content-Length 0 on a POST: an empty body, the handler is called
        r = rig.exchange(make_request("POST", "/rooms", "", "Content-Length: 0\r\n", true, false));
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.seen.back().body, "");
        // a GET or DELETE may carry a body
        r = rig.exchange("GET /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 3\r\n\r\nabc");
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.seen.back().body, "abc");
        r = rig.exchange("DELETE /rooms/A HTTP/1.1\r\n" + auth_line() + "Content-Length: 2\r\n\r\nxy");
        ASSERT_EQ(rig.seen.back().body, "xy");
        // the body is exactly as long as announced: what follows is not part of it
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 3\r\n\r\nabcdef");
        ASSERT_EQ(rig.seen.back().body, "abc");
        // 400: the length is not a plain number, or there are two (the handler is not called for any of them)
        const int calls_before = rig.calls;
        for (const char* bad : {"Content-Length: abc\r\n", "Content-Length: -1\r\n", "Content-Length: +5\r\n", "Content-Length: 5, 5\r\n", "Content-Length: 1 2\r\n",
                                "Content-Length: 0x10\r\n", "Content-Length: 1.5\r\n", "Content-Length: \r\n", "Content-Length: 5\r\nContent-Length: 5\r\n",
                                "Content-Length: 5\r\nContent-Length: 6\r\n", "Content-Length: 5\r\ncontent-length: 5\r\n",
                                "Content-Length: 5\r\nTransfer-Encoding: chunked\r\n", "Transfer-Encoding: chunked\r\nContent-Length: 5\r\n"}) {
            r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + bad + "\r\nhello");
            ASSERT_MSG(r.status == 400, std::string("400 for ") + shown(bad) + " (got " + std::to_string(r.status) + ")");
        }
        ASSERT_EQ(rig.calls, calls_before);
    } TEST_END();

    TEST_CASE("CTL2.9 HTTP: Expect: 100-continue is answered, other expectations are refused") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        Client c;
        ASSERT_TRUE(rig.connect(c));
        c.send_all("POST /rooms HTTP/1.1\r\n" + auth_line() + "Expect: 100-continue\r\nContent-Length: 5\r\n\r\n");
        const std::string interim = rig.quiet_for(c, 500);
        ASSERT_EQ(interim, "HTTP/1.1 100 Continue\r\n\r\n");
        ASSERT_EQ(rig.calls, 0);
        c.send_all("hello");
        const Reply r = rig.read_reply(c);
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(r.responses, 1);
        ASSERT_EQ(rig.calls, 1);
        ASSERT_EQ(rig.seen.back().body, "hello");
        // a body that already came with the head needs no 100
        const Reply r2 = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Expect: 100-continue\r\nContent-Length: 2\r\n\r\nhi");
        ASSERT_EQ(r2.status, 200);
        ASSERT_EQ(r2.responses, 1);
        ASSERT_TRUE(r2.raw.compare(0, 12, "HTTP/1.1 200") == 0);
        // an unknown expectation, and a request that is refused before its body: no 100
        ASSERT_EQ(rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Expect: magic\r\nContent-Length: 2\r\n\r\nhi").status, 417);
        const Reply r3 = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Expect: 100-continue\r\nContent-Length: 70000\r\n\r\n");
        ASSERT_EQ(r3.status, 413);
        ASSERT_EQ(r3.responses, 1);
        const Reply r4 = rig.exchange("POST /rooms HTTP/1.1\r\nExpect: 100-continue\r\nContent-Length: 5\r\n\r\n");
        ASSERT_EQ(r4.status, 401);
        ASSERT_EQ(r4.responses, 1);
        ASSERT_EQ(rig.calls, 2);
    } TEST_END();

    TEST_CASE("CTL2.10 HTTP: a request that is not well formed gets 400, a wrong version 505, a head that is too large 431") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        const std::string a = auth_line();
        const std::vector<std::pair<std::string, std::string>> bad = {
            {"no version", "GET /\r\n" + a + "\r\n"},
            {"a fourth word", "GET / HTTP/1.1 extra\r\n" + a + "\r\n"},
            {"a space in the target", "GET /a b HTTP/1.1\r\n" + a + "\r\n"},
            {"two spaces", "GET  / HTTP/1.1\r\n" + a + "\r\n"},
            {"leading space", " GET / HTTP/1.1\r\n" + a + "\r\n"},
            {"trailing space", "GET / HTTP/1.1 \r\n" + a + "\r\n"},
            {"tab as separator", "GET\t/\tHTTP/1.1\r\n" + a + "\r\n"},
            {"no target", "GET  HTTP/1.1\r\n" + a + "\r\n"},
            {"only a method", "GET\r\n" + a + "\r\n"},
            {"blank request line", "\r\n" + a + "\r\n"},
            {"garbage", "\x01\x02\x03\x04 garbage\r\n\r\n"},
            {"binary", std::string("\xFF\xFE\xFD\r\n\r\n")},
            {"lower case version", "GET / http/1.1\r\n" + a + "\r\n"},
            {"short version", "GET / HTTP/1\r\n" + a + "\r\n"},
            {"long version", "GET / HTTP/1.11\r\n" + a + "\r\n"},
            {"version letters", "GET / HTTP/x.y\r\n" + a + "\r\n"},
            {"no slash in version", "GET / HTTP1.1\r\n" + a + "\r\n"},
            {"absolute form", "GET http://example.com/ HTTP/1.1\r\n" + a + "\r\n"},
            {"asterisk form", "GET * HTTP/1.1\r\n" + a + "\r\n"},
            {"authority form", "GET example.com:80 HTTP/1.1\r\n" + a + "\r\n"},
            {"no leading slash", "GET rooms HTTP/1.1\r\n" + a + "\r\n"},
            {"fragment", "GET /a#frag HTTP/1.1\r\n" + a + "\r\n"},
            {"control character in the target", "GET /\x01 HTTP/1.1\r\n" + a + "\r\n"},
            {"DEL in the target", "GET /\x7f HTTP/1.1\r\n" + a + "\r\n"},
            {"non-ASCII in the target", "GET /\xC3\xA9 HTTP/1.1\r\n" + a + "\r\n"},
            {"NUL in the target", std::string("GET /a\0b HTTP/1.1\r\n", 19) + a + "\r\n"},
            {"header without a colon", "GET / HTTP/1.1\r\n" + a + "Host\r\n\r\n"},
            {"space in a header name", "GET / HTTP/1.1\r\n" + a + "Bad Name: x\r\n\r\n"},
            {"space before the colon", "GET / HTTP/1.1\r\n" + a + "Name : x\r\n\r\n"},
            {"empty header name", "GET / HTTP/1.1\r\n" + a + ": x\r\n\r\n"},
            {"folded header (space)", "GET / HTTP/1.1\r\n" + a + "X: a\r\n b\r\n\r\n"},
            {"folded header (tab)", "GET / HTTP/1.1\r\n" + a + "X: a\r\n\tb\r\n\r\n"},
            {"folded first header", "GET / HTTP/1.1\r\n folded\r\n" + a + "\r\n"},
            {"bare LF in a header", "GET / HTTP/1.1\r\n" + a + "X: a\nb: c\r\n\r\n"},
            {"bare CR in a header", "GET / HTTP/1.1\r\n" + a + "X: a\rb: c\r\n\r\n"},
            {"NUL in a header", std::string("GET / HTTP/1.1\r\n") + a + std::string("X: a\0b\r\n\r\n", 10)},
            {"control character in a header", "GET / HTTP/1.1\r\n" + a + "X: a\x01" "b\r\n\r\n"},
            {"DEL in a header", "GET / HTTP/1.1\r\n" + a + "X: a\x7f" "b\r\n\r\n"},
            {"bad character in a header name", "GET / HTTP/1.1\r\n" + a + "X(Y): z\r\n\r\n"},
            {"duplicate Host", "GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n" + a + "\r\n"},
        };
        for (const auto& t : bad) {
            const Reply r = rig.exchange(t.second);
            ASSERT_MSG(r.complete && r.status == 400, "should be 400: " + t.first + " (got " + std::to_string(r.status) + ")");
            ASSERT_MSG(r.body.find("\"error\"") != std::string::npos, "an error body: " + t.first);
            ASSERT_MSG(r.header("content-length") != nullptr && *r.header("content-length") == std::to_string(r.body.size()), t.first);
            ASSERT_MSG(r.responses == 1, t.first);
        }
        // a bare LF as the end of the head is refused at once, not after a timeout
        Client c;
        ASSERT_TRUE(rig.connect(c));
        c.send_all("GET / HTTP/1.1\n" + a + "\n");
        const Reply lf = rig.read_reply(c, 500);
        ASSERT_EQ(lf.status, 400);
        ASSERT_EQ(rig.calls, 0);
        // versions
        for (const char* v : {"HTTP/2.0", "HTTP/0.9", "HTTP/1.2", "HTTP/3.0", "HTTP/9.9", "HTTP/1.9"}) {
            const Reply r = rig.exchange(std::string("GET / ") + v + "\r\n" + a + "\r\n");
            ASSERT_MSG(r.status == 505, std::string("505 for ") + v + " (got " + std::to_string(r.status) + ")");
        }
        ASSERT_EQ(rig.calls, 0);
        // 431: more than 8 KB of head, with and without the end of the head, and more than 100 headers
        Reply r = rig.exchange("GET / HTTP/1.1\r\n" + a + "X: " + std::string(9000, 'v') + "\r\n\r\n");
        ASSERT_EQ(r.status, 431);
        r = rig.exchange("GET /" + std::string(9000, 'a') + " HTTP/1.1\r\n" + a + "\r\n");
        ASSERT_EQ(r.status, 431);
        r = rig.exchange("GET /" + std::string(9000, 'a'));                                          // no end of the head at all
        ASSERT_EQ(r.status, 431);
        r = rig.exchange(std::string(100000, 'a'));
        ASSERT_EQ(r.status, 431);
        std::string many = "GET / HTTP/1.1\r\n" + a;
        for (int i = 0; i < 99; ++i) many += "H" + std::to_string(i) + ": v\r\n";                    // 100 headers with the secret
        r = rig.exchange(many + "\r\n");
        ASSERT_EQ(r.status, 200);
        r = rig.exchange(many + "H99: v\r\n\r\n");                                                    // 101
        ASSERT_EQ(r.status, 431);
        // the head may be exactly 8192 bytes, not one more
        const std::string prefix = "GET / HTTP/1.1\r\n" + a + "X: ";
        const size_t pad = HttpServer::kMaxHeadBytes - prefix.size() - 4;
        r = rig.exchange(prefix + std::string(pad, 'p') + "\r\n\r\n");
        ASSERT_EQ(r.status, 200);
        r = rig.exchange(prefix + std::string(pad + 1, 'p') + "\r\n\r\n");
        ASSERT_EQ(r.status, 431);
        // the 8 KB are about the head: a body of 64 KB behind a small head is fine (covered in CTL2.4); the handler count is what the 200s made
        ASSERT_EQ(rig.calls, 2);
    } TEST_END();

    TEST_CASE("CTL2.11 HTTP: parse_http_head on its own") {
        HttpHead h;
        std::string e;
        ASSERT_EQ(parse_http_head("GET /a/b?c=d HTTP/1.1\r\nHost: Example\r\nX-Y:  z \r\n\r\n", h, &e), 0);
        ASSERT_EQ(h.request.method, "GET");
        ASSERT_EQ(h.request.path, "/a/b");
        ASSERT_EQ(h.request.query, "c=d");
        ASSERT_EQ(h.request.headers.size(), 2u);
        ASSERT_EQ(*h.request.header("host"), "Example");
        ASSERT_EQ(*h.request.header("x-y"), "z");
        ASSERT_FALSE(h.has_content_length);
        ASSERT_EQ(h.body_status, 0);
        ASSERT_EQ(parse_http_head("POST / HTTP/1.1\r\nContent-Length: 12\r\n\r\n", h, &e), 0);
        ASSERT_TRUE(h.has_content_length);
        ASSERT_EQ(h.content_length, 12u);
        ASSERT_EQ(h.body_status, 0);
        ASSERT_EQ(parse_http_head("POST / HTTP/1.1\r\n\r\n", h, &e), 0);
        ASSERT_EQ(h.body_status, 411);
        ASSERT_EQ(parse_http_head("POST / HTTP/1.1\r\nContent-Length: 70000\r\n\r\n", h, &e), 0);
        ASSERT_EQ(h.body_status, 413);
        ASSERT_EQ(parse_http_head("GET / HTTP/1.1\r\nExpect: 100-Continue\r\nContent-Length: 1\r\n\r\n", h, &e), 0);
        ASSERT_TRUE(h.expect_continue);
        ASSERT_EQ(parse_http_head("GET / HTTP/1.1\r\n", h, &e), 400);                  // not the whole head
        ASSERT_EQ(parse_http_head("", h, &e), 400);
        ASSERT_EQ(parse_http_head("\r\n\r\n", h, &e), 400);
        ASSERT_EQ(parse_http_head("GET / HTTP/1.1\r\n\r\nX: y\r\n\r\n", h, &e), 400);   // a blank line in the middle
        ASSERT_EQ(parse_http_head("GET / HTTP/2.0\r\n\r\n", h, &e), 505);
        ASSERT_EQ(parse_http_head(std::string(HttpServer::kMaxHeadBytes + 1, 'a'), h, &e), 431);
        ASSERT_FALSE(e.empty());
        ASSERT_EQ(parse_http_head("GET / HTTP/1.1\r\n\r\n", h, nullptr), 0);               // no error pointer is fine
        ASSERT_EQ(parse_http_head("GET / HTTP/1.1\r\nbad\r\n\r\n", h, nullptr), 400);
    } TEST_END();

    TEST_CASE("CTL2.12 HTTP: a request that is not complete 5 seconds after accept gets 408 (the clock is the caller's)") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        // an idle connection
        {
            Client c;
            rig.now = 10000;
            ASSERT_TRUE(rig.connect_accepted(c));                                        // accepted at 10000
            rig.now = 14999;
            int rc = 0;
            std::string raw = rig.quiet_for(c, 20, &rc);
            ASSERT_TRUE(raw.empty() && rc == -1);                                        // nothing yet, the connection is open
            ASSERT_EQ(rig.server->connection_count(), 1u);
            rig.now = 15000;
            const Reply r = rig.read_reply(c);
            ASSERT_TRUE(r.complete);
            ASSERT_EQ(r.status, 408);
            ASSERT_EQ(r.reason, "Request Timeout");
            ASSERT_EQ(r.body, "{\"error\":\"request timeout\"}");
            ASSERT_TRUE(r.header("connection") != nullptr && *r.header("connection") == "close");
            ASSERT_EQ(rig.calls, 0);
        }
        // an incomplete head
        {
            Client c;
            rig.now = 20000;
            ASSERT_TRUE(rig.connect_accepted(c));
            c.send_all("GET /rooms HTTP/1.1\r\n" + auth_line() + "X-Slow: ");
            rig.pump();
            rig.now = 24999;
            int rc = 0;
            ASSERT_TRUE(rig.quiet_for(c, 20, &rc).empty() && rc == -1);
            rig.now = 25000;
            const Reply r = rig.read_reply(c);
            ASSERT_EQ(r.status, 408);
            ASSERT_EQ(rig.calls, 0);
        }
        // a head without its body
        {
            Client c;
            rig.now = 30000;
            ASSERT_TRUE(rig.connect_accepted(c));
            c.send_all("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 10\r\n\r\n12345");
            rig.pump();
            rig.now = 34999;
            int rc = 0;
            ASSERT_TRUE(rig.quiet_for(c, 20, &rc).empty() && rc == -1);
            rig.now = 35000;
            const Reply r = rig.read_reply(c);
            ASSERT_EQ(r.status, 408);
            ASSERT_EQ(rig.calls, 0);
        }
        // a request that completes just in time is served
        {
            Client c;
            rig.now = 40000;
            ASSERT_TRUE(rig.connect_accepted(c));
            c.send_all("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 4\r\n\r\n12");
            rig.pump();
            rig.now = 44999;
            rig.pump();
            c.send_all("34");
            const Reply r = rig.read_reply(c);
            ASSERT_EQ(r.status, 200);
            ASSERT_EQ(rig.calls, 1);
            ASSERT_EQ(rig.seen.back().body, "1234");
        }
        // the timeout can be changed
        {
            rig.server->set_request_timeout_ms(200);
            Client c;
            rig.now = 50000;
            ASSERT_TRUE(rig.connect_accepted(c));
            rig.now = 50199;
            int rc = 0;
            ASSERT_TRUE(rig.quiet_for(c, 20, &rc).empty() && rc == -1);
            rig.now = 50200;
            ASSERT_EQ(rig.read_reply(c).status, 408);
            rig.server->set_request_timeout_ms(HttpServer::kRequestTimeoutMs);
        }
        // the clock may wrap around 2^32
        {
            Client c;
            rig.now = 0xFFFFFFFFu - 999u;
            ASSERT_TRUE(rig.connect_accepted(c));
            rig.now = rig.now + 4999u;                                                   // wrapped past zero
            ASSERT_TRUE(rig.now < 10000u);
            int rc = 0;
            ASSERT_TRUE(rig.quiet_for(c, 20, &rc).empty() && rc == -1);
            rig.now = rig.now + 1u;
            ASSERT_EQ(rig.read_reply(c).status, 408);
        }
        ASSERT_EQ(rig.calls, 1);
    } TEST_END();

    TEST_CASE("CTL2.13 HTTP: a request that arrives one byte at a time is read whole") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        const std::string body = "{\"seats\":4,\"note\":\"one byte at a time\"}";
        const std::string request = make_request("POST", "/rooms?x=1", body, "X-Piece: yes\r\n");
        Client c;
        ASSERT_TRUE(rig.connect(c));
        for (size_t i = 0; i < request.size(); ++i) {
            ASSERT_EQ(c.send_all(request.substr(i, 1)), 1u);
            rig.pump();
            if (i % 8 == 0) sleep_ms(1);
            ASSERT_MSG(rig.calls == 0 || i + 1 == request.size(), "the handler ran before the request was complete, at byte " + std::to_string(i));
        }
        const Reply r = rig.read_reply(c);
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.calls, 1);
        ASSERT_EQ(rig.seen[0].method, "POST");
        ASSERT_EQ(rig.seen[0].path, "/rooms");
        ASSERT_EQ(rig.seen[0].query, "x=1");
        ASSERT_EQ(rig.seen[0].body, body);
        ASSERT_TRUE(rig.seen[0].header("x-piece") != nullptr && *rig.seen[0].header("x-piece") == "yes");
        // the same with CRLF split across pieces, and a refusal in pieces
        Client d;
        ASSERT_TRUE(rig.connect(d));
        const std::string bad = "GET / HTTP/1.1\r\nBad Name: x\r\n\r\n";
        for (size_t i = 0; i < bad.size(); ++i) {
            d.send_all(bad.substr(i, 1));
            rig.pump();
        }
        ASSERT_EQ(rig.read_reply(d).status, 400);
        ASSERT_EQ(rig.calls, 1);
    } TEST_END();

    TEST_CASE("CTL2.14 HTTP: a second request behind the first on the same connection is ignored, the connection closes") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        const std::string first = make_request("GET", "/first");
        const std::string second = make_request("GET", "/second");
        Reply r = rig.exchange(first + second);
        ASSERT_TRUE(r.complete);
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(r.responses, 1);
        ASSERT_EQ(rig.calls, 1);
        ASSERT_EQ(rig.seen[0].path, "/first");
        // a POST whose body is followed by another request
        r = rig.exchange(make_request("POST", "/p1", "abc") + make_request("POST", "/p2", "def"));
        ASSERT_EQ(r.responses, 1);
        ASSERT_EQ(rig.calls, 2);
        ASSERT_EQ(rig.seen[1].body, "abc");
        // the first one refused: still one answer
        r = rig.exchange(make_request("GET", "/first", "", "", false) + second);
        ASSERT_EQ(r.status, 401);
        ASSERT_EQ(r.responses, 1);
        ASSERT_EQ(rig.calls, 2);
        // /healthz first, then a valid request: the server answers the first only
        r = rig.exchange("GET /healthz HTTP/1.1\r\n\r\n" + second);
        ASSERT_EQ(r.responses, 1);
        ASSERT_EQ(r.body, "{\"ok\":true}");
        ASSERT_EQ(rig.calls, 2);
        // a lot of data behind the request does not make the answer get lost: the server has answered long before the client has stopped sending,
        // and a server that closed the socket then would answer the rest with a reset that wipes the unread answer out of the client's buffer
        {
            Client c;
            ASSERT_TRUE(rig.connect(c));
            c.send_all(first);
            rig.pump_n(3);
            size_t sent = 0;
            const std::string junk(8192, 'z');
            for (int i = 0; i < 40; ++i) {
                sent += c.send_all(junk);
                rig.pump();
            }
            ASSERT_EQ(sent, 40u * 8192u);
            r = rig.read_reply(c);
            ASSERT_TRUE(r.complete);
            ASSERT_FALSE(r.reset);
            ASSERT_EQ(r.status, 200);
            ASSERT_EQ(r.responses, 1);
            ASSERT_EQ(rig.calls, 3);
        }
        r = rig.exchange(first + std::string(60000, 'z'));
        ASSERT_TRUE(r.complete);
        ASSERT_FALSE(r.reset);
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.calls, 4);
        // a request the server refuses on its head, with a body that is on its way
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 65537\r\n\r\n" + std::string(65537, 'b'));
        ASSERT_FALSE(r.reset);
        ASSERT_EQ(r.status, 413);
        ASSERT_EQ(rig.calls, 4);
    } TEST_END();

    TEST_CASE("CTL2.15 HTTP: after the answer the connection lingers until the client closes or 2 s pass, then it is gone") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        Client c;
        rig.now = 1000;
        ASSERT_TRUE(rig.connect(c));
        c.send_all(make_request("GET", "/a"));
        const Reply r = rig.read_reply(c);
        ASSERT_TRUE(r.complete);                                                        // the answer, then the end of the stream
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.server->connection_count(), 1u);                                  // the client has not closed: the server waits
        rig.now = 1000 + HttpServer::kLingerMs - 1;
        rig.pump_n(5);
        ASSERT_EQ(rig.server->connection_count(), 1u);
        rig.now = 1000 + HttpServer::kLingerMs;
        ASSERT_TRUE(rig.wait_count(0));
        // a client that closes first frees the slot at once
        Client d;
        rig.now = 5000;
        ASSERT_TRUE(rig.connect(d));
        d.send_all(make_request("GET", "/a"));
        ASSERT_TRUE(rig.read_reply(d).complete);
        d.close();
        ASSERT_TRUE(rig.wait_count(0));
        // a client that half-closes right after its request still gets the answer
        Client e;
        ASSERT_TRUE(rig.connect(e));
        e.send_all(make_request("GET", "/half"));
        e.shutdown_write();
        const Reply h = rig.read_reply(e);
        ASSERT_EQ(h.status, 200);
        ASSERT_EQ(rig.seen.back().path, "/half");
    } TEST_END();

    TEST_CASE("CTL2.16 HTTP: clients that disconnect in the middle of a request are dropped quietly") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        const std::string head = "POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 100\r\n\r\n";
        const std::vector<std::string> pieces = {"", "G", "GET /ro", "GET /rooms HTTP/1.1\r\n", "GET /rooms HTTP/1.1\r\n" + auth_line(), head, head + "0123456789"};
        for (const bool reset : {false, true}) {
            for (const std::string& p : pieces) {
                Client c;
                ASSERT_TRUE(rig.connect(c));
                if (!p.empty()) c.send_all(p);
                rig.pump_n(3);
                if (reset) c.abort_connection();
                else c.close();
                ASSERT_MSG(rig.wait_count(0), "a vanished client is dropped: piece " + shown(p) + (reset ? " (reset)" : " (closed)"));
            }
        }
        ASSERT_EQ(rig.calls, 0);
        // a client that closes while the answer is on its way
        {
            Client c;
            ASSERT_TRUE(rig.connect(c));
            c.send_all(make_request("GET", "/a"));
            c.abort_connection();
            ASSERT_TRUE(rig.wait_count(0));
        }
        // and the server is still fine
        const Reply r = rig.exchange(make_request("GET", "/after"));
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.seen.back().path, "/after");
    } TEST_END();

    TEST_CASE("CTL2.17 HTTP: at most 32 connections at once; a new one takes the slot of the oldest connection that has not sent its head, so idle ones can never lock the interface out; freed slots are used again") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        // 40 clients connect and say nothing: every one beyond the 32nd takes the slot of the oldest silent one (clients 0 - 7 are evicted, 8 - 39 remain)
        std::vector<Client> clients(40);
        for (Client& c : clients) ASSERT_TRUE(rig.connect(c));
        ASSERT_TRUE(rig.wait_count(HttpServer::kMaxConnections));
        for (size_t i = 0; i < clients.size() - HttpServer::kMaxConnections; ++i) {             // the server closes the evicted ones without an answer
            std::string raw;
            int rc = -1;
            for (int round = 0; round < 2000 && rc == -1; ++round) {
                rig.pump();
                rc = clients[i].recv_some(raw);
                if (rc == -1) sleep_ms(1);
            }
            ASSERT_MSG(rc == 0 || rc == -2, "connection " + std::to_string(i) + " was evicted");
            ASSERT_TRUE(raw.empty());
        }
        ASSERT_EQ(rig.server->connection_count(), HttpServer::kMaxConnections);
        size_t closed = 0;
        size_t open = 0;
        for (size_t i = 0; i < clients.size(); ++i) {
            std::string raw;
            const int rc = clients[i].recv_some(raw);
            if (rc == 0 || rc == -2) ++closed;
            if (rc == -1) ++open;
            ASSERT_MSG((i >= clients.size() - HttpServer::kMaxConnections) == (rc == -1), "connection " + std::to_string(i) + (rc == -1 ? " is open" : " is closed"));
            ASSERT_TRUE(raw.empty());
        }
        ASSERT_EQ(open, 32u);
        ASSERT_EQ(closed, 8u);
        // the newest one is served; a request that comes while all 32 are held by silent connections is served too (that is the point)
        clients[8].send_all(make_request("GET", "/first"));
        const Reply r = rig.read_reply(clients[8]);
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.seen.back().path, "/first");
        clients[8].close();
        ASSERT_TRUE(rig.wait_count(31));
        Client fresh;
        ASSERT_TRUE(rig.connect(fresh));
        ASSERT_TRUE(rig.wait_count(32));
        Client real;                                                                           // all 32 slots are held by silent connections: the real request still gets in
        ASSERT_TRUE(rig.connect(real));
        real.send_all(make_request("GET", "/real"));
        ASSERT_EQ(rig.read_reply(real).status, 200);
        ASSERT_EQ(rig.seen.back().path, "/real");
        std::string raw;
        ASSERT_EQ(fresh.recv_some(raw), -1);                                                   // (the oldest silent one, clients[9], is the one that gave way)
        int rc9 = -1;
        for (int round = 0; round < 2000 && rc9 == -1; ++round) {
            rig.pump();
            rc9 = clients[9].recv_some(raw);
            if (rc9 == -1) sleep_ms(1);
        }
        ASSERT_TRUE(rc9 == 0 || rc9 == -2);
        fresh.send_all(make_request("GET", "/fresh"));
        ASSERT_EQ(rig.read_reply(fresh).status, 200);
        // the remaining idle ones time out together after 5 seconds
        rig.now += HttpServer::kRequestTimeoutMs;
        int answered = 0;
        for (size_t i = 10; i < clients.size(); ++i) {
            if (rig.read_reply(clients[i], 500).status == 408) ++answered;
        }
        ASSERT_EQ(answered, 30);
        ASSERT_TRUE(rig.server->connection_count() >= 30u);                         // they wait for their clients to close (or for the linger time)
        rig.now += HttpServer::kLingerMs;
        ASSERT_TRUE(rig.wait_count(0));
        // connections that are being answered are never evicted: with every slot busy writing or receiving a body, a further connection is closed at once
        {
            std::vector<Client> bodies(HttpServer::kMaxConnections);
            const std::string head = "POST /body HTTP/1.1\r\nHost: x\r\n" + auth_line() + "Content-Length: 100\r\n\r\n";
            for (Client& c : bodies) {
                ASSERT_TRUE(rig.connect(c));
                c.send_all(head);                                                              // the head is complete; the body never comes
            }
            for (int i = 0; i < 30; ++i) {
                rig.pump();
                sleep_ms(2);
            }
            ASSERT_EQ(rig.server->connection_count(), HttpServer::kMaxConnections);
            Client turned_away;
            ASSERT_TRUE(rig.connect(turned_away));
            int rc = -1;
            const std::string nothing = rig.quiet_for(turned_away, 2000, &rc);
            ASSERT_TRUE(rc == 0 || rc == -2);
            ASSERT_TRUE(nothing.empty());
            ASSERT_EQ(rig.server->connection_count(), HttpServer::kMaxConnections);
            for (Client& c : bodies) c.close();
            ASSERT_TRUE(rig.wait_count(0));
        }
        // a flood of connections that connect and leave does not leak slots
        for (int i = 0; i < 300; ++i) {
            Client c;
            ASSERT_TRUE(rig.connect(c));
            if (i % 3 == 0) c.send_all("GET");
            if (i % 50 == 0) rig.pump();
        }
        fresh.close();
        real.close();
        ASSERT_TRUE(rig.wait_count(0));
    } TEST_END();

    TEST_CASE("CTL2.17b HTTP: when every slot is taken, silent connections give way before connections that only wait to close, and the oldest of the kind first; a request that is waiting is never the one that gives way") {
        // every slot waits to close after its answer (32 clients got an answer and keep their side open): a new connection takes the slot of the oldest
        {
            Rig rig;
            ASSERT_TRUE(rig.ok());
            std::vector<Client> clients(HttpServer::kMaxConnections);
            for (size_t i = 0; i < clients.size(); ++i) {
                ASSERT_TRUE(rig.connect(clients[i]));
                clients[i].send_all(make_request("GET", "/linger" + std::to_string(i)));
                ASSERT_EQ(rig.read_reply(clients[i]).status, 200);                      // answered; the server now waits for the client to close
            }
            ASSERT_EQ(rig.server->connection_count(), HttpServer::kMaxConnections);
            Client fresh;
            ASSERT_TRUE(rig.connect(fresh));
            fresh.send_all(make_request("GET", "/new"));
            ASSERT_EQ(rig.read_reply(fresh).status, 200);                               // it got in
            ASSERT_EQ(rig.seen.back().path, "/new");
            ASSERT_EQ(rig.server->connection_count(), HttpServer::kMaxConnections);     // one of the waiting connections was closed to make room (the server's end of a
                                                                                       // finished answer is half-closed anyway: the client cannot tell which one from its side)
        }
        // 16 that only wait to close (the oldest) and 16 silent ones (newer): the new connection takes a SILENT slot, the waiting ones stay
        {
            Rig rig;
            ASSERT_TRUE(rig.ok());
            std::vector<Client> clients(HttpServer::kMaxConnections);
            for (size_t i = 0; i < 16; ++i) {
                ASSERT_TRUE(rig.connect(clients[i]));
                clients[i].send_all(make_request("GET", "/linger"));
                ASSERT_EQ(rig.read_reply(clients[i]).status, 200);
            }
            for (size_t i = 16; i < clients.size(); ++i) ASSERT_TRUE(rig.connect(clients[i]));
            ASSERT_TRUE(rig.wait_count(HttpServer::kMaxConnections));
            Client fresh;
            ASSERT_TRUE(rig.connect(fresh));
            fresh.send_all(make_request("GET", "/new"));
            ASSERT_EQ(rig.read_reply(fresh).status, 200);
            std::string raw;
            int rc = -1;
            for (int round = 0; round < 2000 && rc == -1; ++round) {                    // the oldest SILENT connection (16) gave way
                rig.pump();
                rc = clients[16].recv_some(raw);
                if (rc == -1) sleep_ms(1);
            }
            ASSERT_TRUE(rc == 0 || rc == -2);
            ASSERT_EQ(clients[17].recv_some(raw), -1);                                  // only that one: the next silent one is still open
            ASSERT_EQ(rig.server->connection_count(), HttpServer::kMaxConnections);
        }
        // a request that has arrived but has not been read yet is not idle: 32 connections with their requests already waiting, a 33rd connects in the same
        // pass: the 33rd is turned away, and every one of the 32 requests is answered
        {
            Rig rig;
            ASSERT_TRUE(rig.ok());
            std::vector<Client> clients(HttpServer::kMaxConnections + 1);
            for (size_t i = 0; i < HttpServer::kMaxConnections; ++i) {
                ASSERT_TRUE(rig.connect(clients[i]));
                clients[i].send_all(make_request("GET", "/waiting" + std::to_string(i)));
            }
            ASSERT_TRUE(rig.connect(clients[HttpServer::kMaxConnections]));            // (nothing has been pumped yet: the server has accepted nothing)
            size_t answered = 0;
            for (size_t i = 0; i < HttpServer::kMaxConnections; ++i) {
                if (rig.read_reply(clients[i]).status == 200) ++answered;
            }
            ASSERT_EQ(answered, HttpServer::kMaxConnections);
        }
    } TEST_END();

    TEST_CASE("CTL2.18 HTTP response safety: the handler's output cannot split a response, bad statuses and exceptions become 500") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        const auto answer_with = [&](const HttpResponse& resp) {
            rig.handler = [resp, &rig](const HttpRequest&) {
                ++rig.calls;
                return resp;
            };
            return rig.exchange(make_request("GET", "/x"));
        };
        // a content type with CR / LF / controls is replaced; nothing of it can start a header
        for (const char* type : {"text/plain\r\nX-Evil: 1", "text/plain\nX-Evil: 1", "text/plain\rX-Evil: 1", "a\x01" "b", "tab\there", "x\x7f", "caf\xC3\xA9", ""}) {
            HttpResponse resp;
            resp.content_type = type;
            resp.body = "ok";
            const Reply r = answer_with(resp);
            ASSERT_MSG(r.status == 200, "status kept for " + shown(type));
            ASSERT_MSG(r.header("x-evil") == nullptr, "no injected header for " + shown(type));
            ASSERT_MSG(r.header("content-type") != nullptr && *r.header("content-type") == "application/json", "fallback type for " + shown(type));
            ASSERT_MSG(r.raw.find("X-Evil") == std::string::npos || r.raw.find("\r\nX-Evil") == std::string::npos, "no injected line for " + shown(type));
            ASSERT_MSG(r.body == "ok", "body for " + shown(type));
            ASSERT_MSG(r.responses == 1, shown(type));
        }
        HttpResponse html;
        html.content_type = "text/html; charset=utf-8";
        html.body = "<p>hi</p>\r\n\r\nHTTP/1.1 200 OK";                                  // CR LF in the body are the handler's business and stay in the body
        Reply r = answer_with(html);
        ASSERT_TRUE(r.header("content-type") != nullptr && *r.header("content-type") == "text/html; charset=utf-8");
        ASSERT_EQ(r.body, html.body);
        ASSERT_TRUE(r.header("content-length") != nullptr && *r.header("content-length") == std::to_string(html.body.size()));
        // statuses outside 200 - 599 are the server's 500
        for (const int status : {0, 99, 100, 101, 199, 600, 999, -5, 100000}) {
            HttpResponse resp;
            resp.status = status;
            resp.body = "secret handler text";
            r = answer_with(resp);
            ASSERT_MSG(r.status == 500, "500 for status " + std::to_string(status) + " (got " + std::to_string(r.status) + ")");
            ASSERT_MSG(r.body == "{\"error\":\"internal error\"}", "body for status " + std::to_string(status));
        }
        for (const int status : {200, 201, 202, 301, 302, 304, 400, 403, 404, 409, 422, 429, 500, 503, 599}) {
            HttpResponse resp;
            resp.status = status;
            resp.body = "{}";
            r = answer_with(resp);
            ASSERT_MSG(r.status == status, "status " + std::to_string(status) + " is passed on (got " + std::to_string(r.status) + ")");
        }
        // 204 has no body, no type and no length
        HttpResponse none;
        none.status = 204;
        none.body = "must not be sent";
        r = answer_with(none);
        ASSERT_EQ(r.status, 204);
        ASSERT_EQ(r.body, "");
        ASSERT_TRUE(r.header("content-length") == nullptr);
        ASSERT_TRUE(r.header("content-type") == nullptr);
        // an empty body has a length of 0
        HttpResponse empty;
        empty.status = 200;
        r = answer_with(empty);
        ASSERT_EQ(r.body, "");
        ASSERT_TRUE(r.header("content-length") != nullptr && *r.header("content-length") == "0");
        // a handler that throws, throws something else, or is missing
        rig.handler = [](const HttpRequest&) -> HttpResponse { throw std::runtime_error("boom with details /secret/path"); };
        r = rig.exchange(make_request("GET", "/x"));
        ASSERT_EQ(r.status, 500);
        ASSERT_EQ(r.body, "{\"error\":\"internal error\"}");
        ASSERT_TRUE(r.raw.find("boom") == std::string::npos);
        rig.handler = [](const HttpRequest&) -> HttpResponse { throw 42; };
        ASSERT_EQ(rig.exchange(make_request("GET", "/x")).status, 500);
        rig.handler = nullptr;
        ASSERT_EQ(rig.exchange(make_request("GET", "/x")).status, 500);
        ASSERT_EQ(rig.exchange("GET /healthz HTTP/1.1\r\n\r\n").status, 200);              // /healthz does not need a handler at all
        // a body above 1 MB is refused
        HttpResponse oversized;
        oversized.body.assign(HttpServer::kMaxResponseBytes + 1, 'h');
        ASSERT_EQ(answer_with(oversized).status, 500);
        // the server's own answers never repeat what the client sent
        rig.handler = [&](const HttpRequest&) {
            ++rig.calls;
            return HttpResponse();
        };
        r = rig.exchange("GET /\xC3\xA9<script>alert(1)</script> HTTP/1.1\r\nX-Evil: \"quoted\\ text\"\r\n" + auth_line() + "\r\n");
        ASSERT_EQ(r.status, 400);
        ASSERT_TRUE(r.raw.find("script") == std::string::npos && r.raw.find("quoted") == std::string::npos);
        r = rig.exchange("GET /a%0d%0aX-Evil:%201 HTTP/1.1\r\n" + auth_line() + "\r\n");
        ASSERT_EQ(r.status, 200);
        ASSERT_TRUE(r.header("x-evil") == nullptr);
        ASSERT_TRUE(r.raw.find("%0d") == std::string::npos);
        // every answer carries the same safety headers
        r = rig.exchange(make_request("GET", "/x"));
        ASSERT_TRUE(r.header("x-content-type-options") != nullptr && *r.header("x-content-type-options") == "nosniff");
        ASSERT_TRUE(r.header("cache-control") != nullptr && *r.header("cache-control") == "no-store");
        ASSERT_TRUE(r.header("connection") != nullptr && *r.header("connection") == "close");
    } TEST_END();

    TEST_CASE("CTL2.19 HTTP: a large answer reaches a slow reader whole, a reader that never reads is dropped") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        const std::string big = body_pattern(900000);
        rig.handler = [&](const HttpRequest&) {
            ++rig.calls;
            HttpResponse resp;
            resp.content_type = "application/octet-stream";
            resp.body = big;
            return resp;
        };
        {
            Client c;
            ASSERT_TRUE(rig.connect(c));
            c.send_all(make_request("GET", "/big"));
            rig.pump_n(20);                                                              // the reader is slow: the buffers fill, the server waits
            const Reply r = rig.read_reply(c, 5000);
            ASSERT_TRUE(r.complete);
            ASSERT_EQ(r.status, 200);
            ASSERT_EQ(r.body.size(), big.size());
            ASSERT_TRUE(r.body == big);
        }
        {   // small buffers on both sides keep the answer stuck in the server's write: the system's own can take all of it (Linux's take 4 MB at once),
            // or more of it later (macOS grows them), and a write that finished was dropped kLingerMs after it, or never once the clock stood still
            rig.server->set_send_buffer_bytes(16 * 1024);
            Client c;
            c.set_receive_buffer(16 * 1024);
            rig.now = 100000;
            ASSERT_TRUE(rig.connect(c));
            const int before = rig.calls;
            c.send_all(make_request("GET", "/big"));
            ASSERT_TRUE(rig.wait_calls(before + 1));                                     // answered (and stuck in the write) at 100000
            ASSERT_EQ(rig.server->connection_count(), 1u);
            rig.now += HttpServer::kLingerMs;                                            // a finished answer would be dropped now, a stuck one is not
            rig.pump_n(5);
            ASSERT_EQ(rig.server->connection_count(), 1u);
            rig.now = 100000 + HttpServer::kWriteTimeoutMs - 1;
            rig.pump_n(5);
            ASSERT_EQ(rig.server->connection_count(), 1u);
            rig.now += 1;                                                                // dropped kWriteTimeoutMs after the answer
            ASSERT_TRUE(rig.wait_count(0));
        }
    } TEST_END();

    TEST_CASE("CTL2.20 HTTP: garbage on real connections never stops the server") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        Rng r(0xBAD5EED);
        const std::vector<std::string> seeds = {make_request("GET", "/rooms/ABCD"), make_request("POST", "/rooms", "{\"seats\":4}"), "GET /healthz HTTP/1.1\r\n\r\n",
                                                make_request("DELETE", "/rooms/ABCD?x=1", "", "Expect: 100-continue\r\n")};
        for (int i = 0; i < 300; ++i) {
            std::string data;
            if (r.chance(40)) {
                const uint32_t n = r.below(300);
                for (uint32_t k = 0; k < n; ++k) data.push_back(static_cast<char>(r.below(256)));
            } else {
                data = seeds[r.below(static_cast<uint32_t>(seeds.size()))];
                const uint32_t edits = 1 + r.below(5);
                for (uint32_t e = 0; e < edits && !data.empty(); ++e) {
                    const uint32_t at = r.below(static_cast<uint32_t>(data.size()));
                    switch (r.below(4)) {
                        case 0: data[at] = static_cast<char>(r.below(256)); break;
                        case 1: data.erase(at, 1 + r.below(6)); break;
                        case 2: data.insert(at, 1, "\r\n :,/?#%0"[r.below(10)]); break;
                        default: data.resize(at); break;
                    }
                }
            }
            Client c;
            ASSERT_TRUE(rig.connect(c));
            c.send_all(data);
            rig.pump_n(2);
            if (r.chance(30)) c.abort_connection();
            else if (r.chance(50)) c.shutdown_write();
            rig.pump_n(2);
            std::string raw;
            while (c.recv_some(raw) > 0) {}
            if (!raw.empty()) {                                                          // whatever it answered is a well formed response
                ASSERT_MSG(raw.compare(0, 9, "HTTP/1.1 ") == 0, "an answer starts with the status line: " + shown(data) + " -> " + shown(raw));
                const Reply a = parse_reply(raw);
                ASSERT_MSG(a.status >= 100 && a.status <= 599, "a sane status: " + shown(raw));
                ASSERT_MSG(a.header("content-length") != nullptr || a.status == 204 || a.status == 100, "a length: " + shown(raw));
            }
            c.close();
        }
        ASSERT_TRUE(rig.wait_count(0));
        const Reply ok = rig.exchange(make_request("GET", "/still-alive"));
        ASSERT_EQ(ok.status, 200);
        ASSERT_EQ(rig.seen.back().path, "/still-alive");
    } TEST_END();

    TEST_CASE("CTL2.21 HTTP: 100,000 random and mutated request heads parse to a request that is safe to use, or are refused") {
        const std::vector<std::string> seeds = {
            "GET /rooms/ABCD HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer s\r\n\r\n",
            "POST /rooms?x=1&y=2 HTTP/1.1\r\nHost: a\r\nContent-Length: 12\r\nContent-Type: application/json\r\nX-A: b\r\n\r\n",
            "DELETE /rooms/ABCD HTTP/1.0\r\nExpect: 100-continue\r\nX-Multi: 1\r\nX-Multi: 2\r\n\r\n",
            "GET / HTTP/1.1\r\n\r\n",
            "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",
        };
        const std::string alphabet = "\r\n :,/?#%\tabcXYZ019-_.GETPOSDLHTP";
        Rng r(0x4EAD);
        int accepted = 0;
        for (int i = 0; i < 100000; ++i) {
            std::string head = seeds[r.below(static_cast<uint32_t>(seeds.size()))];
            const uint32_t edits = r.below(4);
            for (uint32_t e = 0; e < edits && !head.empty(); ++e) {
                const uint32_t at = r.below(static_cast<uint32_t>(head.size()));
                switch (r.below(5)) {
                    case 0: head[at] = alphabet[r.below(static_cast<uint32_t>(alphabet.size()))]; break;
                    case 1: head[at] = static_cast<char>(r.below(256)); break;
                    case 2: head.erase(at, 1 + r.below(5)); break;
                    case 3: head.insert(at, 1, alphabet[r.below(static_cast<uint32_t>(alphabet.size()))]); break;
                    default: head.insert(at, head.substr(at, 1 + r.below(10))); break;
                }
            }
            if (r.chance(10)) head = std::string(r.below(20), static_cast<char>(r.below(256)));
            HttpHead h;
            std::string error;
            const int status = parse_http_head(head, h, &error);
            if (status != 0) {
                ASSERT_MSG(status == 400 || status == 431 || status == 505, "a refusal status: " + std::to_string(status));
                ASSERT_MSG(!error.empty(), "an error text: " + shown(head));
                continue;
            }
            ++accepted;
            const HttpRequest& q = h.request;
            ASSERT_MSG(!q.method.empty() && q.method.find_first_of(" \r\n\t") == std::string::npos, "method: " + shown(head));
            ASSERT_MSG(!q.path.empty() && q.path[0] == '/', "path: " + shown(head));
            ASSERT_MSG(q.path.find_first_of(" \r\n\t#?") == std::string::npos && q.query.find_first_of(" \r\n\t#") == std::string::npos, "target: " + shown(head));
            for (const auto& kv : q.headers) {
                ASSERT_MSG(!kv.first.empty() && kv.first.find_first_of(" \r\n\t:") == std::string::npos, "header name: " + shown(head));
                ASSERT_MSG(std::none_of(kv.first.begin(), kv.first.end(), [](char c) { return c >= 'A' && c <= 'Z'; }), "lower case name: " + shown(head));
                ASSERT_MSG(kv.second.find_first_of("\r\n") == std::string::npos, "header value: " + shown(head));
                ASSERT_MSG(kv.second.empty() || (kv.second.front() != ' ' && kv.second.front() != '\t' && kv.second.back() != ' ' && kv.second.back() != '\t') ||
                               q.headers.count(kv.first) > 1 || kv.second.find(", ") != std::string::npos,
                           "trimmed value: " + shown(head));
            }
            ASSERT_MSG(!h.has_content_length || h.content_length <= (size_t{1} << 30), "length: " + shown(head));
            if (h.body_status == 0 && h.has_content_length) ASSERT_MSG(h.content_length <= HttpServer::kMaxBodyBytes, "length within the limit: " + shown(head));
            if (h.body_status != 0) ASSERT_MSG(h.body_error != nullptr && h.body_error[0] != '\0', "body error text: " + shown(head));
            ASSERT_MSG(h.body_status == 0 || h.body_status == 411 || h.body_status == 413 || h.body_status == 417, "body status: " + shown(head));
        }
        ASSERT_TRUE(accepted > 1000);
    } TEST_END();

    TEST_CASE("CTL2.22 HTTP public door (listen_public, the replays' list behind the site): no secret is asked, a plain GET reaches the handler whoever sends it, /healthz is the server's own answer, every other method gets 405 with Allow: GET, a request with a body is refused") {
        Rig rig(true);
        ASSERT_TRUE(rig.ok());
        Reply r = rig.exchange("GET /replays HTTP/1.1\r\nHost: x\r\n\r\n");
        ASSERT_TRUE(r.complete && r.status == 200 && r.body == "{\"handled\":true}");
        ASSERT_TRUE(r.header("cache-control") != nullptr && *r.header("cache-control") == "no-store" && r.header("www-authenticate") == nullptr);
        ASSERT_EQ(rig.calls, 1);
        ASSERT_TRUE(rig.seen[0].method == "GET" && rig.seen[0].path == "/replays" && rig.seen[0].query.empty());
        r = rig.exchange("GET /replays/ants-X-20261008-143209Z.antsrep?limit=5 HTTP/1.1\r\nAuthorization: Bearer nonsense\r\n\r\n");      // (a header that means nothing here)
        ASSERT_EQ(r.status, 200);
        ASSERT_TRUE(rig.calls == 2 && rig.seen[1].path == "/replays/ants-X-20261008-143209Z.antsrep" && rig.seen[1].query == "limit=5");
        r = rig.exchange(make_request("GET", "/replays"));                                                      // (and the secret of the other door is no key to anything)
        ASSERT_TRUE(r.status == 200 && rig.calls == 3);
        // /healthz is answered by the server itself
        r = rig.exchange("GET /healthz HTTP/1.1\r\n\r\n");
        ASSERT_TRUE(r.status == 200 && r.body == "{\"ok\":true}" && rig.calls == 3);
        // nothing but GET gets as far as the handler
        for (const char* method : {"POST", "DELETE", "PUT", "PATCH", "HEAD", "OPTIONS", "TRACE", "CONNECT", "FOO"}) {
            r = rig.exchange(std::string(method) + " /replays HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
            ASSERT_MSG(r.complete && r.status == 405, std::string("405 for ") + method + " (got " + std::to_string(r.status) + ")");
            ASSERT_MSG(r.header("allow") != nullptr && *r.header("allow") == "GET", method);
            ASSERT_MSG(r.body == "{\"error\":\"method not allowed\"}", method);
            r = rig.exchange(std::string(method) + " /healthz HTTP/1.1\r\n\r\n");                              // (not even the server's own path, for a method that is not GET)
            ASSERT_MSG(r.status == 405, method);
        }
        for (const char* method : {"G(T", "GE T", "get"}) ASSERT_EQ(rig.exchange(std::string(method) + " /replays HTTP/1.1\r\n\r\n").status, method == std::string("get") ? 405 : 400);
        ASSERT_EQ(rig.calls, 3);
        // a body: refused, never read, never handed over
        ASSERT_EQ(rig.exchange("GET /replays HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello").status, 400);
        ASSERT_EQ(rig.exchange("GET /replays HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n").status, 411);
        ASSERT_EQ(rig.exchange("GET /replays HTTP/1.1\r\nContent-Length: 70000\r\n\r\n").status, 413);
        ASSERT_EQ(rig.exchange("GET /replays HTTP/1.1\r\nContent-Length: x\r\n\r\n").status, 400);
        ASSERT_EQ(rig.calls, 3);
        ASSERT_EQ(rig.exchange("GET /replays HTTP/1.1\r\nContent-Length: 0\r\n\r\n").status, 200);           // (an empty body is none)
        ASSERT_EQ(rig.calls, 4);
        // a head that is not well formed is refused like at the other door
        ASSERT_EQ(rig.exchange("GET /replays HTTP/2.0\r\n\r\n").status, 505);
        ASSERT_EQ(rig.exchange("GET replays HTTP/1.1\r\n\r\n").status, 400);
        ASSERT_EQ(rig.exchange("GET /replays HTTP/1.1\nHost: x\n\n").status, 400);
        ASSERT_EQ(rig.calls, 4);
        // a handler that throws, or answers with a status that is none, is an internal error here too
        rig.handler = [](const HttpRequest&) -> HttpResponse { throw std::runtime_error("boom"); };
        r = rig.exchange("GET /replays HTTP/1.1\r\n\r\n");
        ASSERT_TRUE(r.status == 500 && r.body == "{\"error\":\"internal error\"}");
        rig.handler = [](const HttpRequest&) {
            HttpResponse resp;
            resp.status = 99;
            return resp;
        };
        ASSERT_EQ(rig.exchange("GET /replays HTTP/1.1\r\n\r\n").status, 500);
        ASSERT_TRUE(rig.wait_count(0));
    } TEST_END();

    TEST_CASE("CTL2.23 HTTP public door: the port is exclusive like the other door's, the loopback address is the default and only that; a door open to the network says so explicitly") {
        auto door = HttpServer::listen_public(0, true);
        ASSERT_TRUE(door != nullptr && door->port() != 0);
        const uint16_t port = door->port();
        ASSERT_TRUE(HttpServer::listen_public(port, true) == nullptr);
        ASSERT_TRUE(HttpServer::listen(port, "another-secret", true) == nullptr);
        door.reset();
        auto again = HttpServer::listen_public(port, true);
        ASSERT_TRUE(again != nullptr && again->port() == port);
        again.reset();
        auto open_door = HttpServer::listen_public(0, false);
        ASSERT_TRUE(open_door != nullptr && open_door->port() != 0);
        Rig rig(true);
        ASSERT_TRUE(rig.ok());
        Client c;
        ASSERT_TRUE(rig.connect(c));                                                                            // (the loopback address answers)
        ASSERT_TRUE(rig.wait_count(1));
    } TEST_END();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
