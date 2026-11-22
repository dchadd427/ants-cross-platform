#pragma once

// Limits per network address on the game server's doors (docs/ADDRESS_LIMITS.md).
//
// Many people share one address: a phone carrier puts thousands of them behind one, a household or a LAN party plays from one. So nothing here counts people. The limits stop what only a program does
// (rooms made in a burst, rooms held open without a game, codes tried one after another, relay credentials collected) and are set well above what a household does; a refusal is polite and soon over
// (a sliding window of seconds or minutes, never a ban), and a Hello for a room that exists is never refused: a player who knows the code gets in (when the address has been trying codes, after a short wait).
//
// An address is counted as address_identity says: an IPv4 address as it is, an IPv6 address as its /64. The address of a browser is the one that the site's proxy writes in X-Client-Address, and only
// a connection from a trusted proxy (TrustedProxies) is believed; the address of any other connection is its own. A connection whose address is unknown (a trusted proxy that sent none) is not limited:
// better no limit than one that every visitor shares. Every number is an option of the server (kAddressLimitFields lists them once: the options, the control interface's /limits and /stats and the
// documentation all walk through it) and 0 switches that limit off.

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace ants::server {

struct AddressLimits {
    uint32_t connections_per_second{0};         // new connections from one address in any second
    uint32_t rooms_per_minute{0};               // rooms that one address makes in any minute (a burst and a rate in one: the window slides)
    uint32_t rooms_open{0};                     // rooms that one address made and that are still open (a waiting lobby, a public room, a match that runs)
    uint32_t wrong_codes_per_minute{0};         // Hellos for a room that does not exist, in any minute, that are answered at once; the next ones wait (kWrongCodeStepMs more each, at most kWrongCodeMaxHoldMs)
    uint32_t crowded_rooms{0};                  // an address that holds more open rooms than this is "crowded": ...
    uint32_t crowded_idle_s{0};                 // ... its waiting lobby in which nobody has joined, planned or started anything (chat and ready marks are not that) for this long is closed
    uint32_t relay_per_hour{0};                 // relay credentials that the players of one address are given in any hour

    /// The numbers that the program runs with unless an option says otherwise (kAddressLimitFields' `standard`)
    static AddressLimits standard();
};

/// One limit: its key (the control interface's /limits and /stats; the option is "--address-" and the key with '-' for '_'), the member, the range that an option or a call may give (0 is always allowed: off),
/// the number that the program runs with, and what it limits in a few words (the documentation's table)
struct AddressLimitField {
    const char* key;
    uint32_t AddressLimits::*member;
    uint32_t min;
    uint32_t max;
    uint32_t standard;
    const char* what;
};

/// Every limit once, in the order of the struct. A limit that is added to the struct and not here has no option and no key (the tests compare the two).
inline constexpr std::array<AddressLimitField, 7> kAddressLimitFields{{
    {"connections_per_second", &AddressLimits::connections_per_second, 1, 1000, 20, "new connections from one address in a second"},
    {"rooms_per_minute", &AddressLimits::rooms_per_minute, 1, 1000, 10, "rooms made from one address in a minute"},
    {"rooms_open", &AddressLimits::rooms_open, 1, 255, 16, "rooms from one address that are open at once"},
    {"wrong_codes_per_minute", &AddressLimits::wrong_codes_per_minute, 1, 10000, 30, "Hellos for a room that does not exist, answered at once, in a minute"},
    {"crowded_rooms", &AddressLimits::crowded_rooms, 1, 255, 8, "open rooms of one address above which its idle lobbies are closed sooner"},
    {"crowded_idle_seconds", &AddressLimits::crowded_idle_s, 30, 86400, 300, "seconds of quiet after which such a lobby is closed"},
    {"relay_per_hour", &AddressLimits::relay_per_hour, 1, 10000, 24, "relay credentials for the players of one address in an hour"},
}};

inline AddressLimits AddressLimits::standard() {
    AddressLimits l;
    for (const AddressLimitField& f : kAddressLimitFields) l.*f.member = f.standard;
    return l;
}

/// Sets the limit with this key (kAddressLimitFields) to `value`: 0 (off) or a number in the field's range. False, with the reason, when the key is unknown or the number is out of range; `limits` is then as it was.
bool set_address_limit(AddressLimits& limits, const std::string& key, int64_t value, std::string& why);

/// The answers to a Hello for a room that does not exist wait this much longer for each one beyond the minute's allowance, up to the longest wait; at most this many wait for one address (the next ones are
/// dropped without an answer, as a link that never completed would be) and at most kWrongCodeHeldTotal for all
inline constexpr uint32_t kWrongCodeStepMs = 500;
inline constexpr uint32_t kWrongCodeMaxHoldMs = 5000;
inline constexpr size_t kWrongCodeHeldPerAddress = 8;
inline constexpr size_t kWrongCodeHeldTotal = 256;

/// The addresses that are counted at one time (the one that has not been seen for the longest gives its place up when none is left)
inline constexpr size_t kMaxTrackedAddresses = 4096;

/// What the limits refused, counted since the server started (never an address): the control interface's GET /stats
struct AddressRefusals {
    uint64_t connections{0};            // connections closed for coming too fast
    uint64_t rooms_rate{0};             // rooms not made: more than rooms_per_minute in the minute
    uint64_t rooms_open{0};             // rooms not made: rooms_open of the address are open
    uint64_t wrong_codes_slowed{0};     // answers to a Hello for a room that does not exist that were held back
    uint64_t wrong_codes_dropped{0};    // ... and Hellos that were dropped without an answer: too many were waiting
    uint64_t relay{0};                  // relay credentials not given (the player is answered as a server that has no relay)
    uint64_t idle_closed{0};            // lobbies of crowded addresses that were closed for their quiet
    uint64_t total() const noexcept { return connections + rooms_rate + rooms_open + wrong_codes_slowed + wrong_codes_dropped + relay + idle_closed; }
};

/// The text of an address as the limits count it ("" when it is none): an IPv4 address as it is, an IPv4-mapped IPv6 address as its IPv4 address, any other IPv6 address as its /64 ("v6:" and 16 hex digits).
/// Anything that is no address (a name, a list, a port, too long) is "".
std::string address_identity(const std::string& text);

/// The reverse proxies whose X-Client-Address is believed (ANTS_TRUSTED_PROXIES, --trusted-proxies): addresses and networks ("10.0.0.0/8", "2001:db8::/32", "127.0.0.1")
class TrustedProxies {
public:
    /// The loopback address and the private networks (127.0.0.0/8, 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, ::1, fc00::/7): the site's own proxy on this machine or on the stack's network, as the web image's
    /// docker/trusted-proxies.conf believes. A connection from any other address is believed in nothing.
    static TrustedProxies standard();
    /// A list: entries separated by commas, blanks or newlines; "none" alone is an empty list (nobody is believed). False, with the entry in `why`, when one is no address or network.
    static bool parse(const std::string& text, TrustedProxies& out, std::string& why);
    /// Whether the address (text of a connection's peer, no port) is one of them. An IPv4-mapped IPv6 address is its IPv4 address.
    bool contains(const std::string& address) const;
    size_t size() const noexcept { return nets_.size(); }

private:
    struct Net {
        std::array<uint8_t, 16> bytes{};     // IPv4 as an IPv4-mapped IPv6 address, so that one comparison serves both
        unsigned prefix{128};                // bits, counted in the 16-byte form
    };
    std::vector<Net> nets_;
};

/// The address that a WebSocket connection counts under: the proxy's header when the connection comes from a trusted proxy and the header holds one address (what nginx sets: the visitor's own header of that
/// name never reaches the server through it); the connection's own address in every other case: a connection from anyone else (a header from anyone else is no news), and a connection from a trusted proxy
/// whose header is missing or is no address (nginx never sends one like that, so it is a connection that did not come through the site's proxy: all of them count under the proxy's own address, one count).
/// `peer` is the connection's address without its port, `header` the value of the one X-Client-Address header ("" when there was none or more than one).
std::string door_address(const std::string& peer, const std::string& header, const TrustedProxies& proxies);

/// A sliding window of events: how many happened in the last `span_ms` (the clock is 32 bits and wraps: only differences are used)
class SlidingWindow {
public:
    size_t count(uint32_t now_ms, uint32_t span_ms);
    /// Notes an event; the oldest are forgotten beyond `keep` of them (the window never holds more than the limit asks about)
    void add(uint32_t now_ms, size_t keep);
    bool empty() const noexcept { return times_.empty(); }

private:
    std::deque<uint32_t> times_;
};

/// What the limits keep of the addresses that were seen lately, and the questions that the doors ask. Time is the server's millisecond clock, passed in by the caller.
class AddressGuard {
public:
    AddressGuard() = default;
    explicit AddressGuard(const AddressLimits& limits) : limits_(limits) {}
    void set_limits(const AddressLimits& limits) { limits_ = limits; }
    const AddressLimits& limits() const noexcept { return limits_; }
    const AddressRefusals& refusals() const noexcept { return refusals_; }
    size_t tracked() const noexcept { return seen_.size(); }

    /// A new connection from the address (an identity, address_identity): false when it has made connections_per_second in the last second (the caller closes it)
    bool allow_connection(const std::string& id, uint32_t now_ms);

    enum class RoomVerdict : uint8_t { Allowed, TooFast, TooMany };
    /// May this address make a room now? `open_now` is how many rooms of it are open. Nothing is noted: note_room_made does that when the room exists. The refusal is counted.
    RoomVerdict allow_room(const std::string& id, size_t open_now, uint32_t now_ms);
    void note_room_made(const std::string& id, uint32_t now_ms);

    /// A Hello for a room that does not exist, from this address: how long the answer waits (0: it is sent at once). Every one is noted.
    uint32_t wrong_code(const std::string& id, uint32_t now_ms);
    /// How long a try at a code that EXISTS waits from this address now: what the next wrong code would wait (0 while the address is within its allowance). Nothing is noted: only wrong codes count,
    /// so a person who joins many rooms is never slowed by that, and an address that is trying codes waits at the right ones as at the wrong ones.
    uint32_t hold_now(const std::string& id, uint32_t now_ms);
    /// What became of an answer that was to wait: it waits (counted as slowed), or the door's places for waiting answers were used up and the Hello is dropped without one (counted as dropped)
    void note_wrong_code_slowed() noexcept { ++refusals_.wrong_codes_slowed; }
    void note_wrong_code_dropped() noexcept { ++refusals_.wrong_codes_dropped; }

    /// One relay credential for a player of this address: false when the address has been given relay_per_hour in the last hour
    bool take_relay(const std::string& id, uint32_t now_ms);

    void note_idle_closed() noexcept { ++refusals_.idle_closed; }

    /// Forgets the addresses that have done nothing for an hour (the server calls it about once a minute)
    void sweep(uint32_t now_ms);

private:
    struct Seen {
        uint32_t last_ms{0};
        SlidingWindow connections;
        SlidingWindow rooms;
        SlidingWindow wrong;
        SlidingWindow relay;
    };
    Seen& seen(const std::string& id, uint32_t now_ms);

    AddressLimits limits_;
    AddressRefusals refusals_;
    std::unordered_map<std::string, Seen> seen_;
};

}  // namespace ants::server
