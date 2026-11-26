#pragma once

// The pass of a name (protocol 29, docs/HUB_NICKNAMES.md "The pass"): what lets a person go by a nickname in a room of this server. The chat hub signs the name that a logged-in person plays under
// ("[TAG] Nickname", or the name of their login when they chose none) for about two minutes; the page tells it in its Hello and in a NameMsg, and the room (ants_net HostLobby, Config::name_gate) takes the
// name as it is only when the pass is good for exactly that name. Every other name on such a server is a guest's ("Wren (Guest)": net::guest_name). The hub's twin is hub/namepass.py.
//
// What this file promises:
//  * The key is the setting ANTS_ADMIN_GATE_SECRET (the one key of the hub and this server; the same rules as admin_pass.hpp: gate_secret_is_good), with its own label, so a pass of another kind is never good for
//    a name. With no good key no name has a pass (open() is false), and the server leaves its names as they are (LAN rules).
//  * A pass is `<exp>.<mac>`: exp is seconds since 1970 in digits, mac the first 32 hex digits of HMAC-SHA-256, under the RAW key (the secret as the KEY of HMAC-SHA-256 and `ants-name-pass-v1` as the MESSAGE), of
//    `name1`, a NUL, the digits of exp, a NUL and the name's bytes. It is compared in constant time before anything else is looked at.
//  * exp is checked against the clock that the caller gives: a pass that has ended is refused, and so is one that claims to last longer than the hub makes them (kNamePassSeconds and the slack).
//  * A pass is not used up: it is good for the one name that it names until it ends (a page tells it in its Hello and again for each rename; a stolen pass gives the thief that name for two minutes, and nothing else).
// One thread, like the rest of the server.

#include <cstdint>
#include <string>
#include <string_view>

namespace ants::server {

inline constexpr int64_t kNamePassSeconds = 120;       // how long the hub makes a pass last
inline constexpr int64_t kNamePassSlackSeconds = 30;   // a pass that ends later than this past kNamePassSeconds is not one that the hub made

class NameGate {
public:
    /// `setting` is the text of ANTS_ADMIN_GATE_SECRET ("" when it is not set)
    explicit NameGate(std::string_view setting);
    /// Whether passes can be checked at all
    bool open() const { return open_; }
    /// Whether `pass` is a good pass for exactly `name` at the time `now` (seconds since 1970)
    bool check(std::string_view name, std::string_view pass, int64_t now) const;
    /// The pass of `name` that ends at `exp` ("" with no key): the hub makes the same text (the tests make passes with this and with the contract's words alone)
    std::string make(std::string_view name, int64_t exp) const;

private:
    bool open_{false};
    std::string key_;                                  // 32 raw bytes
};

}  // namespace ants::server
