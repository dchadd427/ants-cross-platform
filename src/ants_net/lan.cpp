// Discovery of games on the local network: the datagram codec, the announcer and the browser (see ants_net/lan.hpp). Plain UDP with the same small socket
// layer as tcp.cpp (Winsock or BSD sockets, non-blocking, nothing is waited for).
#include "ants_net/lan.hpp"

#include <algorithm>
#include <cstring>
#include <set>

#include "ants_net/wire.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef IFF_UP                      // the interface flags of SIO_GET_INTERFACE_LIST (winsock2.h defines them; the values are fixed by the API)
#define IFF_UP 0x00000001
#endif
#ifndef IFF_BROADCAST
#define IFF_BROADCAST 0x00000002
#endif
#ifndef IFF_LOOPBACK
#define IFF_LOOPBACK 0x00000004
#endif
using socket_t = SOCKET;
using addrlen_t = int;
using iolen_t = int;
static constexpr socket_t kBadSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
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

constexpr uint8_t kLanFormat = 1;
constexpr size_t kLanMinDatagram = 16 + 3;                // the fixed part and three empty strings
constexpr uint32_t kLoopbackHost = 0x7F000001u;
constexpr uint32_t kLimitedBroadcast = 0xFFFFFFFFu;
constexpr uint32_t kTargetRefreshMs = 5000;               // the network interfaces are looked at again this often (a cable is plugged in, Wi-Fi joins)
constexpr int kMaxDatagramsPerUpdate = 64;                // a flood is read a bit at a time and cannot hold the game up

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
#else
void ensure_sockets() {}
void close_socket(socket_t s) { ::close(s); }
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

// What goes out is printable ASCII (the original's names and chat are 0x20 - 0x7e text) and never longer than its limit
std::string printable(const std::string& s, size_t max) {
    std::string out;
    for (char c : s) {
        if (out.size() >= max) break;
        const unsigned char u = static_cast<unsigned char>(c);
        out.push_back(u >= 0x20 && u <= 0x7E ? c : '?');
    }
    return out;
}

bool is_printable(const std::string& s) {
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7E) return false;
    }
    return true;
}

bool is_loopback(uint32_t host_order_address) { return (host_order_address >> 24) == 127u; }

std::string address_text(uint32_t host_order_address) {
    in_addr a;
    a.s_addr = htonl(host_order_address);
    char buf[INET_ADDRSTRLEN] = {0};
    return inet_ntop(AF_INET, &a, buf, sizeof(buf)) != nullptr ? std::string(buf) : std::string("?");
}

// The directed broadcast address of every IPv4 interface that is up and can broadcast (a limited broadcast alone leaves a machine with several
// networks through one of them only, and the first one the system picks is often a VPN)
std::vector<uint32_t> interface_broadcasts() {
    std::vector<uint32_t> out;
#ifdef _WIN32
    const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) return out;
    INTERFACE_INFO info[32];
    DWORD bytes = 0;
    if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, nullptr, 0, info, sizeof(info), &bytes, nullptr, nullptr) == 0) {
        const size_t count = bytes / sizeof(INTERFACE_INFO);
        for (size_t i = 0; i < count && i < 32; ++i) {
            const INTERFACE_INFO& ii = info[i];
            if ((ii.iiFlags & IFF_UP) == 0 || (ii.iiFlags & IFF_LOOPBACK) != 0 || (ii.iiFlags & IFF_BROADCAST) == 0) continue;
            if (ii.iiAddress.Address.sa_family != AF_INET) continue;
            const uint32_t address = ntohl(ii.iiAddress.AddressIn.sin_addr.s_addr);
            const uint32_t mask = ntohl(ii.iiNetmask.AddressIn.sin_addr.s_addr);
            out.push_back(address | ~mask);
        }
    }
    closesocket(s);
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return out;
    for (const ifaddrs* a = list; a != nullptr; a = a->ifa_next) {
        if (a->ifa_addr == nullptr || a->ifa_addr->sa_family != AF_INET) continue;
        if ((a->ifa_flags & IFF_UP) == 0 || (a->ifa_flags & IFF_LOOPBACK) != 0 || (a->ifa_flags & IFF_BROADCAST) == 0) continue;
        if (a->ifa_broadaddr == nullptr || a->ifa_broadaddr->sa_family != AF_INET) continue;
        out.push_back(ntohl(reinterpret_cast<const sockaddr_in*>(a->ifa_broadaddr)->sin_addr.s_addr));
    }
    freeifaddrs(list);
#endif
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------------------------------
// The datagram: "ANTL", format, type, protocol u16, room id u32, TCP port u16, players u8, seats u8, then three strings (u8 length + bytes): the game's
// version, the host's name, the map. Little endian throughout; the longest message is 99 bytes.
// ---------------------------------------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> encode_lan_message(LanMessageType type, const LanRoomInfo& info) {
    std::vector<uint8_t> out;
    out.reserve(kLanMaxDatagram);
    ByteWriter w(out);
    w.u8('A');
    w.u8('N');
    w.u8('T');
    w.u8('L');
    w.u8(kLanFormat);
    w.u8(static_cast<uint8_t>(type));
    w.u16(info.protocol);
    w.u32(info.room_id);
    w.u16(info.tcp_port == 0 ? uint16_t{1} : info.tcp_port);                        // a message that could not be decoded would be no use
    const uint8_t seats = std::min<uint8_t>(std::max<uint8_t>(info.seats, 1), kLanMaxSeats);
    w.u8(std::min<uint8_t>(std::max<uint8_t>(info.players, 1), seats));
    w.u8(seats);
    w.str8(printable(info.version, kLanMaxVersionChars));
    w.str8(printable(info.host_name, kMaxNameChars));
    w.str8(printable(info.map_name, kMaxMapNameChars));
    return out;
}

bool decode_lan_message(const uint8_t* data, size_t size, LanMessageType& type, LanRoomInfo& info) {
    if (data == nullptr || size < kLanMinDatagram || size > kLanMaxDatagram) return false;
    ByteReader r(data, size);
    if (r.u8() != 'A' || r.u8() != 'N' || r.u8() != 'T' || r.u8() != 'L') return false;
    if (r.u8() != kLanFormat) return false;
    const uint8_t t = r.u8();
    if (t != static_cast<uint8_t>(LanMessageType::Announce) && t != static_cast<uint8_t>(LanMessageType::Goodbye)) return false;
    LanRoomInfo i;
    i.protocol = r.u16();
    i.room_id = r.u32();
    i.tcp_port = r.u16();
    i.players = r.u8();
    i.seats = r.u8();
    i.version = r.str8();
    i.host_name = r.str8();
    i.map_name = r.str8();
    if (!r.done()) return false;                                                     // whole datagram, exactly
    if (i.version.size() > kLanMaxVersionChars || i.host_name.size() > kMaxNameChars || i.map_name.size() > kMaxMapNameChars) return false;
    if (!is_printable(i.version) || !is_printable(i.host_name) || !is_printable(i.map_name)) return false;
    if (i.seats == 0 || i.seats > kLanMaxSeats || i.players == 0 || i.players > i.seats || i.tcp_port == 0) return false;
    type = static_cast<LanMessageType>(t);
    info = std::move(i);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// The announcer
// ---------------------------------------------------------------------------------------------------------------------------------------------

struct LanAnnouncer::Impl {
    socket_t sock{kBadSocket};
    uint16_t port{0};
    bool loopback_only{false};
    LanRoomInfo room;
    bool have_room{false};
    bool dirty{false};
    bool announced{false};                   // at least one announcement went out
    bool said_goodbye{false};
    uint32_t last_send_ms{0};
    uint32_t last_targets_ms{0};
    bool have_targets{false};
    std::vector<uint32_t> targets;           // IPv4 addresses, host order
    uint32_t sent{0};

    ~Impl() {
        if (sock != kBadSocket) close_socket(sock);
    }

    void refresh_targets(uint32_t now) {
        std::set<uint32_t> set;
        set.insert(kLoopbackHost);                                                   // another copy of the game on this machine
        if (!loopback_only) {
            set.insert(kLimitedBroadcast);
            for (uint32_t b : interface_broadcasts()) set.insert(b);
        }
        targets.assign(set.begin(), set.end());
        last_targets_ms = now;
        have_targets = true;
    }

    void send_all(LanMessageType type) {
        const std::vector<uint8_t> bytes = encode_lan_message(type, room);
        for (uint32_t target : targets) {
            sockaddr_in to;
            std::memset(&to, 0, sizeof(to));
            to.sin_family = AF_INET;
            to.sin_port = htons(port);
            to.sin_addr.s_addr = htonl(target);
            ::sendto(sock, reinterpret_cast<const char*>(bytes.data()), static_cast<iolen_t>(bytes.size()), 0, reinterpret_cast<const sockaddr*>(&to),
                     static_cast<addrlen_t>(sizeof(to)));      // a network that is down is not an error worth a word: the next second tries again
            ++sent;
        }
    }
};

LanAnnouncer::LanAnnouncer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
LanAnnouncer::~LanAnnouncer() = default;

std::unique_ptr<LanAnnouncer> LanAnnouncer::open(uint16_t port, bool loopback_only) {
    ensure_sockets();
    const socket_t s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kBadSocket) return nullptr;
    if (!set_nonblocking(s)) {
        close_socket(s);
        return nullptr;
    }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof(one));
    auto impl = std::make_unique<Impl>();
    impl->sock = s;
    impl->port = port;
    impl->loopback_only = loopback_only;
    return std::unique_ptr<LanAnnouncer>(new LanAnnouncer(std::move(impl)));
}

void LanAnnouncer::set_room(const LanRoomInfo& info) {
    if (!impl_->have_room || impl_->room != info) {
        impl_->room = info;
        impl_->have_room = true;
        impl_->dirty = true;
    }
}

void LanAnnouncer::update(uint32_t now_ms) {
    Impl& m = *impl_;
    if (!m.have_room || m.said_goodbye) return;
    if (!m.have_targets || now_ms - m.last_targets_ms >= kTargetRefreshMs) m.refresh_targets(now_ms);
    if (!m.announced || m.dirty || now_ms - m.last_send_ms >= kLanAnnounceMs) {
        m.send_all(LanMessageType::Announce);
        m.last_send_ms = now_ms;
        m.announced = true;
        m.dirty = false;
    }
}

void LanAnnouncer::goodbye() {
    Impl& m = *impl_;
    if (!m.have_room || m.said_goodbye) return;
    m.said_goodbye = true;
    if (!m.announced) return;                                                        // nobody ever heard of the room
    if (!m.have_targets) m.refresh_targets(m.last_targets_ms);
    m.send_all(LanMessageType::Goodbye);
    m.send_all(LanMessageType::Goodbye);                                             // a datagram can be lost; the list also drops the room after a silence
}

uint32_t LanAnnouncer::datagrams_sent() const noexcept { return impl_->sent; }

// ---------------------------------------------------------------------------------------------------------------------------------------------
// The browser
// ---------------------------------------------------------------------------------------------------------------------------------------------

struct LanBrowser::Impl {
    socket_t sock{kBadSocket};
    uint16_t port{0};
    std::vector<LanRoom> rooms;              // unsorted

    ~Impl() {
        if (sock != kBadSocket) close_socket(sock);
    }

    LanRoom* find(uint32_t room_id) {
        for (LanRoom& r : rooms) {
            if (r.info.room_id == room_id) return &r;
        }
        return nullptr;
    }

    void take(const uint8_t* data, size_t size, uint32_t from, uint32_t now) {
        LanMessageType type;
        LanRoomInfo info;
        if (!decode_lan_message(data, size, type, info)) return;
        LanRoom* known = find(info.room_id);
        if (type == LanMessageType::Goodbye) {
            if (known != nullptr) rooms.erase(rooms.begin() + (known - rooms.data()));
            return;
        }
        if (known == nullptr) {
            if (rooms.size() >= kLanMaxRooms) return;                                // a flood of made-up rooms cannot grow the list
            rooms.emplace_back();
            known = &rooms.back();
            known->address = address_text(from);
        } else if (!is_loopback(from) || known->address.rfind("127.", 0) == 0) {
            known->address = address_text(from);                                     // a room heard on its LAN address and on its loopback is at the LAN address
        }
        known->info = std::move(info);
        known->last_heard_ms = now;
        known->compatible = known->info.protocol == kProtocolVersion;
    }
};

LanBrowser::LanBrowser(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
LanBrowser::~LanBrowser() = default;

std::unique_ptr<LanBrowser> LanBrowser::open(uint16_t port) {
    ensure_sockets();
    const socket_t s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kBadSocket) return nullptr;
    if (!set_nonblocking(s)) {
        close_socket(s);
        return nullptr;
    }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));   // two copies of the game on one machine share the port
#ifdef SO_REUSEPORT
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&one), sizeof(one));
#endif
    sockaddr_in any;
    std::memset(&any, 0, sizeof(any));
    any.sin_family = AF_INET;
    any.sin_addr.s_addr = htonl(INADDR_ANY);
    any.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&any), static_cast<addrlen_t>(sizeof(any))) != 0) {
        close_socket(s);
        return nullptr;
    }
    sockaddr_in bound;
    std::memset(&bound, 0, sizeof(bound));
    addrlen_t len = static_cast<addrlen_t>(sizeof(bound));
    uint16_t actual = port;
    if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) == 0) actual = ntohs(bound.sin_port);
    auto impl = std::make_unique<Impl>();
    impl->sock = s;
    impl->port = actual;
    return std::unique_ptr<LanBrowser>(new LanBrowser(std::move(impl)));
}

void LanBrowser::update(uint32_t now_ms) {
    Impl& m = *impl_;
    uint8_t buf[512];                                                                // anything longer than a message is cut and then refused as too long
    for (int i = 0; i < kMaxDatagramsPerUpdate; ++i) {
        sockaddr_in from;
        std::memset(&from, 0, sizeof(from));
        addrlen_t from_len = static_cast<addrlen_t>(sizeof(from));
        const auto n = ::recvfrom(m.sock, reinterpret_cast<char*>(buf), static_cast<iolen_t>(sizeof(buf)), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
        if (n < 0) break;                                                            // nothing waiting (or an error that will not get better by asking again now)
        if (from.sin_family != AF_INET) continue;
        m.take(buf, static_cast<size_t>(n), ntohl(from.sin_addr.s_addr), now_ms);
    }
    m.rooms.erase(std::remove_if(m.rooms.begin(), m.rooms.end(), [now_ms](const LanRoom& r) { return now_ms - r.last_heard_ms > kLanExpireMs; }), m.rooms.end());
}

std::vector<LanRoom> LanBrowser::rooms() const {
    std::vector<LanRoom> out = impl_->rooms;
    std::sort(out.begin(), out.end(), [](const LanRoom& a, const LanRoom& b) {
        if (a.info.host_name != b.info.host_name) return a.info.host_name < b.info.host_name;
        if (a.address != b.address) return a.address < b.address;
        return a.info.room_id < b.info.room_id;
    });
    return out;
}

uint16_t LanBrowser::port() const noexcept { return impl_->port; }

}  // namespace ants::net
