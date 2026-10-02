#pragma once

// The name lookup of the start menu's connection. Turning a server's name into an address is the one step of joining that blocks (TcpConnection::connect asks the system
// resolver, which can take seconds when a network is slow or gone): done on the game's thread it would freeze the window, and Esc could not cancel it. HostLookup does it on
// a worker thread and the menu polls it once per frame; the connection is then made to the NUMERIC address, which needs no lookup.
//
// A host that is already an address (127.0.0.1, ::1) is answered at once, with no thread. A lookup that is cancelled (or whose owner is gone) is abandoned: its thread finishes
// alone and its answer goes nowhere. The web build has no sockets of its own and no threads: its HostLookup answers at once with the name unchanged (the start menu is not
// shown there).

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace ants::app {

class HostLookup {
public:
    /// The lookup itself: true with a numeric address in `address`, or false with the reason in `error`. The default asks the system's resolver (getaddrinfo; an IPv4 address
    /// is preferred when the name has one: the game's server listens on IPv4). The tests give their own (a slow one that waits to be released, one that fails).
    using Resolver = std::function<bool(const std::string& host, std::string& address, std::string& error)>;
    enum class State : uint8_t { Idle, Pending, Done, Failed };

    HostLookup() = default;
    ~HostLookup() { cancel(); }
    HostLookup(const HostLookup&) = delete;
    HostLookup& operator=(const HostLookup&) = delete;

    /// Begins a lookup of `host` (a lookup that is still running is abandoned). A numeric address is Done at once.
    void start(const std::string& host, const Resolver& resolver = Resolver());
    /// Where it stands now: Pending until the worker has answered
    State poll();
    /// Gives it up: the state is Idle again
    void cancel();
    State state() const noexcept { return state_; }
    /// Done: the numeric address; Failed: the resolver's reason
    const std::string& address() const noexcept { return address_; }
    const std::string& error() const noexcept { return error_; }

    /// The system resolver (what start() uses without a Resolver of its own)
    static bool system_resolve(const std::string& host, std::string& address, std::string& error);
    /// Is `host` an IPv4 or IPv6 address (no lookup is needed)?
    static bool is_numeric(const std::string& host);

private:
    struct Shared;
    std::shared_ptr<Shared> shared_;
    State state_{State::Idle};
    std::string address_;
    std::string error_;
};

}  // namespace ants::app
