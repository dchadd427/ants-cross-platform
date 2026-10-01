// Tests of the discovery of games on the local network (ants_net/lan.hpp): the datagram codec (layout, limits, refusal of everything that is not a whole
// and sane message, fuzzing), and an announcer and browsers over real UDP sockets on the loopback interface (appearing, changing, goodbye, expiry, telling
// rooms apart, garbage from the network, the size of the list, the pace of the announcements).
#include "ants_net/lan.hpp"
#include "ants_net/protocol.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace ants::net;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond)                                                                                        \
    do {                                                                                                         \
        ++g_assert_count;                                                                                        \
        if (!(cond)) {                                                                                           \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures;                                                                                   \
            return;                                                                                              \
        }                                                                                                        \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

namespace {

LanRoomInfo make_info(uint32_t id, const std::string& host, const std::string& map, uint8_t players = 1, uint8_t seats = 4, uint16_t port = 4001,
                      const std::string& version = "v0.0.78") {
    LanRoomInfo i;
    i.room_id = id;
    i.host_name = host;
    i.map_name = map;
    i.players = players;
    i.seats = seats;
    i.tcp_port = port;
    i.version = version;
    return i;
}

bool decodes(const std::vector<uint8_t>& bytes) {
    LanMessageType t;
    LanRoomInfo i;
    return decode_lan_message(bytes.data(), bytes.size(), t, i);
}

// Runs `step(now)` with a game clock that moves 50 ms per round (and a moment of real time so that the kernel can deliver the datagrams) until
// `cond()` holds. The game clock is virtual but the sockets are real: a wait that has used its game time gets up to a second more of real time with the
// clock standing still (the whole budget can pass in a few real milliseconds on a busy machine); a wait that succeeds never gets there.
bool wait_until(uint32_t& now, const std::function<void(uint32_t)>& step, const std::function<bool()>& cond, uint32_t max_ms = 4000) {
    const uint32_t end = now + max_ms;
    while (now < end) {
        if (cond()) return true;
        now += 50;
        step(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (int i = 0; i < 1000 && !cond(); ++i) {
        step(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return cond();
}

// Game time passes without waiting for anything (a silent network)
void run_for(uint32_t& now, const std::function<void(uint32_t)>& step, uint32_t ms) {
    const uint32_t end = now + ms;
    while (now < end) {
        now += 50;
        step(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// A plain UDP socket that sends raw datagrams to 127.0.0.1:port (garbage from the network)
struct RawSender {
#ifdef _WIN32
    using sock_t = SOCKET;
    using len_t = int;
    static constexpr sock_t kBad = INVALID_SOCKET;
#else
    using sock_t = int;
    using len_t = size_t;
    static constexpr sock_t kBad = -1;
#endif
    sock_t s{kBad};
    RawSender() { s = ::socket(AF_INET, SOCK_DGRAM, 0); }
    ~RawSender() {
        if (s == kBad) return;
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
    }
    bool ok() const { return s != kBad; }
    void send(uint16_t port, const std::vector<uint8_t>& bytes) const {
        sockaddr_in to;
        std::memset(&to, 0, sizeof(to));
        to.sin_family = AF_INET;
        to.sin_port = htons(port);
        to.sin_addr.s_addr = htonl(0x7F000001u);
        ::sendto(s, reinterpret_cast<const char*>(bytes.data()), static_cast<len_t>(bytes.size()), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    }
};

uint64_t next_random(uint64_t& state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port: discovery of games on the local network\n"
                 "=======================================================\n";

    // ---------------------------------------------------------------------------------------------------------------------------------------------
    // The datagram
    // ---------------------------------------------------------------------------------------------------------------------------------------------
    TEST_CASE("L1.1 A room message round-trips: layout, announce and goodbye, empty and longest strings") {
        const LanRoomInfo a = make_info(0xDEADBEEFu, "Alice", "TINY.LVL", 2, 4, 4001, "v0.0.78");
        const std::vector<uint8_t> bytes = encode_lan_message(LanMessageType::Announce, a);
        ASSERT_EQ(bytes.size(), size_t{16 + 8 + 6 + 9});
        ASSERT_TRUE(bytes[0] == 'A' && bytes[1] == 'N' && bytes[2] == 'T' && bytes[3] == 'L');
        ASSERT_EQ(bytes[4], uint8_t{1});                                   // format
        ASSERT_EQ(bytes[5], uint8_t{1});                                   // Announce
        ASSERT_EQ(bytes[6], static_cast<uint8_t>(kProtocolVersion & 0xFF)); // protocol, little endian
        ASSERT_EQ(bytes[7], static_cast<uint8_t>(kProtocolVersion >> 8));
        ASSERT_TRUE(bytes[8] == 0xEF && bytes[9] == 0xBE && bytes[10] == 0xAD && bytes[11] == 0xDE);    // room id
        ASSERT_TRUE(bytes[12] == (4001 & 0xFF) && bytes[13] == (4001 >> 8));                              // TCP port
        ASSERT_EQ(bytes[14], uint8_t{2});                                  // players
        ASSERT_EQ(bytes[15], uint8_t{4});                                  // seats
        LanMessageType t = LanMessageType::Goodbye;
        LanRoomInfo b;
        ASSERT_TRUE(decode_lan_message(bytes.data(), bytes.size(), t, b));
        ASSERT_TRUE(t == LanMessageType::Announce);
        ASSERT_TRUE(b == a);

        const std::vector<uint8_t> bye = encode_lan_message(LanMessageType::Goodbye, a);
        ASSERT_EQ(bye[5], uint8_t{2});
        ASSERT_TRUE(decode_lan_message(bye.data(), bye.size(), t, b));
        ASSERT_TRUE(t == LanMessageType::Goodbye && b == a);

        const LanRoomInfo empty = make_info(1, "", "", 1, 1, 1, "");
        const std::vector<uint8_t> e = encode_lan_message(LanMessageType::Announce, empty);
        ASSERT_EQ(e.size(), size_t{16 + 3});
        ASSERT_TRUE(decode_lan_message(e.data(), e.size(), t, b) && b == empty);

        const LanRoomInfo longest = make_info(0xFFFFFFFFu, std::string(kMaxNameChars, 'N'), std::string(kMaxMapNameChars, 'M'), 8, 8, 65535,
                                              std::string(kLanMaxVersionChars, 'V'));
        const std::vector<uint8_t> l = encode_lan_message(LanMessageType::Announce, longest);
        ASSERT_EQ(l.size(), size_t{16 + 17 + 33 + 65});                    // protocol 6: map names of up to 64 characters (131 bytes: below kLanMaxDatagram)
        ASSERT_TRUE(l.size() <= kLanMaxDatagram);
        ASSERT_TRUE(decode_lan_message(l.data(), l.size(), t, b) && b == longest);
    } TEST_END();

    TEST_CASE("L1.2 What goes out is printable ASCII and cut to its limits; what comes in must already be") {
        LanRoomInfo a = make_info(7, "Jos\xC3\xA9\t\x01Q", std::string(90, 'm'), 1, 4, 4001, std::string(40, 'v'));
        const std::vector<uint8_t> bytes = encode_lan_message(LanMessageType::Announce, a);
        LanMessageType t;
        LanRoomInfo b;
        ASSERT_TRUE(decode_lan_message(bytes.data(), bytes.size(), t, b));
        ASSERT_EQ(b.host_name, std::string("Jos????Q"));                  // every byte that is not 0x20 .. 0x7E became '?'
        ASSERT_EQ(b.map_name, std::string(kMaxMapNameChars, 'm'));
        ASSERT_EQ(b.version, std::string(kLanMaxVersionChars, 'v'));
        // a datagram that carries such a byte itself is refused
        std::vector<uint8_t> raw = encode_lan_message(LanMessageType::Announce, make_info(7, "Alice", "TINY.LVL"));
        ASSERT_TRUE(decodes(raw));
        raw[16 + 8 + 1] = 0x07;                                            // a control character inside the host name
        ASSERT_FALSE(decodes(raw));
        raw[16 + 8 + 1] = 0xE9;                                            // a byte above 0x7E
        ASSERT_FALSE(decodes(raw));
    } TEST_END();

    TEST_CASE("L1.3 Everything that is not a whole, sane message is refused") {
        const std::vector<uint8_t> good = encode_lan_message(LanMessageType::Announce, make_info(0x01020304u, "Alice", "TINY.LVL", 2, 4, 4001, "v0.0.78"));
        ASSERT_TRUE(decodes(good));
        ASSERT_FALSE(decodes({}));
        for (size_t n = 0; n < good.size(); ++n) ASSERT_FALSE(decodes(std::vector<uint8_t>(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(n))));   // every truncation
        for (size_t i = 0; i < 4; ++i) {                                   // the magic
            std::vector<uint8_t> m = good;
            m[i] = static_cast<uint8_t>(m[i] ^ 0x20);
            ASSERT_FALSE(decodes(m));
        }
        const auto with = [&](size_t index, uint8_t value) {
            std::vector<uint8_t> m = good;
            m[index] = value;
            return m;
        };
        ASSERT_FALSE(decodes(with(4, 0)));                                 // format
        ASSERT_FALSE(decodes(with(4, 2)));
        ASSERT_FALSE(decodes(with(5, 0)));                                 // type
        ASSERT_FALSE(decodes(with(5, 3)));
        ASSERT_FALSE(decodes(with(5, 255)));
        ASSERT_FALSE(decodes(with(14, 0)));                                // no player at all
        ASSERT_FALSE(decodes(with(15, 0)));                                // no seats
        ASSERT_FALSE(decodes(with(15, kLanMaxSeats + 1)));                 // too many seats
        ASSERT_FALSE(decodes(with(14, 5)));                                // more players than seats (seats is 4)
        ASSERT_TRUE(decodes(with(14, 4)));
        std::vector<uint8_t> noport = good;
        noport[12] = 0;
        noport[13] = 0;
        ASSERT_FALSE(decodes(noport));
        std::vector<uint8_t> trailing = good;
        trailing.push_back(0);
        ASSERT_FALSE(decodes(trailing));                                   // exact length
        std::vector<uint8_t> toolong = good;                               // a version text longer than its limit
        toolong[16] = static_cast<uint8_t>(kLanMaxVersionChars + 1);
        toolong.insert(toolong.begin() + 17, kLanMaxVersionChars + 1 - 7, 'x');
        ASSERT_FALSE(decodes(toolong));
        std::vector<uint8_t> beyond = good;                                // a string that claims to reach beyond the datagram
        beyond[16 + 8] = 200;
        ASSERT_FALSE(decodes(beyond));
        ASSERT_FALSE(decodes(std::vector<uint8_t>(kLanMaxDatagram + 1, 'A')));
    } TEST_END();

    TEST_CASE("L1.4 20000 random and mutated datagrams never crash the decoder, and every accepted one is exactly what the encoder writes") {
        uint64_t rng = 0x9E3779B97F4A7C15ull;
        size_t accepted = 0;
        const std::vector<uint8_t> seed_msg = encode_lan_message(LanMessageType::Announce, make_info(99, "Queen", "GAUNTLET.LVL", 3, 4, 4001, "v0.0.78"));
        for (int round = 0; round < 20000; ++round) {
            std::vector<uint8_t> d;
            const uint64_t kind = next_random(rng) % 4;
            if (kind == 0) {                                               // pure noise
                d.resize(next_random(rng) % 140);
                for (auto& b : d) b = static_cast<uint8_t>(next_random(rng));
            } else if (kind == 1) {                                        // noise behind the real magic
                d = {'A', 'N', 'T', 'L', 1};
                const size_t n = next_random(rng) % 100;
                for (size_t i = 0; i < n; ++i) d.push_back(static_cast<uint8_t>(next_random(rng)));
            } else {                                                       // a real message with a few damaged bytes
                d = seed_msg;
                const size_t flips = 1 + next_random(rng) % 3;
                for (size_t i = 0; i < flips; ++i) d[next_random(rng) % d.size()] = static_cast<uint8_t>(next_random(rng));
                if (kind == 3 && next_random(rng) % 2 == 0) d.resize(next_random(rng) % (d.size() + 1));
            }
            LanMessageType t;
            LanRoomInfo info;
            if (decode_lan_message(d.data(), d.size(), t, info)) {
                ++accepted;
                ASSERT_TRUE(info.host_name.size() <= kMaxNameChars && info.map_name.size() <= kMaxMapNameChars && info.version.size() <= kLanMaxVersionChars);
                ASSERT_TRUE(info.players >= 1 && info.players <= info.seats && info.seats <= kLanMaxSeats && info.tcp_port != 0);
                ASSERT_TRUE(encode_lan_message(t, info) == d);             // canonical: nothing was silently repaired
            }
        }
        ASSERT_TRUE(accepted > 500);                                       // the mutations that leave a message intact exist, so the test does reach the accepting path
    } TEST_END();

    // ---------------------------------------------------------------------------------------------------------------------------------------------
    // Announcer and browsers over the loopback interface
    // ---------------------------------------------------------------------------------------------------------------------------------------------
    TEST_CASE("L1.5 A browser hears an announcing room, with its fields and the address it came from") {
        auto browser = LanBrowser::open(0);
        ASSERT_TRUE(browser != nullptr);
        ASSERT_TRUE(browser->port() != 0);
        auto announcer = LanAnnouncer::open(browser->port(), true);
        ASSERT_TRUE(announcer != nullptr);
        ASSERT_TRUE(browser->rooms().empty());
        announcer->set_room(make_info(1234, "Alice", "TINY.LVL", 1, 4, 4321, "v0.0.78"));
        uint32_t now = 1000;
        const auto step = [&](uint32_t t) {
            announcer->update(t);
            browser->update(t);
        };
        ASSERT_TRUE(wait_until(now, step, [&]() { return browser->rooms().size() == 1; }));
        const LanRoom room = browser->rooms()[0];
        ASSERT_EQ(room.address, std::string("127.0.0.1"));
        ASSERT_EQ(room.info.room_id, 1234u);
        ASSERT_EQ(room.info.host_name, std::string("Alice"));
        ASSERT_EQ(room.info.map_name, std::string("TINY.LVL"));
        ASSERT_EQ(room.info.players, uint8_t{1});
        ASSERT_EQ(room.info.seats, uint8_t{4});
        ASSERT_EQ(room.info.tcp_port, uint16_t{4321});
        ASSERT_EQ(room.info.version, std::string("v0.0.78"));
        ASSERT_TRUE(room.compatible);
    } TEST_END();

    TEST_CASE("L1.6 A changed room is updated in place at once; a goodbye removes it at once") {
        auto browser = LanBrowser::open(0);
        auto announcer = LanAnnouncer::open(browser->port(), true);
        uint32_t now = 1000;
        const auto step = [&](uint32_t t) {
            announcer->update(t);
            browser->update(t);
        };
        announcer->set_room(make_info(5, "Alice", "TINY.LVL", 1));
        ASSERT_TRUE(wait_until(now, step, [&]() { return browser->rooms().size() == 1; }));
        // a change does not wait for the next second: fewer than one announce period passes
        const uint32_t before = now;
        announcer->set_room(make_info(5, "Alice", "SMALL.LVL", 2));
        ASSERT_TRUE(wait_until(now, step, [&]() { return !browser->rooms().empty() && browser->rooms()[0].info.players == 2; }, 400));
        ASSERT_TRUE(now - before < kLanAnnounceMs);
        ASSERT_EQ(browser->rooms().size(), size_t{1});
        ASSERT_EQ(browser->rooms()[0].info.map_name, std::string("SMALL.LVL"));
        announcer->goodbye();
        ASSERT_TRUE(wait_until(now, step, [&]() { return browser->rooms().empty(); }, 400));
        const uint32_t sent = announcer->datagrams_sent();
        run_for(now, step, 3000);                                          // a room that said goodbye stays silent
        ASSERT_EQ(announcer->datagrams_sent(), sent);
        ASSERT_TRUE(browser->rooms().empty());
    } TEST_END();

    TEST_CASE("L1.7 A room that falls silent is dropped after 3.5 seconds, not before") {
        auto browser = LanBrowser::open(0);
        auto announcer = LanAnnouncer::open(browser->port(), true);
        uint32_t now = 1000;
        announcer->set_room(make_info(6, "Alice", "TINY.LVL"));
        const auto both = [&](uint32_t t) {
            announcer->update(t);
            browser->update(t);
        };
        ASSERT_TRUE(wait_until(now, both, [&]() { return browser->rooms().size() == 1; }));
        announcer.reset();                                                 // the host vanishes without a word (crash, cable)
        const auto listen = [&](uint32_t t) { browser->update(t); };
        const uint32_t heard = browser->rooms()[0].last_heard_ms;
        now = heard;
        run_for(now, listen, kLanExpireMs - 100);
        ASSERT_EQ(browser->rooms().size(), size_t{1});
        run_for(now, listen, 300);
        ASSERT_TRUE(browser->rooms().empty());
    } TEST_END();

    TEST_CASE("L1.8 Rooms are told apart by their id and listed by host name; the same room is one entry however often it is heard") {
        auto browser = LanBrowser::open(0);
        auto zed = LanAnnouncer::open(browser->port(), true);
        auto amy = LanAnnouncer::open(browser->port(), true);
        auto amy_second = LanAnnouncer::open(browser->port(), true);       // one host with two rooms
        uint32_t now = 1000;
        zed->set_room(make_info(300, "Zed", "TINY.LVL"));
        amy->set_room(make_info(100, "Amy", "SMALL.LVL"));
        amy_second->set_room(make_info(200, "Amy", "MEDIUM.LVL"));
        const auto step = [&](uint32_t t) {
            zed->update(t);
            amy->update(t);
            amy_second->update(t);
            browser->update(t);
        };
        ASSERT_TRUE(wait_until(now, step, [&]() { return browser->rooms().size() == 3; }));
        run_for(now, step, 2500);                                          // heard again and again: still three
        const std::vector<LanRoom> rooms = browser->rooms();
        ASSERT_EQ(rooms.size(), size_t{3});
        ASSERT_EQ(rooms[0].info.host_name, std::string("Amy"));            // by host name, then by address, then by id
        ASSERT_EQ(rooms[0].info.room_id, 100u);
        ASSERT_EQ(rooms[1].info.host_name, std::string("Amy"));
        ASSERT_EQ(rooms[1].info.room_id, 200u);
        ASSERT_EQ(rooms[2].info.host_name, std::string("Zed"));
    } TEST_END();

    TEST_CASE("L1.9 Garbage from the network is ignored; a room of another protocol version is listed but marked") {
        auto browser = LanBrowser::open(0);
        RawSender raw;
        ASSERT_TRUE(raw.ok());
        uint32_t now = 1000;
        const auto listen = [&](uint32_t t) { browser->update(t); };
        raw.send(browser->port(), {});
        raw.send(browser->port(), {'h', 'e', 'l', 'l', 'o'});
        raw.send(browser->port(), std::vector<uint8_t>(200, 0xFF));
        std::vector<uint8_t> damaged = encode_lan_message(LanMessageType::Announce, make_info(1, "Alice", "TINY.LVL"));
        damaged[5] = 9;
        raw.send(browser->port(), damaged);
        LanRoomInfo old_room = make_info(2, "Old", "TINY.LVL");
        old_room.protocol = static_cast<uint16_t>(kProtocolVersion + 1);
        raw.send(browser->port(), encode_lan_message(LanMessageType::Announce, old_room));
        ASSERT_TRUE(wait_until(now, listen, [&]() { return !browser->rooms().empty(); }));
        run_for(now, listen, 200);
        const std::vector<LanRoom> rooms = browser->rooms();
        ASSERT_EQ(rooms.size(), size_t{1});                                // only the well-formed message was taken
        ASSERT_EQ(rooms[0].info.host_name, std::string("Old"));
        ASSERT_FALSE(rooms[0].compatible);
        // a goodbye for a room that was never heard does nothing
        raw.send(browser->port(), encode_lan_message(LanMessageType::Goodbye, make_info(777, "Ghost", "TINY.LVL")));
        run_for(now, listen, 200);
        ASSERT_EQ(browser->rooms().size(), size_t{1});
    } TEST_END();

    TEST_CASE("L1.10 The list never holds more than 64 rooms; the rooms it knows keep updating") {
        auto browser = LanBrowser::open(0);
        RawSender raw;
        ASSERT_TRUE(raw.ok());
        uint32_t now = 1000;
        const auto listen = [&](uint32_t t) { browser->update(t); };
        for (uint32_t id = 1; id <= 100; ++id) raw.send(browser->port(), encode_lan_message(LanMessageType::Announce, make_info(id, "Host" + std::to_string(id), "TINY.LVL")));
        ASSERT_TRUE(wait_until(now, listen, [&]() { return browser->rooms().size() >= kLanMaxRooms; }));
        run_for(now, listen, 300);
        ASSERT_EQ(browser->rooms().size(), kLanMaxRooms);
        // the first room is known (datagrams keep their order on the loopback): its change is taken although the list is full
        raw.send(browser->port(), encode_lan_message(LanMessageType::Announce, make_info(1, "Host1", "SMALL.LVL", 3)));
        ASSERT_TRUE(wait_until(now, listen, [&]() {
            for (const LanRoom& r : browser->rooms()) {
                if (r.info.room_id == 1) return r.info.players == 3;
            }
            return false;
        }));
        ASSERT_EQ(browser->rooms().size(), kLanMaxRooms);
    } TEST_END();

    TEST_CASE("L1.11 An announcer speaks once a second, and at once when the room changes") {
        auto browser = LanBrowser::open(0);
        auto announcer = LanAnnouncer::open(browser->port(), true);
        announcer->set_room(make_info(9, "Alice", "TINY.LVL"));
        uint32_t now = 1000;
        announcer->update(now);                                            // the first update announces
        const uint32_t first = announcer->datagrams_sent();
        ASSERT_TRUE(first >= 1);
        for (int i = 0; i < 18; ++i) {                                     // 900 ms: nothing more
            now += 50;
            announcer->update(now);
        }
        ASSERT_EQ(announcer->datagrams_sent(), first);
        now += 100;                                                        // 1000 ms after the first
        announcer->update(now);
        const uint32_t second = announcer->datagrams_sent();
        ASSERT_EQ(second, 2 * first);                                      // one datagram per destination each time
        announcer->set_room(make_info(9, "Alice", "TINY.LVL"));            // the same room again: no news
        now += 50;
        announcer->update(now);
        ASSERT_EQ(announcer->datagrams_sent(), second);
        announcer->set_room(make_info(9, "Alice", "TINY.LVL", 2));         // a change: at once
        now += 50;
        announcer->update(now);
        ASSERT_EQ(announcer->datagrams_sent(), 3 * first);
    } TEST_END();

    TEST_CASE("L1.12 A broadcast reaches a browser beside the host (skipped where the machine has no broadcast route)") {
        auto browser = LanBrowser::open(0);
        auto announcer = LanAnnouncer::open(browser->port(), false);       // the limited broadcast and every interface's broadcast address, and this machine
        ASSERT_TRUE(announcer != nullptr);
        announcer->set_room(make_info(31, "Alice", "TINY.LVL"));
        uint32_t now = 1000;
        const auto step = [&](uint32_t t) {
            announcer->update(t);
            browser->update(t);
        };
        // the loopback destination is always among them, so the room is heard; how many copies arrive depends on the machine
        ASSERT_TRUE(wait_until(now, step, [&]() { return !browser->rooms().empty(); }));
        run_for(now, step, 2200);
        ASSERT_EQ(browser->rooms().size(), size_t{1});                     // the same room heard over several destinations is still one entry
        ASSERT_EQ(browser->rooms()[0].info.room_id, 31u);
    } TEST_END();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
