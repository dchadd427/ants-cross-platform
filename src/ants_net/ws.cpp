// WebSocket server transport: the SHA-1 / base64 of the handshake, the frame codec, the HTTP upgrade, and the non-blocking sockets (see ants_net/ws.hpp for
// the rules and for the proxy setup). The socket layer is the same small one as tcp.cpp and lan.cpp (Winsock or BSD sockets, non-blocking, nothing is
// waited for, no SIGPIPE).
#include "ants_net/ws.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
using addrlen_t = int;
using iolen_t = int;
static constexpr socket_t kBadSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using socket_t = int;
using addrlen_t = socklen_t;
using iolen_t = size_t;
static constexpr socket_t kBadSocket = -1;
#endif

namespace ants::net {

namespace {

constexpr char kWsGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";    // RFC 6455 section 1.3
constexpr size_t kMaxHeaders = 64;                                   // header lines of a handshake request (8 KB of request cannot hold many more)
constexpr int kMaxAcceptsPerCall = 16;                               // sockets taken in per accept(): a flood is taken in a bit at a time
constexpr size_t kMaxClosing = 64;                                   // refused sockets that are still being read empty
constexpr uint32_t kClosingLingerMs = 1000;                          // ... for at most this long
constexpr size_t kMaxInboxMessages = 4096;                           // received messages waiting to be polled: more and the socket is not read
constexpr size_t kMaxInboxBytes = 1024 * 1024;                       // (the sender is held back by TCP, as it should be)

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
bool interrupted() { return WSAGetLastError() == WSAEINTR; }
constexpr int kShutdownSend = SD_SEND;
#else
void ensure_sockets() {}
void close_socket(socket_t s) { ::close(s); }
bool would_block() { return errno == EWOULDBLOCK || errno == EAGAIN; }
bool interrupted() { return errno == EINTR; }
constexpr int kShutdownSend = SHUT_WR;
#endif

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;                           // Linux: no SIGPIPE on a dead peer
#else
constexpr int kSendFlags = 0;
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

void tune(socket_t s) {
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));    // small messages, low latency
#ifdef SO_NOSIGPIPE
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&one), sizeof(one));    // macOS: no SIGPIPE on a dead peer
#endif
}

std::string address_text(const sockaddr* sa) {
    char buf[64] = {0};
    if (sa->sa_family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(sa);
        inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(ntohs(in->sin_port));
    }
    if (sa->sa_family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(sa);
        inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof(buf));
        return std::string("[") + buf + "]:" + std::to_string(ntohs(in6->sin6_port));
    }
    return "?";
}

uint64_t now_ms() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Reads whatever is waiting and throws it away (so that closing the socket does not reset the connection and with it the answer still on its way)
void discard_input(socket_t s) {
    uint8_t scratch[4096];
    for (int i = 0; i < 16; ++i) {
        const auto n = ::recv(s, reinterpret_cast<char*>(scratch), static_cast<iolen_t>(sizeof(scratch)), 0);
        if (n <= 0) break;
    }
}

// Appends one frame to `out` (the server never masks; the tests' client does)
void append_frame(std::vector<uint8_t>& out, WsOpcode opcode, const uint8_t* payload, size_t size, bool fin, const std::array<uint8_t, 4>* mask) {
    // Grow with slack: reserve(size + n) on every call reallocates and copies the WHOLE backlog each time (libc++ allocates exactly what is asked), so a peer that
    // never reads and makes the server answer many tiny frames (pings) would cost time that grows with the square of the backlog
    const size_t need = out.size() + size + 14;
    if (out.capacity() < need) out.reserve(std::max(need, out.capacity() * 2));
    out.push_back(static_cast<uint8_t>((fin ? 0x80u : 0x00u) | static_cast<uint8_t>(opcode)));
    const uint8_t mask_bit = mask != nullptr ? 0x80u : 0x00u;
    if (size < 126) {
        out.push_back(static_cast<uint8_t>(mask_bit | size));
    } else if (size <= 0xFFFFu) {
        out.push_back(static_cast<uint8_t>(mask_bit | 126u));
        out.push_back(static_cast<uint8_t>((size >> 8) & 0xFFu));
        out.push_back(static_cast<uint8_t>(size & 0xFFu));
    } else {
        out.push_back(static_cast<uint8_t>(mask_bit | 127u));
        const uint64_t wide = static_cast<uint64_t>(size);
        for (int shift = 56; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>((wide >> shift) & 0xFFu));
    }
    if (mask != nullptr) {
        out.insert(out.end(), mask->begin(), mask->end());
        for (size_t i = 0; i < size; ++i) out.push_back(static_cast<uint8_t>(payload[i] ^ (*mask)[i & 3u]));
    } else if (size > 0) {
        out.insert(out.end(), payload, payload + size);
    }
}

// ------------------------------------------------------------------------------------------------
// HTTP helpers

const char* reason_phrase(int status) {
    switch (status) {
        case 101: return "Switching Protocols";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 426: return "Upgrade Required";
        case 431: return "Request Header Fields Too Large";
        default: return "Error";
    }
}

// The answer to a refused handshake: no body, and the connection ends
std::string http_error_response(int status) {
    std::string r = "HTTP/1.1 " + std::to_string(status) + " " + reason_phrase(status) + "\r\n";
    if (status == 405) r += "Allow: GET\r\n";
    if (status == 426) r += "Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n";
    r += "Connection: close\r\nContent-Length: 0\r\n\r\n";
    return r;
}

bool is_token_char(unsigned char c) {
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
    return c != 0 && std::strchr("!#$%&'*+-.^_`|~", static_cast<int>(c)) != nullptr;
}

std::string to_lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}

// The comma separated items of a header value, trimmed, empty ones left out
std::vector<std::string> list_items(const std::string& value) {
    std::vector<std::string> items;
    size_t pos = 0;
    while (pos <= value.size()) {
        size_t comma = value.find(',', pos);
        if (comma == std::string::npos) comma = value.size();
        const std::string item = trim(value.substr(pos, comma - pos));
        if (!item.empty()) items.push_back(item);
        pos = comma + 1;
    }
    return items;
}

bool has_token(const std::string& value, const char* token_lower) {
    for (const std::string& item : list_items(value)) {
        if (to_lower(item) == token_lower) return true;
    }
    return false;
}

struct HttpHeader {
    std::string name;       // lower case
    std::string value;      // trimmed
};

size_t header_count(const std::vector<HttpHeader>& headers, const char* name) {
    size_t n = 0;
    for (const HttpHeader& h : headers) n += h.name == name ? 1u : 0u;
    return n;
}

// The values of every header of that name, joined like HTTP joins a repeated list header
std::string header_joined(const std::vector<HttpHeader>& headers, const char* name) {
    std::string out;
    for (const HttpHeader& h : headers) {
        if (h.name != name) continue;
        if (!out.empty()) out += ",";
        out += h.value;
    }
    return out;
}

WsHandshakeResult rejected(int status, size_t consumed) {
    WsHandshakeResult r;
    r.status = WsHandshakeResult::Status::Rejected;
    r.http_status = status;
    r.response = http_error_response(status);
    r.consumed = consumed;
    return r;
}

}  // namespace

// ------------------------------------------------------------------------------------------------
// Primitives

std::array<uint8_t, 20> ws_sha1(const uint8_t* data, size_t size) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    const auto rol = [](uint32_t v, unsigned n) { return (v << n) | (v >> (32u - n)); };
    const auto block = [&h, &rol](const uint8_t* p) {
        uint32_t w[80];
        for (size_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(p[4 * i]) << 24) | (static_cast<uint32_t>(p[4 * i + 1]) << 16) | (static_cast<uint32_t>(p[4 * i + 2]) << 8) |
                   static_cast<uint32_t>(p[4 * i + 3]);
        }
        for (size_t i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0];
        uint32_t b = h[1];
        uint32_t c = h[2];
        uint32_t d = h[3];
        uint32_t e = h[4];
        for (size_t i = 0; i < 80; ++i) {
            uint32_t f = 0;
            uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    };
    const size_t whole = size / 64;
    for (size_t i = 0; i < whole; ++i) block(data + 64 * i);
    // the rest, a 1 bit, zeros, and the length in bits (64 bit, big endian) at the end of the last block
    uint8_t tail[128];
    std::memset(tail, 0, sizeof(tail));
    const size_t rest = size - whole * 64;
    if (rest > 0) std::memcpy(tail, data + whole * 64, rest);
    tail[rest] = 0x80;
    const size_t tail_size = rest < 56 ? 64 : 128;
    const uint64_t bits = static_cast<uint64_t>(size) * 8u;
    for (size_t i = 0; i < 8; ++i) tail[tail_size - 1 - i] = static_cast<uint8_t>((bits >> (8 * i)) & 0xFFu);
    block(tail);
    if (tail_size == 128) block(tail + 64);
    std::array<uint8_t, 20> out{};
    for (size_t i = 0; i < 5; ++i) {
        out[4 * i] = static_cast<uint8_t>(h[i] >> 24);
        out[4 * i + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFFu);
        out[4 * i + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFFu);
        out[4 * i + 3] = static_cast<uint8_t>(h[i] & 0xFFu);
    }
    return out;
}

namespace {
constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
}  // namespace

std::string ws_base64_encode(const uint8_t* data, size_t size) {
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 3 <= size; i += 3) {
        const uint32_t v = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) | static_cast<uint32_t>(data[i + 2]);
        out.push_back(kBase64[(v >> 18) & 63u]);
        out.push_back(kBase64[(v >> 12) & 63u]);
        out.push_back(kBase64[(v >> 6) & 63u]);
        out.push_back(kBase64[v & 63u]);
    }
    if (size - i == 1) {
        const uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(kBase64[(v >> 18) & 63u]);
        out.push_back(kBase64[(v >> 12) & 63u]);
        out += "==";
    } else if (size - i == 2) {
        const uint32_t v = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(kBase64[(v >> 18) & 63u]);
        out.push_back(kBase64[(v >> 12) & 63u]);
        out.push_back(kBase64[(v >> 6) & 63u]);
        out.push_back('=');
    }
    return out;
}

bool ws_base64_decode(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    if (text.size() % 4 != 0) return false;
    for (size_t q = 0; q < text.size(); q += 4) {
        const bool last = q + 4 == text.size();
        int v[4];
        int pad = 0;
        for (size_t k = 0; k < 4; ++k) {
            const char c = text[q + k];
            if (c == '=') {
                if (!last || k < 2) return false;                   // padding only closes the last group, after at least two digits
                ++pad;
                v[k] = 0;
            } else {
                if (pad > 0) return false;                          // a digit after padding
                v[k] = base64_value(c);
                if (v[k] < 0) return false;
            }
        }
        const uint32_t bits = (static_cast<uint32_t>(v[0]) << 18) | (static_cast<uint32_t>(v[1]) << 12) | (static_cast<uint32_t>(v[2]) << 6) |
                              static_cast<uint32_t>(v[3]);
        out.push_back(static_cast<uint8_t>((bits >> 16) & 0xFFu));
        if (pad < 2) out.push_back(static_cast<uint8_t>((bits >> 8) & 0xFFu));
        if (pad < 1) out.push_back(static_cast<uint8_t>(bits & 0xFFu));
        // the bits that the padding leaves unused must be zero: one text has one encoding
        if (pad == 2 && (bits & 0xFFFFu) != 0) return false;
        if (pad == 1 && (bits & 0xFFu) != 0) return false;
    }
    return true;
}

std::string ws_accept_key(const std::string& client_key) {
    const std::string joined = client_key + kWsGuid;
    const auto digest = ws_sha1(reinterpret_cast<const uint8_t*>(joined.data()), joined.size());
    return ws_base64_encode(digest.data(), digest.size());
}

// ------------------------------------------------------------------------------------------------
// Frame codec

std::vector<uint8_t> ws_encode_frame(WsOpcode opcode, const uint8_t* payload, size_t size, bool fin, const std::array<uint8_t, 4>* mask) {
    std::vector<uint8_t> out;
    append_frame(out, opcode, payload, size, fin, mask);
    return out;
}

std::vector<uint8_t> ws_encode_frame(WsOpcode opcode, const std::vector<uint8_t>& payload, bool fin, const std::array<uint8_t, 4>* mask) {
    return ws_encode_frame(opcode, payload.data(), payload.size(), fin, mask);
}

std::vector<uint8_t> ws_close_payload(uint16_t status, const std::string& reason) {
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(status >> 8));
    out.push_back(static_cast<uint8_t>(status & 0xFFu));
    out.insert(out.end(), reason.begin(), reason.end());
    return out;
}

bool ws_valid_close_status(uint16_t status) {
    if (status >= 1000 && status <= 1014) return status != 1004 && status != 1005 && status != 1006;
    return status >= 3000 && status <= 4999;
}

bool ws_valid_utf8(const uint8_t* data, size_t size) {
    size_t i = 0;
    while (i < size) {
        const uint8_t c = data[i];
        if (c < 0x80) {
            ++i;
            continue;
        }
        size_t len = 0;
        uint32_t smallest = 0;
        uint32_t cp = 0;
        if ((c & 0xE0u) == 0xC0u) {
            len = 2;
            smallest = 0x80;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0u) == 0xE0u) {
            len = 3;
            smallest = 0x800;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8u) == 0xF0u) {
            len = 4;
            smallest = 0x10000;
            cp = c & 0x07u;
        } else {
            return false;                                       // a continuation byte alone, or 0xF8 and up
        }
        if (size - i < len) return false;
        for (size_t k = 1; k < len; ++k) {
            if ((data[i + k] & 0xC0u) != 0x80u) return false;
            cp = (cp << 6) | (data[i + k] & 0x3Fu);
        }
        if (cp < smallest || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return false;    // overlong, too big, a surrogate
        i += len;
    }
    return true;
}

WsFrameParser::WsFrameParser(bool from_client, size_t max_message) : from_client_(from_client), max_message_(max_message) {}

void WsFrameParser::feed(const uint8_t* data, size_t size) {
    if (done_ || size == 0) return;
    if (pos_ > 0) {                                             // what was consumed goes first, so that the buffer holds one frame at most
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(pos_));
        pos_ = 0;
    }
    buf_.insert(buf_.end(), data, data + size);
}

WsFrameParser::Result WsFrameParser::fail(uint16_t status_code, uint16_t& status) {
    done_ = true;
    error_ = true;
    error_status_ = status_code;
    status = status_code;
    std::vector<uint8_t>().swap(buf_);                          // nothing more is kept for a connection that is over
    std::vector<uint8_t>().swap(message_);
    pos_ = 0;
    return Result::Error;
}

void WsFrameParser::compact() {
    if (pos_ == buf_.size()) {
        buf_.clear();
        pos_ = 0;
    } else if (pos_ >= 65536) {
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(pos_));
        pos_ = 0;
    }
}

WsFrameParser::Result WsFrameParser::next(std::vector<uint8_t>& payload, uint16_t& status) {
    for (;;) {
        if (done_) {
            if (error_) {
                status = error_status_;
                return Result::Error;
            }
            return Result::NeedMore;
        }
        const size_t avail = buf_.size() - pos_;
        if (avail < 2) return Result::NeedMore;
        const uint8_t* p = buf_.data() + pos_;
        const bool fin = (p[0] & 0x80u) != 0;
        const uint8_t opcode = p[0] & 0x0Fu;
        const bool masked = (p[1] & 0x80u) != 0;
        uint64_t length = p[1] & 0x7Fu;

        // Everything that the first two bytes already decide is decided now, before more of the frame is waited for
        if ((p[0] & 0x70u) != 0) return fail(kWsCloseProtocolError, status);          // reserved bits: no extension was negotiated
        const bool control = (opcode & 0x08u) != 0;
        if (control) {
            if (opcode > static_cast<uint8_t>(WsOpcode::Pong) || opcode < static_cast<uint8_t>(WsOpcode::Close)) return fail(kWsCloseProtocolError, status);
            if (!fin || length > kWsMaxControlBytes) return fail(kWsCloseProtocolError, status);    // never fragmented, at most 125 bytes
        } else if (opcode == static_cast<uint8_t>(WsOpcode::Text)) {
            return fail(kWsCloseUnsupportedData, status);                              // the protocol is binary only
        } else if (opcode == static_cast<uint8_t>(WsOpcode::Binary)) {
            if (fragmenting_) return fail(kWsCloseProtocolError, status);              // a new message inside an unfinished one
        } else if (opcode == static_cast<uint8_t>(WsOpcode::Continuation)) {
            if (!fragmenting_) return fail(kWsCloseProtocolError, status);             // a continuation of nothing
        } else {
            return fail(kWsCloseProtocolError, status);                                // reserved data opcodes 3 - 7
        }
        if (masked != from_client_) return fail(kWsCloseProtocolError, status);        // a client masks every frame, a server none

        size_t header = 2;
        if (length == 126) header += 2;
        else if (length == 127) header += 8;
        if (masked) header += 4;
        if (avail < header) return Result::NeedMore;
        if (length == 126) {
            length = (static_cast<uint64_t>(p[2]) << 8) | p[3];
            if (length < 126) return fail(kWsCloseProtocolError, status);              // the shortest encoding of a length is the only one
        } else if (length == 127) {
            length = 0;
            for (size_t i = 0; i < 8; ++i) length = (length << 8) | p[2 + i];
            if ((length >> 63) != 0 || length < 65536) return fail(kWsCloseProtocolError, status);
        }
        // the length is judged before a byte is buffered or allocated for it: the whole message must fit
        if (!control && length > max_message_ - message_.size()) return fail(kWsCloseMessageTooBig, status);
        const size_t size = static_cast<size_t>(length);
        if (avail - header < size) return Result::NeedMore;

        const uint8_t* body = p + header;
        const uint8_t* key = masked ? p + header - 4 : nullptr;
        std::vector<uint8_t>* target = &payload;
        size_t at = 0;
        if (!control) {
            target = &message_;
            at = message_.size();
            message_.resize(at + size);
        } else {
            payload.resize(size);
        }
        uint8_t* dst = target->data() + at;
        if (key != nullptr) {
            for (size_t i = 0; i < size; ++i) dst[i] = static_cast<uint8_t>(body[i] ^ key[i & 3u]);
        } else if (size > 0) {
            std::memcpy(dst, body, size);
        }
        pos_ += header + size;
        compact();

        if (!control) {
            fragmenting_ = !fin;
            if (!fin) continue;                                                         // the rest of the message is still to come
            payload = std::move(message_);
            message_.clear();
            status = 0;
            return Result::Message;
        }
        switch (static_cast<WsOpcode>(opcode)) {
            case WsOpcode::Ping:
                status = 0;
                return Result::Ping;
            case WsOpcode::Pong:
                status = 0;
                return Result::Pong;
            default:
                break;                                                                  // Close (the opcode was checked above)
        }
        if (size == 0) {
            status = kWsCloseNoStatus;
        } else if (size == 1) {
            return fail(kWsCloseProtocolError, status);                                 // half a status
        } else {
            const uint16_t code = static_cast<uint16_t>((payload[0] << 8) | payload[1]);
            if (!ws_valid_close_status(code)) return fail(kWsCloseProtocolError, status);
            if (!ws_valid_utf8(payload.data() + 2, size - 2)) return fail(kWsCloseInvalidPayload, status);
            status = code;
            payload.erase(payload.begin(), payload.begin() + 2);                        // what is left is the reason
        }
        done_ = true;
        std::vector<uint8_t>().swap(buf_);
        std::vector<uint8_t>().swap(message_);
        pos_ = 0;
        return Result::Close;
    }
}

// ------------------------------------------------------------------------------------------------
// Handshake

WsHandshakeResult ws_parse_handshake(const std::string& request, const WsServerOptions& options) {
    const size_t blank = request.find("\r\n\r\n");
    const size_t scanned = blank == std::string::npos ? request.size() : blank + 4;

    // The bytes so far must look like HTTP already: CRLF line ends only (a lone CR at the very end may still get its LF), no control characters
    for (size_t i = 0; i < scanned; ++i) {
        const unsigned char c = static_cast<unsigned char>(request[i]);
        if (c == '\n') {
            if (i == 0 || request[i - 1] != '\r') return rejected(400, scanned);
        } else if (c == '\r') {
            if (i + 1 < request.size() && request[i + 1] != '\n') return rejected(400, scanned);
        } else if ((c < 0x20 && c != '\t') || c == 0x7F) {
            return rejected(400, scanned);
        }
    }
    if (blank == std::string::npos) {
        if (request.size() >= kWsMaxRequestBytes) return rejected(431, scanned);        // no blank line within 8 KB, and none can come within them now
        return WsHandshakeResult();                                                      // NeedMore
    }
    if (scanned > kWsMaxRequestBytes) return rejected(431, scanned);
    const size_t consumed = scanned;

    // The lines: request line, then "Name: value" lines up to the blank line
    const std::string head = request.substr(0, blank);
    std::vector<std::string> lines;
    for (size_t pos = 0;;) {
        const size_t eol = head.find("\r\n", pos);
        if (eol == std::string::npos) {
            lines.push_back(head.substr(pos));
            break;
        }
        lines.push_back(head.substr(pos, eol - pos));
        pos = eol + 2;
    }
    // request line: METHOD SP target SP version, exactly
    const std::string& first = lines[0];
    const size_t sp1 = first.find(' ');
    const size_t sp2 = sp1 == std::string::npos ? std::string::npos : first.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos || sp1 == 0 || sp2 == sp1 + 1 || first.find(' ', sp2 + 1) != std::string::npos) {
        return rejected(400, consumed);
    }
    const std::string method = first.substr(0, sp1);
    const std::string target = first.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::string version = first.substr(sp2 + 1);
    for (char c : method) {
        if (!is_token_char(static_cast<unsigned char>(c))) return rejected(400, consumed);
    }
    if (method != "GET") return rejected(405, consumed);
    if (version != "HTTP/1.1") return rejected(400, consumed);
    if (target[0] != '/') return rejected(400, consumed);                               // origin form only: what a proxy sends

    std::vector<HttpHeader> headers;
    for (size_t i = 1; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.empty() || line[0] == ' ' || line[0] == '\t') return rejected(400, consumed);    // an empty line cannot be inside, obsolete folding is refused
        const size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0) return rejected(400, consumed);
        for (size_t k = 0; k < colon; ++k) {
            if (!is_token_char(static_cast<unsigned char>(line[k]))) return rejected(400, consumed);    // also catches a space before the colon
        }
        if (headers.size() >= kMaxHeaders) return rejected(431, consumed);
        headers.push_back({to_lower(line.substr(0, colon)), trim(line.substr(colon + 1))});
    }

    // The path (the query is not part of it)
    if (!options.path.empty()) {
        const size_t cut = target.find_first_of("?#");
        if (target.substr(0, cut) != options.path) return rejected(404, consumed);
    }
    // The origin: only a page of a listed site may open the connection from a browser (other clients send no Origin)
    if (!options.allowed_origins.empty() && header_count(headers, "origin") > 0) {
        bool listed = false;
        if (header_count(headers, "origin") == 1) {
            const std::string origin = to_lower(header_joined(headers, "origin"));
            for (const std::string& allowed : options.allowed_origins) listed = listed || to_lower(allowed) == origin;
        }
        if (!listed) return rejected(403, consumed);
    }
    if (header_count(headers, "host") != 1 || header_joined(headers, "host").empty()) return rejected(400, consumed);
    // a request that has a body is not a handshake (and a body would be taken for frames)
    if (header_count(headers, "transfer-encoding") > 0) return rejected(400, consumed);
    if (header_count(headers, "content-length") > 0 && header_joined(headers, "content-length") != "0") return rejected(400, consumed);

    if (!has_token(header_joined(headers, "upgrade"), "websocket")) return rejected(426, consumed);
    if (!has_token(header_joined(headers, "connection"), "upgrade")) return rejected(400, consumed);
    if (header_count(headers, "sec-websocket-version") > 1) return rejected(400, consumed);
    if (header_joined(headers, "sec-websocket-version") != "13") return rejected(426, consumed);
    if (header_count(headers, "sec-websocket-key") != 1) return rejected(400, consumed);
    const std::string key = header_joined(headers, "sec-websocket-key");
    std::vector<uint8_t> raw_key;
    if (!ws_base64_decode(key, raw_key) || raw_key.size() != 16) return rejected(400, consumed);

    WsHandshakeResult r;
    r.status = WsHandshakeResult::Status::Accepted;
    r.http_status = 101;
    r.consumed = consumed;
    for (const std::string& item : list_items(header_joined(headers, "sec-websocket-protocol"))) r.subprotocol = r.subprotocol || item == "ants";
    r.response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + ws_accept_key(key) + "\r\n";
    if (r.subprotocol) r.response += "Sec-WebSocket-Protocol: ants\r\n";
    r.response += "\r\n";
    return r;
}

// ------------------------------------------------------------------------------------------------
// WsConnection

WsConnection::WsConnection(int fd, std::string peer, uint32_t ping_interval_ms, const std::string& answer, const std::string& early_bytes)
    : fd_(fd), peer_(std::move(peer)), ping_interval_ms_(ping_interval_ms), last_send_ms_(now_ms()), parser_(true) {
    out_.assign(answer.begin(), answer.end());              // the 101 goes out before anything the game sends
    if (!early_bytes.empty()) parser_.feed(reinterpret_cast<const uint8_t*>(early_bytes.data()), early_bytes.size());   // frames that came with the request
}

WsConnection::~WsConnection() { close(); }

void WsConnection::release_socket() {
    if (fd_ < 0) return;
    const socket_t s = static_cast<socket_t>(fd_);
    discard_input(s);
    close_socket(s);
    fd_ = -1;
}

void WsConnection::fail() {
    release_socket();
    state_ = State::Failed;
    out_.clear();
    out_pos_ = 0;
}

void WsConnection::finish(State end_state) {
    release_socket();
    state_ = end_state;
}

bool WsConnection::enqueue(WsOpcode opcode, const uint8_t* data, size_t size) {
    if (fd_ < 0) return false;
    // a peer that never reads must not make the queue grow without bound
    if (backlog() > kWsMaxBacklogBytes) {
        fail();
        return false;
    }
    append_frame(out_, opcode, data, size, true, nullptr);
    last_send_ms_ = now_ms();
    return true;
}

bool WsConnection::flush() {
    if (fd_ < 0) return false;
    const socket_t s = static_cast<socket_t>(fd_);
    while (out_pos_ < out_.size()) {
        const auto n = ::send(s, reinterpret_cast<const char*>(out_.data() + out_pos_), static_cast<iolen_t>(std::min<size_t>(out_.size() - out_pos_, 1u << 16)),
                              kSendFlags);
        if (n > 0) {
            out_pos_ += static_cast<size_t>(n);
        } else if (n < 0 && (would_block() || interrupted())) {
            break;
        } else {
            return false;
        }
    }
    if (out_pos_ == out_.size()) {
        out_.clear();
        out_pos_ = 0;
    } else if (out_pos_ >= (1u << 16) && out_pos_ * 2 >= out_.size()) {
        out_.erase(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(out_pos_));
        out_pos_ = 0;
    }
    return true;
}

bool WsConnection::drain_parser() {
    std::vector<uint8_t> payload;
    uint16_t status = 0;
    for (;;) {
        switch (parser_.next(payload, status)) {
            case WsFrameParser::Result::NeedMore:
                return true;
            case WsFrameParser::Result::Message:
                inbox_bytes_ += payload.size();
                messages_.push_back(std::move(payload));
                payload = std::vector<uint8_t>();
                break;
            case WsFrameParser::Result::Ping:
                if (!enqueue(WsOpcode::Pong, payload.data(), payload.size())) return false;     // answered at once (the next flush sends it)
                break;
            case WsFrameParser::Result::Pong:
                break;
            case WsFrameParser::Result::Close: {
                // the peer's status is echoed (nothing, if it sent none), then the socket is closed
                const std::vector<uint8_t> body = status == kWsCloseNoStatus ? std::vector<uint8_t>() : ws_close_payload(status);
                if (enqueue(WsOpcode::Close, body.data(), body.size())) {
                    flush();
                    finish(State::Closed);
                }
                return false;
            }
            case WsFrameParser::Result::Error: {
                // the protocol was broken: say with which status, then close
                const std::vector<uint8_t> body = ws_close_payload(status);
                if (enqueue(WsOpcode::Close, body.data(), body.size())) {
                    flush();
                    finish(State::Failed);
                }
                return false;
            }
        }
    }
}

void WsConnection::pump() {
    if (fd_ < 0 || state_ != State::Open) return;
    if (!drain_parser()) return;
    if (!flush()) return fail();
    const socket_t s = static_cast<socket_t>(fd_);
    uint8_t buf[16384];
    for (int rounds = 0; rounds < 16; ++rounds) {           // bounded per pump so that a flooding peer cannot stall the game
        if (messages_.size() >= kMaxInboxMessages || inbox_bytes_ >= kMaxInboxBytes) break;     // not read until the game has taken what is here
        const auto n = ::recv(s, reinterpret_cast<char*>(buf), static_cast<iolen_t>(sizeof(buf)), 0);
        if (n > 0) {
            parser_.feed(buf, static_cast<size_t>(n));
            if (!drain_parser()) return;
        } else if (n == 0) {                                // the peer closed the socket without a close frame
            return finish(State::Closed);
        } else if (would_block() || interrupted()) {
            break;
        } else {
            return fail();
        }
    }
    // a quiet connection is kept alive for the proxies in between (the peer's pong, if it comes, is read and dropped)
    if (ping_interval_ms_ != 0 && backlog() == 0 && now_ms() - last_send_ms_ >= ping_interval_ms_) {
        if (!enqueue(WsOpcode::Ping, nullptr, 0)) return;
    }
    if (!flush()) fail();
}

bool WsConnection::send(const std::vector<uint8_t>& message) {
    if (message.size() > kMaxMessageBytes) return false;
    if (state_ != State::Open) return false;
    if (!enqueue(WsOpcode::Binary, message.data(), message.size())) return false;
    pump();
    return state_ == State::Open;
}

bool WsConnection::poll(std::vector<uint8_t>& message) {
    pump();
    if (messages_.empty()) return false;
    message = std::move(messages_.front());
    messages_.pop_front();
    inbox_bytes_ -= message.size();
    return true;
}

void WsConnection::close() {
    if (state_ == State::Open && fd_ >= 0) {
        // goodbye, best effort: nothing is waited for
        const std::vector<uint8_t> body = ws_close_payload(kWsCloseNormal);
        if (enqueue(WsOpcode::Close, body.data(), body.size())) flush();
    }
    if (state_ != State::Failed) finish(State::Closed);
}

// ------------------------------------------------------------------------------------------------
// WsListener

std::unique_ptr<WsListener> WsListener::listen(uint16_t port, bool loopback_only, const WsServerOptions& options) {
    ensure_sockets();
    const socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == kBadSocket) return nullptr;
#ifndef _WIN32
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));       // (on Windows the option would let another process share the port)
#endif
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(loopback_only ? INADDR_LOOPBACK : INADDR_ANY);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(s, 32) != 0 || !set_nonblocking(s)) {
        close_socket(s);
        return nullptr;
    }
    sockaddr_in bound;
    socklen_t len = sizeof(bound);
    uint16_t actual = port;
    if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) == 0) actual = ntohs(bound.sin_port);
    return std::unique_ptr<WsListener>(new WsListener(static_cast<int>(s), actual, options));
}

WsListener::~WsListener() {
    if (fd_ >= 0) close_socket(static_cast<socket_t>(fd_));
    for (const Pending& p : pending_) {
        if (p.fd >= 0) close_socket(static_cast<socket_t>(p.fd));
    }
    for (const Closing& c : closing_) close_socket(static_cast<socket_t>(c.fd));
}

void WsListener::take_in(uint64_t now) {
    for (int i = 0; i < kMaxAcceptsPerCall; ++i) {
        sockaddr_storage peer;
        socklen_t len = sizeof(peer);
        const socket_t c = ::accept(static_cast<socket_t>(fd_), reinterpret_cast<sockaddr*>(&peer), &len);
        if (c == kBadSocket) break;
        // handshakes in progress (and finished ones nobody has taken yet) are limited: past that, sockets are closed unanswered
        if (pending_.size() + ready_.size() >= options_.max_pending || !set_nonblocking(c)) {
            close_socket(c);
            continue;
        }
        tune(c);
        pending_.push_back(Pending{static_cast<int>(c), address_text(reinterpret_cast<sockaddr*>(&peer)), std::string(), now});
    }
}

void WsListener::refuse(Pending& p, const std::string& response, uint64_t now) {
    const socket_t s = static_cast<socket_t>(p.fd);
    // the answer is small and the socket is new: it goes out whole (a stuck one is simply dropped)
    const auto sent = ::send(s, response.data(), static_cast<iolen_t>(response.size()), kSendFlags);
    (void)sent;
    ::shutdown(s, kShutdownSend);
    if (closing_.size() < kMaxClosing) {
        closing_.push_back(Closing{p.fd, now});
    } else {
        close_socket(s);
    }
    p.fd = -1;
}

// Reads what the pending socket has sent and decides what can be decided. True when the entry is done (accepted, refused or dropped).
bool WsListener::advance(Pending& p, uint64_t now) {
    const socket_t s = static_cast<socket_t>(p.fd);
    bool judge = false;
    uint8_t buf[2048];
    for (int rounds = 0; rounds < 4; ++rounds) {
        const size_t room = kWsMaxRequestBytes - std::min(p.request.size(), kWsMaxRequestBytes);
        if (room == 0) break;
        const auto n = ::recv(s, reinterpret_cast<char*>(buf), static_cast<iolen_t>(std::min(room, sizeof(buf))), 0);
        if (n > 0) {
            p.request.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
            if (std::memchr(buf, '\n', static_cast<size_t>(n)) != nullptr) judge = true;
        } else if (n == 0 || !(would_block() || interrupted())) {   // the peer left before the handshake was done: nothing to answer
            close_socket(s);
            p.fd = -1;
            return true;
        } else {
            break;
        }
    }
    if (p.request.size() >= kWsMaxRequestBytes) judge = true;
    if (judge) {
        const WsHandshakeResult r = ws_parse_handshake(p.request, options_);
        if (r.status == WsHandshakeResult::Status::Accepted) {
            std::unique_ptr<WsConnection> c(new WsConnection(p.fd, p.peer, options_.ping_interval_ms, r.response, p.request.substr(r.consumed)));
            p.fd = -1;                                              // the connection owns the socket now
            c->pump();                                              // sends the 101 and reads frames that came with the request
            ready_.push_back(std::move(c));
            return true;
        }
        if (r.status == WsHandshakeResult::Status::Rejected) {
            refuse(p, r.response, now);
            return true;
        }
    }
    if (now - p.started_ms >= options_.handshake_timeout_ms) {      // too slow
        refuse(p, http_error_response(408), now);
        return true;
    }
    return false;
}

void WsListener::drain_closing(uint64_t now) {
    for (size_t i = 0; i < closing_.size();) {
        const socket_t s = static_cast<socket_t>(closing_[i].fd);
        bool done = now - closing_[i].started_ms >= kClosingLingerMs;
        uint8_t scratch[2048];
        for (int rounds = 0; rounds < 4 && !done; ++rounds) {
            const auto n = ::recv(s, reinterpret_cast<char*>(scratch), static_cast<iolen_t>(sizeof(scratch)), 0);
            if (n == 0 || (n < 0 && !(would_block() || interrupted()))) done = true;      // the peer has seen the answer and closed
            if (n <= 0) break;
        }
        if (done) {
            close_socket(s);
            closing_.erase(closing_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

std::unique_ptr<WsConnection> WsListener::accept() {
    const uint64_t now = now_ms();
    take_in(now);
    for (size_t i = 0; i < pending_.size();) {
        if (advance(pending_[i], now)) {
            pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    drain_closing(now);
    if (ready_.empty()) return nullptr;
    std::unique_ptr<WsConnection> c = std::move(ready_.front());
    ready_.pop_front();
    return c;
}

}  // namespace ants::net
