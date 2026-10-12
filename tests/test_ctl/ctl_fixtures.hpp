// The fixtures of the control interface tests (test_ctl.cpp holds main(); the cases are in test_ctl_json.cpp and test_ctl_http.cpp). Inline or in an anonymous namespace.
#pragma once
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
#include "../common/test_assert.hpp"

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

inline int g_test_count = 0;
inline int g_test_failures = 0;
inline int g_assert_count = 0;

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
inline std::string lit(const char (&a)[N]) {                  // a string literal with its embedded NULs
    return std::string(a, N - 1);
}

inline std::string shown(const std::string& s) {              // a text with its unprintable bytes made visible (for failure messages)
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

inline bool parses(const std::string& text) {
    JsonValue v;
    std::string e;
    return parse_json(text, v, &e);
}

inline JsonValue parse_ok(const std::string& text) {
    JsonValue v;
    std::string e;
    if (!parse_json(text, v, &e)) return JsonValue::make_string("PARSE FAILED: " + e);
    return v;
}

inline std::string error_of(const std::string& text, const JsonLimits& limits = {}) {
    JsonValue v;
    std::string e;
    return parse_json(text, v, &e, limits) ? std::string() : e;
}

inline std::string nested_arrays(size_t depth) { return std::string(depth, '[') + std::string(depth, ']'); }

inline std::string nested_objects(size_t depth) {
    std::string s;
    for (size_t i = 0; i < depth; ++i) s += "{\"a\":";
    s += "1";
    s += std::string(depth, '}');
    return s;
}

// ---- an independent recognizer of JSON text (an explicit stack and a state machine instead of recursion; no values are built) for the differential
// fuzz. It knows grammar, UTF-8, escapes and the depth / number-length limits, not repeated keys and not doubles that overflow.

inline bool recognizer_utf8(const std::string& s, size_t& i) {                 // one non-ASCII character at s[i]
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

inline bool recognizer_string(const std::string& s, size_t& i) {                // s[i] is the opening quote
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

inline bool recognizer_number(const std::string& s, size_t& i) {
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

inline bool recognizes(const std::string& s, size_t max_depth = 16) {
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

inline std::string random_string(Rng& r) {
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

inline double random_double(Rng& r) {
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

inline JsonValue random_value(Rng& r, int depth) {
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
inline void sock_close(sock_t s) { closesocket(s); }
inline bool sock_would_block() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
inline void sock_close(sock_t s) { ::close(s); }
inline bool sock_would_block() { return errno == EWOULDBLOCK || errno == EAGAIN; }
#endif

inline void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

inline bool sock_nonblocking(sock_t s) {
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

inline Reply parse_reply(const std::string& raw) {
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

inline std::string auth_line() { return std::string("Authorization: Bearer ") + Rig::kSecret + "\r\n"; }

// A request with the secret (unless `with_secret` is false); the body gets its Content-Length
inline std::string make_request(const std::string& method, const std::string& target, const std::string& body = "", const std::string& extra = "",
                         bool with_secret = true, bool with_length = true) {
    std::string s = method + " " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
    if (with_secret) s += auth_line();
    s += extra;
    if (with_length && (!body.empty() || method == "POST")) s += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    s += "\r\n";
    s += body;
    return s;
}

inline std::string body_pattern(size_t n) {
    std::string s(n, 'x');
    for (size_t i = 0; i < n; ++i) s[i] = static_cast<char>('a' + (i * 7 + i / 251) % 26);
    return s;
}

// The address of a network interface of this machine other than the loopback (empty when there is none or the platform does not tell)
inline std::string other_local_address() {
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

// The groups of cases, run by main() in test_ctl.cpp in this order
void run_json_tests();
void run_http_tests();
