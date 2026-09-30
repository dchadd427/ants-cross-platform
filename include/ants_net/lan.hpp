#pragma once

// Discovery of games on the local network (native builds; a browser cannot send UDP). A room announces itself with one small UDP datagram a second to
// the directed broadcast address of every network interface (and to the limited broadcast and to this machine), a browser listens on the same port and
// lists what it hears; a guest then connects over TCP to the address the datagram came from and the port it names. Nothing here blocks and nothing
// runs in a thread: the game polls both objects from its main loop with a monotonic millisecond clock, like NetGame.
//
// The datagram is what the original's lobby did not have to say: who hosts, which map, how many seats are taken, which protocol version. It is read from
// the open network, so the decoder is strict: a fixed layout, every length checked against the datagram, printable ASCII only, sane numbers; whatever
// does not fit is ignored. A room is never trusted beyond what it says about itself (the TCP join still checks the protocol version, the room is full
// or not, the map hash is compared at the start).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ants_net/protocol.hpp"

namespace ants::net {

inline constexpr uint16_t kLanDiscoveryPort = 4001;     // UDP; the game's own TCP port has the same number
inline constexpr uint32_t kLanAnnounceMs = 1000;         // a room announces itself this often
inline constexpr uint32_t kLanExpireMs = 3500;           // a room that was not heard for this long is gone
inline constexpr size_t kLanMaxDatagram = 128;           // a datagram is never longer (the longest message is 99 bytes)
inline constexpr size_t kLanMaxRooms = 64;               // a browser keeps at most this many rooms (a flood cannot make it grow)
inline constexpr size_t kLanMaxVersionChars = 16;
inline constexpr uint8_t kLanMaxSeats = 8;

/// What a room says about itself
struct LanRoomInfo {
    uint32_t room_id{0};                     // drawn at random when the room opens; with it the browser tells rooms apart
    uint16_t protocol{kProtocolVersion};     // the network protocol the host speaks
    uint16_t tcp_port{4001};                 // where a guest connects
    uint8_t players{1};                      // in the room now (1 .. seats)
    uint8_t seats{4};                        // the most the room takes (1 .. kLanMaxSeats)
    std::string version;                     // the game's version text ("v0.0.78"), for the list only
    std::string host_name;                   // the host's player name (at most kMaxNameChars)
    std::string map_name;                    // the map the host picked (at most kMaxMapNameChars), empty before a pick

    bool operator==(const LanRoomInfo& o) const {
        return room_id == o.room_id && protocol == o.protocol && tcp_port == o.tcp_port && players == o.players && seats == o.seats && version == o.version &&
               host_name == o.host_name && map_name == o.map_name;
    }
    bool operator!=(const LanRoomInfo& o) const { return !(*this == o); }
};

enum class LanMessageType : uint8_t {
    Announce = 1,    // the room is open
    Goodbye = 2      // the room closed (the match began or the host left): drop it from the list at once
};

/// One datagram. Strings are reduced to printable ASCII and cut to their limits.
std::vector<uint8_t> encode_lan_message(LanMessageType type, const LanRoomInfo& info);
/// False unless the datagram is a whole, sane message (exact length, magic, format, known type, 1 <= players <= seats <= kLanMaxSeats, a port, printable text)
bool decode_lan_message(const uint8_t* data, size_t size, LanMessageType& type, LanRoomInfo& info);

/// A room that a browser hears
struct LanRoom {
    LanRoomInfo info;
    std::string address;            // the sender's IPv4 address as text ("192.168.1.20"): the place to connect to
    uint32_t last_heard_ms{0};
    bool compatible{true};          // the room speaks this build's protocol version
};

/// The host's side: announces one room, once a second and at once when the room changes.
class LanAnnouncer final {
public:
    /// `port` is where the browsers listen. `loopback_only` sends to this machine only (tests, two copies on one computer). nullptr when no socket can be made.
    static std::unique_ptr<LanAnnouncer> open(uint16_t port = kLanDiscoveryPort, bool loopback_only = false);
    ~LanAnnouncer();
    LanAnnouncer(const LanAnnouncer&) = delete;
    LanAnnouncer& operator=(const LanAnnouncer&) = delete;

    /// The room as it is now; a change is announced at the next update
    void set_room(const LanRoomInfo& info);
    void update(uint32_t now_ms);
    /// Tells the browsers that the room is gone (twice, datagrams can be lost); later updates send nothing
    void goodbye();
    /// The datagrams handed to the network so far (one per destination each time; diagnostics and tests)
    uint32_t datagrams_sent() const noexcept;

private:
    struct Impl;
    explicit LanAnnouncer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

/// The guest's side: listens and keeps the list of rooms that were heard lately.
class LanBrowser final {
public:
    /// Listens on UDP `port` (0 = any free port, see port()). Several browsers, and a browser beside a host, may share a port (two copies on one machine).
    /// nullptr when the port cannot be used.
    static std::unique_ptr<LanBrowser> open(uint16_t port = kLanDiscoveryPort);
    ~LanBrowser();
    LanBrowser(const LanBrowser&) = delete;
    LanBrowser& operator=(const LanBrowser&) = delete;

    /// Reads what arrived (at most a bounded number of datagrams per call) and drops the rooms that went silent
    void update(uint32_t now_ms);
    /// The rooms now, sorted by host name, then address, then room id
    std::vector<LanRoom> rooms() const;
    uint16_t port() const noexcept;

private:
    struct Impl;
    explicit LanBrowser(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace ants::net
