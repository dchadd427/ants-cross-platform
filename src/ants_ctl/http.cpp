// The minimal HTTP/1.1 server of the control interface (see ants_ctl/http.hpp for what it promises). Plain non-blocking sockets with the same small
// layer as ants_net/tcp.cpp (Winsock or BSD sockets), polled from the main loop; every connection carries exactly one request.
#include "ants_ctl/http.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>
#include <vector>

#include "ants_ctl/json.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
using iolen_t = int;
static constexpr socket_t kBadSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using socket_t = int;
using iolen_t = size_t;
static constexpr socket_t kBadSocket = -1;
#endif

namespace ants::ctl {

namespace {

constexpr int kReadRounds = 8;                    // recv calls of 8 KB per connection and update (a flood is read a bit at a time)
constexpr int kMaxAcceptsPerUpdate = 128;
constexpr int kListenBacklog = 64;                // the connections beyond kMaxConnections are accepted (and closed) here, not left to time out
constexpr size_t kMaxExpectedLength = size_t{1} << 30;     // a Content-Length is clipped to this (only its being too large matters then)

#ifdef _WIN32
struct WinsockInit {
    WinsockInit() {
        WSADATA d;
        ok = WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }
    ~WinsockInit() {
        if (ok) WSACleanup();
    }
    bool ok{false};
};
void ensure_sockets() {
    static WinsockInit init;
    (void)init;
}
void close_socket(socket_t s) { closesocket(s); }
bool would_block() { return WSAGetLastError() == WSAEWOULDBLOCK; }
bool interrupted() { return false; }
constexpr int kShutdownSend = SD_SEND;
#else
void ensure_sockets() {}
void close_socket(socket_t s) { ::close(s); }
bool would_block() { return errno == EWOULDBLOCK || errno == EAGAIN; }
bool interrupted() { return errno == EINTR; }
constexpr int kShutdownSend = SHUT_WR;
#endif

bool set_nonblocking(socket_t s) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// A program started by the game must not inherit the sockets (the game starts none on Windows, where sockets are inheritable by default: nothing to do)
void set_no_inherit(socket_t s) {
#ifndef _WIN32
    fcntl(s, F_SETFD, FD_CLOEXEC);
#else
    (void)s;
#endif
}

void tune(socket_t s) {
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&one), sizeof(one));    // macOS: no SIGPIPE on a dead peer
#else
    (void)s;
#endif
}

// ---- text helpers ----

bool is_tchar(unsigned char c) {
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
    return c != 0 && std::strchr("!#$%&'*+-.^_`|~", static_cast<int>(c)) != nullptr;
}

bool all_tchar(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return is_tchar(static_cast<unsigned char>(c)); });
}

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lower(c);
    return out;
}

bool equals_ignore_case(std::string_view a, std::string_view lower_case_b) {
    if (a.size() != lower_case_b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower_case_b[i]) return false;
    }
    return true;
}

std::string_view trim_ows(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// The secret travels in a header: visible ASCII only (no space, no control character, nothing that a proxy or a parser might treat as a separator)
bool valid_secret(const std::string& s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        const unsigned char u = static_cast<unsigned char>(c);
        return u >= 0x21 && u <= 0x7E;
    });
}

// What may be put into a response header from a program's hands: visible ASCII and spaces, so no CR, LF or other control character can end the
// line and start another
bool safe_header_value(std::string_view s) {
    if (s.empty() || s.size() > 256) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        const unsigned char u = static_cast<unsigned char>(c);
        return u >= 0x20 && u <= 0x7E;
    });
}

const char* reason_phrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 417: return "Expectation Failed";
        case 422: return "Unprocessable Entity";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        case 505: return "HTTP Version Not Supported";
        default: return status < 400 ? "OK" : "Error";
    }
}

std::string error_body(const char* message) {
    JsonValue o = JsonValue::make_object();
    o.set("error", JsonValue::make_string(message));
    return to_json(o);
}

// One whole response. Everything in the header block is a literal of this file, a number, or the handler's content type after safe_header_value.
std::string build_response(int status, std::string_view content_type, const std::string& body, const char* extra_headers) {
    std::string out;
    out.reserve(body.size() + 256);
    out += "HTTP/1.1 ";
    out += std::to_string(status);
    out += ' ';
    out += reason_phrase(status);
    out += "\r\n";
    if (status != 204) {                                        // 204 has no body, no type and no length
        out += "Content-Type: ";
        out += content_type;
        out += "\r\nContent-Length: ";
        out += std::to_string(body.size());
        out += "\r\n";
    }
    out += "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n";
    out += extra_headers;
    out += "\r\n";
    if (status != 204) out += body;
    return out;
}

// ---- connections ----

enum class Phase : uint8_t {
    Head,         // reading the request line and the headers
    Body,         // the head is accepted, the body (Content-Length bytes) is still coming
    Writing,      // the answer is queued and is being written
    Lingering     // the answer is out and the sending side is shut: wait (reading and dropping) until the client closes
};

struct Conn {
    Conn(socket_t s, uint32_t now) : fd(s), accepted_ms(now), phase_ms(now) {}
    ~Conn() {
        if (fd != kBadSocket) close_socket(fd);
    }
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    socket_t fd;
    Phase phase{Phase::Head};
    uint32_t accepted_ms;
    uint32_t phase_ms;                // when the current phase after the request began (Writing, Lingering)
    std::string in;                   // bytes received and not yet used
    std::string out;                  // bytes to write (the interim 100 and then the answer)
    size_t out_pos{0};
    HttpHead head;
};

// Reads what is there (bounded). False on a hard error; `eof` says the client has closed its sending side.
bool read_available(socket_t s, std::string& in, bool& eof) {
    char buf[8192];
    for (int round = 0; round < kReadRounds; ++round) {
        const auto n = ::recv(s, buf, static_cast<iolen_t>(sizeof(buf)), 0);
        if (n > 0) {
            in.append(buf, static_cast<size_t>(n));
        } else if (n == 0) {
            eof = true;
            return true;
        } else if (would_block() || interrupted()) {
            return true;
        } else {
            return false;
        }
    }
    return true;
}

// Writes what the socket takes. False on a hard error (the client is gone).
bool flush(Conn& c) {
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif
    while (c.out_pos < c.out.size()) {
        const size_t chunk = std::min<size_t>(c.out.size() - c.out_pos, size_t{1} << 16);
        const auto n = ::send(c.fd, c.out.data() + c.out_pos, static_cast<iolen_t>(chunk), flags);
        if (n > 0) {
            c.out_pos += static_cast<size_t>(n);
        } else if (n == 0 || would_block() || interrupted()) {
            break;
        } else {
            return false;
        }
    }
    if (c.out_pos == c.out.size()) {
        c.out.clear();
        c.out_pos = 0;
    }
    return true;
}

// A request line or header ends with CRLF; a LF alone (which some programs treat as a line end and others do not) is how requests get smuggled
bool has_bare_lf(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n' && (i == 0 || s[i - 1] != '\r')) return true;
    }
    return false;
}

// "Authorization: Bearer <secret>", the scheme in any case, one or more spaces before the token. The comparison runs even when the scheme is wrong,
// so that every failure takes the same path.
bool authenticated(const HttpRequest& r, const std::string& secret) {
    const std::string* h = r.header("authorization");
    const std::string_view value = h != nullptr ? std::string_view(*h) : std::string_view();
    const bool scheme = value.size() > 6 && equals_ignore_case(value.substr(0, 6), "bearer") && value[6] == ' ';
    std::string_view token = scheme ? value.substr(6) : std::string_view();
    while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
    const bool equal = constant_time_equal(token, secret);
    return scheme && equal;
}

}  // namespace

// ------------------------------------------------------------------------------------------------

bool constant_time_equal(std::string_view a, std::string_view b) {
    const size_t n = std::max(a.size(), b.size());
    size_t diff = a.size() ^ b.size();
    for (size_t i = 0; i < n; ++i) {
        const unsigned char ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
        const unsigned char cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
        diff |= static_cast<size_t>(ca ^ cb);
    }
    return diff == 0;
}

int parse_http_head(std::string_view head, HttpHead& out, std::string* error) {
    out = HttpHead();
    const auto bad = [&](int status, const char* what) {
        if (error != nullptr) *error = what;
        return status;
    };
    if (head.size() > HttpServer::kMaxHeadBytes) return bad(431, "request head too large");
    if (head.size() < 4 || head.substr(head.size() - 4) != "\r\n\r\n") return bad(400, "incomplete request head");

    // No control characters (a tab is fine inside a header value), no CR or LF alone
    for (size_t i = 0; i < head.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(head[i]);
        if (c == '\r') {
            if (i + 1 >= head.size() || head[i + 1] != '\n') return bad(400, "bare CR in the request");
        } else if (c == '\n') {
            if (i == 0 || head[i - 1] != '\r') return bad(400, "bare LF in the request");
        } else if ((c < 0x20 && c != '\t') || c == 0x7F) {
            return bad(400, "control character in the request");
        }
    }

    // request line: method SP target SP version
    const size_t line_end = head.find("\r\n");
    const std::string_view line = head.substr(0, line_end);
    const size_t sp1 = line.find(' ');
    const size_t sp2 = sp1 == std::string_view::npos ? std::string_view::npos : line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos || line.find(' ', sp2 + 1) != std::string_view::npos) return bad(400, "malformed request line");
    const std::string_view method = line.substr(0, sp1);
    const std::string_view target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::string_view version = line.substr(sp2 + 1);
    if (method.size() > 32 || !all_tchar(method)) return bad(400, "malformed request line");
    const bool version_syntax = version.size() == 8 && version.substr(0, 5) == "HTTP/" && version[5] >= '0' && version[5] <= '9' && version[6] == '.' &&
                                version[7] >= '0' && version[7] <= '9';
    if (!version_syntax) return bad(400, "malformed request line");
    if (version != "HTTP/1.1" && version != "HTTP/1.0") return bad(505, "HTTP version not supported");
    if (target.empty() || target[0] != '/') return bad(400, "the request target must be an absolute path");
    for (const char ch : target) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (u < 0x21 || u > 0x7E || ch == '#') return bad(400, "invalid character in the request target");
    }
    out.request.method = std::string(method);
    const size_t question = target.find('?');
    out.request.path = std::string(target.substr(0, question));
    if (question != std::string_view::npos) out.request.query = std::string(target.substr(question + 1));

    // headers
    size_t pos = line_end + 2;
    const size_t end = head.size() - 2;                           // where the blank line begins
    size_t count = 0;
    while (pos < end) {
        const size_t eol = head.find("\r\n", pos);
        const std::string_view h = head.substr(pos, eol - pos);
        pos = eol + 2;
        if (h.empty()) return bad(400, "blank line inside the headers");
        if (h[0] == ' ' || h[0] == '\t') return bad(400, "folded header line");
        const size_t colon = h.find(':');
        if (colon == std::string_view::npos) return bad(400, "header line without a colon");
        const std::string_view name = h.substr(0, colon);
        if (!all_tchar(name)) return bad(400, "invalid header name");
        if (++count > HttpServer::kMaxHeaders) return bad(431, "too many headers");
        const std::string_view value = trim_ows(h.substr(colon + 1));
        const std::string key = to_lower(name);
        const auto it = out.request.headers.find(key);
        if (it == out.request.headers.end()) {
            out.request.headers.emplace(key, std::string(value));
        } else if (key == "content-length" || key == "transfer-encoding" || key == "authorization" || key == "host" || key == "expect") {
            return bad(400, "duplicate header");                  // two answers to one question: refuse rather than pick one
        } else {
            it->second += ", ";
            it->second += value;
        }
    }

    // how the body is framed
    const std::string* length = out.request.header("content-length");
    const std::string* coding = out.request.header("transfer-encoding");
    if (length != nullptr && coding != nullptr) return bad(400, "Content-Length together with Transfer-Encoding");
    if (length != nullptr) {
        if (length->empty() || !std::all_of(length->begin(), length->end(), [](char c) { return c >= '0' && c <= '9'; })) {
            return bad(400, "invalid Content-Length");
        }
        size_t first = 0;
        while (first + 1 < length->size() && (*length)[first] == '0') ++first;
        out.has_content_length = true;
        if (length->size() - first > 9) {                              // 10 digits are 1e9 and more: far above the limit, and not worth computing
            out.content_length = kMaxExpectedLength;
        } else {
            size_t v = 0;
            for (size_t i = first; i < length->size(); ++i) v = v * 10 + static_cast<size_t>((*length)[i] - '0');
            out.content_length = std::min(v, kMaxExpectedLength);
        }
        if (out.content_length > HttpServer::kMaxBodyBytes) {
            out.body_status = 413;
            out.body_error = "request body too large";
        }
    } else if (coding != nullptr) {
        out.body_status = 411;
        out.body_error = "chunked bodies are not supported, send Content-Length";
    } else if (out.request.method == "POST") {
        out.body_status = 411;
        out.body_error = "Content-Length required";
    }
    if (const std::string* expect = out.request.header("expect")) {
        if (equals_ignore_case(*expect, "100-continue")) {
            out.expect_continue = true;
        } else if (out.body_status == 0) {
            out.body_status = 417;
            out.body_error = "unsupported expectation";
        }
    }
    return 0;
}

// ------------------------------------------------------------------------------------------------

struct HttpServer::Impl {
    ~Impl() {
        conns.clear();
        if (listen_fd != kBadSocket) close_socket(listen_fd);
    }

    void accept_new(uint32_t now);
    bool evict_for_new_connection();
    bool step(Conn& c, uint32_t now, const Handler& handler);
    void advance_head(Conn& c, uint32_t now, const Handler& handler);
    void advance_body(Conn& c, uint32_t now, const Handler& handler);
    void dispatch(Conn& c, uint32_t now, const Handler& handler);
    void respond(Conn& c, uint32_t now, int status, std::string_view content_type, const std::string& body, const char* extra_headers = "");
    void respond_error(Conn& c, uint32_t now, int status, const char* message, const char* extra_headers = "");

    socket_t listen_fd{kBadSocket};
    uint16_t port{0};
    std::string secret;
    bool read_only{false};                                        // listen_public(): no secret, GET only, no body
    uint32_t request_timeout_ms{HttpServer::kRequestTimeoutMs};
    int send_buffer_bytes{0};
    std::vector<std::unique_ptr<Conn>> conns;
};

void HttpServer::Impl::respond(Conn& c, uint32_t now, int status, std::string_view content_type, const std::string& body, const char* extra_headers) {
    c.out += build_response(status, content_type, body, extra_headers);
    c.in.clear();
    c.phase = Phase::Writing;
    c.phase_ms = now;
}

void HttpServer::Impl::respond_error(Conn& c, uint32_t now, int status, const char* message, const char* extra_headers) {
    respond(c, now, status, "application/json", error_body(message), extra_headers);
}

// True when the peer has sent something that this server has not read yet
bool has_unread_input(socket_t fd) {
    char byte;
    return ::recv(fd, &byte, 1, MSG_PEEK) > 0;
}

// When every slot is taken, a new connection wins the slot of the OLDEST connection that is waiting for its request head and has nothing unread on its socket
// (a real client delivers its request within milliseconds of connecting, so a connection that has sent nothing, or only the beginning of a head, is idle or
// hostile; a request that has arrived but has not been read yet, for instance one accepted in the same pass, is NOT idle), or else of the oldest one that is only
// waiting for its client to close after an answer that was delivered. Connections that are being answered or whose body is arriving are never evicted.
// False when there is nothing to evict. (Without this, a program that opens 32 silent connections would lock the interface out for good.)
bool HttpServer::Impl::evict_for_new_connection() {
    for (const Phase wanted : {Phase::Head, Phase::Lingering}) {
        for (size_t i = 0; i < conns.size(); ++i) {                // (the vector is in the order of acceptance: the first one found is the oldest)
            if (conns[i]->phase != wanted) continue;
            if (wanted == Phase::Head && has_unread_input(conns[i]->fd)) continue;
            conns.erase(conns.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

void HttpServer::Impl::accept_new(uint32_t now) {
    for (int n = 0; n < kMaxAcceptsPerUpdate; ++n) {
        const socket_t s = ::accept(listen_fd, nullptr, nullptr);
        if (s == kBadSocket) break;                               // nothing waiting (or an error that the next update will meet again)
        if ((conns.size() >= HttpServer::kMaxConnections && !evict_for_new_connection()) || !set_nonblocking(s)) {
            close_socket(s);                                      // too many and none that may give way: closed at once, without an answer
            continue;
        }
        set_no_inherit(s);
        tune(s);
        if (send_buffer_bytes > 0) {
            setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&send_buffer_bytes), sizeof(send_buffer_bytes));
        }
        conns.push_back(std::make_unique<Conn>(s, now));
    }
}

void HttpServer::Impl::advance_head(Conn& c, uint32_t now, const Handler& handler) {
    const size_t end = c.in.find("\r\n\r\n");
    if (end == std::string::npos) {
        if (c.in.size() >= HttpServer::kMaxHeadBytes) return respond_error(c, now, 431, "request head too large");
        if (has_bare_lf(c.in)) return respond_error(c, now, 400, "bare LF in the request");
        return;                                                   // the rest has not arrived yet
    }
    const size_t head_len = end + 4;
    std::string error;
    HttpHead head;
    const int status = parse_http_head(std::string_view(c.in).substr(0, head_len), head, &error);
    if (status != 0) return respond_error(c, now, status, error.c_str());
    c.head = std::move(head);
    c.in.erase(0, head_len);                                      // what is left is the body, and then whatever follows the request (ignored)

    const HttpRequest& r = c.head.request;
    const bool health = r.method == "GET" && r.path == "/healthz";
    if (read_only) {                                              // the public door: nobody is asked for a secret, and nothing but a plain GET gets as far as the handler
        if (health) return respond(c, now, 200, "application/json", "{\"ok\":true}");
        if (r.method != "GET") return respond_error(c, now, 405, "method not allowed", "Allow: GET\r\n");
        if (c.head.body_status != 0) return respond_error(c, now, c.head.body_status, c.head.body_error);
        if (c.head.has_content_length && c.head.content_length > 0) return respond_error(c, now, 400, "a request here has no body");
        return dispatch(c, now, handler);
    }
    if (!health && !authenticated(r, secret)) return respond_error(c, now, 401, "unauthorized", "WWW-Authenticate: Bearer\r\n");
    if (health) return respond(c, now, 200, "application/json", "{\"ok\":true}");
    if (r.method != "GET" && r.method != "POST" && r.method != "DELETE") {
        return respond_error(c, now, 405, "method not allowed", "Allow: GET, POST, DELETE\r\n");
    }
    if (c.head.body_status != 0) return respond_error(c, now, c.head.body_status, c.head.body_error);

    if (c.head.has_content_length && c.head.content_length > 0) {
        c.phase = Phase::Body;
        if (c.head.expect_continue && c.in.size() < c.head.content_length) c.out += "HTTP/1.1 100 Continue\r\n\r\n";
        return;
    }
    dispatch(c, now, handler);
}

void HttpServer::Impl::advance_body(Conn& c, uint32_t now, const Handler& handler) {
    if (c.in.size() < c.head.content_length) return;
    c.head.request.body.assign(c.in, 0, c.head.content_length);
    dispatch(c, now, handler);                                    // c.in is cleared there: a second request behind the first is ignored
}

void HttpServer::Impl::dispatch(Conn& c, uint32_t now, const Handler& handler) {
    HttpResponse response;
    bool ok = true;
    try {
        response = handler(c.head.request);                       // an empty handler throws std::bad_function_call
    } catch (...) {
        ok = false;
    }
    if (!ok || response.status < 200 || response.status > 599 || response.body.size() > HttpServer::kMaxResponseBytes) {
        return respond_error(c, now, 500, "internal error");
    }
    const std::string_view type = safe_header_value(response.content_type) ? std::string_view(response.content_type) : std::string_view("application/json");
    respond(c, now, response.status, type, response.body);
}

// One connection's turn. False when it is finished (or lost) and must be dropped.
bool HttpServer::Impl::step(Conn& c, uint32_t now, const Handler& handler) {
    if (c.phase == Phase::Head || c.phase == Phase::Body) {
        bool eof = false;
        if (!read_available(c.fd, c.in, eof)) return false;
        if (c.phase == Phase::Head) advance_head(c, now, handler);
        if (c.phase == Phase::Body) advance_body(c, now, handler);
        if (c.phase == Phase::Head || c.phase == Phase::Body) {
            if (eof) return false;                                // the client left before it finished: nobody to answer
            if (static_cast<uint32_t>(now - c.accepted_ms) >= request_timeout_ms) {
                respond_error(c, now, 408, "request timeout");
            } else if (!flush(c)) {                               // the interim "100 Continue"
                return false;
            }
        }
    }
    if (c.phase == Phase::Writing) {
        if (!flush(c)) return false;
        if (c.out.empty()) {
            ::shutdown(c.fd, kShutdownSend);                      // the client sees the end of the answer now, and closes its side in turn
            c.phase = Phase::Lingering;
            c.phase_ms = now;
        } else if (static_cast<uint32_t>(now - c.phase_ms) >= HttpServer::kWriteTimeoutMs) {
            return false;                                         // a client that does not take its answer
        }
    }
    if (c.phase == Phase::Lingering) {
        // Closing a socket that still has unread data makes the system send a reset, which can wipe the answer out of the client's buffer before
        // the client has read it (a pipelined second request is exactly such data). So the server reads and drops until the client closes.
        char buf[4096];
        for (int round = 0; round < kReadRounds; ++round) {
            const auto n = ::recv(c.fd, buf, static_cast<iolen_t>(sizeof(buf)), 0);
            if (n > 0) continue;
            if (n == 0) return false;                             // the client has closed: done
            if (would_block() || interrupted()) break;
            return false;
        }
        if (static_cast<uint32_t>(now - c.phase_ms) >= HttpServer::kLingerMs) return false;
    }
    return true;
}

// ------------------------------------------------------------------------------------------------

HttpServer::HttpServer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

HttpServer::~HttpServer() = default;

std::unique_ptr<HttpServer> HttpServer::listen(uint16_t port, std::string bearer_secret, bool loopback_only) {
    if (!valid_secret(bearer_secret)) return nullptr;             // no secret, no server
    return open(port, std::move(bearer_secret), loopback_only, false);
}

std::unique_ptr<HttpServer> HttpServer::listen_public(uint16_t port, bool loopback_only) { return open(port, std::string(), loopback_only, true); }

std::unique_ptr<HttpServer> HttpServer::open(uint16_t port, std::string bearer_secret, bool loopback_only, bool read_only) {
    ensure_sockets();
    const socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == kBadSocket) return nullptr;
    int one = 1;
#ifdef _WIN32
    // SO_REUSEADDR on Windows would let another program bind the same port and take the requests: the port is taken exclusively
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&one), sizeof(one));
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));     // a restart does not wait for TIME_WAIT
#endif
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(loopback_only ? INADDR_LOOPBACK : INADDR_ANY);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(s, kListenBacklog) != 0 || !set_nonblocking(s)) {
        close_socket(s);
        return nullptr;
    }
    set_no_inherit(s);
    sockaddr_in bound;
    socklen_t len = sizeof(bound);
    uint16_t actual = port;
    if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) == 0) actual = ntohs(bound.sin_port);

    auto impl = std::make_unique<Impl>();
    impl->listen_fd = s;
    impl->port = actual;
    impl->secret = std::move(bearer_secret);
    impl->read_only = read_only;
    return std::unique_ptr<HttpServer>(new HttpServer(std::move(impl)));
}

uint16_t HttpServer::port() const { return impl_->port; }

size_t HttpServer::connection_count() const { return impl_->conns.size(); }

void HttpServer::set_request_timeout_ms(uint32_t ms) { impl_->request_timeout_ms = ms; }

void HttpServer::set_send_buffer_bytes(int bytes) { impl_->send_buffer_bytes = bytes; }

void HttpServer::update(uint32_t now_ms, const std::function<HttpResponse(const HttpRequest&)>& handler) {
    impl_->accept_new(now_ms);
    for (size_t i = 0; i < impl_->conns.size();) {
        if (impl_->step(*impl_->conns[i], now_ms, handler)) {
            ++i;
        } else {
            impl_->conns.erase(impl_->conns.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

}  // namespace ants::ctl
