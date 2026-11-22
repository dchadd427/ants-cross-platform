// The limits per network address on the game server's doors (docs/ADDRESS_LIMITS.md): what an address may do in a minute and an hour (connections, rooms made, rooms held open, Hellos for rooms that do not
// exist, relay credentials), which address a connection counts under (the proxy's X-Client-Address only from a trusted proxy, an IPv6 address as its /64), and what the limits never do: stop the people of one
// household from hosting and playing at once, or stop anybody who knows a room's code from joining it.
#include "server_fixtures.hpp"

#include "ants_net/p2p.hpp"
#include "ants_net/turn.hpp"
#include "ants_server/address_limits.hpp"

namespace {

constexpr const char* kHousehold = "192.0.2.10";           // the address of a house whose people all play from it
constexpr const char* kCarrier = "198.51.100.7";            // an address that a phone carrier shares among many people: used as a plain second address here

// The limits that the program runs with, on a server that offers public rooms and lobbies on TINY
ServerLimits server_limits() {
    ServerLimits l;
    l.demo_rooms = 40;
    l.demo_lobbies = 40;
    l.demo_map = "TINY.LVL";
    l.demo_maps = {"TINY.LVL", "SMALL.LVL"};
    l.address = AddressLimits::standard();
    return l;
}

// What a Hello was answered: 0 while it has not been, else the reason of the Reject
net::RejectReason answered(Client& c) { return c.lobby->phase() == net::ClientLobby::Phase::Rejected ? c.lobby->reject_reason() : static_cast<net::RejectReason>(0); }

bool in_room(Client& c) { return c.lobby->phase() == net::ClientLobby::Phase::InRoom || c.lobby->phase() == net::ClientLobby::Phase::Loading || c.lobby->phase() == net::ClientLobby::Phase::Begun; }

std::string code_of(const char* prefix, int n) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%s%04d", prefix, n);
    return buf;
}

// The waiting lobby rooms of a server
size_t lobbies(World& w) {
    size_t n = 0;
    for (const RoomStatus& s : w.mgr.list(w.now)) n += s.lobby && s.state == RoomState::Waiting ? 1u : 0u;
    return n;
}

uint32_t unix_seconds() { return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()); }

// A match of two persons on a server that has a relay: each person asks for it once the match runs. `address` is where the person of each seat connects from. The answers, one list for each person.
std::vector<std::vector<net::IceConfigMsg>> relay_match(World& w, const std::string& code, const std::array<std::string, 2>& address) {
    RoomSpec spec = spec_of(code, 2);
    std::vector<std::vector<net::IceConfigMsg>> out(2);
    if (!w.mgr.create_room(spec, w.now).ok) return out;
    std::vector<Client*> persons;
    for (size_t i = 0; i < 2; ++i) {
        w.next_address = address[i];
        persons.push_back(&w.connect("Person" + std::to_string(i), code));
    }
    for (Client* c : persons) c->next_order_ms = 0xFFFFFFFFu;
    for (int i = 0; i < 400 && w.status(code).state != RoomState::Running; ++i) w.run(10);
    w.run(300);
    for (size_t i = 0; i < 2; ++i) {
        if (persons[i]->session == nullptr) return out;
        persons[i]->session->set_on_p2p([&out, i](const std::vector<uint8_t>& m) {
            net::IceConfigMsg ice;
            if (net::decode(m, ice)) out[i].push_back(ice);
        });
        persons[i]->session->send_p2p(net::encode(net::P2pConfigMsg{false}));
    }
    w.run(500);
    return out;
}

bool given(const std::vector<net::IceConfigMsg>& answers) { return answers.size() == 1 && !answers[0].credential.empty() && !answers[0].urls.empty(); }

const std::string kGoodRequest = "GET /ws HTTP/1.1\r\nHost: example.org\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n";

#if !defined(_WIN32) && defined(ANTS_SERVER_BINARY)
// The real program as a child process (only the process that this object started is ever signalled)
struct ProgramProcess {
    pid_t pid{-1};
    bool start(const std::vector<std::string>& args) {
        std::vector<std::string> storage;
        storage.push_back(ANTS_SERVER_BINARY);
        for (const std::string& a : args) storage.push_back(a);
        std::vector<char*> argv;
        for (std::string& a : storage) argv.push_back(&a[0]);
        argv.push_back(nullptr);
        pid = ::fork();
        if (pid < 0) return false;
        if (pid == 0) {
            const int quiet = ::open("/dev/null", O_WRONLY);
            if (quiet >= 0) {
                ::dup2(quiet, 1);
                ::dup2(quiet, 2);
                ::close(quiet);
            }
            ::execv(argv[0], argv.data());
            ::_exit(127);
        }
        return true;
    }
    // The exit status when the program ends within `timeout_ms`, else -1 (it is still there)
    int exit_status(uint32_t timeout_ms) {
        for (uint32_t waited = 0; waited <= timeout_ms; waited += 5) {
            int status = 0;
            if (::waitpid(pid, &status, WNOHANG) == pid) {
                pid = -1;
                return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return -1;
    }
    ~ProgramProcess() {
        if (pid > 0) {
            ::kill(pid, SIGTERM);
            int status = 0;
            ::waitpid(pid, &status, 0);
        }
    }
};

uint16_t free_port() {
    auto listener = net::TcpListener::listen(0, true);
    return listener != nullptr ? listener->port() : uint16_t{0};
}

// A browser as the server's WebSocket door sees one: a handshake with the header that the site's proxy writes, then binary frames (masked) that hold the game's messages
struct WsPage {
    int sock{-1};
    net::WsFrameParser parser{false};
    bool open(uint16_t port, const std::string& client_address) {
        sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;
        sockaddr_in to;
        std::memset(&to, 0, sizeof to);
        to.sin_family = AF_INET;
        to.sin_port = htons(port);
        to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        timeval tv;
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        if (::connect(sock, reinterpret_cast<sockaddr*>(&to), sizeof to) != 0) return false;
        std::string request = kGoodRequest + (client_address.empty() ? std::string() : "X-Client-Address: " + client_address + "\r\n") + "\r\n";
        if (::send(sock, request.data(), request.size(), 0) != static_cast<ssize_t>(request.size())) return false;
        std::string head;
        char c;
        while (head.size() < 4096 && (head.size() < 4 || head.compare(head.size() - 4, 4, "\r\n\r\n") != 0)) {
            if (::recv(sock, &c, 1, 0) != 1) return false;
            head.push_back(c);
        }
        return head.compare(0, 12, "HTTP/1.1 101") == 0;
    }
    bool send(const std::vector<uint8_t>& message) {
        const std::array<uint8_t, 4> mask = {0x12, 0x34, 0x56, 0x78};
        const std::vector<uint8_t> frame = net::ws_encode_frame(net::WsOpcode::Binary, message, true, &mask);
        return ::send(sock, frame.data(), frame.size(), 0) == static_cast<ssize_t>(frame.size());
    }
    // The next message of the server (a Ping is answered by nobody here: the server sends none in a few seconds)
    bool read(std::vector<uint8_t>& message) {
        for (int i = 0; i < 100; ++i) {
            std::vector<uint8_t> payload;
            uint16_t status = 0;
            const auto got = parser.next(payload, status);
            if (got == net::WsFrameParser::Result::Message) {
                message = std::move(payload);
                return true;
            }
            if (got != net::WsFrameParser::Result::NeedMore) return false;
            uint8_t buf[2048];
            const ssize_t n = ::recv(sock, buf, sizeof buf, 0);
            if (n <= 0) return false;
            parser.feed(buf, static_cast<size_t>(n));
        }
        return false;
    }
    // What a Hello with the lobby block for `room` was answered: Welcome, a Reject's reason, or 0 when no answer came
    int ask_for_a_lobby(const std::string& room) {
        net::HelloMsg hello;
        hello.name = "Page";
        hello.room = room;
        hello.client_kind = net::kClientPage;
        hello.create = lobby_block_of();
        std::vector<uint8_t> answer;
        if (!send(net::encode(hello)) || !read(answer)) return 0;
        if (net::peek_type(answer) == net::MsgType::Welcome) return -1;
        net::RejectMsg reject;
        return net::peek_type(answer) == net::MsgType::Reject && net::decode(answer, reject) ? static_cast<int>(reject.reason) : 0;
    }
    ~WsPage() {
        if (sock >= 0) ::close(sock);
    }
};

// The program with the WebSocket door and lobbies, and one room open for an address: `extra` are the options of the case. Waits until the door answers.
bool start_program(ProgramProcess& program, uint16_t port, const std::vector<std::string>& extra) {
    std::vector<std::string> args = {"--maps", maps_dir(), "--port", "0", "--ws-port", std::to_string(port), "--demo-rooms", "8", "--demo-map", "TINY.LVL", "--demo-lobbies", "8", "--address-rooms-open", "1"};
    for (const std::string& a : extra) args.push_back(a);
    if (!program.start(args)) return false;
    for (int i = 0; i < 1000; ++i) {
        WsPage probe;
        if (probe.open(port, "")) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
#endif

}  // namespace

void run_address_limit_tests() {
    TEST_CASE("S3.350 The Windows Count Per Address: Connections In A Second, Rooms In A Minute And Rooms Open, Wrong Codes In A Minute (The Wait Grows By Half A Second To Five), Relay Credentials In An Hour; Each Is Its Own Count, No Address Counts Another's, 0 Is Off, An Unknown Address Is Never Limited") {
        AddressLimits l;
        l.connections_per_second = 3;
        l.rooms_per_minute = 2;
        l.rooms_open = 4;
        l.wrong_codes_per_minute = 2;
        l.relay_per_hour = 2;
        AddressGuard g(l);
        const std::string a = "203.0.113.1";
        const std::string b = "203.0.113.2";
        // connections: three in a second, the fourth is refused, and the window slides (the first one leaves it exactly a second later)
        ASSERT_TRUE(g.allow_connection(a, 1000) && g.allow_connection(a, 1000) && g.allow_connection(a, 1400));
        ASSERT_FALSE(g.allow_connection(a, 1999));
        ASSERT_TRUE(g.allow_connection(a, 2000));                         // (one of the three of 1000 is a second old)
        ASSERT_TRUE(g.allow_connection(b, 1999));                         // (another address has its own count)
        ASSERT_EQ(g.refusals().connections, uint64_t{1});
        // rooms in a minute: two, then TooFast until the first is a minute old; rooms open is a count that the caller brings, and is asked first
        ASSERT_TRUE(g.allow_room(a, 0, 5000) == AddressGuard::RoomVerdict::Allowed);
        g.note_room_made(a, 5000);
        ASSERT_TRUE(g.allow_room(a, 1, 6000) == AddressGuard::RoomVerdict::Allowed);
        g.note_room_made(a, 6000);
        ASSERT_TRUE(g.allow_room(a, 2, 7000) == AddressGuard::RoomVerdict::TooFast);
        ASSERT_TRUE(g.allow_room(a, 4, 7000) == AddressGuard::RoomVerdict::TooMany);
        ASSERT_TRUE(g.allow_room(b, 3, 7000) == AddressGuard::RoomVerdict::Allowed);
        ASSERT_TRUE(g.allow_room(a, 2, 64999) == AddressGuard::RoomVerdict::TooFast);        // (the room of 5000 is 59,999 ms old)
        ASSERT_TRUE(g.allow_room(a, 2, 65000) == AddressGuard::RoomVerdict::Allowed);        // (a minute old: it has left the window, the one of 6000 counts, so one more may come)
        g.note_room_made(a, 65000);
        ASSERT_TRUE(g.allow_room(a, 2, 65500) == AddressGuard::RoomVerdict::TooFast);
        ASSERT_EQ(g.refusals().rooms_rate, uint64_t{3});
        ASSERT_EQ(g.refusals().rooms_open, uint64_t{1});
        // wrong codes: two answered at once, then each one waits half a second longer, up to five seconds; a minute later all is forgotten; another address has its own
        ASSERT_EQ(g.hold_now(a, 100000), 0u);                                        // (a try at a code that exists waits as the next wrong code would, and notes nothing)
        ASSERT_EQ(g.wrong_code(a, 100000), 0u);
        ASSERT_EQ(g.wrong_code(a, 100000), 0u);
        const size_t tracked_before = g.tracked();
        ASSERT_TRUE(g.hold_now(a, 100000) == 500u && g.hold_now(a, 100000) == 500u);
        ASSERT_TRUE(g.hold_now(b, 100000) == 0u && g.hold_now("203.0.113.99", 100000) == 0u && g.hold_now("", 100000) == 0u);
        ASSERT_EQ(g.tracked(), tracked_before);                                      // (an address that was never seen is not tracked for it)
        for (uint32_t k = 1; k <= 10; ++k) ASSERT_EQ(g.wrong_code(a, 100000), 500u * k);
        for (int k = 0; k < 40; ++k) ASSERT_EQ(g.wrong_code(a, 100000), kWrongCodeMaxHoldMs);     // (the count is kept a little past the allowance and no more: the wait stays at its longest)
        ASSERT_EQ(g.wrong_code(b, 100000), 0u);
        ASSERT_EQ(g.wrong_code(a, 160000), 0u);
        ASSERT_EQ(g.hold_now(a, 160000), 0u);                                        // (after it: asking forgets what is a minute old, and the first must see it for itself)
        // relay credentials in an hour
        ASSERT_TRUE(g.take_relay(a, 200000) && g.take_relay(a, 201000));
        ASSERT_FALSE(g.take_relay(a, 3000000));
        ASSERT_TRUE(g.take_relay(b, 3000000));
        ASSERT_FALSE(g.take_relay(a, 200000 + 3599999));
        ASSERT_TRUE(g.take_relay(a, 200000 + 3600000));
        ASSERT_EQ(g.refusals().relay, uint64_t{2});
        // an address that is not known is never limited, and neither is anything with its limit at 0
        for (int k = 0; k < 100; ++k) {
            ASSERT_TRUE(g.allow_connection("", 1000) && g.take_relay("", 1000) && g.allow_room("", 99, 1000) == AddressGuard::RoomVerdict::Allowed);
            ASSERT_EQ(g.wrong_code("", 1000), 0u);
        }
        AddressGuard off{AddressLimits()};
        for (int k = 0; k < 100; ++k) {
            ASSERT_TRUE(off.allow_connection(a, 1000) && off.take_relay(a, 1000) && off.allow_room(a, 99, 1000) == AddressGuard::RoomVerdict::Allowed);
            ASSERT_EQ(off.wrong_code(a, 1000), 0u);
        }
        ASSERT_EQ(off.refusals().total(), uint64_t{0});
        // the addresses that are counted are forgotten when they have been quiet for an hour, and there are never more than kMaxTrackedAddresses
        ASSERT_TRUE(g.tracked() >= 2);
        g.sweep(200000 + 3600000 + 3600001);
        ASSERT_EQ(g.tracked(), size_t{0});
        AddressGuard many(l);
        for (size_t k = 0; k < kMaxTrackedAddresses + 50; ++k) ASSERT_TRUE(many.allow_connection("a" + std::to_string(k), 1000));
        ASSERT_EQ(many.tracked(), kMaxTrackedAddresses);
    } TEST_END();

    TEST_CASE("S3.351 Every Limit Has An Option's Range And A Key: The Table Lists Each Member Once, The Program's Numbers Are In Their Ranges, 0 Or A Number Of The Range Sets One And Nothing Else Does") {
        AddressLimits standard = AddressLimits::standard();
        std::set<const void*> seen;
        for (const AddressLimitField& f : kAddressLimitFields) {
            ASSERT_TRUE(seen.insert(&(standard.*f.member)).second);
            ASSERT_TRUE(f.standard >= f.min && f.standard <= f.max);
            AddressLimits l = standard;
            std::string why;
            ASSERT_TRUE(set_address_limit(l, f.key, f.min, why) && l.*f.member == f.min);
            ASSERT_TRUE(set_address_limit(l, f.key, f.max, why) && l.*f.member == f.max);
            ASSERT_TRUE(set_address_limit(l, f.key, 0, why) && l.*f.member == 0u);
            AddressLimits before = l;
            ASSERT_FALSE(set_address_limit(l, f.key, f.max + 1, why));
            ASSERT_FALSE(why.empty());
            ASSERT_EQ(l.*f.member, before.*f.member);
            ASSERT_FALSE(set_address_limit(l, f.key, -1, why));
            if (f.min > 1) ASSERT_FALSE(set_address_limit(l, f.key, f.min - 1, why));
        }
        ASSERT_EQ(seen.size(), size_t{7});                                  // (the table is the struct: seven numbers)
        AddressLimits l;
        std::string why;
        ASSERT_FALSE(set_address_limit(l, "rooms_per_hour", 5, why));
        ASSERT_TRUE(why.find("rooms_per_hour") != std::string::npos);
        ASSERT_EQ(AddressLimits().rooms_open, 0u);                          // (a Limits made by code has no limit: only the program's options switch them on)
    } TEST_END();

    TEST_CASE("S3.352 Which Address A Connection Counts Under: An IPv4 Address As It Is, A Mapped One As Its IPv4, Any Other IPv6 Address As Its /64; The Proxy's Header Only From A Trusted Proxy And Only When It Holds One Address; A Trusted Proxy That Names Nobody Is Nobody's, Never Everybody's") {
        ASSERT_EQ(address_identity("203.0.113.7"), std::string("203.0.113.7"));
        ASSERT_EQ(address_identity("::ffff:203.0.113.7"), std::string("203.0.113.7"));
        ASSERT_EQ(address_identity("2001:db8:1:2:3:4:5:6"), address_identity("2001:db8:1:2:ffff::1"));        // (one /64: one household)
        ASSERT_TRUE(address_identity("2001:db8:1:2::1") != address_identity("2001:db8:1:3::1"));
        ASSERT_TRUE(!address_identity("2001:db8:1:2::1").empty() && address_identity("2001:db8:1:2::1") != address_identity("203.0.113.7"));
        for (const char* bad : {"", "garbage", "1.2.3.4, 5.6.7.8", "1.2.3.4:80", "[::1]", "999.1.1.1", "fe80::1%2", " 1.2.3.4"}) ASSERT_EQ(address_identity(bad), std::string());

        const TrustedProxies standard = TrustedProxies::standard();
        for (const char* in : {"127.0.0.1", "10.1.2.3", "172.16.0.1", "172.31.255.255", "192.168.1.1", "::1", "fd00::1", "::ffff:10.0.0.1"}) ASSERT_TRUE(standard.contains(in));
        for (const char* out : {"8.8.8.8", "172.32.0.0", "172.15.255.255", "192.169.0.1", "11.0.0.1", "2001:db8::1", "fe00::1", "", "garbage"}) ASSERT_FALSE(standard.contains(out));

        TrustedProxies own;
        std::string why;
        ASSERT_TRUE(TrustedProxies::parse("203.0.113.0/24, 2001:db8::/32\n192.0.2.9", own, why) && own.size() == 3);
        ASSERT_TRUE(own.contains("203.0.113.99") && own.contains("2001:db8:5::1") && own.contains("192.0.2.9"));
        ASSERT_FALSE(own.contains("203.0.114.1") || own.contains("2001:db9::1") || own.contains("192.0.2.10") || own.contains("127.0.0.1"));
        TrustedProxies none;
        ASSERT_TRUE(TrustedProxies::parse("none", none, why) && none.size() == 0 && !none.contains("127.0.0.1"));
        TrustedProxies exact;
        ASSERT_TRUE(TrustedProxies::parse("10.0.0.1/32, ::1/128", exact, why) && exact.contains("10.0.0.1") && !exact.contains("10.0.0.2") && exact.contains("::1") && !exact.contains("::2"));
        for (const char* bad : {"10.0.0.0/33", "10.0.0.0/", "10.0.0.0/x", "bogus", "10.0.0.0/8, bogus", "::/129", "10.0.0.0/-1"}) {
            TrustedProxies t;
            ASSERT_FALSE(TrustedProxies::parse(bad, t, why));
            ASSERT_FALSE(why.empty());
        }

        ASSERT_EQ(door_address("10.0.0.5", "203.0.113.7", standard), std::string("203.0.113.7"));                 // (the site's own proxy: the visitor's address)
        ASSERT_EQ(door_address("10.0.0.5", "2001:db8::7", standard), std::string("2001:db8::7"));
        ASSERT_EQ(door_address("203.0.113.9", "198.51.100.1", standard), std::string("203.0.113.9"));              // (a connection from anywhere else counts as itself, whatever it sends)
        ASSERT_EQ(door_address("203.0.113.9", "", standard), std::string("203.0.113.9"));
        for (const char* odd : {"", "garbage", "1.2.3.4, 5.6.7.8", "1.2.3.4:80"}) ASSERT_EQ(door_address("10.0.0.5", odd, standard), std::string("10.0.0.5"));       // (a trusted proxy that cannot say whose: its own address, one count)
        ASSERT_EQ(door_address("10.0.0.5", "203.0.113.7", none), std::string("10.0.0.5"));                         // (no trusted proxy: nothing is believed)
        ASSERT_EQ(door_address("127.0.0.1", "203.0.113.7", own), std::string("127.0.0.1"));
    } TEST_END();

    TEST_CASE("S3.353 The WebSocket Door Hands Over The Handshake's X-Client-Address: Exactly One Header Is The Proxy's Word, None Or Two Is No Word; Over A Real Socket The Connection's Own Address Is Still The Peer's") {
        const net::WsServerOptions options;
        const auto parsed = [&](const std::string& extra) { return net::ws_parse_handshake(kGoodRequest + extra + "\r\n", options); };
        ASSERT_TRUE(parsed("X-Client-Address: 203.0.113.7\r\n").status == net::WsHandshakeResult::Status::Accepted);
        ASSERT_EQ(parsed("X-Client-Address: 203.0.113.7\r\n").client_address, std::string("203.0.113.7"));
        ASSERT_EQ(parsed("x-client-address:   2001:db8::7  \r\n").client_address, std::string("2001:db8::7"));      // (any case of the name, blanks cut)
        ASSERT_EQ(parsed("").client_address, std::string());
        ASSERT_EQ(parsed("X-Client-Address: 203.0.113.7\r\nX-Client-Address: 198.51.100.1\r\n").client_address, std::string());    // (two of them: the proxy sets one)
#ifndef _WIN32
        auto listener = net::WsListener::listen(0, true);
        ASSERT_TRUE(listener != nullptr);
        const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_TRUE(sock >= 0);
        sockaddr_in to;
        std::memset(&to, 0, sizeof to);
        to.sin_family = AF_INET;
        to.sin_port = htons(listener->port());
        to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ASSERT_TRUE(::connect(sock, reinterpret_cast<sockaddr*>(&to), sizeof to) == 0);
        const std::string request = kGoodRequest + "X-Client-Address: 203.0.113.7\r\n\r\n";
        ASSERT_TRUE(::send(sock, request.data(), request.size(), 0) == static_cast<ssize_t>(request.size()));
        std::unique_ptr<net::WsConnection> accepted;
        for (int i = 0; i < 400 && accepted == nullptr; ++i) {
            accepted = listener->accept();
            if (accepted == nullptr) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ::close(sock);
        ASSERT_TRUE(accepted != nullptr);
        ASSERT_EQ(accepted->client_address(), std::string("203.0.113.7"));
        const std::string peer = accepted->peer().substr(0, accepted->peer().find(':'));
        ASSERT_EQ(peer, std::string("127.0.0.1"));
        ASSERT_EQ(door_address(peer, accepted->client_address(), TrustedProxies::standard()), std::string("203.0.113.7"));
        TrustedProxies elsewhere;
        std::string why;
        ASSERT_TRUE(TrustedProxies::parse("203.0.113.0/24", elsewhere, why));
        ASSERT_EQ(door_address(peer, accepted->client_address(), elsewhere), std::string("127.0.0.1"));        // (the connection is not from a trusted proxy: its own address)
#endif
    } TEST_END();

    TEST_CASE("S3.354 A Household Is Never Limited: Four People From One Address Host And Play Two Rooms At Once And Keep Two Lobbies With Not One Refusal, Everybody Joins A Room Of The Address As Often As They Like, And Eight Quiet Rooms Are Not Closed (Eight Are The Rooms That An Address May Hold Quietly)") {
        {
            World w(server_limits());
            w.next_address = kHousehold;
            Client& ann = w.connect_creating("Ann", "ann00001", block_of("TINY.LVL", 2));
            Client& bob = w.connect("Bob", "ann00001");
            Client& cy = w.connect_creating("Cy", "cy000001", block_of("TINY.LVL", 2));
            Client& di = w.connect("Di", "cy000001");
            Client& page_a = w.connect_page("AnnPage", "lob00001", lobby_block_of());
            Client& page_c = w.connect_page("CyPage", "lob00002", lobby_block_of());
            for (Client* c : {&ann, &bob, &cy, &di}) c->next_order_ms = 0xFFFFFFFFu;
            for (int i = 0; i < 600 && !(w.status("ann00001").state == RoomState::Running && w.status("cy000001").state == RoomState::Running); ++i) w.run(10);
            ASSERT_TRUE(w.status("ann00001").state == RoomState::Running && w.status("cy000001").state == RoomState::Running);       // both matches play at once
            ASSERT_TRUE(in_room(ann) && in_room(bob) && in_room(cy) && in_room(di) && in_room(page_a) && in_room(page_c));
            w.run(10000);
            ASSERT_EQ(lobbies(w), size_t{2});
            // everybody who shares the address joins the same room, as often as they like: a Hello for a room that exists is not counted
            for (int k = 0; k < 60; ++k) w.connect_page("Guest" + std::to_string(k), "lob00001", std::nullopt);
            w.run(2000);
            ASSERT_EQ(w.mgr.address_refusals().total(), uint64_t{0});
        }
        {   // eight rooms that nobody does anything in, for six minutes (the idle time is five): that is as many as the address may hold quietly, and none is closed
            World w(server_limits());
            w.next_address = kHousehold;
            for (int k = 0; k < 4; ++k) w.connect_page("Page" + std::to_string(k), code_of("lob", k), lobby_block_of());
            for (int k = 0; k < 4; ++k) w.connect_creating("Host" + std::to_string(k), code_of("pub", k), block_of("TINY.LVL", 4));
            w.run(6 * 60 * 1000);
            ASSERT_EQ(lobbies(w), size_t{4});
            for (int k = 0; k < 4; ++k) ASSERT_TRUE(w.status(code_of("pub", k)).state == RoomState::Waiting);
            ASSERT_EQ(w.mgr.address_refusals().total(), uint64_t{0});
        }
    } TEST_END();

    TEST_CASE("S3.355 A Burst Of Rooms From One Address Is Refused Politely: Ten Are Made In A Minute And The Rest Are Answered As A Server With No Place Answers (NoSuchRoom, Which The Pages And The Game Word As 'try again'); Other Addresses Are Not Touched; The Minute Is A Sliding Window; Sixteen Open Rooms Are The Most, And A Room That Closes Gives Its Place Back")  {
        {   // rooms in a minute: ten of twelve, the rest refused until the first are a minute old
            World w(server_limits());
            std::vector<Client*> made;
            w.next_address = kHousehold;
            for (int k = 0; k < 12; ++k) made.push_back(&w.connect_creating("Host" + std::to_string(k), code_of("brst", k), block_of("TINY.LVL", 4)));
            w.run(300);
            size_t ok = 0;
            size_t refused = 0;
            for (Client* c : made) {
                ok += in_room(*c) ? 1u : 0u;
                refused += answered(*c) == net::RejectReason::NoSuchRoom ? 1u : 0u;
            }
            ASSERT_EQ(ok, size_t{10});
            ASSERT_EQ(refused, size_t{2});
            ASSERT_EQ(w.mgr.address_refusals().rooms_rate, uint64_t{2});
            ASSERT_EQ(w.mgr.address_refusals().rooms_open, uint64_t{0});
            for (int k = 10; k < 12; ++k) ASSERT_TRUE(w.status(code_of("brst", k)).code.empty());                // (the refused ones were not made)
            w.next_address = kCarrier;                                                                         // another address makes its room in the same minute
            Client& other = w.connect_creating("Other", "othr0001", block_of("TINY.LVL", 4));
            ASSERT_TRUE(in_room(other));
            w.next_address = kHousehold;
            w.run(50000);
            Client& early = w.connect_creating("Early", "brst0100", block_of("TINY.LVL", 4));
            w.run(300);
            ASSERT_EQ(answered(early), net::RejectReason::NoSuchRoom);                                         // (the ten are 50 s old)
            w.run(12000);
            Client& later = w.connect_creating("Later", "brst0101", block_of("TINY.LVL", 4));
            ASSERT_TRUE(in_room(later));                                                                       // (they have left the minute)
        }
        {   // rooms open: sixteen (lobbies and public rooms together), however slowly they come, and a room that closes gives its place back
            World w(server_limits());
            w.next_address = kHousehold;
            const auto make = [&](int k) {
                const std::string name = "Host" + std::to_string(k);
                return in_room(k % 2 == 0 ? w.connect_page(name, code_of("open", k), lobby_block_of()) : w.connect_creating(name, code_of("open", k), block_of("TINY.LVL", 4)));
            };
            for (int k = 0; k < 10; ++k) ASSERT_TRUE(make(k));
            w.run(61000);                                                                                      // the ten have left the minute; they are still open
            for (int k = 10; k < 16; ++k) ASSERT_TRUE(make(k));
            Client& over = w.connect_creating("Over", "open0100", block_of("TINY.LVL", 4));
            Client& over_page = w.connect_page("OverPage", "open0101", lobby_block_of());
            w.run(300);
            ASSERT_EQ(answered(over), net::RejectReason::NoSuchRoom);
            ASSERT_EQ(answered(over_page), net::RejectReason::NoSuchRoom);
            ASSERT_EQ(w.mgr.address_refusals().rooms_open, uint64_t{2});
            ASSERT_EQ(w.mgr.address_refusals().rooms_rate, uint64_t{0});
            ASSERT_TRUE(w.mgr.close_room("open0001", w.now));                                                  // a room that closes gives its place back
            w.run(11000);                                                                                      // (it is over; its keep time passes)
            Client& placed = w.connect_page("Placed", "open0102", lobby_block_of());
            ASSERT_TRUE(in_room(placed));
        }
        {   // lobby rooms are rooms too: the page's lobby block is counted the same way
            World w(server_limits());
            w.next_address = kHousehold;
            size_t ok = 0;
            for (int k = 0; k < 12; ++k) ok += in_room(w.connect_page("Page" + std::to_string(k), code_of("lbb", k), lobby_block_of())) ? 1u : 0u;
            ASSERT_EQ(ok, size_t{10});
            ASSERT_EQ(lobbies(w), size_t{10});
        }
    } TEST_END();

    TEST_CASE("S3.356 Trying Codes One After Another Is Slowed: Up To The Minute's Allowance A Wrong Code Is Answered At Once, Then Each Answer Waits Longer, At Most Eight Wait For One Address And The Rest Are Dropped; A Right Code Is Never Refused Or Counted But Waits As A Wrong One Does Above The Allowance")  {
        ServerLimits l = server_limits();
        l.address.wrong_codes_per_minute = 5;
        l.address.connections_per_second = 0;                                                                   // (the connections' own limit is S3.357's)
        World w(l);
        w.next_address = kCarrier;
        Client& host = w.connect_creating("Host", "good0001", block_of("TINY.LVL", 4));
        ASSERT_TRUE(in_room(host));
        w.next_address = kHousehold;
        std::vector<Client*> wrong;
        for (int k = 0; k < 5; ++k) wrong.push_back(&w.connect("Guess" + std::to_string(k), code_of("nope", k)));
        w.run(200);
        for (Client* c : wrong) ASSERT_EQ(answered(*c), net::RejectReason::NoSuchRoom);                       // (the allowance: answered at once)
        Client& sixth = w.connect("Guess5", "nope0005");
        w.run(200);
        ASSERT_EQ(answered(sixth), static_cast<net::RejectReason>(0));                                         // (it waits half a second ...)
        w.run(700);
        ASSERT_EQ(answered(sixth), net::RejectReason::NoSuchRoom);                                             // (... and is told the same)
        ASSERT_EQ(w.mgr.address_refusals().wrong_codes_slowed, uint64_t{1});
        // a try at a code that exists, from the address that is over its allowance, waits as the next wrong code would (a second here), is not counted, and is then answered as it would have been
        Client& right = w.connect("Right", "good0001");
        w.run(200);
        ASSERT_TRUE(!in_room(right) && w.status("good0001").joined == 1);
        w.run(1000);
        ASSERT_TRUE(in_room(right) && w.status("good0001").joined == 2);
        // a burst of guesses in one pass: the answers that wait are bounded
        std::vector<Client*> burst;
        for (int k = 0; k < 20; ++k) burst.push_back(&w.connect("Burst" + std::to_string(k), code_of("zzzz", k)));
        w.run(200);
        const AddressRefusals r = w.mgr.address_refusals();
        ASSERT_EQ(r.wrong_codes_slowed, uint64_t{2 + kWrongCodeHeldPerAddress});                                  // (the sixth wrong code, the right one, and the eight of the burst)
        ASSERT_EQ(r.wrong_codes_dropped, uint64_t{20 - kWrongCodeHeldPerAddress});
        w.run(6000);
        size_t told = 0;
        for (Client* c : burst) told += answered(*c) == net::RejectReason::NoSuchRoom ? 1u : 0u;
        ASSERT_EQ(told, kWrongCodeHeldPerAddress);                                                             // (the eight that waited are answered when their time comes)
        ASSERT_EQ(w.mgr.pending_count(), size_t{0});
        // a minute later the allowance is whole again; the other address was never held
        w.run(61000);
        Client& again = w.connect("Again", "nope0099");
        w.run(200);
        ASSERT_EQ(answered(again), net::RejectReason::NoSuchRoom);
        w.next_address = kCarrier;
        Client& other = w.connect("Other", "nope0098");
        w.run(200);
        ASSERT_EQ(answered(other), net::RejectReason::NoSuchRoom);
    } TEST_END();

    TEST_CASE("S3.357 Connections In A Second: Five From An Address Are Taken And The Rest Are Closed With No Answer; Another Address Is Not Touched; A Second Later The Address Is Let In Again") {
        ServerLimits l = server_limits();
        l.address.connections_per_second = 5;
        World w(l);
        w.next_address = kHousehold;
        for (int k = 0; k < 8; ++k) w.connect("Joiner" + std::to_string(k), code_of("conn", k));
        ASSERT_EQ(w.mgr.pending_count(), size_t{5});
        ASSERT_EQ(w.mgr.address_refusals().connections, uint64_t{3});
        w.next_address = kCarrier;
        w.connect("Other", "conn0100");
        ASSERT_EQ(w.mgr.pending_count(), size_t{6});
        w.now += 1000;
        w.next_address = kHousehold;
        w.connect("Later", "conn0101");
        ASSERT_EQ(w.mgr.pending_count(), size_t{7});
        ASSERT_EQ(w.mgr.address_refusals().connections, uint64_t{3});
        // an IPv6 address counts as its /64, so a person's machines share one count, and two households in different /64s do not
        for (int k = 0; k < 6; ++k) {
            w.next_address = k % 2 == 0 ? "2001:db8:1:2::5" : "2001:db8:1:2:aaaa:bbbb:cccc:dddd";
            w.connect("V6a" + std::to_string(k), code_of("sixa", k));
        }
        ASSERT_EQ(w.mgr.address_refusals().connections, uint64_t{4});
        w.next_address = "2001:db8:1:3::5";
        w.connect("V6b", "sixb0001");
        ASSERT_EQ(w.mgr.address_refusals().connections, uint64_t{4});
    } TEST_END();

    TEST_CASE("S3.358 Relay Credentials Are Counted Per Address Across Matches: With Three An Hour, The First Match Of Two People From One Address Gets Two, The Second Gets One And Then The Answer Of A Server With No Relay, And Another Address Is Given Its Own; 50 Minutes Later The Count Still Holds, An Hour After The Credentials It Is Empty") {
        ServerLimits l;
        l.address.relay_per_hour = 3;
        auto turn = std::make_shared<net::TurnSettings>();
        turn->urls = {"stun:relay.example:3478", "turn:relay.example:3478?transport=udp"};
        turn->credential = net::make_turn_signer("room-secret");
        l.turn = turn;
        World w(l);
        w.now += 20u * 60u * 1000u;                                                                              // (the server has been up for twenty minutes: the credentials below are from minute 20)
        w.run(100);
        const std::array<std::string, 2> home = {kHousehold, kHousehold};
        const auto first = relay_match(w, "RLY-1", home);
        ASSERT_TRUE(given(first[0]) && given(first[1]));
        const auto second = relay_match(w, "RLY-2", home);
        ASSERT_TRUE(given(second[0]) != given(second[1]));                                                      // (one of the two is given the third, the other is answered as a server with no relay answers)
        const auto& left_out = given(second[0]) ? second[1] : second[0];
        ASSERT_TRUE(left_out.size() == 1 && left_out[0].credential.empty() && left_out[0].urls.empty() && left_out[0].username.empty());
        ASSERT_EQ(w.mgr.address_refusals().relay, uint64_t{1});
        const auto third = relay_match(w, "RLY-3", {kCarrier, kCarrier});                                       // (another address: its own hour)
        ASSERT_TRUE(given(third[0]) && given(third[1]));
        ASSERT_EQ(w.mgr.address_refusals().relay, uint64_t{1});
        const auto mixed = relay_match(w, "RLY-4", {kHousehold, kCarrier});                                     // (a person of each: the household's seat is refused, the other seat is not)
        ASSERT_TRUE(mixed[0].size() == 1 && mixed[0][0].credential.empty());
        ASSERT_TRUE(given(mixed[1]));
        for (const char* code : {"RLY-1", "RLY-2", "RLY-3", "RLY-4"}) w.mgr.close_room(code, w.now);         // (the matches end)
        w.run(1000);
        w.now += 50u * 60u * 1000u;                                                                              // minute 70: the household's credentials are 50 minutes old, and still count
        w.run(100);
        const auto still = relay_match(w, "RLY-5", home);
        ASSERT_TRUE(still[0].size() == 1 && still[0][0].credential.empty() && still[1].size() == 1 && still[1][0].credential.empty());
        ASSERT_EQ(w.mgr.address_refusals().relay, uint64_t{4});                                                      // (the second match, the household's seat of the mixed one, and both of these)
        w.mgr.close_room("RLY-5", w.now);
        w.run(1000);
        w.now += 15u * 60u * 1000u;                                                                              // minute 85: an hour has passed since minute 20
        w.run(100);
        const auto next_hour = relay_match(w, "RLY-6", home);
        ASSERT_TRUE(given(next_hour[0]) && given(next_hour[1]));
        ASSERT_TRUE(net::turn_credential_valid("room-secret", next_hour[0][0].username, next_hour[0][0].credential, unix_seconds()));
        // with no limit the same house is never refused
        ServerLimits none = l;
        none.address = AddressLimits();
        World free(none);
        for (int k = 0; k < 4; ++k) {
            const auto m = relay_match(free, code_of("FRE", k), home);
            ASSERT_TRUE(given(m[0]) && given(m[1]));
        }
        ASSERT_EQ(free.mgr.address_refusals().relay, uint64_t{0});
    } TEST_END();

    TEST_CASE("S3.359 An Address That Holds Many Lobbies Loses The Idle Ones Sooner: Above The Rooms It May Hold Quietly, A Lobby In Which Nobody Has Joined, Planned Or Started Anything For The Idle Time Is Closed (A Line Of Chat Or A Ready Mark Every Twenty Seconds Does Not Keep It), Down To That Number; An Address At The Number Is Left Alone, And So Is A Lobby That Somebody Joined")  {
        ServerLimits l = server_limits();
        l.address.crowded_rooms = 2;
        l.address.crowded_idle_s = 60;
        l.lobby_idle_evict_ms = 0;
        World w(l);
        w.next_address = kHousehold;
        std::vector<Client*> pages;
        for (int k = 0; k < 4; ++k) pages.push_back(&w.connect_page("Page" + std::to_string(k), code_of("crwd", k), lobby_block_of()));
        Client& ready_page = w.connect_page("Ready", "crwd0002", std::nullopt);                  // a second person in the third lobby, who flips a Ready mark now and then
        w.next_address = kCarrier;
        w.connect_page("TwoA", "tway0001", lobby_block_of());
        w.connect_page("TwoB", "tway0002", lobby_block_of());
        ASSERT_EQ(lobbies(w), size_t{6});
        for (int step = 0; step < 2; ++step) {                                                  // a line of chat from each page every twenty seconds: the use that does not count
            w.run(10000);
            for (Client* c : pages) c->lobby->chat("still here");
            ready_page.lobby->request_ready(step == 0);
            w.run(10000);
        }
        w.run(5000);                                                                            // 45 s
        w.next_address = kHousehold;
        w.connect_page("Joiner", "crwd0000", std::nullopt);                                       // somebody joins the first of them
        w.run(25000);                                                                           // 70 s: the others have been quiet for more than a minute, the joined one for 25 s
        ASSERT_EQ(w.mgr.address_refusals().idle_closed, uint64_t{2});
        ASSERT_EQ(lobbies(w), size_t{4});                                                         // the household keeps two, the other address its two
        ASSERT_TRUE(w.status("crwd0000").state == RoomState::Waiting && w.status("crwd0003").state == RoomState::Waiting);       // (the one that somebody joined, and the last that was left when the address was down to two)
        ASSERT_TRUE(w.status("crwd0001").code.empty() && w.status("crwd0002").code.empty());
        ASSERT_TRUE(w.status("tway0001").state == RoomState::Waiting && w.status("tway0002").state == RoomState::Waiting);
        ASSERT_FALSE(pages[1]->server_end->is_open() || pages[2]->server_end->is_open());          // (the people of a closed lobby are dropped)
        ASSERT_TRUE(pages[0]->server_end->is_open() && pages[3]->server_end->is_open());
        ASSERT_TRUE(w.mgr.take_ended(w.now).empty());                                             // (no word of it in the log of ended rooms: no match was begun)
        w.run(6 * 60 * 1000);                                                                     // what is left is not closed: the address is down to the number that it may hold
        ASSERT_EQ(lobbies(w), size_t{4});
        ASSERT_EQ(w.mgr.address_refusals().idle_closed, uint64_t{2});
    } TEST_END();

    TEST_CASE("S3.360 The Control Interface Shows The Limits And What They Refused (GET /stats and GET /limits, never an address) And Changes Them At Once (POST /limits: any keys, 0 or a number of the key's range, all or none)") {
        ServerLimits l = server_limits();
        World w(l);
        const auto call = [&](const std::string& method, const std::string& path, const std::string& body = std::string()) {
            ctl::HttpRequest r;
            r.method = method;
            r.path = path;
            r.body = body;
            return handle_control(w.mgr, r, w.now);
        };
        ctl::JsonValue stats;
        ASSERT_TRUE(ctl::parse_json(call("GET", "/stats").body, stats, nullptr));
        for (const AddressLimitField& f : kAddressLimitFields) ASSERT_EQ(stats.get("address_limits").get(f.key).as_int_or(-1), static_cast<int64_t>(f.standard));
        for (const char* key : {"connections", "rooms_rate", "rooms_open", "wrong_codes_slowed", "wrong_codes_dropped", "relay", "idle_closed"}) ASSERT_EQ(stats.get("address_refusals").get(key).as_int_or(-1), 0);
        ASSERT_EQ(stats.get("addresses_tracked").as_int_or(-1), 0);
        ctl::JsonValue limits;
        ASSERT_TRUE(call("GET", "/limits").status == 200 && ctl::parse_json(call("GET", "/limits").body, limits, nullptr));
        ASSERT_EQ(limits.get("rooms_open").as_int_or(-1), 16);
        // a change takes effect at once, and a refusal shows in the counts
        ASSERT_EQ(call("POST", "/limits", "{\"rooms_open\":1,\"rooms_per_minute\":0}").status, 200);
        ASSERT_EQ(w.mgr.address_limits().rooms_open, 1u);
        ASSERT_EQ(w.mgr.address_limits().rooms_per_minute, 0u);
        ASSERT_EQ(w.mgr.address_limits().relay_per_hour, 24u);                                      // (a key that is not sent keeps its number)
        w.next_address = kHousehold;
        ASSERT_TRUE(in_room(w.connect_creating("A", "ctl00001", block_of("TINY.LVL", 4))));
        Client& refused = w.connect_creating("B", "ctl00002", block_of("TINY.LVL", 4));
        w.run(300);
        ASSERT_EQ(answered(refused), net::RejectReason::NoSuchRoom);
        ASSERT_TRUE(ctl::parse_json(call("GET", "/stats").body, stats, nullptr));
        ASSERT_EQ(stats.get("address_refusals").get("rooms_open").as_int_or(-1), 1);
        ASSERT_EQ(stats.get("addresses_tracked").as_int_or(-1), 1);
        // refusals: nothing changes unless every key is good
        for (const char* bad : {"{\"rooms_open\":300}", "{\"rooms_open\":2,\"nope\":1}", "{\"rooms_open\":\"2\"}", "{\"rooms_open\":2.5}", "{}", "[]", "not json", "", "{\"rooms_open\":-1}"}) {
            ASSERT_EQ(call("POST", "/limits", bad).status, 400);
            ASSERT_EQ(w.mgr.address_limits().rooms_open, 1u);
        }
        ASSERT_EQ(call("DELETE", "/limits").status, 405);
        ASSERT_EQ(call("POST", "/limits", "{\"rooms_open\":0}").status, 200);                          // (0 switches a limit off)
        Client& freed = w.connect_creating("C", "ctl00003", block_of("TINY.LVL", 4));
        ASSERT_TRUE(in_room(freed));
        // no address anywhere in the answers
        const std::string text = call("GET", "/stats").body + call("GET", "/limits").body + call("GET", "/rooms").body;
        ASSERT_TRUE(text.find(kHousehold) == std::string::npos && text.find(kCarrier) == std::string::npos);
    } TEST_END();

    TEST_CASE("S3.363 An Address That Is Over Its Wrong Codes Waits At The Right Codes Too: Within The Allowance A Right Code Is Answered At Once; Above It The Hello For A Room That Exists Waits As A Wrong Code Would (And Is Then Answered As It Would Have Been, A Room That Has Begun Told So), Is Not Counted, Shares The List Of Waiting Answers And Is Dropped When It Is Full; Other Addresses And The Next Minute Are Not Held") {
        ServerLimits l = server_limits();
        l.address.wrong_codes_per_minute = 3;
        l.address.connections_per_second = 0;
        World w(l);
        w.next_address = kCarrier;
        Client& host = w.connect_creating("Host", "real0001", block_of("TINY.LVL", 4));
        ASSERT_TRUE(in_room(host));
        w.next_address = kHousehold;
        Client& first = w.connect("First", "real0001");
        w.run(200);
        ASSERT_TRUE(in_room(first));                                                                           // (within the allowance: at once)
        for (int k = 0; k < 3; ++k) {
            Client& guess = w.connect("Guess" + std::to_string(k), code_of("nope", k));
            w.run(200);
            ASSERT_EQ(answered(guess), net::RejectReason::NoSuchRoom);
        }
        Client& second = w.connect("Second", "real0001");
        w.run(200);
        ASSERT_TRUE(!in_room(second) && answered(second) == static_cast<net::RejectReason>(0));                  // (over the allowance: it waits, as the next wrong code would, half a second)
        ASSERT_TRUE(w.status("real0001").joined == 2);                                                         // (and the room has not seen it yet)
        w.run(700);
        ASSERT_TRUE(in_room(second) && w.status("real0001").joined == 3);                                      // (then it is answered as it would have been)
        w.next_address = kCarrier;
        Client& other = w.connect("Other", "real0001");
        w.run(200);
        ASSERT_TRUE(in_room(other));                                                                           // (another address is not held)
        w.next_address = kHousehold;
        Client& late = w.connect("Late", "real0001");                                                          // (the room is full and has begun: the answer is the room's own, after the same wait)
        w.run(200);
        ASSERT_EQ(answered(late), static_cast<net::RejectReason>(0));
        w.run(700);
        ASSERT_EQ(answered(late), net::RejectReason::MatchRunning);
        ASSERT_EQ(w.mgr.address_refusals().wrong_codes_slowed, uint64_t{2});                                   // (a try at a right code is not counted as a wrong one: the second wait was as long as the first)
        w.run(61000);                                                                                          // a minute later the allowance is whole
        Client& after = w.connect("After", "real0001");
        w.run(200);
        ASSERT_EQ(answered(after), net::RejectReason::MatchRunning);
        // a burst of tries at a right code, in one pass, from an address that is over its allowance: eight wait, the rest are dropped without an answer, and a hit is no faster than a miss
        World b(l);
        b.next_address = kCarrier;
        ASSERT_TRUE(in_room(b.connect_creating("Host", "real0002", block_of("TINY.LVL", 4))));
        b.next_address = kHousehold;
        for (int k = 0; k < 3; ++k) b.connect("Guess" + std::to_string(k), code_of("nope", k));
        b.run(200);
        std::vector<Client*> burst;
        for (int k = 0; k < 12; ++k) burst.push_back(&b.connect("Try" + std::to_string(k), "real0002"));
        b.run(300);
        for (Client* c : burst) ASSERT_TRUE(!in_room(*c) && answered(*c) == static_cast<net::RejectReason>(0));       // (nobody is answered before the wait is over)
        ASSERT_EQ(b.mgr.address_refusals().wrong_codes_slowed, uint64_t{kWrongCodeHeldPerAddress});
        ASSERT_EQ(b.mgr.address_refusals().wrong_codes_dropped, uint64_t{12 - kWrongCodeHeldPerAddress});
        b.run(2000);
        size_t seated = 0;
        size_t told = 0;
        size_t silent = 0;
        for (Client* c : burst) {
            seated += in_room(*c) ? 1u : 0u;
            told += answered(*c) == net::RejectReason::Full ? 1u : 0u;
            silent += !in_room(*c) && answered(*c) == static_cast<net::RejectReason>(0) ? 1u : 0u;
        }
        ASSERT_EQ(seated, size_t{3});                                                                          // (the three seats that were free; the room begins when they are taken)
        ASSERT_EQ(told, kWrongCodeHeldPerAddress - 3);                                                         // (the rest of the eight, answered in the same pass, are told that the room is full)
        ASSERT_EQ(silent, 12 - kWrongCodeHeldPerAddress);                                                      // (the dropped ones got no answer, right code or not)
        // a seat holder who comes back to a match that runs, from an address that is over its allowance, is not held: it needs its key, and a guess has none
        World k(l);
        RoomSpec spec = spec_of("real0003", 2);
        spec.reconnect = true;
        ASSERT_TRUE(k.mgr.create_room(spec, k.now).ok);
        k.next_address = kHousehold;
        Client& ann = k.connect("Ann", "real0003");
        Client& bob = k.connect("Bob", "real0003");
        for (Client* c : {&ann, &bob}) c->next_order_ms = 0xFFFFFFFFu;
        for (int i = 0; i < 600 && k.status("real0003").state != RoomState::Running; ++i) k.run(10);
        ASSERT_TRUE(k.status("real0003").state == RoomState::Running);
        for (int g = 0; g < 3; ++g) k.connect("Guess" + std::to_string(g), code_of("nope", g));
        k.run(200);
        Client& back = k.connect_game("Ann", "real0003", ann.lobby->key());
        ASSERT_TRUE(back.lobby->rejoined());                                                                   // (answered at once, as it was)
    } TEST_END();

    TEST_CASE("S3.361 An Address That The Door Could Not Tell Is Not Limited, And With The Limits Off The Server Is What It Was: Any Number Of Rooms, Codes And Connections From One Address") {
        {
            World w(server_limits());
            for (int k = 0; k < 14; ++k) {
                w.next_address = "";                                                              // (what the door gives when the connection's own address is no address at all)
                ASSERT_TRUE(in_room(w.connect_page("NoAddr" + std::to_string(k), code_of("anon", k), lobby_block_of())));
            }
            ASSERT_EQ(w.mgr.address_refusals().total(), uint64_t{0});
            ASSERT_EQ(w.mgr.addresses_tracked(), size_t{0});
        }
        {
            ServerLimits l = server_limits();
            l.address = AddressLimits();
            World w(l);
            w.next_address = kHousehold;
            size_t ok = 0;
            for (int k = 0; k < 14; ++k) ok += in_room(w.connect_page("Free" + std::to_string(k), code_of("free", k), lobby_block_of())) ? 1u : 0u;
            ASSERT_EQ(ok, size_t{14});
            for (int k = 0; k < 40; ++k) w.connect("Guess" + std::to_string(k), code_of("miss", k));
            w.run(300);
            ASSERT_EQ(w.mgr.address_refusals().total(), uint64_t{0});
        }
    } TEST_END();

#if !defined(_WIN32) && defined(ANTS_SERVER_BINARY)
    TEST_CASE("S3.362 The Real Program: Its WebSocket Door Counts A Browser By The Address That A Trusted Proxy Writes (Two Visitors Behind One Proxy Are Two Addresses) And By The Connection's Own Address When The Proxy Is Not Trusted; The Options Of The Limits Are Taken Or Refused At The Start")  {
        const int welcome = -1;
        const int no_place = static_cast<int>(net::RejectReason::NoSuchRoom);
        {   // the proxy is this machine, and the default list trusts it: one room open for each visitor's address
            ProgramProcess program;
            const uint16_t port = free_port();
            ASSERT_TRUE(port != 0 && start_program(program, port, {}));
            WsPage a1, a2, a3, b1;
            ASSERT_TRUE(a1.open(port, "203.0.113.1") && a2.open(port, "203.0.113.1") && a3.open(port, "203.0.113.1") && b1.open(port, "203.0.113.2"));
            ASSERT_EQ(a1.ask_for_a_lobby("proc0001"), welcome);
            ASSERT_EQ(a2.ask_for_a_lobby("proc0002"), no_place);                          // (the same visitor: its one room is open)
            ASSERT_EQ(b1.ask_for_a_lobby("proc0003"), welcome);                           // (another visitor behind the same proxy: its own count)
            ASSERT_EQ(a3.ask_for_a_lobby("proc0001"), welcome);                           // (a Hello for a room that exists is no new room: the visitor whose room is open joins it)
        }
        {   // a proxy that is not this machine: the header of a connection from here is no news, both visitors are one address
            ProgramProcess program;
            const uint16_t port = free_port();
            ASSERT_TRUE(port != 0 && start_program(program, port, {"--trusted-proxies", "10.0.0.0/8"}));
            WsPage a, b;
            ASSERT_TRUE(a.open(port, "203.0.113.1") && b.open(port, "203.0.113.2"));
            ASSERT_EQ(a.ask_for_a_lobby("proc0011"), welcome);
            ASSERT_EQ(b.ask_for_a_lobby("proc0012"), no_place);
        }
        {   // the variable works as the option does, and the option wins over it
            ProgramProcess program;
            const uint16_t port = free_port();
            ::setenv("ANTS_TRUSTED_PROXIES", "none", 1);
            const bool started = start_program(program, port, {});
            ::unsetenv("ANTS_TRUSTED_PROXIES");
            ASSERT_TRUE(port != 0 && started);
            WsPage a, b;
            ASSERT_TRUE(a.open(port, "203.0.113.1") && b.open(port, "203.0.113.2"));
            ASSERT_EQ(a.ask_for_a_lobby("proc0021"), welcome);
            ASSERT_EQ(b.ask_for_a_lobby("proc0022"), no_place);
            ProgramProcess second;
            const uint16_t port2 = free_port();
            ::setenv("ANTS_TRUSTED_PROXIES", "none", 1);
            const bool started2 = start_program(second, port2, {"--trusted-proxies", "127.0.0.1"});
            ::unsetenv("ANTS_TRUSTED_PROXIES");
            ASSERT_TRUE(port2 != 0 && started2);
            WsPage c, d;
            ASSERT_TRUE(c.open(port2, "203.0.113.1") && d.open(port2, "203.0.113.2"));
            ASSERT_EQ(c.ask_for_a_lobby("proc0031"), welcome);
            ASSERT_EQ(d.ask_for_a_lobby("proc0032"), welcome);
        }
        {   // a connection from a trusted proxy that writes no address counts under the proxy's own address: all of them are one address
            ProgramProcess program;
            const uint16_t port = free_port();
            ASSERT_TRUE(port != 0 && start_program(program, port, {}));
            WsPage a, b, c;
            ASSERT_TRUE(a.open(port, "") && b.open(port, "") && c.open(port, "203.0.113.5"));
            ASSERT_EQ(a.ask_for_a_lobby("proc0041"), welcome);
            ASSERT_EQ(b.ask_for_a_lobby("proc0042"), no_place);
            ASSERT_EQ(c.ask_for_a_lobby("proc0043"), welcome);                            // (a visitor whose address the proxy wrote is its own count)
        }
        {   // 0 and --no-address-limits switch them off: the same visitor makes two rooms (the program has one room open for an address in the cases above; the last option wins)
            for (const std::vector<std::string>& extra : std::vector<std::vector<std::string>>{{"--address-rooms-open", "0"}, {"--no-address-limits"}}) {
                ProgramProcess program;
                const uint16_t port = free_port();
                ASSERT_TRUE(port != 0 && start_program(program, port, extra));
                WsPage a, b;
                ASSERT_TRUE(a.open(port, "203.0.113.1") && b.open(port, "203.0.113.1"));
                ASSERT_EQ(a.ask_for_a_lobby("proc0051"), welcome);
                ASSERT_EQ(b.ask_for_a_lobby("proc0052"), welcome);
            }
        }
        {   // the limits' options: a number outside the range, a key that is none, a word that is no number and a list that is no address all stop the program at its start with status 2
            const std::vector<std::vector<std::string>> bad = {
                {"--address-rooms-open", "300"}, {"--address-rooms-open", "-1"}, {"--address-rooms-open", "many"}, {"--address-crowded-idle-seconds", "5"}, {"--address-nope", "3"},
                {"--trusted-proxies", "bogus"}, {"--trusted-proxies", "10.0.0.0/40"}};
            for (const std::vector<std::string>& extra : bad) {
                ProgramProcess program;
                std::vector<std::string> args = {"--maps", maps_dir(), "--port", "0", "--ws-port", std::to_string(free_port())};
                for (const std::string& a : extra) args.push_back(a);
                ASSERT_TRUE(program.start(args));
                ASSERT_EQ(program.exit_status(10000), 2);
            }
        }
    } TEST_END();
#endif
}

int main() {
    std::cout << "\n=======================================================\n [SUITE] Limits per address: the windows, the address of a connection, rooms, codes, relay credentials\n"
                 "=======================================================\n";
    run_address_limit_tests();
    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}
