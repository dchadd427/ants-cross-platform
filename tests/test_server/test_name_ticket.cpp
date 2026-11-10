// Tests of the check of names on a server run with --verified-names (name_ticket.hpp): the pass and each way it can fail, the window of used passes, the guest rule, and the door with
// the check on and off. The passes here are made with throwaway secrets that belong to no stack (random at run time, or the test values of tests/data/name_tickets.txt).
#include "server_fixtures.hpp"

#include "ants_server/crypto.hpp"
#include "ants_server/name_ticket.hpp"

namespace {

using Result = NameTicketCheck::Result;

// A pass as the hub makes it (hub/ticket.py): "n1." and the base64url of the account, the end, the nonce and the first 16 bytes of the HMAC over the first 20 bytes and the name
std::string make_pass(const std::string& secret, uint64_t account, uint32_t end, uint64_t nonce, const std::string& name) {
    std::string b(20, '\0');
    for (int i = 0; i < 8; ++i) b[static_cast<size_t>(i)] = static_cast<char>(account >> (8 * i));
    for (int i = 0; i < 4; ++i) b[static_cast<size_t>(8 + i)] = static_cast<char>(end >> (8 * i));
    for (int i = 0; i < 8; ++i) b[static_cast<size_t>(12 + i)] = static_cast<char>(nonce >> (8 * i));
    std::string signed_bytes("ants-name-ticket-1", 18);
    signed_bytes.push_back('\0');
    signed_bytes += b;
    signed_bytes += name;
    b += hmac_sha256(secret, signed_bytes).substr(0, 16);
    return "n1." + base64url_encode(b);
}

struct Vector {
    std::string secret, name, token;
    uint64_t account{0};
    uint32_t end{0};
    uint64_t nonce{0};
};

std::vector<Vector> read_vectors() {
    std::vector<Vector> out;
    std::ifstream in(std::string(TEST_DATA_DIR) + "/name_tickets.txt");
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        size_t from = 0;
        while (true) {
            const size_t tab = line.find('\t', from);
            f.push_back(line.substr(from, tab == std::string::npos ? std::string::npos : tab - from));
            if (tab == std::string::npos) break;
            from = tab + 1;
        }
        if (f.size() != 6) continue;
        Vector v;
        v.secret = f[0];
        v.account = std::stoull(f[1]);
        v.end = static_cast<uint32_t>(std::stoull(f[2]));
        for (size_t i = 0; i < 8; ++i) v.nonce |= static_cast<uint64_t>(std::stoul(f[3].substr(2 * i, 2), nullptr, 16)) << (8 * i);
        v.name = f[4];
        v.token = f[5];
        out.push_back(v);
    }
    return out;
}

std::string raw_of(const std::string& token) {
    std::string raw;
    base64url_decode(std::string_view(token).substr(3), raw);
    return raw;
}

std::string flip_bit(const std::string& token, size_t bit) {
    std::string raw = raw_of(token);
    raw[bit / 8] = static_cast<char>(static_cast<unsigned char>(raw[bit / 8]) ^ (1u << (bit % 8)));
    return "n1." + base64url_encode(raw);
}

struct Xorshift {
    uint64_t s;
    explicit Xorshift(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint64_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    size_t below(size_t n) { return static_cast<size_t>(next() % n); }
};

// A secret that nobody else has: random at run time
std::string throwaway_secret(uint64_t seed) {
    Xorshift r(seed ^ static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string s;
    for (int i = 0; i < 40; ++i) s.push_back(static_cast<char>('!' + r.below(94)));
    return s;
}

ServerLimits gate_limits(bool on = true) {
    ServerLimits l;
    l.demo_rooms = 4;
    l.demo_lobbies = 40;
    l.max_rooms = 64;
    l.demo_map = "TINY.LVL";
    l.demo_maps = {"TINY.LVL", "SMALL.LVL"};
    l.verified_names = on;
    return l;
}

std::string seat_name(const Client& c) { return c.lobby->room().slots[c.lobby->my_seat()].name; }

}  // namespace

void run_name_ticket_tests() {
    constexpr uint32_t kEnd = 1700000600u;

    TEST_CASE("S3.264 The Pass: Every Vector Of The Shared Table Is Good With Its Own Secret Before Its End, And Bad With One Bit Of Any Of Its 288 Flipped, With One Character Of The Name Changed, Or With Another Secret; The Test's Own Maker Gives The Table's Passes") {
        const std::vector<Vector> vectors = read_vectors();
        ASSERT_EQ(vectors.size(), size_t{8});
        for (const Vector& v : vectors) {
            ASSERT_EQ(make_pass(v.secret, v.account, v.end, v.nonce, v.name), v.token);       // (the hub's maker and this one agree byte for byte)
            {
                NameTicketCheck check(v.secret);
                ASSERT_TRUE(check.check(v.token, v.name, v.end - 600) == Result::Good);
            }
            for (size_t bit = 0; bit < 288; ++bit) {                                         // the account (64 bits), the end (32), the nonce (64), the MAC (128): each is under the MAC
                NameTicketCheck check(v.secret);
                ASSERT_MSG(check.check(flip_bit(v.token, bit), v.name, v.end - 600) == Result::Bad, std::to_string(bit));
            }
            for (size_t i = 0; i < v.name.size(); ++i) {
                std::string other = v.name;
                other[i] = other[i] == 'q' ? 'r' : 'q';
                NameTicketCheck check(v.secret);
                ASSERT_MSG(check.check(v.token, other, v.end - 600) == Result::Bad, other);
            }
            NameTicketCheck other_secret(v.secret + "x");
            ASSERT_TRUE(other_secret.check(v.token, v.name, v.end - 600) == Result::Bad);
            NameTicketCheck shorter(v.secret.substr(1));
            ASSERT_TRUE(shorter.check(v.token, v.name, v.end - 600) == Result::Bad);
        }
    } TEST_END();

    TEST_CASE("S3.265 The Pass: No Pass Is None (And So Is Any Pass On A Server With No Secret); A Pass Of Another Prefix, Length Or Alphabet, Or With A Pad, Is Malformed; The Name Must Be One That A Pass May Be For") {
        const std::string secret = throwaway_secret(1);
        const std::string good = make_pass(secret, 77, kEnd, 5, "Ann");
        NameTicketCheck check(secret);
        ASSERT_TRUE(check.check("", "Ann", kEnd - 10) == Result::None);
        NameTicketCheck without_secret("");
        ASSERT_TRUE(without_secret.check(good, "Ann", kEnd - 10) == Result::None);
        ASSERT_TRUE(!without_secret.has_secret());
        const std::string body = good.substr(3);
        ASSERT_EQ(good.size(), size_t{51});
        const std::vector<std::string> malformed = {
            "n1.",                                                  // (nothing after the prefix)
            "x1." + body, "N1." + body, "n2." + body, "n1:" + body, body + "...",      // (another prefix)
            good.substr(0, 50), good + "A", good + "AA", good.substr(0, 47), good + "AAAA",   // (50, 52, 47 and 55 characters: 35, 36 and a half, and 41 bytes)
            "n1." + body.substr(0, 47) + "=",                       // (a pad where a character was)
            "n1." + body.substr(0, 10) + "+" + body.substr(11),     // (a character outside base64url)
            "n1." + body.substr(0, 10) + "/" + body.substr(11),
            "n1." + body.substr(0, 10) + " " + body.substr(11),
            "n1." + body.substr(0, 10) + std::string(1, '\0') + body.substr(11),
            "n1." + body.substr(0, 10) + "\xc3" + body.substr(12),  // (a byte above 127)
            " " + good, good + " ", good + "\n",
            std::string(51, 'A'),
        };
        for (const std::string& token : malformed) ASSERT_MSG(check.check(token, "Ann", kEnd - 10) == Result::Malformed, token);
        ASSERT_TRUE(check.check(good, "Ann", kEnd - 10) == Result::Good);
        for (const char* name : {"A", "", "Guest 123456", "guest123456", "G u e s t", "Pip (bot, Hard)", "Bot (Hard)", " Ann", "Ann "}) {      // (a pass is for a name of the rules: the hub never signs these)
            NameTicketCheck once(secret);
            const std::string pass = make_pass(secret, 77, kEnd, 9, name);
            ASSERT_MSG(once.check(pass, name, kEnd - 10) == Result::Bad, name);
        }
        for (const char* name : {"Guest", "Guest12345a", "Guests", "Guest 12 Smith", "Ann", "Al", "Bob Smith", "A_B-C.d!"}) {
            NameTicketCheck once(secret);
            const std::string pass = make_pass(secret, 77, kEnd, 9, name);
            ASSERT_MSG(once.check(pass, name, kEnd - 10) == (std::string(name) == "Guest" ? Result::Bad : Result::Good), name);
        }
        const NameTicketCheck::Counts& c = check.counts();
        ASSERT_EQ(c.none, uint64_t{1});
        ASSERT_EQ(c.malformed, malformed.size());
        ASSERT_EQ(c.good, uint64_t{1});
    } TEST_END();

    TEST_CASE("S3.266 The Pass: It Is Late At Its End (now == end) And Good A Second Before; One That Ends More Than 630 s From Now Is Bad (630 Is Good); The Window Lets A Pass Go At Its End, And Then The Same Pass Is Late And Never Good") {
        const std::string secret = throwaway_secret(2);
        const std::string pass = make_pass(secret, 77, kEnd, 5, "Ann");
        {
            NameTicketCheck check(secret);
            ASSERT_TRUE(check.check(pass, "Ann", kEnd) == Result::Late);
            ASSERT_TRUE(check.check(pass, "Ann", kEnd + 1) == Result::Late);
            ASSERT_TRUE(check.check(pass, "Ann", kEnd + 100000) == Result::Late);
            ASSERT_TRUE(check.check(pass, "Ann", kEnd - 1) == Result::Good);
        }
        {
            NameTicketCheck check(secret);
            ASSERT_TRUE(check.check(pass, "Ann", kEnd - 631) == Result::Bad);
            ASSERT_TRUE(check.check(pass, "Ann", kEnd - 100000) == Result::Bad);
            ASSERT_TRUE(check.check(pass, "Ann", kEnd - 630) == Result::Good);
        }
        {
            NameTicketCheck check(secret);
            ASSERT_TRUE(check.check(pass, "Ann", kEnd - 100) == Result::Good);
            ASSERT_TRUE(check.check(pass, "Ann", kEnd - 50) == Result::Reused);
            ASSERT_TRUE(check.check(pass, "Ann", kEnd - 1) == Result::Reused);
            ASSERT_EQ(check.window_size(), size_t{1});
            ASSERT_TRUE(check.check(pass, "Ann", kEnd) == Result::Late);                     // the end has passed: late, and never good again
            ASSERT_TRUE(check.check(make_pass(secret, 77, kEnd + 400, 6, "Ann"), "Ann", kEnd + 1) == Result::Good);     // (a new pass drops the one that ran out)
            ASSERT_EQ(check.window_size(), size_t{1});
            ASSERT_TRUE(check.check(pass, "Ann", kEnd + 2) == Result::Late);
        }
    } TEST_END();

    TEST_CASE("S3.267 The Window: The Second Use Is Reused; A Different Nonce Is Good Again, Also For The Same Account And Name; A Full Window (32,768 Live Passes) Is Full For A Newcomer, Drops Nothing (The First Is Still Reused), And Takes Passes Again When Some Have Run Out") {
        const std::string secret = throwaway_secret(3);
        NameTicketCheck check(secret);
        const uint32_t now = kEnd - 600;
        ASSERT_TRUE(check.check(make_pass(secret, 1, kEnd, 1, "Ann"), "Ann", now) == Result::Good);
        ASSERT_TRUE(check.check(make_pass(secret, 1, kEnd, 1, "Ann"), "Ann", now) == Result::Reused);
        ASSERT_TRUE(check.check(make_pass(secret, 1, kEnd, 2, "Ann"), "Ann", now) == Result::Good);
        ASSERT_TRUE(check.check(make_pass(secret, 2, kEnd, 1, "Bob"), "Bob", now) == Result::Reused);       // (the nonce is the pass's own number, whoever holds it)
        ASSERT_EQ(kNameWindowCap, size_t{32768});
        NameTicketCheck full(secret);
        for (uint64_t n = 0; n < kNameWindowCap; ++n) {
            // half of them end sooner, so that the window has entries in two different ends
            const uint32_t end = kEnd + (n % 2 == 0 ? 30u : 0u);
            ASSERT_TRUE(full.check(make_pass(secret, 5, end, 1000 + n, "Ann"), "Ann", now) == Result::Good);
        }
        ASSERT_EQ(full.window_size(), kNameWindowCap);
        ASSERT_TRUE(full.check(make_pass(secret, 5, kEnd, 999999, "Ann"), "Ann", now) == Result::Full);
        ASSERT_TRUE(full.check(make_pass(secret, 5, kEnd, 1000, "Ann"), "Ann", now) == Result::Reused);      // (nothing was pushed out for the newcomer: the oldest is still there)
        ASSERT_TRUE(full.check(make_pass(secret, 5, kEnd, 1000 + kNameWindowCap - 1, "Ann"), "Ann", now) == Result::Reused);
        ASSERT_EQ(full.window_size(), kNameWindowCap);
        ASSERT_EQ(full.counts().full, uint64_t{1});
        ASSERT_TRUE(full.check(make_pass(secret, 5, kEnd + 600, 999999, "Ann"), "Ann", kEnd + 1) == Result::Good);       // the odd ones ran out (their end is kEnd): room again; the even ones (kEnd + 30) are still live
        ASSERT_EQ(full.window_size(), kNameWindowCap / 2 + 1);
    } TEST_END();

    TEST_CASE("S3.268 Guest Names: The Form Is Exactly Guest And Six Digits With No Leading Zero; A Drawn Name Has The Form, Comes From The Source's Bytes (Little Endian), Throws Away The Uneven Rest, And Is Empty When The Source Fails") {
        ASSERT_TRUE(is_guest_name("Guest 100000") && is_guest_name("Guest 999999") && is_guest_name("Guest 123456"));
        for (const char* bad : {"guest 123456", "GUEST 123456", "Guest 012345", "Guest 1234567", "Guest 12345", "Guest 123456 ", "Guest  123456", "Guest-123456", "Guest 123 56", "Guest 12345x", ""}) ASSERT_MSG(!is_guest_name(bad), bad);
        const auto from_word = [](uint32_t w) {
            return draw_guest_name([w](uint8_t* out, size_t n) {
                for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(w >> (8 * i));
                return true;
            });
        };
        ASSERT_EQ(from_word(0), std::string("Guest 100000"));
        ASSERT_EQ(from_word(899999u), std::string("Guest 999999"));
        ASSERT_EQ(from_word(4294799999u), std::string("Guest 999999"));
        ASSERT_EQ(from_word(0x01020304u), "Guest " + std::to_string(100000u + 0x01020304u % 900000u));
        ASSERT_EQ(from_word(4294800000u), std::string());                                     // (the same word again and again: always thrown away: no name rather than a leaning one)
        ASSERT_EQ(draw_guest_name([](uint8_t*, size_t) { return false; }), std::string());
        int calls = 0;
        const std::string second = draw_guest_name([&calls](uint8_t* out, size_t n) {         // (the first word is thrown away, the second is used)
            const uint32_t w = calls++ == 0 ? 0xFFFFFFFFu : 12345u;
            for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(w >> (8 * i));
            return true;
        });
        ASSERT_EQ(second, std::string("Guest 112345"));
        // fresh draws from the system's generator: all of the form, and spread over the range (every tenth of it is hit by about a tenth of 5,000 draws)
        std::vector<int> tenths(10, 0);
        for (int i = 0; i < 5000; ++i) {
            const std::string g = draw_guest_name();
            ASSERT_MSG(is_guest_name(g), g);
            ++tenths[(std::stoul(g.substr(6)) - 100000u) / 90000u];
        }
        for (const int n : tenths) ASSERT_TRUE(n > 350 && n < 650);
    } TEST_END();

    TEST_CASE("S3.269 The Door With The Check On: A Hello Without A Pass Is Seated As A Guest Whose Name The Room Shows; A Guest Name Of The Exact Form Is Kept; Anything Else Typed (Lower Case, A Leading Zero, Seven Digits, A Name) Gets A Drawn Guest Name; A Good Pass Keeps Its Name; A Pass With A Changed Name Or A Second Use Gets A Guest Name; With No Secret Everybody Is A Guest") {
        const std::string secret = throwaway_secret(4);
        uint64_t unix_now = 1700000000ull;
        World w(gate_limits());
        w.mgr.set_name_secret(secret);
        w.mgr.set_unix_clock([&unix_now]() { return unix_now; });
        int room_no = 0;
        const auto join = [&](const std::string& typed, const std::string& pass) -> std::string {
            w.next_token = pass;
            Client& c = w.connect_page(typed, "nm" + std::to_string(100000 + room_no++), lobby_block_of());
            w.next_token.clear();
            w.run(200);
            if (c.lobby->phase() != net::ClientLobby::Phase::InRoom) return "(not seated)";
            return seat_name(c);
        };
        {
            const std::string none = join("Ann", "");
            ASSERT_TRUE(is_guest_name(none));
            ASSERT_TRUE(none != "Ann");
        }
        ASSERT_EQ(join("Guest 123456", ""), std::string("Guest 123456"));
        for (const char* typed : {"guest 123456", "Guest 012345", "Guest 1234567", "Ann", "GUEST 123456", "Guest 12345", "Pip (bot, Hard)"}) {
            const std::string got = join(typed, "");
            ASSERT_MSG(is_guest_name(got) && got != typed, std::string(typed) + " -> " + got);
        }
        ASSERT_EQ(join("Ann", make_pass(secret, 77, static_cast<uint32_t>(unix_now + 600), 1, "Ann")), std::string("Ann"));
        {
            const std::string got = join("Bob", make_pass(secret, 77, static_cast<uint32_t>(unix_now + 600), 2, "Ann"));      // a good pass of Ann's, the Hello says Bob
            ASSERT_TRUE(is_guest_name(got));
        }
        {
            const std::string pass = make_pass(secret, 77, static_cast<uint32_t>(unix_now + 600), 3, "Cleo");
            ASSERT_EQ(join("Cleo", pass), std::string("Cleo"));
            const std::string again = join("Cleo", pass);                                    // the second use, in another room
            ASSERT_TRUE(is_guest_name(again));
        }
        {
            const std::string late = join("Dora", make_pass(secret, 77, static_cast<uint32_t>(unix_now - 1), 4, "Dora"));
            ASSERT_TRUE(is_guest_name(late));
            const std::string other = join("Eli", make_pass(throwaway_secret(40), 77, static_cast<uint32_t>(unix_now + 600), 5, "Eli"));
            ASSERT_TRUE(is_guest_name(other));
        }
        const NameTicketCheck::Counts& c = w.mgr.name_gate()->check().counts();
        ASSERT_EQ(c.good, uint64_t{2});
        ASSERT_EQ(c.reused, uint64_t{1});
        ASSERT_EQ(c.late, uint64_t{1});
        ASSERT_EQ(c.bad, uint64_t{2});
        ASSERT_EQ(c.none, uint64_t{9});
        ASSERT_EQ(w.mgr.name_gate()->guests_named(), uint64_t{13});                    // (nine with no pass, the changed name, the second use, the late pass, the pass of another secret)
    } TEST_END();

    TEST_CASE("S3.270 The Door With The Check On: A Key That Fits A Seat Takes It Over With The Seat's Own Name And The Pass Is Not Looked At Or Spent (The Same Pass Then Seats A New Person); A Key That Fits No Seat Is A New Person; In A Running Match A Game That Comes Back Keeps The Roster's Names") {
        const std::string secret = throwaway_secret(5);
        uint64_t unix_now = 1700000000ull;
        World w(gate_limits());
        w.mgr.set_name_secret(secret);
        w.mgr.set_unix_clock([&unix_now]() { return unix_now; });
        const std::string zed_pass = make_pass(secret, 9, static_cast<uint32_t>(unix_now + 600), 77, "Zed");
        Client& pia = w.connect_page("Pia", "tk000001", lobby_block_of());
        w.run(200);
        const std::string pia_name = seat_name(pia);
        ASSERT_TRUE(is_guest_name(pia_name));
        const net::SeatKey pia_key = pia.lobby->key();
        w.next_token = zed_pass;
        Client& game = w.connect_game("Zed", "tk000001", pia_key);                          // a game with Pia's key, a name and a good pass of another person's name: the seat's
        w.next_token.clear();
        w.run(300);
        ASSERT_EQ(game.lobby->my_seat(), pia.lobby->my_seat());
        ASSERT_EQ(seat_name(game), pia_name);
        ASSERT_EQ(w.mgr.name_gate()->check().counts().good, uint64_t{0});                    // (not looked at)
        ASSERT_EQ(w.mgr.name_gate()->check().counts().reused, uint64_t{0});
        w.next_token = zed_pass;
        Client& zed = w.connect_page("Zed", "tk000001", lobby_block_of());                   // the same pass seats a new person: it was not spent
        w.next_token.clear();
        w.run(300);
        ASSERT_EQ(seat_name(zed), std::string("Zed"));
        ASSERT_EQ(w.mgr.name_gate()->check().counts().good, uint64_t{1});
        {   // a key that fits no seat is a new person: the pass is looked at, and a name without one is a guest
            net::SeatKey stranger{};
            stranger[0] = 7;
            stranger[15] = 9;
            w.next_key = stranger;
            Client& lost = w.connect_page("Mallory", "tk000001", lobby_block_of());
            w.next_key = net::SeatKey{};
            w.run(300);
            ASSERT_TRUE(is_guest_name(seat_name(lost)));
        }
        {   // a running match: the games take the seats of the pages (names of their own, no pass), and a game that comes back keeps what the roster says
            World m(gate_limits());
            m.mgr.set_name_secret(secret);
            m.mgr.set_unix_clock([&unix_now]() { return unix_now; });
            m.next_token = make_pass(secret, 1, static_cast<uint32_t>(unix_now + 600), 1, "Ann");
            Client& a = m.connect_page("Ann", "tk000002", lobby_block_of());
            m.next_token = make_pass(secret, 2, static_cast<uint32_t>(unix_now + 600), 2, "Bea");
            Client& b = m.connect_page("Bea", "tk000002", lobby_block_of());
            m.next_token.clear();
            m.run(200);
            ASSERT_TRUE(seat_name(a) == "Ann" && seat_name(b) == "Bea");
            const net::SeatKey ka = a.lobby->key();
            const net::SeatKey kb = b.lobby->key();
            ASSERT_TRUE(a.lobby->request_start());
            m.run(300);
            m.next_token = "n1." + std::string(48, 'A');                                     // (whatever a game's Hello carries: it takes a seat over, it is not looked at)
            Client& ga = m.connect_game("Mallory", "tk000002", ka);
            Client& gb = m.connect_game("Eve", "tk000002", kb);
            m.next_token.clear();
            m.run(4000);
            const RoomStatus s = m.status("tk000002");
            ASSERT_TRUE(s.state == RoomState::Running);
            ASSERT_TRUE(s.names[0] == "Ann" && s.names[1] == "Bea");
            ASSERT_EQ(m.mgr.name_gate()->check().counts().malformed, uint64_t{0});
            Client& back = m.connect_game("Mallory", "tk000002", ka);                        // the game comes back to the running match (a key)
            m.run(1000);
            const RoomStatus after = m.status("tk000002");
            ASSERT_TRUE(after.names[0] == "Ann" && after.names[1] == "Bea");
            (void)ga; (void)gb; (void)back;
        }
    } TEST_END();

    TEST_CASE("S3.271 The Door With No Secret (The Check On): Everybody Is A Guest Whatever The Hello Carries; With The Check Off Nothing Changes: The Typed Name Is Kept, Two People May Share It, A Name Message Renames, And The Token Is Never Read") {
        const std::string secret = throwaway_secret(6);
        uint64_t unix_now = 1700000000ull;
        {
            World w(gate_limits());                                                         // (the check is on, no secret was given)
            w.mgr.set_unix_clock([&unix_now]() { return unix_now; });
            w.next_token = make_pass(secret, 1, static_cast<uint32_t>(unix_now + 600), 1, "Ann");
            Client& a = w.connect_page("Ann", "ns000001", lobby_block_of());
            w.next_token.clear();
            w.run(200);
            ASSERT_TRUE(is_guest_name(seat_name(a)));
            ASSERT_EQ(w.mgr.name_gate()->check().counts().good, uint64_t{0});
        }
        {   // the check is off unless the server is told: default limits check nothing, and a name is kept as it was typed
            ASSERT_FALSE(ServerLimits().verified_names);
            World w;
            ASSERT_TRUE(w.mgr.name_gate() == nullptr && !w.mgr.default_spec().verified_names);
            ASSERT_TRUE(w.mgr.create_room(spec_of("DF-1", 2), w.now).ok);
            Client& a = w.connect("Ann", "DF-1");
            Client& b = w.connect("Ann", "DF-1");
            w.run(300);
            ASSERT_TRUE(seat_name(a) == "Ann" && seat_name(b) == "Ann");
        }
        {
            World w(gate_limits(false));
            ASSERT_TRUE(w.mgr.name_gate() == nullptr);
            w.mgr.set_name_secret(secret);                                                  // (a secret with the check off: nothing)
            ASSERT_TRUE(w.mgr.name_gate() == nullptr);
            w.next_token = "n1." + std::string(48, 'A');
            Client& a = w.connect_page("Ann", "ns000002", lobby_block_of());
            w.next_token.clear();
            Client& b = w.connect_page("Ann", "ns000002", lobby_block_of());
            Client& c = w.connect_page("guest 123456", "ns000002", lobby_block_of());
            w.run(200);
            ASSERT_EQ(seat_name(a), std::string("Ann"));
            ASSERT_EQ(seat_name(b), std::string("Ann"));                                    // (two people with one name: as it was)
            ASSERT_EQ(seat_name(c), std::string("guest 123456"));
            ASSERT_TRUE(b.lobby->request_name("Bobby"));
            w.run(200);
            ASSERT_EQ(seat_name(b), std::string("Bobby"));
            ASSERT_EQ(w.status("ns000002").names[1], std::string("Bobby"));
        }
        {   // with the check on, a name message does not rename
            World w(gate_limits());
            w.mgr.set_name_secret(secret);
            w.mgr.set_unix_clock([&unix_now]() { return unix_now; });
            w.next_token = make_pass(secret, 1, static_cast<uint32_t>(unix_now + 600), 1, "Ann");
            Client& a = w.connect_page("Ann", "ns000003", lobby_block_of());
            w.next_token.clear();
            w.run(200);
            ASSERT_TRUE(a.lobby->request_name("Mallory"));
            w.run(200);
            ASSERT_EQ(seat_name(a), std::string("Ann"));
        }
    } TEST_END();

    TEST_CASE("S3.272 Two People With The Same Name Through The Door: A Second Pass For The Same Name Is Seated As Name (2) (Two Accounts May Share A Display Name), Two Of One Browser's Guest Name Get Different Ones, The Control Interface's Rooms Follow The Server's Setting") {
        const std::string secret = throwaway_secret(7);
        uint64_t unix_now = 1700000000ull;
        World w(gate_limits());
        w.mgr.set_name_secret(secret);
        w.mgr.set_unix_clock([&unix_now]() { return unix_now; });
        w.next_token = make_pass(secret, 1, static_cast<uint32_t>(unix_now + 600), 1, "Ann");
        Client& a = w.connect_page("Ann", "dp000001", lobby_block_of());
        w.next_token = make_pass(secret, 2, static_cast<uint32_t>(unix_now + 600), 2, "Ann");
        Client& b = w.connect_page("Ann", "dp000001", lobby_block_of());
        w.next_token.clear();
        Client& g1 = w.connect_page("Guest 424242", "dp000001", lobby_block_of());
        Client& g2 = w.connect_page("Guest 424242", "dp000001", lobby_block_of());
        w.run(300);
        ASSERT_EQ(seat_name(a), std::string("Ann"));
        ASSERT_EQ(seat_name(b), std::string("Ann (2)"));
        ASSERT_EQ(seat_name(g1), std::string("Guest 424242"));
        ASSERT_TRUE(is_guest_name(seat_name(g2)) && seat_name(g2) != "Guest 424242");
        ASSERT_EQ(w.mgr.name_gate()->check().counts().good, uint64_t{2});
        w.mgr.take_notices();
        ASSERT_TRUE(w.mgr.default_spec().verified_names);
        ServerLimits off = gate_limits(false);
        World plain(off);
        ASSERT_FALSE(plain.mgr.default_spec().verified_names);
        RoomSpec spec = spec_of("CR-9", 2);
        spec.verified_names = true;
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);                                      // (a room of the control interface: names checked too)
        Client& x = w.connect("Xan", "CR-9");
        Client& y = w.connect("Xan", "CR-9");
        w.run(300);
        ASSERT_TRUE(is_guest_name(x.lobby->room().slots[x.lobby->my_seat()].name) && is_guest_name(y.lobby->room().slots[y.lobby->my_seat()].name));
        ASSERT_TRUE(x.lobby->room().slots[x.lobby->my_seat()].name != y.lobby->room().slots[y.lobby->my_seat()].name);
    } TEST_END();

    TEST_CASE("S3.273 The Log Lines: At Most One Count Line A Minute And Only When A Count Moved, With The Seven Counts, The Guests Named And The Names Changed In Rooms; The Line Never Holds A Name, A Pass, A Number Or The Secret; The Secrets Line Comes After Ten Minutes With Five Bad Passes And No Good One, At Most Once An Hour") {
        const std::string secret = throwaway_secret(8);
        NameGate gate(secret);
        uint64_t t = 1700000000ull;
        ASSERT_TRUE(gate.report(t).empty());                                                 // (nothing happened)
        gate.name_for("", "Ann", t);
        std::vector<std::string> lines = gate.report(t);
        ASSERT_EQ(lines.size(), size_t{1});
        ASSERT_EQ(lines[0], std::string("name tickets: good 0, none 1, malformed 0, bad 0, late 0, reused 0, full 0, guests named 1, renamed in room 0"));
        gate.name_for("", "Ann", t + 10);
        ASSERT_TRUE(gate.report(t + 10).empty());                                            // (a count moved, but a line came less than a minute ago)
        ASSERT_TRUE(gate.report(t + 59).empty());
        gate.add_renamed(2);
        lines = gate.report(t + 60);
        ASSERT_EQ(lines.size(), size_t{1});
        ASSERT_EQ(lines[0], std::string("name tickets: good 0, none 2, malformed 0, bad 0, late 0, reused 0, full 0, guests named 2, renamed in room 2"));
        ASSERT_TRUE(gate.report(t + 500).empty());                                           // (nothing moved)
        const std::string name = "Zebulon Quux";
        const std::string pass = make_pass(secret, 31415926535ull, static_cast<uint32_t>(t + 1000), 0x1122334455667788ull, name);
        ASSERT_EQ(gate.name_for(pass, name, t + 400), name);
        lines = gate.report(t + 600);
        ASSERT_EQ(lines.size(), size_t{1});
        for (const std::string& line : lines) {
            ASSERT_TRUE(line.find("Zebulon") == std::string::npos && line.find(pass) == std::string::npos && line.find(secret) == std::string::npos);
            ASSERT_TRUE(line.find("31415926535") == std::string::npos && line.find("1122334455667788") == std::string::npos && line.find("Guest") == std::string::npos);
        }
        // the secrets line: ten minutes, at least five bad passes, no good one
        NameGate wrong(secret);
        const std::string other = make_pass("x" + secret, 1, static_cast<uint32_t>(t + 5000), 1, "Ann");
        wrong.report(t);                                                                     // (the window of ten minutes starts)
        for (int i = 0; i < 5; ++i) wrong.name_for(make_pass("y" + secret, 1, static_cast<uint32_t>(t + 5000), 10 + static_cast<uint64_t>(i), "Ann"), "Ann", t + 30);
        (void)other;
        bool said = false;
        for (const std::string& l : wrong.report(t + 599)) said = said || l.find("secrets") != std::string::npos;
        ASSERT_FALSE(said);
        std::vector<std::string> at_600 = wrong.report(t + 600);
        said = false;
        for (const std::string& l : at_600) said = said || l == "every name ticket failed its check: are the two secrets the same?";
        ASSERT_TRUE(said);
        for (int i = 0; i < 5; ++i) wrong.name_for(make_pass("y" + secret, 1, static_cast<uint32_t>(t + 5000), 100 + static_cast<uint64_t>(i), "Ann"), "Ann", t + 700);
        said = false;
        for (const std::string& l : wrong.report(t + 1200)) said = said || l.find("secrets") != std::string::npos;
        ASSERT_FALSE(said);                                                                  // (at most once an hour)
        for (int i = 0; i < 5; ++i) wrong.name_for(make_pass("y" + secret, 1, static_cast<uint32_t>(t + 9000), 200 + static_cast<uint64_t>(i), "Ann"), "Ann", t + 3000);
        wrong.report(t + 3000);
        for (int i = 0; i < 5; ++i) wrong.name_for(make_pass("y" + secret, 1, static_cast<uint32_t>(t + 9000), 300 + static_cast<uint64_t>(i), "Ann"), "Ann", t + 3700);
        said = false;
        for (const std::string& l : wrong.report(t + 4300)) said = said || l.find("secrets") != std::string::npos;
        ASSERT_TRUE(said);                                                                   // (an hour after the first)
        // one good pass in the window: no secrets line
        NameGate fine(secret);
        fine.report(t);
        for (int i = 0; i < 5; ++i) fine.name_for(make_pass("y" + secret, 1, static_cast<uint32_t>(t + 5000), 10 + static_cast<uint64_t>(i), "Ann"), "Ann", t + 30);
        ASSERT_EQ(fine.name_for(make_pass(secret, 1, static_cast<uint32_t>(t + 600), 99, "Ann"), "Ann", t + 40), std::string("Ann"));
        said = false;
        for (const std::string& l : fine.report(t + 700)) said = said || l.find("secrets") != std::string::npos;
        ASSERT_FALSE(said);
    } TEST_END();

    TEST_CASE("S3.274 Fresh Inputs: 20,000 Random Tokens (Random Lengths And Alphabets, And Mutations Of Good Passes) Are Never Good Unless They Were Made With The Secret And Not Used Yet; No Crash; The Slowest Check Is Bounded") {
        const std::string secret = throwaway_secret(9);
        NameTicketCheck check(secret);
        Xorshift rng(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
        const std::string alphabet_url = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        uint64_t honest = 0, honest_good = 0, others_good = 0;
        double slowest_ms = 0;
        const uint64_t now = kEnd - 300;
        for (int i = 0; i < 20000; ++i) {
            std::string token, name = "Ann";
            const bool made = i % 5 == 0;
            switch (i % 5) {
                case 0: token = make_pass(secret, rng.next() >> 2, kEnd, rng.next(), name); break;                  // (made with the secret)
                case 1: {                                                                                        // (a good pass with a character changed)
                    token = make_pass(secret, 3, kEnd, rng.next(), name);
                    const size_t at = 3 + rng.below(48);
                    char other = alphabet_url[rng.below(64)];
                    if (other == token[at]) other = other == 'A' ? 'B' : 'A';                                     // (a different character, or the pass would be the honest one)
                    token[at] = other;
                    break;
                }
                case 2: {                                                                                        // (random alphabet, random length, sometimes the prefix)
                    const size_t len = rng.below(80);
                    for (size_t k = 0; k < len; ++k) token.push_back(static_cast<char>(33 + rng.below(94)));
                    if (rng.below(2) == 0 && token.size() >= 3) token.replace(0, 3, "n1.");
                    break;
                }
                case 3: {                                                                                        // (right length, right alphabet, right prefix)
                    token = "n1.";
                    for (int k = 0; k < 48; ++k) token.push_back(alphabet_url[rng.below(64)]);
                    break;
                }
                default: {                                                                                       // (another secret, or another name, or a time out of range)
                    const uint64_t pick = rng.below(3);
                    if (pick == 0) token = make_pass(secret + "z", 3, kEnd, rng.next(), name);
                    else if (pick == 1) { token = make_pass(secret, 3, kEnd, rng.next(), "Bob"); }
                    else token = make_pass(secret, 3, kEnd + 100000u, rng.next(), name);
                    break;
                }
            }
            const auto began = std::chrono::steady_clock::now();
            const Result r = check.check(token, name, now);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
            slowest_ms = std::max(slowest_ms, ms);
            if (made) {
                ++honest;
                honest_good += r == Result::Good ? 1u : 0u;
            } else {
                others_good += r == Result::Good ? 1u : 0u;
            }
        }
        std::cout << "(slowest of 20,000 checks: " << slowest_ms << " ms) " << std::flush;
        ASSERT_EQ(others_good, uint64_t{0});
        ASSERT_EQ(honest_good, honest);
        ASSERT_TRUE(slowest_ms < 50.0);
        ASSERT_EQ(check.counts().good, honest);
    } TEST_END();
}
