#include "ants_app/host_lookup.hpp"

#include <cstring>
#include <mutex>
#include <thread>

#if !defined(__EMSCRIPTEN__)
  #if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
  #else
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <sys/types.h>
  #endif
#endif

namespace ants::app {

struct HostLookup::Shared {
    std::mutex mutex;
    State state{State::Pending};
    std::string address;
    std::string error;
};

#if defined(__EMSCRIPTEN__)

bool HostLookup::is_numeric(const std::string&) { return true; }

bool HostLookup::system_resolve(const std::string& host, std::string& address, std::string&) {
    address = host;
    return true;
}

void HostLookup::start(const std::string& host, const Resolver&) {
    cancel();
    address_ = host;
    state_ = State::Done;
}

#else

namespace {

#ifdef _WIN32
// getaddrinfo needs Winsock to be started (the TCP transport starts it too; the count of starts is the system's)
void ensure_sockets() {
    struct Init {
        Init() { WSADATA d; ok = WSAStartup(MAKEWORD(2, 2), &d) == 0; }
        ~Init() { if (ok) WSACleanup(); }
        bool ok{false};
    };
    static Init init;
    (void)init;
}
const char* gai_text(int rc) { return gai_strerrorA(rc); }
#else
void ensure_sockets() {}
const char* gai_text(int rc) { return gai_strerror(rc); }
#endif

std::string numeric_text(const sockaddr* sa) {
    char buf[64] = {0};
    if (sa->sa_family == AF_INET) {
        inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(sa)->sin_addr, buf, sizeof(buf));
    } else if (sa->sa_family == AF_INET6) {
        inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(sa)->sin6_addr, buf, sizeof(buf));
    }
    return buf;
}

}  // anonymous namespace

bool HostLookup::is_numeric(const std::string& host) {
    ensure_sockets();
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST;                       // no name lookup: only an address is accepted
    addrinfo* res = nullptr;
    const bool numeric = getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res != nullptr;
    if (res != nullptr) freeaddrinfo(res);
    return numeric;
}

bool HostLookup::system_resolve(const std::string& host, std::string& address, std::string& error) {
    ensure_sockets();
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const int rc = getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (rc != 0 || res == nullptr) {
        error = rc != 0 ? gai_text(rc) : "no address";
        if (res != nullptr) freeaddrinfo(res);
        return false;
    }
    std::string v4;
    std::string v6;
    for (const addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        if (ai->ai_addr == nullptr) continue;
        if (ai->ai_family == AF_INET && v4.empty()) v4 = numeric_text(ai->ai_addr);
        else if (ai->ai_family == AF_INET6 && v6.empty()) v6 = numeric_text(ai->ai_addr);
    }
    freeaddrinfo(res);
    address = !v4.empty() ? v4 : v6;
    if (address.empty()) {
        error = "no address";
        return false;
    }
    return true;
}

void HostLookup::start(const std::string& host, const Resolver& resolver) {
    cancel();
    error_.clear();
    if (is_numeric(host)) {
        address_ = host;
        state_ = State::Done;
        return;
    }
    shared_ = std::make_shared<Shared>();
    state_ = State::Pending;
    std::shared_ptr<Shared> shared = shared_;
    std::thread([shared, host, resolver]() {
        std::string address;
        std::string error;
        const bool ok = resolver ? resolver(host, address, error) : HostLookup::system_resolve(host, address, error);
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->address = address;
        shared->error = error;
        shared->state = ok ? State::Done : State::Failed;
    }).detach();
}

#endif  // !__EMSCRIPTEN__

HostLookup::State HostLookup::poll() {
    if (state_ != State::Pending || !shared_) return state_;
    std::lock_guard<std::mutex> lock(shared_->mutex);
    if (shared_->state == State::Pending) return State::Pending;
    state_ = shared_->state;
    address_ = shared_->address;
    error_ = shared_->error;
    return state_;
}

void HostLookup::cancel() {
    shared_.reset();                                       // the worker keeps its own reference and finishes alone
    state_ = State::Idle;
    address_.clear();
    error_.clear();
}

}  // namespace ants::app
