#pragma once

// Addresses as text, with no sockets and no system calls (every build, the web one included): the wss URL of a game server's encrypted door, and which numeric addresses are of this machine or of a local network.

#include <cstdint>
#include <string>

namespace ants::net {

inline constexpr uint16_t kWssDefaultPort = 443;

/// A wss URL taken apart
struct WssUrl {
    std::string host;                       // lower case; an IPv6 address without its brackets
    uint16_t port{kWssDefaultPort};
    std::string target{"/"};                // the path and the query, starting with '/'
    bool operator==(const WssUrl& o) const noexcept { return host == o.host && port == o.port && target == o.target; }
};

/// wss://HOST[:PORT][/PATH][?QUERY]: a host name (letters, digits, dots, hyphens) or a bracketed IPv6 address, a port of 1 - 65535, then an optional path and query without blanks, controls, '#' or
/// non-ASCII characters; no user information; at most 512 characters. The scheme is read without regard to case. Anything else is false with the reason in `why` ("ws://" gets the one that says why it
/// is refused).
bool parse_wss_url(const std::string& text, WssUrl& out, std::string& why);
/// The one written form of a URL: "wss://" + the host (IPv6 in brackets) + ":PORT" unless the port is 443 + the target. The text of the rejoin keys and the menu use it.
std::string wss_url_text(const WssUrl& url);

/// Is this numeric address (as resolve_host gives it, an IPv6 scope id allowed) one of this machine or of a local network: loopback, private (10/8, 172.16/12, 192.168/16, fc00::/7), link-local (169.254/16,
/// fe80::/10), the shared range that VPNs use (100.64/10) or the unspecified address; an IPv4 address in IPv6 form is judged as the IPv4 one. False for every other address and for text that is no address.
/// Plain TCP to a game server is allowed only for such addresses (NetGame::join): anywhere else the link must be encrypted (wss.hpp).
bool is_local_address(const std::string& numeric);

}  // namespace ants::net
