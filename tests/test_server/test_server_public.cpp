// Tests of the dedicated game server: public lobby rooms and the public list of games (network protocol 22, docs/SERVER.md "Lobby rooms"): GET /lobbies of the public door, what it lists and in which order, what it
// leaves out and never says, and the idle rule that makes an unused public room private
#include "server_fixtures.hpp"

namespace {

// The options of a server that offers the front page's lobbies, with a pool large enough for a list of more than 50
ServerLimits public_limits() {
    ServerLimits l;
    l.demo_rooms = 4;
    l.demo_lobbies = 70;
    l.demo_map = "TINY.LVL";
    l.demo_maps = {"TINY.LVL", "SMALL.LVL", "GAUNTLET.LVL"};
    return l;
}

// GET /lobbies (or another request) at the public door
ctl::HttpResponse door(World& w, const char* method, const std::string& path = "/lobbies", const std::string& query = std::string()) {
    ctl::HttpRequest rq;
    rq.method = method;
    rq.path = path;
    rq.query = query;
    return handle_public_replays(w.mgr, rq, w.now);
}

// The answer to GET /lobbies, parsed (null when it is no JSON)
ctl::JsonValue list_of(World& w) {
    ctl::JsonValue j;
    const ctl::HttpResponse r = door(w, "GET");
    std::string why;
    if (r.status != 200 || !ctl::parse_json(r.body, j, &why)) return ctl::JsonValue();
    return j;
}

// The codes of the list, in its order, joined by commas
std::string codes_of(const ctl::JsonValue& list) {
    std::string out;
    const ctl::JsonValue& rows = list.get("lobbies");
    for (size_t i = 0; i < rows.size(); ++i) out += (out.empty() ? "" : ",") + rows.at(i).get("code").str();
    return out;
}

std::string codes_of(World& w) { return codes_of(list_of(w)); }

// The row of a code in the list (null when it is not there)
ctl::JsonValue row_of(World& w, const std::string& code) {
    const ctl::JsonValue list = list_of(w);
    const ctl::JsonValue& rows = list.get("lobbies");
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows.at(i).get("code").str() == code) return rows.at(i);
    }
    return ctl::JsonValue();
}

// A page whose Hello (no jitter on its link: two of them sent in one pass arrive in one pass) makes or joins the lobby of `code`
Client& page_on_clean_link(World& w, const std::string& name, const std::string& code) {
    w.next_kind = net::kClientPage;
    w.next_create = lobby_block_of();
    Client& c = w.connect(name, code, 255, {20, 0});
    w.next_kind = net::kClientGame;
    w.next_create.reset();
    return c;
}

// The leader of the room says public or private, and the room has heard it (and told everybody)
void say_visibility(World& w, Client& leader, bool make_public) {
    leader.lobby->request_visibility(make_public);
    w.run(300);
}

net::PlanMsg plan_msg(const std::array<net::PlanKind, 4>& kinds, uint8_t mode = 0, const std::string& map = std::string()) {
    net::PlanMsg m;
    m.map_name = map;
    m.plan = kinds;
    m.mode = mode;
    return m;
}

std::string hex_of(const net::SeatKey& key, bool upper) {
    static const char* const lower = "0123456789abcdef";
    static const char* const capital = "0123456789ABCDEF";
    const char* digits = upper ? capital : lower;
    std::string s;
    for (const uint8_t b : key) {
        s.push_back(digits[b >> 4]);
        s.push_back(digits[b & 15]);
    }
    return s;
}

}  // namespace

void run_public_room_tests() {
    using K = net::PlanKind;

    TEST_CASE("S3.232 The List Of Games (Protocol 22, GET /lobbies Of The Public Door): Nothing But An Empty List On A Server With No Public Room; A Private Lobby Is Not In It; When The Leader Makes The Room Public It Is, With The Code, The Leader's Name, The Map, The Mode And The Numbers (People, Bots, Free Colors, Size, Age In Seconds), Exactly These And In This Order, And The Numbers Follow The Plan And The People") {
        World w(public_limits());
        const std::string empty = R"({"format":1,"lobbies":[],"count":0})";
        ASSERT_TRUE(door(w, "GET").status == 200 && door(w, "GET").body == empty && door(w, "GET").content_type == "application/json");
        Client& pia = w.connect_page("Pia", "gam00001", lobby_block_of());
        w.run(300);
        ASSERT_TRUE(pia.lobby->is_leader() && !w.status("gam00001").lobby_public);
        ASSERT_EQ(door(w, "GET").body, empty);                                                   // (a new room is private)
        say_visibility(w, pia, true);
        ASSERT_TRUE(w.status("gam00001").lobby_public && pia.lobby->room_is_public());
        ASSERT_EQ(door(w, "GET").body, R"({"format":1,"lobbies":[{"code":"gam00001","host":"Pia","map":"TINY.LVL","mode":"highest-score","people":1,"bots":0,"free":3,"size":4,"age":0}],"count":1})");
        // the numbers follow the people: Bob joins
        Client& bob = w.connect_page("Bob", "gam00001", lobby_block_of());
        w.run(300);
        ASSERT_TRUE(bob.lobby->room_is_public());
        ctl::JsonValue row = row_of(w, "gam00001");
        ASSERT_TRUE(row.get("people").as_int_or(-1) == 2 && row.get("free").as_int_or(-1) == 2 && row.get("bots").as_int_or(-1) == 0 && row.get("size").as_int_or(-1) == 4 && row.get("host").str() == "Pia");
        // ... and the plan: a medium bot in the third color, the last color left open, the map and the mode chosen
        ASSERT_TRUE(pia.lobby->request_plan(plan_msg({K::Open, K::Open, K::Medium, K::Open}, 1, "SMALL.LVL")));
        w.run(300);
        row = row_of(w, "gam00001");
        ASSERT_TRUE(row.get("people").as_int_or(-1) == 2 && row.get("bots").as_int_or(-1) == 1 && row.get("free").as_int_or(-1) == 1 && row.get("size").as_int_or(-1) == 4);
        ASSERT_TRUE(row.get("map").str() == "SMALL.LVL" && row.get("mode").str() == "187");
        ASSERT_TRUE(pia.lobby->request_plan(plan_msg({K::Open, K::Open, K::Open, K::Nobody})));  // (a color that is Nobody's is out of the match: size 3)
        w.run(1200);
        row = row_of(w, "gam00001");
        ASSERT_TRUE(row.get("people").as_int_or(-1) == 2 && row.get("bots").as_int_or(-1) == 0 && row.get("free").as_int_or(-1) == 1 && row.get("size").as_int_or(-1) == 3 && row.get("mode").str() == "highest-score");
        ASSERT_TRUE(row.get("people").as_int_or(0) + row.get("bots").as_int_or(0) + row.get("free").as_int_or(0) == row.get("size").as_int_or(-1));
        // the age is the seconds since the room became public, and "public" said again does not make it younger
        const int64_t before = row_of(w, "gam00001").get("age").as_int_or(-1000);
        ASSERT_TRUE(before >= 1 && before <= 3);
        w.run(90000);
        ASSERT_TRUE(row_of(w, "gam00001").get("age").as_int_or(-1) == before + 90);
        pia.lobby->request_visibility(true);
        w.run(300);
        ASSERT_TRUE(row_of(w, "gam00001").get("age").as_int_or(-1) == before + 90);
        // the shape: the keys of the answer and of a row are these and no others, in this order
        const ctl::JsonValue list = list_of(w);
        ASSERT_TRUE(list.size() == 3 && list.key_at(0) == "format" && list.key_at(1) == "lobbies" && list.key_at(2) == "count");
        const ctl::JsonValue& first = list.get("lobbies").at(0);
        const std::vector<std::string> want = {"code", "host", "map", "mode", "people", "bots", "free", "size", "age"};
        ASSERT_TRUE(first.keys() == want);
        ASSERT_TRUE(list.get("format").as_int_or(0) == 1 && list.get("count").as_int_or(-1) == 1);
        // the leader goes: the next person leads, the list says that person's name, and the room stays public
        pia.lobby->leave();
        w.run(300);
        ASSERT_TRUE(bob.lobby->is_leader() && bob.lobby->room_is_public());
        ASSERT_EQ(row_of(w, "gam00001").get("host").str(), "Bob");
        ASSERT_TRUE(row_of(w, "gam00001").get("people").as_int_or(-1) == 1 && row_of(w, "gam00001").get("free").as_int_or(-1) == 2);
        // the leader says private: the list is empty again, and the room goes on
        say_visibility(w, bob, false);
        ASSERT_EQ(door(w, "GET").body, empty);
        ASSERT_TRUE(w.status("gam00001").state == RoomState::Waiting && !w.status("gam00001").lobby_public && !bob.lobby->room_is_public());
        // a room that a game's block made (a plain public room) and a room of the control interface are never in it
        w.connect_creating("Gus", "gus00001", block_of("TINY.LVL", 4));
        RoomSpec spec = w.mgr.default_spec();
        spec.code = "CTL-1";
        spec.map = "TINY.LVL";
        spec.players = 4;
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        w.run(300);
        ASSERT_EQ(door(w, "GET").body, empty);
    } TEST_END();

    TEST_CASE("S3.233 What The List Leaves Out (Protocol 22): A Private Room, A Room That Waits For The Games Of Its START, A Match That Loads Or Runs, A Room Nobody Is Connected To (Only Held Seats), A Room With No Free Color (Four People, Or The Plan Gave The Rest To Bots Or To Nobody); Each Comes Back When It Is Over: The Cancelled START, The Person Who Comes Back With The Key, The Person Who Leaves") {
        World w(public_limits());
        // --- the START and the match
        Client& pia = w.connect_page("Pia", "fil00001", lobby_block_of());
        Client& bob = w.connect_page("Bob", "fil00001", lobby_block_of());
        w.run(300);
        say_visibility(w, pia, true);
        ASSERT_EQ(codes_of(w), "fil00001");
        const net::SeatKey pia_key = pia.lobby->key();
        const net::SeatKey bob_key = bob.lobby->key();
        ASSERT_TRUE(pia.lobby->request_start());
        w.run(300);
        ASSERT_TRUE(w.status("fil00001").starting && w.status("fil00001").lobby_public);        // (public and starting: the leader's choice stands, the list does not show it)
        ASSERT_EQ(codes_of(w), "");
        pia.end->close();                                                                       // (the pages go to the game page)
        bob.end->close();
        Client& pia_game = w.connect_game("Pia", "fil00001", pia_key);
        Client& bob_game = w.connect_game("Bob", "fil00001", bob_key, true);                     // Bob's game cannot load the map: the START is cancelled
        w.run(500);
        ASSERT_TRUE(w.status("fil00001").state == RoomState::Waiting && !w.status("fil00001").starting && w.status("fil00001").lobby_public);
        ASSERT_EQ(codes_of(w), "fil00001");                                                      // (the room waits again with its two games in it: it is listed again)
        ASSERT_EQ(row_of(w, "fil00001").get("people").as_int_or(-1), 2);
        bob_game.fail_load = false;
        w.run(2500);
        ASSERT_TRUE(pia_game.lobby->request_start());
        for (int i = 0; i < 100 && w.status("fil00001").state != RoomState::Loading && w.status("fil00001").state != RoomState::Running; ++i) w.run(10);
        ASSERT_TRUE(w.status("fil00001").state == RoomState::Loading || w.status("fil00001").state == RoomState::Running);
        ASSERT_EQ(codes_of(w), "");                                                              // (a match that loads or runs is no game to join)
        w.run(8000);
        ASSERT_TRUE(w.status("fil00001").state == RoomState::Running);
        ASSERT_EQ(codes_of(w), "");
        // --- nobody connected: a lobby whose people all lost their links is held, not listed
        Client& ann = w.connect_page("Ann", "fil00002", lobby_block_of());
        Client& cat = w.connect_page("Cat", "fil00002", lobby_block_of());
        w.run(300);
        say_visibility(w, ann, true);
        const net::SeatKey ann_key = ann.lobby->key();
        ASSERT_EQ(codes_of(w), "fil00002");
        ann.end->close();                                                                       // the leader's link ends: its seat is held, Cat is still here: listed, under the held leader's name
        w.run(300);
        ASSERT_EQ(codes_of(w), "fil00002");
        ASSERT_TRUE(row_of(w, "fil00002").get("host").str() == "Ann" && row_of(w, "fil00002").get("people").as_int_or(-1) == 2);
        cat.end->close();                                                                       // nobody is connected: two held seats
        w.run(300);
        ASSERT_TRUE(w.status("fil00002").joined == 2 && w.status("fil00002").state == RoomState::Waiting);
        ASSERT_EQ(codes_of(w), "");
        w.next_key = ann_key;
        Client& ann_back = w.connect_page("Ann", "fil00002", lobby_block_of());                  // a page with the key takes the seat back
        w.next_key = net::SeatKey{};
        w.run(300);
        ASSERT_TRUE(ann_back.lobby->phase() == net::ClientLobby::Phase::InRoom && ann_back.lobby->room_is_public());
        ASSERT_EQ(codes_of(w), "fil00002");
        ASSERT_EQ(row_of(w, "fil00002").get("people").as_int_or(-1), 2);
        // --- no free color: four people
        Client& d1 = w.connect_page("D1", "fil00003", lobby_block_of());
        Client& d2 = w.connect_page("D2", "fil00003", lobby_block_of());
        Client& d3 = w.connect_page("D3", "fil00003", lobby_block_of());
        w.run(300);
        say_visibility(w, d1, true);
        ASSERT_EQ(codes_of(w), "fil00003,fil00002");                                            // (three people against two: the room of three comes first)
        w.connect_page("D4", "fil00003", lobby_block_of());
        w.run(300);
        ASSERT_EQ(codes_of(w), "fil00002");                                                      // (full: nobody can join)
        d3.lobby->leave();
        w.run(300);
        ASSERT_EQ(codes_of(w), "fil00003,fil00002");
        // --- no free color: the plan gave the rest to bots, or to nobody (the person in the fourth color keeps the color: it plays, whatever the plan keeps underneath)
        ASSERT_TRUE(d1.lobby->request_plan(plan_msg({K::Open, K::Open, K::Hard, K::Easy})));
        w.run(300);
        ASSERT_EQ(codes_of(w), "fil00002");
        ASSERT_TRUE(d1.lobby->request_plan(plan_msg({K::Open, K::Open, K::Nobody, K::Nobody})));
        w.run(1200);
        ASSERT_EQ(codes_of(w), "fil00002");
        ASSERT_TRUE(d1.lobby->request_plan(plan_msg({K::Open, K::Open, K::Open, K::Open})));
        w.run(1200);
        ASSERT_EQ(codes_of(w), "fil00003,fil00002");
        // ... a color that nobody holds and the plan calls Nobody is out of the match: two people, one open color, size 3
        d2.lobby->leave();
        w.run(300);
        ASSERT_TRUE(d1.lobby->request_plan(plan_msg({K::Open, K::Open, K::Nobody, K::Open})));
        w.run(1200);
        ASSERT_EQ(codes_of(w), "fil00002,fil00003");                                            // (two people each now: the room that has been public the longer comes first)
        const ctl::JsonValue three = row_of(w, "fil00003");
        ASSERT_TRUE(three.get("people").as_int_or(-1) == 2 && three.get("bots").as_int_or(-1) == 0 && three.get("free").as_int_or(-1) == 1 && three.get("size").as_int_or(-1) == 3);
        // --- private
        say_visibility(w, d1, false);
        ASSERT_EQ(codes_of(w), "fil00002");
    } TEST_END();

    TEST_CASE("S3.234 The Order Of The List (Protocol 22): More People First, Then The Room That Has Been Public The Longer, Then The Smaller Code; A Lone Host Is Listed After The Rooms That Have Company; Two Rooms Made Public In One Pass Are Told Apart By Their Codes") {
        World w(public_limits());
        // a room of 2 that is made public first, a room of 3 later, a lone host, another room of 2 later still
        Client& a1 = w.connect_page("A1", "ord00004", lobby_block_of());
        w.connect_page("A2", "ord00004", lobby_block_of());
        w.run(300);
        say_visibility(w, a1, true);
        w.run(5000);
        Client& b1 = w.connect_page("B1", "ord00002", lobby_block_of());
        w.connect_page("B2", "ord00002", lobby_block_of());
        w.connect_page("B3", "ord00002", lobby_block_of());
        w.run(300);
        say_visibility(w, b1, true);
        w.run(5000);
        Client& c1 = w.connect_page("C1", "ord00001", lobby_block_of());
        w.run(300);
        say_visibility(w, c1, true);
        w.run(5000);
        Client& d1 = w.connect_page("D1", "ord00003", lobby_block_of());
        w.connect_page("D2", "ord00003", lobby_block_of());
        w.run(300);
        say_visibility(w, d1, true);
        // 3 people first; of the two rooms of 2 the older listing (ord00004: its code is the largest, so the code does not decide), then the later one; the lone host last
        ASSERT_EQ(codes_of(w), "ord00002,ord00004,ord00003,ord00001");
        // a second person for the lone host: it has company, and is listed before the newer room of 2 and after the older (the same people, the older listing first)
        w.connect_page("C2", "ord00001", lobby_block_of());
        w.run(300);
        ASSERT_EQ(codes_of(w), "ord00002,ord00004,ord00001,ord00003");
        // two rooms made public in the very same pass (their links have no jitter): the same people, the same time, the smaller code first, whichever room spoke first
        Client& z2 = page_on_clean_link(w, "Z2", "ord00007");
        Client& z1 = page_on_clean_link(w, "Z1", "ord00006");
        w.run(300);
        z2.lobby->request_visibility(true);
        z1.lobby->request_visibility(true);
        w.run(300);
        ASSERT_EQ(codes_of(w), "ord00002,ord00004,ord00001,ord00003,ord00006,ord00007");
        ASSERT_EQ(row_of(w, "ord00006").get("age").as_int_or(-1), row_of(w, "ord00007").get("age").as_int_or(-2));
        // the order does not depend on how the rooms are kept: the same list again
        ASSERT_EQ(codes_of(w), "ord00002,ord00004,ord00001,ord00003,ord00006,ord00007");
    } TEST_END();

    TEST_CASE("S3.235 At Most 50 Rooms Are Listed (Protocol 22), The Best 50: The Order Is Made Before The List Is Cut, So A Room That Has Company Is In It Whatever Its Age; The 51st Is Not, And A Room That Goes Makes Room For It") {
        World w(public_limits());
        std::vector<Client*> leaders;
        for (int i = 1; i <= 55; ++i) {
            char code[16];
            std::snprintf(code, sizeof code, "cap%05d", i);
            Client& c = w.connect_page("P" + std::to_string(i), code, lobby_block_of());
            w.run(100);
            c.lobby->request_visibility(true);
            w.run(40);                                                                          // (each room is listed a little after the one before: the older listing comes first)
            leaders.push_back(&c);
        }
        ASSERT_EQ(w.mgr.room_count(), size_t{55});
        ctl::JsonValue list = list_of(w);
        ASSERT_EQ(list.get("count").as_int_or(-1), 50);
        ASSERT_EQ(list.get("lobbies").size(), size_t{50});
        std::string want;
        for (int i = 1; i <= 50; ++i) {
            char code[16];
            std::snprintf(code, sizeof code, "cap%05d", i);
            want += (want.empty() ? "" : ",") + std::string(code);
        }
        ASSERT_EQ(codes_of(list), want);                                                         // the 50 that have been public the longest
        // the newest room gets company: it is the best now, and the cut takes the oldest-but-one of the end of the list, never the room with company
        w.connect_page("Friend", "cap00055", lobby_block_of());
        w.run(300);
        list = list_of(w);
        ASSERT_EQ(list.get("count").as_int_or(-1), 50);
        ASSERT_TRUE(list.get("lobbies").at(0).get("code").str() == "cap00055" && list.get("lobbies").at(0).get("people").as_int_or(-1) == 2);
        ASSERT_TRUE(row_of(w, "cap00049").get("code").str() == "cap00049" && row_of(w, "cap00050").get("code").str() != "cap00050");
        // a room goes private: the room that was the 51st takes its place
        say_visibility(w, *leaders[2], false);
        list = list_of(w);
        ASSERT_EQ(list.get("count").as_int_or(-1), 50);
        ASSERT_TRUE(row_of(w, "cap00003").get("code").str().empty() && row_of(w, "cap00050").get("code").str() == "cap00050");
    } TEST_END();

    TEST_CASE("S3.236 The List Says Nothing That Is Not Meant For Everybody (Protocol 22; The Test Of S3.46's Kind): Over A Whole Answer Of Rooms With People Who Have Keys, Addresses, Platforms, Names And Chat, No Key (In Hex, In Either Case, Or As Bytes), No Address, No Name But The Leader's, No Line Of Chat, No Plan Letters; The Keys Of A Row Are Its Nine And The Answer Is The Same Whoever Asks And Whatever The Request Carries") {
        World w(public_limits());
        w.next_address = "203.0.113.77";
        w.next_platform = static_cast<uint8_t>(net::kOsLinux | net::kPlatformBrowser);
        Client& pia = w.connect_page("Pia", "sec00001", lobby_block_of());
        w.next_address = "198.51.100.23";
        Client& bob = w.connect_page("SecretBob", "sec00001", lobby_block_of());
        w.next_address = "2001:db8::17";
        w.next_platform = net::kOsWindows;
        Client& cat = w.connect_page("SecretCat", "sec00001", lobby_block_of());
        w.next_address = "127.0.0.1";
        w.next_platform = 0;
        w.run(300);
        say_visibility(w, pia, true);
        ASSERT_TRUE(bob.lobby->chat("secretchat from bob"));
        w.run(300);
        ASSERT_EQ(codes_of(w), "sec00001");
        const std::string body = door(w, "GET").body;
        std::vector<std::string> secrets = {"203.0.113.77", "198.51.100.23", "2001:db8", "127.0.0.1", "SecretBob", "SecretCat", "secretchat", "platform", "key", "address", "token", "linux", "Linux"};
        for (Client* c : {&pia, &bob, &cat}) {
            const net::SeatKey key = c->lobby->key();
            ASSERT_FALSE(net::key_is_zero(key));
            secrets.push_back(hex_of(key, false));
            secrets.push_back(hex_of(key, true));
            secrets.push_back(std::string(key.begin(), key.end()));                              // (the raw bytes too)
            secrets.push_back(hex_of(key, false).substr(0, 8));                                  // (and the first eight digits of it)
        }
        for (const std::string& secret : secrets) {
            if (body.find(secret) != std::string::npos) std::cout << "\n    the list holds \"" << secret.substr(0, 40) << "\"";
            ASSERT_TRUE(body.find(secret) == std::string::npos);
        }
        // the plan's letters and the words of the status are not in it either, and the answer is these nine numbers and names of one room, whoever asks
        ASSERT_TRUE(body.find("oooo") == std::string::npos && body.find("reconnect") == std::string::npos && body.find("leader") == std::string::npos);
        ASSERT_EQ(body, R"({"format":1,"lobbies":[{"code":"sec00001","host":"Pia","map":"TINY.LVL","mode":"highest-score","people":3,"bots":0,"free":1,"size":4,"age":0}],"count":1})");
        ctl::HttpRequest asked;
        asked.method = "GET";
        asked.path = "/lobbies";
        asked.body = "key=" + hex_of(pia.lobby->key(), false);
        ASSERT_EQ(handle_public_replays(w.mgr, asked, w.now).body, body);                      // (a body, like any header a visitor sends, is nothing to it)
        ASSERT_EQ(handle_public_replays(w.mgr, asked, w.now + 400).body.substr(0, 20), body.substr(0, 20));
    } TEST_END();

    TEST_CASE("S3.237 The Door Of The List (Protocol 22): GET Only, No Query, Nothing But The Exact Path; Every Other Method, Any Query, A Longer Or Another Path, Another Case Are 404 With A JSON Body (The Control Interface Does Not Serve It At All); It Is The Same On A Server That Keeps No Replays; A Leader's Name Or A Map's That Is Hostile Text (Quotes, Backslash, Markup, The Longest Name) Is Escaped, And Parses Back To The Name") {
        World w(public_limits());
        Client& pia = w.connect_page("Q\"uote\\ <b>&'x</b>", "esc00001", lobby_block_of());
        w.run(300);
        ASSERT_TRUE(pia.lobby->is_leader());
        say_visibility(w, pia, true);
        const ctl::HttpResponse ok = door(w, "GET");
        ASSERT_TRUE(ok.status == 200 && ok.content_type == "application/json");
        for (const char* method : {"POST", "PUT", "DELETE", "HEAD", "PATCH", "OPTIONS", "get", ""}) {
            const ctl::HttpResponse r = door(w, method);
            ASSERT_TRUE(r.status == 404);
            ctl::JsonValue j;
            ASSERT_TRUE(ctl::parse_json(r.body, j, nullptr) && j.get("error").str() == "not found");
        }
        for (const char* query : {"a=1", "limit=5", "x", "?", "format=1"}) ASSERT_TRUE(door(w, "GET", "/lobbies", query).status == 404);
        for (const char* path : {"/lobbies/", "/lobbies/esc00001", "/lobbies/..", "/Lobbies", "/LOBBIES", "/lobby", "/lobbiess", "/lobbies.json", "//lobbies", "/ lobbies", "/lobbies%20", "/lobbies\n", ""}) {
            ASSERT_TRUE(door(w, "GET", path).status == 404);
        }
        // the secret door does not serve it: the list is for the public one
        ctl::HttpRequest rq;
        rq.method = "GET";
        rq.path = "/lobbies";
        ASSERT_TRUE(handle_control(w.mgr, rq, w.now).status == 404);
        // the hostile name is JSON that parses back to the name; the body holds no raw control character and no unescaped quote
        const ctl::JsonValue list = list_of(w);
        ASSERT_EQ(list.get("lobbies").at(0).get("host").str(), std::string("Q\"uote\\ <b>&'x</b>"));
        for (const char c : ok.body) ASSERT_TRUE(static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F);
        ASSERT_TRUE(ok.body.find("\\\"uote\\\\ ") != std::string::npos);
        // the longest name of a person: 32 characters, exactly as the room shows it
        Client& longer = w.connect_page(std::string(32, 'W'), "esc00002", lobby_block_of());
        w.run(300);
        say_visibility(w, longer, true);
        ASSERT_EQ(row_of(w, "esc00002").get("host").str(), std::string(32, 'W'));
        // the answer is far below the door's own limit, even for the full 50 rooms of the longest names and maps
        ASSERT_TRUE(door(w, "GET").body.size() < 600);
    } TEST_END();

    TEST_CASE("S3.238 The List Does Not Need The Replays (Protocol 22): A Server That Keeps No Replay Store Answers It As Any Other; A Server That Offers No Lobby Rooms Answers An Empty List") {
        {
            ServerLimits l = public_limits();
            World w(l);
            ASSERT_TRUE(w.mgr.replay_store() == nullptr);
            Client& pia = w.connect_page("Pia", "norp0001", lobby_block_of());
            w.run(300);
            say_visibility(w, pia, true);
            ASSERT_EQ(codes_of(w), "norp0001");
            ASSERT_TRUE(door(w, "GET", "/live").status == 404);                                  // (the live board needs the replays; the list of games does not)
        }
        {
            ServerLimits l = public_limits();
            l.demo_lobbies = 0;
            World w(l);
            Client& pia = w.connect_page("Pia", "norp0002", lobby_block_of());
            w.run(300);
            ASSERT_TRUE(pia.lobby->phase() == net::ClientLobby::Phase::Rejected);
            ASSERT_EQ(door(w, "GET").body, R"({"format":1,"lobbies":[],"count":0})");
        }
    } TEST_END();

    TEST_CASE("S3.239 The Idle Rule On A Real Server (Protocol 22): A Public Room In Which Nothing Is Done For 30 Minutes Is Private And Out Of The List; A Line Of Chat, A Plan, A Newcomer And A Page That Says Public Again Keep It; Pings Do Not; The Leader Makes It Public Again; All Of It Across The Wrap Of The 32-Bit Clock; The Status Says Public Or Not") {
        for (const uint32_t start : {1000u, 0x7FFFF000u - 100000u, 0xFFFFFFFFu - 100000u}) {
            World w(public_limits());
            w.now = start;
            Client& pia = w.connect_page("Pia", "idl00001", lobby_block_of());
            Client& bob = w.connect_page("Bob", "idl00001", lobby_block_of());
            w.run(300);
            say_visibility(w, pia, true);
            ASSERT_EQ(codes_of(w), "idl00001");
            ASSERT_TRUE(w.status("idl00001").lobby_public);
            // 25 minutes of nothing, then a line of chat: the 30 minutes start again
            w.run_coarse(25u * 60u * 1000u, 5000);
            ASSERT_EQ(codes_of(w), "idl00001");
            ASSERT_TRUE(bob.lobby->chat("still here"));
            w.run_coarse(29u * 60u * 1000u, 5000);
            ASSERT_EQ(codes_of(w), "idl00001");                                                  // (54 minutes after the room became public)
            ASSERT_EQ(row_of(w, "idl00001").get("age").as_int_or(-1), 54 * 60);                  // (and its age says so: the line of chat made it no younger)
            w.run_coarse(2u * 60u * 1000u, 5000);
            ASSERT_EQ(codes_of(w), "");
            ASSERT_TRUE(!w.status("idl00001").lobby_public && !pia.lobby->room_is_public() && !bob.lobby->room_is_public());
            ASSERT_TRUE(w.status("idl00001").state == RoomState::Waiting && w.status("idl00001").joined == 2);        // (the room goes on, with its people: only the listing ends)
            // the leader makes it public again: a new listing, and a page that says "public" every ten minutes keeps it for hours
            say_visibility(w, pia, true);
            ASSERT_EQ(codes_of(w), "idl00001");
            ASSERT_EQ(row_of(w, "idl00001").get("age").as_int_or(-1), 0);
            for (int i = 0; i < 18; ++i) {                                                       // three hours
                w.run_coarse(10u * 60u * 1000u, 5000);
                pia.lobby->request_visibility(true);
                w.run_coarse(5000, 5000);
            }
            ASSERT_EQ(codes_of(w), "idl00001");
            ASSERT_TRUE(row_of(w, "idl00001").get("age").as_int_or(0) >= 3 * 3600);
            // a page that is looked at no more: 30 minutes after the last "public", private
            w.run_coarse(29u * 60u * 1000u, 5000);
            ASSERT_EQ(codes_of(w), "idl00001");
            w.run_coarse(2u * 60u * 1000u, 5000);
            ASSERT_EQ(codes_of(w), "");
        }
    } TEST_END();

    TEST_CASE("S3.240 What The Status Says Of A Public Room (Protocol 22): \"public\" Is In The Status Of A Lobby Room (The Control Interface, The List Of Rooms, The Result File) And Only There; It Follows The Leader's Choice And The Idle Rule") {
        World w(public_limits());
        Client& pia = w.connect_page("Pia", "sta00001", lobby_block_of());
        w.connect_creating("Gus", "gus00002", block_of("TINY.LVL", 4));
        w.run(300);
        const auto status_json = [&](const std::string& code) {
            ctl::HttpRequest rq;
            rq.method = "GET";
            rq.path = "/rooms/" + code;
            ctl::JsonValue j;
            ctl::parse_json(handle_control(w.mgr, rq, w.now).body, j, nullptr);
            return j;
        };
        ctl::JsonValue j = status_json("sta00001");
        ASSERT_TRUE(j.has("lobby") && j.has("public") && j.get("public").is_bool() && !j.get("public").as_bool_or(true));
        say_visibility(w, pia, true);
        j = status_json("sta00001");
        ASSERT_TRUE(j.get("public").as_bool_or(false));
        ASSERT_TRUE(j.keys().size() > 4 && j.get("lobby").as_bool_or(false) && j.get("starting").is_bool() && j.get("plan").str() == "oooo -");
        ASSERT_FALSE(status_json("gus00002").has("public"));                                    // (a room that is no lobby has none: its status is what it was)
        ctl::HttpRequest rq;
        rq.method = "GET";
        rq.path = "/rooms";
        ctl::JsonValue all;
        ASSERT_TRUE(ctl::parse_json(handle_control(w.mgr, rq, w.now).body, all, nullptr));
        int with = 0, true_ones = 0;
        for (size_t i = 0; i < all.get("rooms").size(); ++i) {
            const ctl::JsonValue& r = all.get("rooms").at(i);
            with += r.has("public") ? 1 : 0;
            true_ones += r.get("public").as_bool_or(false) ? 1 : 0;
        }
        ASSERT_TRUE(with == 1 && true_ones == 1);
        say_visibility(w, pia, false);
        ASSERT_FALSE(status_json("sta00001").get("public").as_bool_or(true));
    } TEST_END();
}
