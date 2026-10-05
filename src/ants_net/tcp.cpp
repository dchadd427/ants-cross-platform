#include "ants_net/tcp.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "ants_net/protocol.hpp"

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
#include <netdb.h>
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

// Connections that have finished their handshake and wait for accept(). What does not fit is dropped: a lobby may send its players all at once while a pass of the server's loop is
// slow, and with a backlog of 8 a burst of 48 had 8 accepted on macOS and the other 40 never came (measured for 12 s). 64 holds the whole burst; the operating system caps the value
// at its own limit (somaxconn, 128 on macOS).
constexpr int kListenBacklog = 64;

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
bool in_progress() { return WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINPROGRESS; }
#else
void ensure_sockets() {}
void close_socket(socket_t s) { ::close(s); }
bool would_block() { return errno == EWOULDBLOCK || errno == EAGAIN; }
bool in_progress() { return errno == EINPROGRESS; }
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

// The address of a socket address as text, without its port ("" for a family that is not an internet one)
std::string host_text(const sockaddr* sa) {
    char buf[64] = {0};
    if (sa->sa_family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(sa);
        inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf));
        return buf;
    }
    if (sa->sa_family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(sa);
        inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof(buf));
        return buf;
    }
    return std::string();
}

std::string address_text(const sockaddr* sa) {
    if (sa->sa_family == AF_INET) return host_text(sa) + ":" + std::to_string(ntohs(reinterpret_cast<const sockaddr_in*>(sa)->sin_port));
    if (sa->sa_family == AF_INET6) return "[" + host_text(sa) + "]:" + std::to_string(ntohs(reinterpret_cast<const sockaddr_in6*>(sa)->sin6_port));
    return "?";
}

}  // namespace

TcpConnection::TcpConnection(int fd, bool connecting, std::string peer)
    : fd_(fd), state_(connecting ? State::Connecting : State::Open), peer_(std::move(peer)) {}

TcpConnection::~TcpConnection() { close(); }

std::string pick_address(const std::vector<ResolvedAddress>& found) {
    std::string first_v6;
    for (const ResolvedAddress& a : found) {
        if (a.text.empty()) continue;
        if (!a.ipv6) return a.text;                                  // the first IPv4 one, wherever the system put it
        if (first_v6.empty()) first_v6 = a.text;
    }
    return first_v6;
}

std::string address_with_scope(const std::string& numeric, uint32_t scope_id) {
    return scope_id != 0 && !numeric.empty() ? numeric + "%" + std::to_string(scope_id) : numeric;
}

ResolvedAddress describe_address(const void* entry) {
    ResolvedAddress out;
    if (entry == nullptr) return out;
    const auto* sa = static_cast<const sockaddr*>(entry);
    out.ipv6 = sa->sa_family == AF_INET6;
    out.text = out.ipv6 ? address_with_scope(host_text(sa), reinterpret_cast<const sockaddr_in6*>(sa)->sin6_scope_id) : host_text(sa);
    return out;
}

std::string resolve_host(const std::string& host) {
    if (host.empty()) return std::string();
    ensure_sockets();
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) return std::string();
    std::vector<ResolvedAddress> found;
    for (addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        const socket_t s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);       // (as connect() does: an address that no socket can be made for is passed over)
        if (s == kBadSocket) continue;
        close_socket(s);
        found.push_back(describe_address(ai->ai_addr));
    }
    freeaddrinfo(res);
    return pick_address(found);
}

std::unique_ptr<TcpConnection> TcpConnection::connect(const std::string& host, uint16_t port) {
    ensure_sockets();
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &res) != 0 || res == nullptr) return nullptr;
    std::unique_ptr<TcpConnection> out;
    for (addrinfo* ai = res; ai != nullptr && !out; ai = ai->ai_next) {
        const socket_t s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == kBadSocket) continue;
        if (!set_nonblocking(s)) {
            close_socket(s);
            continue;
        }
        tune(s);
        const int rc = ::connect(s, ai->ai_addr, static_cast<addrlen_t>(ai->ai_addrlen));
        if (rc == 0 || in_progress()) {
            out.reset(new TcpConnection(static_cast<int>(s), true, address_text(ai->ai_addr)));
        } else {
            close_socket(s);
        }
    }
    freeaddrinfo(res);
    return out;
}

void TcpConnection::fail() {
    if (fd_ >= 0) {
        close_socket(static_cast<socket_t>(fd_));
        fd_ = -1;
    }
    state_ = State::Failed;
}

void TcpConnection::close() {
    if (fd_ >= 0) {
        close_socket(static_cast<socket_t>(fd_));
        fd_ = -1;
    }
    if (state_ != State::Failed) state_ = State::Closed;
    // This side closed: nobody will read what the peer had sent. A server keeps the object of a connection it dropped for as long as its room lives, so a flooder's backlog (up to the
    // inbox bound) is let go of now. (What a PEER sent before it closed is still delivered: that is a close that was seen, not made.)
    messages_.clear();
    inbox_bytes_ = 0;
    in_.clear();
    in_.shrink_to_fit();
}

void TcpConnection::pump() {
    if (fd_ < 0) return;
    const socket_t s = static_cast<socket_t>(fd_);
    if (state_ == State::Connecting) {
        fd_set wset;
        fd_set eset;
        FD_ZERO(&wset);
        FD_ZERO(&eset);
        FD_SET(s, &wset);
        FD_SET(s, &eset);
        timeval tv{0, 0};
        const int r = ::select(static_cast<int>(s) + 1, nullptr, &wset, &eset, &tv);
        if (r <= 0) return;                                         // still connecting
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
        if (err != 0 || FD_ISSET(s, &eset)) return fail();
        state_ = State::Open;
    }
    if (state_ != State::Open) return;
    // write what is queued
    while (!out_.empty()) {
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif
        const auto n = ::send(s, reinterpret_cast<const char*>(out_.data()), static_cast<iolen_t>(std::min<size_t>(out_.size(), 1u << 16)), flags);
        if (n > 0) {
            out_.erase(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(n));
        } else if (n < 0 && would_block()) {
            break;
        } else {
            return fail();
        }
    }
    // Read what is there. Bounded per pump so that a flooding peer cannot stall the game, and only while the game has room for what it reads: when half of the inbox is full
    // (kMaxInboxMessages, kMaxInboxBytes) or a whole message is waiting unparsed, nothing is read until the game has taken what is here. The bytes stay in the kernel and TCP holds
    // the sender back. (Every poll() used to read and parse up to 256 KB however few messages the game took, and the parsed messages piled up without a limit: one peer that sent
    // tiny messages as fast as it could grew this process by gigabytes in seconds.)
    uint8_t buf[16384];
    for (int rounds = 0; rounds < 16; ++rounds) {
        if (messages_.size() >= kMaxInboxMessages / 2 || inbox_bytes_ >= kMaxInboxBytes / 2 || in_.size() >= kMaxMessageBytes + 4) break;
        const auto n = ::recv(s, reinterpret_cast<char*>(buf), static_cast<iolen_t>(sizeof(buf)), 0);
        if (n > 0) {
            in_.insert(in_.end(), buf, buf + static_cast<size_t>(n));
        } else if (n == 0) {                                        // the peer closed in an orderly way
            close_socket(s);
            fd_ = -1;
            state_ = State::Closed;
            break;
        } else if (would_block()) {
            break;
        } else {
            return fail();
        }
    }
    // parse frames: u32 length, payload (as many as the inbox takes: the rest stays in in_, which is bounded above)
    size_t pos = 0;
    while (in_.size() - pos >= 4 && messages_.size() < kMaxInboxMessages && inbox_bytes_ < kMaxInboxBytes) {
        const uint32_t len = static_cast<uint32_t>(in_[pos]) | (static_cast<uint32_t>(in_[pos + 1]) << 8) | (static_cast<uint32_t>(in_[pos + 2]) << 16) |
                             (static_cast<uint32_t>(in_[pos + 3]) << 24);
        if (len > kMaxMessageBytes) return fail();                  // a hostile length: never allocate for it
        if (in_.size() - pos - 4 < len) break;                      // the rest has not arrived yet
        messages_.emplace_back(in_.begin() + static_cast<std::ptrdiff_t>(pos + 4), in_.begin() + static_cast<std::ptrdiff_t>(pos + 4 + len));
        inbox_bytes_ += len;
        pos += 4 + len;
    }
    if (pos > 0) in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(pos));
}

bool TcpConnection::send(const std::vector<uint8_t>& message) {
    if (message.size() > kMaxMessageBytes) return false;
    if (state_ != State::Open && state_ != State::Connecting) return false;
    // a peer that never reads must not make the queue grow without bound
    if (out_.size() > 8u * kMaxMessageBytes) {
        fail();
        return false;
    }
    const uint32_t len = static_cast<uint32_t>(message.size());
    out_.push_back(static_cast<uint8_t>(len & 0xFFu));
    out_.push_back(static_cast<uint8_t>((len >> 8) & 0xFFu));
    out_.push_back(static_cast<uint8_t>((len >> 16) & 0xFFu));
    out_.push_back(static_cast<uint8_t>((len >> 24) & 0xFFu));
    out_.insert(out_.end(), message.begin(), message.end());
    pump();
    return state_ == State::Open || state_ == State::Connecting;
}

bool TcpConnection::poll(std::vector<uint8_t>& message) {
    pump();
    if (messages_.empty()) return false;
    message = std::move(messages_.front());
    messages_.pop_front();
    inbox_bytes_ -= message.size();
    return true;
}

// ------------------------------------------------------------------------------------------------

std::unique_ptr<TcpListener> TcpListener::listen(uint16_t port, bool loopback_only) {
    ensure_sockets();
    const socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == kBadSocket) return nullptr;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(loopback_only ? INADDR_LOOPBACK : INADDR_ANY);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(s, kListenBacklog) != 0 || !set_nonblocking(s)) {
        close_socket(s);
        return nullptr;
    }
    sockaddr_in bound;
    socklen_t len = sizeof(bound);
    uint16_t actual = port;
    if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) == 0) actual = ntohs(bound.sin_port);
    return std::unique_ptr<TcpListener>(new TcpListener(static_cast<int>(s), actual));
}

TcpListener::~TcpListener() {
    if (fd_ >= 0) close_socket(static_cast<socket_t>(fd_));
}

std::unique_ptr<TcpConnection> TcpListener::accept() {
    sockaddr_storage peer;
    socklen_t len = sizeof(peer);
    const socket_t c = ::accept(static_cast<socket_t>(fd_), reinterpret_cast<sockaddr*>(&peer), &len);
    if (c == kBadSocket) return nullptr;
    if (!set_nonblocking(c)) {
        close_socket(c);
        return nullptr;
    }
    tune(c);
    return std::unique_ptr<TcpConnection>(new TcpConnection(static_cast<int>(c), false, address_text(reinterpret_cast<sockaddr*>(&peer))));
}

}  // namespace ants::net
