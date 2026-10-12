// Tests of the dedicated game server: the store, the room manager, the demo rooms, the hardening, the maps, the matches, the leader's calls, the control calls, the sockets and the secret
#include "server_fixtures.hpp"


void run_store_tests() {
    TEST_CASE("S3.0 The Scratch Folders Of A Run Are Its Own: Named With The Process And A Random Part, Never The Name That Every Run Of The Suite Shared, And Removed With Their Contents") {
        const std::string pid = std::to_string(process_id());
        fs::path a_path;
        fs::path b_path;
        {
            ScratchRoot a;
            ScratchRoot b;                                                          // two roots in one process (and so two processes) never get the same folder
            a_path = a.path();
            b_path = b.path();
            ASSERT_TRUE(a_path != b_path);
            ASSERT_TRUE(fs::is_directory(a_path) && fs::is_directory(b_path));
            ASSERT_TRUE(fs::equivalent(a_path.parent_path(), fs::temp_directory_path()) && fs::equivalent(b_path.parent_path(), fs::temp_directory_path()));   // (the temp path may end in a slash)
            ASSERT_TRUE(a_path.filename().string().rfind("ants_server_test_" + pid + "_", 0) == 0);        // the process's own: its id in the name
            ASSERT_TRUE(b_path.filename().string().rfind("ants_server_test_" + pid + "_", 0) == 0);
            ASSERT_TRUE(a_path.filename().string().size() > std::string("ants_server_test_" + pid + "_").size());   // and a random part after it
            fs::create_directories(a_path / "deep" / "er");
            write_bytes(a_path / "deep" / "er" / "file", 10, 'x');
            ASSERT_TRUE(fs::exists(a_path / "deep" / "er" / "file"));
        }
        ASSERT_FALSE(fs::exists(a_path) || fs::exists(b_path));                     // removed with everything in them
        // the folders that the suite asks for are made fresh under the root of this run, and never at the old shared names
        const std::string dir = temp_dir_for("s30");
        ASSERT_TRUE(fs::path(dir).parent_path() == scratch_root());
        ASSERT_TRUE(fs::path(dir) != fs::temp_directory_path() / "ants_server_test_s30");
        ASSERT_TRUE(fs::path(dir).string().find("ants_server_test_" + pid + "_") != std::string::npos);
        write_bytes(fs::path(dir) / "left_behind", 10, 'x');
        const std::string again = temp_dir_for("s30");                              // a second request for the same name starts empty
        ASSERT_TRUE(again == dir && !fs::exists(fs::path(again) / "left_behind"));
        ASSERT_TRUE(scratch_root().filename().string().rfind("ants_server_test_" + pid + "_", 0) == 0);
    } TEST_END();

    TEST_CASE("S3.1 Map Store: A Map Of The Folder Is Found With Its Hash; Names That Are No Map Of The Folder Are Refused") {
        MapStore store(maps_dir());
        MapEntry e;
        std::string why;
        ASSERT_TRUE(store.find("TINY.LVL", e, &why));
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "/TINY.LVL", hash));
        ASSERT_TRUE(e.hash == hash && e.size > 1000 && e.name == "TINY.LVL");
        for (const char* bad : {"../Original-Ants/Maps/TINY.LVL", "..", "a/b.lvl", "nosuch.lvl", "TINY.LVL ", "TINY", "", "TINY.txt", "x:y.lvl"}) {
            ASSERT_FALSE(store.find(bad, e, &why));
            ASSERT_FALSE(why.empty());
        }
        // odd names of a folder: spaces, '!', '..' inside, a 64 character name; empty and huge files are no maps
        const std::string dir = temp_dir_for("store");
        fs::copy_file(maps_dir() + "/TINY.LVL", fs::path(dir) / "!!! My Map ~v2~ ....lvl");
        write_bytes(fs::path(dir) / "empty.lvl", 0, 'x');
        write_bytes(fs::path(dir) / "huge.lvl", MapStore::kMaxMapBytes + 1, 'x');
        fs::create_directories(fs::path(dir) / "folder.lvl");
        MapStore odd(dir);
        ASSERT_TRUE(odd.find("!!! My Map ~v2~ ....lvl", e, &why) && e.hash == hash);
        ASSERT_FALSE(odd.find("empty.lvl", e, &why));
        ASSERT_FALSE(odd.find("huge.lvl", e, &why));
        ASSERT_FALSE(odd.find("folder.lvl", e, &why));
        fs::remove_all(dir);
    } TEST_END();
}

void run_manager_tests() {
    TEST_CASE("S3.2 Making Rooms: A Good Spec Makes A Waiting Room; Bad Specs Are Refused With The Right Status; Codes Are Unique And Generated When Missing; The Number Of Rooms Is Limited") {
        ServerLimits limits;
        limits.max_rooms = 4;
        RoomManager mgr{MapStore(maps_dir()), limits};
        CreateResult r = mgr.create_room(spec_of("ROOM-1"), 0);
        ASSERT_TRUE(r.ok && r.http_status == 201 && r.code == "ROOM-1");
        RoomStatus s;
        ASSERT_TRUE(mgr.status("ROOM-1", s, 0));
        ASSERT_TRUE(s.state == RoomState::Waiting && s.expected == 2 && s.joined == 0 && s.map == "TINY.LVL" && !s.fog);
        ASSERT_EQ(mgr.create_room(spec_of("ROOM-1"), 0).http_status, 409);              // the code exists
        ASSERT_EQ(mgr.create_room(spec_of("bad code"), 0).http_status, 400);
        ASSERT_EQ(mgr.create_room(spec_of("ROOM-2", 1), 0).http_status, 400);           // players 1
        ASSERT_EQ(mgr.create_room(spec_of("ROOM-2", 5), 0).http_status, 400);
        ASSERT_EQ(mgr.create_room(spec_of("ROOM-2", 2, "nosuch.lvl"), 0).http_status, 404);
        ASSERT_EQ(mgr.create_room(spec_of("ROOM-2", 2, "../x.lvl"), 0).http_status, 404);
        RoomSpec short_wait = spec_of("ROOM-2");
        short_wait.wait_ms = 10;
        ASSERT_EQ(mgr.create_room(short_wait, 0).http_status, 400);
        // a map the engine cannot read
        const std::string dir = temp_dir_for("manager");
        write_bytes(fs::path(dir) / "junk.lvl", 5000, '\xAB');
        RoomManager junk{MapStore(dir)};
        ASSERT_EQ(junk.create_room(spec_of("J-1", 2, "junk.lvl"), 0).http_status, 422);
        fs::remove_all(dir);
        // generated codes: six characters, valid, different
        RoomSpec anon = spec_of("");
        const CreateResult a = mgr.create_room(anon, 0);
        const CreateResult b = mgr.create_room(anon, 0);
        ASSERT_TRUE(a.ok && b.ok && a.code.size() == kDrawnRoomCodeChars && b.code.size() == kDrawnRoomCodeChars && a.code != b.code && net::valid_room_code(a.code));
        ASSERT_TRUE(mgr.create_room(spec_of("ROOM-3"), 0).ok);                          // the fourth room
        ASSERT_EQ(mgr.create_room(spec_of("ROOM-4"), 0).http_status, 503);              // no room for a fifth
        ASSERT_EQ(mgr.room_count(), size_t{4});
        ASSERT_TRUE(mgr.close_room("ROOM-3", 0));
        ASSERT_FALSE(mgr.close_room("NOPE", 0));
    } TEST_END();

    TEST_CASE("S3.3 The Door: A Hello Finds Its Room By Its Code; Wrong Codes, Old Versions, Garbage, Silence, A Full Room And A Running Match Are Refused") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("AAA-1", 2), w.now).ok);
        auto reject_of = [&](Client& c) {
            w.run(300);
            return c.lobby->phase() == net::ClientLobby::Phase::Rejected ? c.lobby->reject_reason() : static_cast<net::RejectReason>(0);
        };
        Client& wrong = w.connect("Wrong", "BBB-9");
        ASSERT_EQ(reject_of(wrong), net::RejectReason::NoSuchRoom);
        Client& none = w.connect("None", "");
        ASSERT_EQ(reject_of(none), net::RejectReason::NoSuchRoom);                       // a Hello without a room code goes nowhere on a server
        Client& ann = w.connect("Ann", "AAA-1");
        w.run(300);
        ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);
        ASSERT_EQ(ann.lobby->my_seat(), 0);                                              // seat 0 is a guest's: the server has none
        // an old client: its layout is not read, the answer is "version mismatch"; so is the answer to a game of protocol 8 (v0.0.94), whose Hello has exactly this protocol's layout:
        // the engine's rules changed in 9 (the community-map rules), and the number is all that the door has to tell the two games apart
        // (the door decides the version before it looks for the room, so an old game that asks for a room that does not exist is refused for its version, not told "no such room")
        ASSERT_TRUE(net::kProtocolVersion != 8);
        // (and so is a game of protocol 11, v0.1.0 or v0.1.1: its Hello is a Hello of 12 byte for byte, the number alone refuses it; it would count its dialog in simulation ticks and be blocked for
        // 100 ticks of the match after the server's late first turn)
        for (const uint16_t old_version : {uint16_t{4}, uint16_t{8}, uint16_t{11}}) {
            for (const char* room : {"AAA-1", "NOPE-9"}) {
                auto ends = w.net.connect({10, 0});
                w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "x", w.now);
                net::HelloMsg old;
                old.version = old_version;
                old.name = "Old";
                old.room = room;
                ends.second->send(net::encode(old));
                w.run(200);
                std::vector<uint8_t> reply;
                net::RejectMsg rj;
                ASSERT_TRUE(ends.second->poll(reply) && net::decode(reply, rj) && rj.reason == net::RejectReason::VersionMismatch);
            }
        }
        // the Hello of protocols 6 to 9 has the layout before the keys (no key, no turns after the token): it is told "version mismatch" without its layout being read, and nobody is seated
        for (const uint16_t version : {uint16_t{6}, uint16_t{7}, uint16_t{8}, uint16_t{9}}) {
            std::vector<uint8_t> raw = {static_cast<uint8_t>(net::MsgType::Hello), static_cast<uint8_t>(version), 0, 3, 'O', 'l', 'd', 0x34, 0x12, 255, 5, 'A', 'A', 'A', '-', '1', 0};
            auto ends = w.net.connect({10, 0});
            w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "x", w.now);
            ends.second->send(raw);
            w.run(200);
            std::vector<uint8_t> reply;
            net::RejectMsg rj;
            ASSERT_TRUE(ends.second->poll(reply) && net::decode(reply, rj) && rj.reason == net::RejectReason::VersionMismatch);
            ASSERT_EQ(w.status("AAA-1").joined, 1u);                                          // (only Ann)
        }
        // garbage and a message that is no Hello
        {
            auto a = w.net.connect({10, 0});
            auto b = w.net.connect({10, 0});
            w.mgr.add_connection(std::make_unique<Borrowed>(a.first), "x", w.now);
            w.mgr.add_connection(std::make_unique<Borrowed>(b.first), "x", w.now);
            a.second->send({99, 1, 2, 3});
            b.second->send(net::encode_leave());
            w.run(200);
            std::vector<uint8_t> reply;
            net::RejectMsg rj;
            ASSERT_TRUE(a.second->poll(reply) && net::decode(reply, rj) && rj.reason == net::RejectReason::BadRequest);
            ASSERT_TRUE(b.second->poll(reply) && net::decode(reply, rj) && rj.reason == net::RejectReason::BadRequest);
        }
        // a connection that never says Hello is closed after the timeout and costs nothing afterwards
        {
            auto s = w.net.connect({10, 0});
            w.mgr.add_connection(std::make_unique<Borrowed>(s.first), "x", w.now);
            ASSERT_EQ(w.mgr.pending_count(), size_t{1});
            w.run(10500);
            ASSERT_EQ(w.mgr.pending_count(), size_t{0});
            ASSERT_FALSE(s.second->is_open());
        }
        // the second player starts the match; a third Hello (the room takes two) and a later one are refused
        Client& bob = w.connect("Bob", "AAA-1");
        Client& cat = w.connect("Cat", "AAA-1");
        w.run(500);
        ASSERT_EQ(bob.lobby->my_seat(), 1);
        ASSERT_TRUE(cat.lobby->phase() == net::ClientLobby::Phase::Rejected);
        ASSERT_TRUE(cat.lobby->reject_reason() == net::RejectReason::Full || cat.lobby->reject_reason() == net::RejectReason::MatchRunning);
        Client& late = w.connect("Late", "AAA-1");
        w.run(1500);
        ASSERT_TRUE(late.lobby->phase() == net::ClientLobby::Phase::Rejected);
        ASSERT_TRUE(w.status("AAA-1").state == RoomState::Running);
        ASSERT_EQ(late.lobby->reject_reason(), net::RejectReason::MatchRunning);
    } TEST_END();
}

// (placed before the match tests: the door's tests)
void run_demo_tests() {
    TEST_CASE("S3.10 Public Rooms: Off Unless Asked For; A Hello With A Create Block Makes The Room For A Code That Does Not Exist, Only With The Block, Only Up To The Limit, And An Unfilled One Fails After Its Wait (One Minute Here, Ten By Default)") {
        auto reject_of = [](World& w, Client& c) {
            w.run(300);
            return c.lobby->phase() == net::ClientLobby::Phase::Rejected ? c.lobby->reject_reason() : static_cast<net::RejectReason>(0);
        };
        {
            World w;                                                                      // the default: no public rooms
            Client& a = w.connect_creating("Ann", "k7m2xq", block_of());
            ASSERT_EQ(reject_of(w, a), net::RejectReason::NoSuchRoom);
            ASSERT_EQ(w.mgr.room_count(), size_t{0});
        }
        ServerLimits limits;
        limits.demo_rooms = 2;
        limits.demo_map = "TINY.LVL";
        limits.demo_wait_ms = 60000;                                                     // (the default is ten minutes: S3.25)
        World w(limits);
        Client& other = w.connect("Other", "other-1");                                    // no block: no room is made, whatever the code
        ASSERT_EQ(reject_of(w, other), net::RejectReason::NoSuchRoom);
        Client& bare = w.connect_creating("Bare", "", block_of());                        // a block and no code: there is no room to make
        ASSERT_EQ(reject_of(w, bare), net::RejectReason::NoSuchRoom);
        Client& dotted = w.connect_creating("Dotted", "no.good", block_of());             // '.' is no character of a room code: the Hello is refused, no room
        w.run(300);
        ASSERT_TRUE(dotted.lobby->phase() != net::ClientLobby::Phase::InRoom);
        ASSERT_EQ(w.mgr.room_count(), size_t{0});
        {   // a capital in the code: the codes of the control interface are the operator's (the server draws capitals, and an operator who chooses one gives it a capital), so a visitor's block cannot take one first
            World wc(limits);
            Client& loud = wc.connect_creating("Loud", "ROOM-1", block_of());
            ASSERT_EQ(reject_of(wc, loud), net::RejectReason::NoSuchRoom);
            Client& mixed = wc.connect_creating("Mixed", "k7m2Xq", block_of());
            ASSERT_EQ(reject_of(wc, mixed), net::RejectReason::NoSuchRoom);
            ASSERT_EQ(wc.mgr.room_count(), size_t{0});
            ASSERT_TRUE(wc.mgr.create_room(spec_of("ROOM-1", 2), wc.now).ok);              // so the operator's POST /rooms is not met by a 409, and its room is not a public one
            ASSERT_FALSE(wc.status("ROOM-1").public_room);
            Client& guest = wc.connect("Guest", "ROOM-1");
            wc.run(300);
            ASSERT_EQ(guest.lobby->phase(), net::ClientLobby::Phase::InRoom);
            ASSERT_EQ(wc.mgr.room_count(), size_t{1});
        }
        {   // a Hello that shows a key makes no room, whatever block it carries: a key is of a seat in a room that was, and that room is gone (RJ1.6 and RJ1.21 have it through a real client)
            auto ends = w.net.connect({20, 10});
            w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", w.now);
            net::HelloMsg hello;
            hello.name = "Keyed";
            hello.room = "eeee5555";
            for (uint8_t& v : hello.key) v = 7;
            hello.create = block_of();
            ends.second->send(net::encode(hello));
            w.run(300);
            ASSERT_EQ(reject_on(ends.second), static_cast<int>(net::RejectReason::NoSuchRoom));
            ASSERT_EQ(w.mgr.room_count(), size_t{0});
        }
        Client& ann = w.connect_creating("Ann", "aaaa1111", block_of());
        w.run(300);
        ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);
        ASSERT_EQ(w.mgr.room_count(), size_t{1});
        ASSERT_TRUE(w.status("aaaa1111").state == RoomState::Waiting);
        ASSERT_EQ(w.status("aaaa1111").map, std::string("TINY.LVL"));
        ASSERT_EQ(w.status("aaaa1111").expected, 2);
        Client& bob = w.connect("Bob", "aaaa1111");                                       // the second Hello (no block needed) finds the room that the first one made
        w.run(800);
        ASSERT_EQ(bob.lobby->my_seat(), 1);
        ASSERT_EQ(w.mgr.room_count(), size_t{1});
        ASSERT_TRUE(w.status("aaaa1111").state == RoomState::Loading || w.status("aaaa1111").state == RoomState::Running);
        Client& cat = w.connect_creating("Cat", "bbbb2222", block_of());                  // a second public room
        w.run(300);
        ASSERT_EQ(cat.lobby->phase(), net::ClientLobby::Phase::InRoom);
        ASSERT_EQ(w.mgr.room_count(), size_t{2});
        Client& dan = w.connect_creating("Dan", "cccc3333", block_of());                  // the limit is two
        ASSERT_EQ(reject_of(w, dan), net::RejectReason::NoSuchRoom);
        ASSERT_EQ(w.mgr.room_count(), size_t{2});
        w.run(62000);                                                                     // the second never filled: it fails after its minute (the match of the first goes on)
        ASSERT_TRUE(w.status("bbbb2222").state == RoomState::Failed);
        ASSERT_EQ(ServerLimits().demo_rooms, size_t{0});
        w.run(31000);                                                                     // a failed public room is forgotten after 30 s, and its place is free again
        ASSERT_EQ(w.mgr.room_count(), size_t{1});
        Client& eve = w.connect_creating("Eve", "dddd4444", block_of());
        w.run(300);
        ASSERT_EQ(eve.lobby->phase(), net::ClientLobby::Phase::InRoom);
        ASSERT_EQ(w.mgr.room_count(), size_t{2});
    } TEST_END();

    TEST_CASE("S3.10c The Codes That The Control Interface Draws Always Have A Capital (The First Character Is A Letter), Six Characters Of The Alphabet Without The Look-Alikes, So That A Visitor's Block, Which Takes Only A Code With No Capital, Never Takes The Name Of A Room That Is Yet To Be Made; net::public_room_code Is That Rule") {
        std::mt19937 rng(20261008);
        const std::string alphabet = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
        for (int i = 0; i < 20000; ++i) {
            const std::string code = draw_room_code(rng);
            ASSERT_EQ(code.size(), kDrawnRoomCodeChars);
            ASSERT_EQ(kDrawnRoomCodeChars, size_t{6});                                    // (as many as the codes that the game makes: the front page and the menu)
            ASSERT_TRUE(net::valid_room_code(code));
            ASSERT_TRUE(code[0] >= 'A' && code[0] <= 'Z');                                // the first is a letter: a capital in every code, not in all but a few
            for (const char c : code) ASSERT_TRUE(alphabet.find(c) != std::string::npos);
            ASSERT_FALSE(net::public_room_code(code));                                    // so no visitor's block makes a room of it
        }
        {   // the draws are not all the same (a generator that is not asked)
            std::set<std::string> seen;
            for (int i = 0; i < 200; ++i) seen.insert(draw_room_code(rng));
            ASSERT_TRUE(seen.size() > 190);
        }
        // the rule: a valid code of one or more characters with no upper-case letter
        for (const char* open : {"k7m2xq", "a", "room_7", "a-b", "0123456789", "demo-treasure-4p-t01-k7m2xq"}) ASSERT_TRUE(net::public_room_code(open));
        for (const char* closed : {"", "ROOM-1", "k7m2Xq", "Aa", "a b", "a.b", "caf\xC3\xA9", "r\n"}) ASSERT_FALSE(net::public_room_code(closed));
        ASSERT_TRUE(net::public_room_code(std::string(net::kMaxRoomCodeChars, 'r')));
        ASSERT_FALSE(net::public_room_code(std::string(net::kMaxRoomCodeChars + 1, 'r')));
    } TEST_END();

    TEST_CASE("S3.23 A Create Block Can Choose Its Map: A File Name That The Server Lists Makes The Room On That Map, In Any Case; Every Other Name, And None, Keeps The Default Map; Without A List Nothing Changes") {
        ServerLimits limits;
        limits.demo_rooms = 13;
        limits.demo_map = "TINY.LVL";
        limits.demo_maps = {"TINY.LVL", "SMALL.LVL", "GAUNTLET.LVL"};
        World w(limits);
        int made = 0;
        auto map_of = [&](World& in, const std::string& map_name) {
            const std::string code = "room" + std::to_string(++made);
            in.connect_creating("P", code, block_of(map_name));
            in.run(300);
            return in.status(code).map;
        };
        ASSERT_EQ(map_of(w, "SMALL.LVL"), std::string("SMALL.LVL"));                      // the choice
        ASSERT_EQ(map_of(w, "small.lvl"), std::string("SMALL.LVL"));                      // any case: the name as the list spells it
        ASSERT_EQ(map_of(w, "Small.LVL"), std::string("SMALL.LVL"));
        ASSERT_EQ(map_of(w, "TINY.LVL"), std::string("TINY.LVL"));
        ASSERT_EQ(map_of(w, "GaUnTlEt.LVL"), std::string("GAUNTLET.LVL"));
        ASSERT_EQ(map_of(w, ""), std::string("TINY.LVL"));                                // no name: the default
        ASSERT_EQ(map_of(w, "MEDIUM.LVL"), std::string("TINY.LVL"));                      // a real map that is not in the list: the default
        ASSERT_EQ(map_of(w, "SMALLISH.LVL"), std::string("TINY.LVL"));                    // a longer name is not the map
        ASSERT_EQ(map_of(w, "SMAL.LVL"), std::string("TINY.LVL"));                        // nor a shorter one
        ASSERT_EQ(map_of(w, "NO-SUCH-MAP.LVL"), std::string("TINY.LVL"));
        ASSERT_EQ(map_of(w, "small"), std::string());                                     // no ".LVL": no valid block, the client leaves it out and the Hello joins: no room
        ASSERT_EQ(w.mgr.room_count(), size_t{10});
        {   // the default is the server's --demo-map, not the first map of the list: with SMALL.LVL as the default (the list's second), no name and a name that the list lacks get SMALL.LVL
            ServerLimits other = limits;
            other.demo_map = "SMALL.LVL";
            World d(other);
            ASSERT_EQ(map_of(d, ""), std::string("SMALL.LVL"));
            ASSERT_EQ(map_of(d, "MEDIUM.LVL"), std::string("SMALL.LVL"));
            ASSERT_EQ(map_of(d, "TINY.LVL"), std::string("TINY.LVL"));                    // (a name that the list has is still the block's)
        }
        // a second Hello for the same code finds the room that the first one made, on the same map, and its own block is ignored
        Client& second = w.connect_creating("Q", "room1", block_of("GAUNTLET.LVL", 4));
        w.run(800);
        ASSERT_EQ(second.lobby->my_seat(), 1);
        ASSERT_EQ(w.mgr.room_count(), size_t{10});
        ASSERT_EQ(w.status("room1").map, std::string("SMALL.LVL"));
        ASSERT_EQ(static_cast<int>(w.status("room1").expected), 2);
        // no list: the name is ignored, every public room is on the default map (what the server did before the list existed)
        ServerLimits plain;
        plain.demo_rooms = 4;
        plain.demo_map = "TINY.LVL";
        ASSERT_TRUE(plain.demo_maps.empty());
        World p(plain);
        ASSERT_EQ(map_of(p, "SMALL.LVL"), std::string("TINY.LVL"));
        // a listed map that does not load makes the Hello fail like any room with a bad map: the room is not made
        ServerLimits broken = limits;
        broken.demo_maps = {"NO-SUCH-MAP.LVL"};
        World b(broken);
        Client& lost = b.connect_creating("P", "lost1111", block_of("NO-SUCH-MAP.LVL"));
        b.run(300);
        ASSERT_TRUE(lost.lobby->phase() != net::ClientLobby::Phase::InRoom);
        ASSERT_EQ(b.mgr.room_count(), size_t{0});
        // a name is the whole file name: of BIG.LVL and BIG-ISLAND.LVL (copies of TINY.LVL) each is its own, whatever the other is called
        const std::string big_dir = temp_dir_for("demo_longest");
        fs::copy_file(maps_dir() + "/TINY.LVL", big_dir + "/BIG.LVL");
        fs::copy_file(maps_dir() + "/TINY.LVL", big_dir + "/BIG-ISLAND.LVL");
        ServerLimits two;
        two.demo_rooms = 4;
        two.demo_map = "BIG.LVL";
        two.demo_maps = {"BIG.LVL", "BIG-ISLAND.LVL"};
        World g(two, big_dir);
        g.connect_creating("P", "gggg0001", block_of("big-island.lvl"));
        g.connect_creating("Q", "gggg0002", block_of("BIG.LVL"));
        g.connect_creating("R", "gggg0003", block_of("BIG-ISLAND.LVL"));
        g.run(300);
        ASSERT_EQ(g.status("gggg0001").map, std::string("BIG-ISLAND.LVL"));
        ASSERT_EQ(g.status("gggg0002").map, std::string("BIG.LVL"));
        ASSERT_EQ(g.status("gggg0003").map, std::string("BIG-ISLAND.LVL"));
        std::error_code ignore;
        fs::remove_all(big_dir, ignore);
    } TEST_END();

    TEST_CASE("S3.24 A Create Block Chooses Its Number Of Seats: A Room For 2, 3 Or 4; A Room For Two Starts With Two And A Third Cannot Get In") {
        ServerLimits limits;
        limits.demo_rooms = 20;
        limits.demo_map = "TINY.LVL";
        limits.demo_maps = {"TINY.LVL", "SMALL.LVL", "GAUNTLET.LVL"};
        World w(limits);
        auto made = [&](const std::string& code, const net::CreateBlock& block) {
            w.connect_creating("P", code, block);
            w.run(300);
            return w.status(code);
        };
        struct Case { const char* code; const char* map; uint8_t seats; };
        const Case cases[] = {
            {"seats002", "SMALL.LVL", 2}, {"seats003", "SMALL.LVL", 3}, {"seats004", "SMALL.LVL", 4},
            {"seats005", "", 2},                                                    // seats without a map: the default map
            {"seats006", "GAUNTLET.LVL", 3},
        };
        for (const Case& c : cases) {
            const RoomStatus st = made(c.code, block_of(c.map, c.seats));
            ASSERT_MSG(st.map == (std::string(c.map).empty() ? "TINY.LVL" : c.map), c.code);
            ASSERT_MSG(st.expected == c.seats, c.code);
        }
        // a block with a seat count that no room has is no block: the client leaves it out, and no room is made
        ASSERT_TRUE(made("seats007", block_of("", 5)).map.empty());
        ASSERT_TRUE(made("seats008", block_of("", 1)).map.empty());
        ASSERT_TRUE(made("seats009", block_of("", 0)).map.empty());
        ASSERT_EQ(w.mgr.room_count(), size_t{5});
        // a room for two starts as soon as two have joined, and a third player cannot get in
        World s2(limits);
        Client& ann = s2.connect_creating("Ann", "duel0001", block_of("SMALL.LVL", 2));
        Client& bob = s2.connect("Bob", "duel0001");
        s2.run(800);
        ASSERT_EQ(ann.lobby->my_seat() != bob.lobby->my_seat(), true);
        ASSERT_TRUE(s2.status("duel0001").state == RoomState::Loading || s2.status("duel0001").state == RoomState::Running);
        ASSERT_EQ(static_cast<int>(s2.status("duel0001").joined), 2);
        Client& cat = s2.connect("Cat", "duel0001");
        s2.run(800);
        ASSERT_TRUE(cat.lobby->phase() == net::ClientLobby::Phase::Rejected || cat.lobby->phase() == net::ClientLobby::Phase::Closed);
        ASSERT_EQ(static_cast<int>(s2.status("duel0001").joined), 2);
    } TEST_END();

    TEST_CASE("S3.24b A Create Block That Says The Leader Starts (Protocol 15): A Room That Is Full Does Not Start By Itself, It Waits For Its Leader's START, And Every Client Is Told So In The Room Message; A Guest's START Does Nothing, The Leader's Starts It With The Block's Teams; The Same Room Without The Flag Starts By Itself; A Full Room Whose Leader Never Starts Ends After Its Wait And Says Why") {
        ServerLimits limits;
        limits.demo_rooms = 20;
        limits.demo_map = "TINY.LVL";
        limits.demo_wait_ms = 60000;                                                     // (ten minutes by default: S3.25)
        World w(limits);
        const auto started = [&](const std::string& code) {
            const RoomState state = w.status(code).state;
            return state == RoomState::Loading || state == RoomState::Running;
        };
        // the same room of three seats, once without the flag and once with it: the flag is what holds a full room
        Client& open_ann = w.connect_creating("Ann", "free0001", block_of("TINY.LVL", 3, 0, 1));
        w.connect("Bob", "free0001");
        w.connect("Cat", "free0001");
        w.run(1500);
        ASSERT_TRUE(started("free0001"));                                                 // (it starts by itself when it is full)
        ASSERT_FALSE(open_ann.lobby->room().leader_starts() || w.status("free0001").leader_starts);
        Client& ann = w.connect_creating("Ann", "hold0001", block_of("TINY.LVL", 3, 0, 1, true));
        Client& bob = w.connect("Bob", "hold0001");
        w.run(100);                                                                       // (Ann is first: she leads)
        Client& cat = w.connect("Cat", "hold0001");
        w.run(2000);
        RoomStatus st = w.status("hold0001");
        ASSERT_TRUE(st.state == RoomState::Waiting && st.joined == 3 && st.expected == 3 && st.public_room && st.leader_starts && st.room_teams == "0+1");
        ASSERT_TRUE(ann.lobby->room().leader_starts() && bob.lobby->room().leader_starts() && cat.lobby->room().leader_starts());      // (every client is told)
        ASSERT_TRUE(ann.lobby->is_leader() && !bob.lobby->is_leader() && st.leader == ann.lobby->my_seat());
        w.run(5000);                                                                      // a full room that waits does not start however long it is full
        ASSERT_TRUE(w.status("hold0001").state == RoomState::Waiting);
        ASSERT_FALSE(bob.lobby->request_start());                                         // (a guest sends nothing: only the leader asks)
        w.run(1000);
        ASSERT_TRUE(w.status("hold0001").state == RoomState::Waiting);
        ASSERT_TRUE(ann.lobby->request_start());                                          // the leader's START starts it, with the teams that the block named
        w.run(1500);
        st = w.status("hold0001");
        ASSERT_TRUE((st.state == RoomState::Loading || st.state == RoomState::Running) && st.teams == "0+1");
        // a full room whose leader never presses START ends after the wait, and the reason names the leader and not the players that did not come
        Client& dan = w.connect_creating("Dan", "idle0001", block_of("TINY.LVL", 2, net::kNoTeam, net::kNoTeam, true));
        w.connect("Eve", "idle0001");
        w.run(2000);
        ASSERT_TRUE(w.status("idle0001").state == RoomState::Waiting && w.status("idle0001").joined == 2);
        ASSERT_TRUE(dan.lobby->is_leader());
        w.run(61000);
        st = w.status("idle0001");
        ASSERT_TRUE(st.state == RoomState::Failed && st.reason.find("leader did not start the match") != std::string::npos && st.reason.find("2 players") != std::string::npos);
        // the same wait for a room that was never full names the players
        w.connect_creating("Fay", "slow0001", block_of("TINY.LVL", 4, net::kNoTeam, net::kNoTeam, true));
        w.run(62000);
        st = w.status("slow0001");
        ASSERT_TRUE(st.state == RoomState::Failed && st.reason.find("did not all come (1 of 4)") != std::string::npos);
    } TEST_END();

    TEST_CASE("S3.24c The Platform Byte Through A Real Room (Protocol 15): What A Player Tells In Its Hello Is Shown To Everybody At Its Seat; It Goes With The Player When The Leader Swaps Two Colours; A Player Who Leaves Takes It Away (The Seat Says 0 Until A Newcomer Says Its Own); The Start Of The Match Carries The Platform Of Every Seat That Plays") {
        ServerLimits limits;
        limits.demo_rooms = 4;
        limits.demo_map = "TINY.LVL";
        World w(limits);
        const uint8_t mac_page = static_cast<uint8_t>(net::kPlatformBrowser | net::kOsMacos);                  // 18
        const auto platforms = [](const Client& c) {                                      // "<seat 0> <seat 1> <seat 2> <seat 3>": the byte of each seat as this player's screen shows it
            std::string out;
            for (const net::RoomMsg::Slot& slot : c.lobby->room().slots) out += (out.empty() ? "" : " ") + std::to_string(static_cast<unsigned>(slot.platform));
            return out;
        };
        w.next_platform = net::kOsWindows;
        Client& ann = w.connect_creating("Ann", "plat0001", block_of("TINY.LVL", 4));
        w.next_platform = mac_page;
        Client& bob = w.connect("Bob", "plat0001");
        w.run(100);
        w.next_platform = net::kOsAndroid;
        Client& cat = w.connect("Cat", "plat0001");
        w.next_platform = net::kPlatformUnknown;
        w.run(1000);
        ASSERT_TRUE(w.status("plat0001").state == RoomState::Waiting && ann.lobby->is_leader());
        for (const Client* c : {&ann, &bob, &cat}) ASSERT_EQ(platforms(*c), std::string("1 18 4 0"));
        ASSERT_TRUE(ann.lobby->request_seat_move(1, 2));                                  // Ann swaps Bob and Cat: each takes its platform to the other colour
        w.run(500);
        for (const Client* c : {&ann, &bob, &cat}) ASSERT_EQ(platforms(*c), std::string("1 4 18 0"));
        bob.lobby->leave();                                                               // Bob goes: his colour says nothing any more
        w.run(500);
        for (const Client* c : {&ann, &cat}) ASSERT_EQ(platforms(*c), std::string("1 4 0 0"));
        w.next_platform = net::kOsIos;
        Client& dan = w.connect("Dan", "plat0001");                                       // a newcomer takes the colour that is free, with its own byte
        w.next_platform = net::kPlatformUnknown;
        w.run(500);
        ASSERT_EQ(dan.lobby->my_seat(), 2);
        for (const Client* c : {&ann, &cat, &dan}) ASSERT_EQ(platforms(*c), std::string("1 4 5 0"));
        ASSERT_TRUE(ann.lobby->request_start());                                          // three people: the leader starts the match
        w.run(1500);
        ASSERT_TRUE(w.status("plat0001").state == RoomState::Loading || w.status("plat0001").state == RoomState::Running);
        for (const Client* c : {&ann, &cat, &dan}) {
            const net::StartMsg& start = c->lobby->start_info();
            ASSERT_TRUE(start.roster == 0x07u && start.platforms[0] == net::kOsWindows && start.platforms[1] == net::kOsAndroid && start.platforms[2] == net::kOsIos && start.platforms[3] == 0);
        }
    } TEST_END();

    TEST_CASE("S3.74 The Public Stack's Rooms (docker-compose.stack.yml: --demo-map TREASURE.LVL and the six maps of the page): A Block That Names No Map Is Made On TREASURE.LVL; One That Names One Of The Six Is Made On It, Whatever Its Case; The Seats Are The Block's") {
        ServerLimits limits;                                                      // the options of the stack file: --demo-rooms 48 --demo-map TREASURE.LVL --demo-maps <the six>
        limits.demo_rooms = 48;
        limits.demo_map = "TREASURE.LVL";
        limits.demo_maps = {"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "ISLANDS.LVL"};
        World w(limits);
        int made_rooms = 0;
        auto made = [&](const net::CreateBlock& block) {
            const std::string code = "stack" + std::to_string(++made_rooms);
            w.connect_creating("P", code, block);
            w.run(300);
            return w.status(code);
        };
        struct Case { const char* map; uint8_t seats; const char* expect; };
        const Case cases[] = {
            {"", 4, "TREASURE.LVL"}, {"", 2, "TREASURE.LVL"},                       // no name: the default map
            {"BIG-FUN.LVL", 4, "TREASURE.LVL"},                                      // a name that the stack does not list: the default map
            {"TINY.LVL", 4, "TINY.LVL"}, {"small.lvl", 4, "SMALL.LVL"}, {"MEDIUM.LVL", 4, "MEDIUM.LVL"},      // a map of the page's six: that map
            {"GAUNTLET.LVL", 4, "GAUNTLET.LVL"}, {"Treasure.LVL", 4, "TREASURE.LVL"}, {"islands.lvl", 4, "ISLANDS.LVL"},
            {"ISLANDS.LVL", 3, "ISLANDS.LVL"}, {"TINY.LVL", 2, "TINY.LVL"},          // with the seats
        };
        for (const Case& c : cases) {
            const RoomStatus st = made(block_of(c.map, c.seats));
            ASSERT_MSG(st.map == c.expect, std::string(c.map) + " " + std::to_string(c.seats));
            ASSERT_MSG(st.expected == c.seats, std::string(c.map) + " " + std::to_string(c.seats));
        }
        ASSERT_EQ(w.mgr.room_count(), sizeof(cases) / sizeof(cases[0]));
    } TEST_END();

    TEST_CASE("S3.25 Friends Who Come Late: A Public Room Waits Ten Minutes By Default; A Code Whose Public Room Is Over Makes A New One When The Hello Brings A Block (Its End Is Still Reported), And Is \"No Such Room\" When It Does Not; A Room Of The Control Interface That Is Over Answers \"No Such Room\", Not \"Match Running\", Block Or Not") {
        ASSERT_EQ(ServerLimits().demo_wait_ms, 600000u);
        ServerLimits limits;
        limits.demo_rooms = 4;
        limits.demo_map = "TINY.LVL";
        limits.demo_maps = {"TINY.LVL", "SMALL.LVL"};
        {
            World w(limits);                                                          // the default wait: a friend can come after a minute
            w.connect_creating("Host", "slow0001", block_of("SMALL.LVL", 2));
            w.run(61000);
            ASSERT_TRUE(w.status("slow0001").state == RoomState::Waiting);
            Client& friend_ = w.connect("Friend", "slow0001");
            w.run(800);
            ASSERT_EQ(friend_.lobby->my_seat(), 1);
            ASSERT_TRUE(w.status("slow0001").state == RoomState::Loading || w.status("slow0001").state == RoomState::Running);
        }
        ServerLimits short_wait = limits;
        short_wait.demo_wait_ms = 60000;
        World w(short_wait);
        Client& host = w.connect_creating("Host", "late0001", block_of("SMALL.LVL", 2));
        w.run(61000);                                                                 // nobody came in time: the room failed
        ASSERT_TRUE(w.status("late0001").state == RoomState::Failed);
        ASSERT_TRUE(host.lobby->phase() != net::ClientLobby::Phase::InRoom);
        Client& plain = w.connect("Plain", "late0001");                               // within the 30 s of keep time, a Hello without a block: the room is over, nothing is made
        w.run(300);
        ASSERT_TRUE(plain.lobby->phase() == net::ClientLobby::Phase::Rejected);
        ASSERT_EQ(plain.lobby->reject_reason(), net::RejectReason::NoSuchRoom);
        ASSERT_TRUE(w.status("late0001").state == RoomState::Failed);
        Client& late = w.connect_creating("Late", "late0001", block_of("SMALL.LVL", 2));       // ... and one with the block (the link carries it): a new room at once, not "match running"
        w.run(300);
        ASSERT_EQ(late.lobby->phase(), net::ClientLobby::Phase::InRoom);
        ASSERT_TRUE(w.status("late0001").state == RoomState::Waiting);
        ASSERT_EQ(static_cast<int>(w.status("late0001").joined), 1);
        ASSERT_EQ(w.status("late0001").map, std::string("SMALL.LVL"));
        bool reported = false;
        for (const RoomStatus& st : w.mgr.take_ended(w.now)) reported = reported || (st.code == "late0001" && st.state == RoomState::Failed);
        ASSERT_TRUE(reported);                                                        // the old room's end is not lost
        // a room of the control interface that is over is not replaced: "no such room", with a block too
        ASSERT_TRUE(w.mgr.create_room(spec_of("CTL-1", 2), w.now).ok);
        ASSERT_TRUE(w.mgr.close_room("CTL-1", w.now));
        ASSERT_TRUE(w.status("CTL-1").state == RoomState::Failed);
        Client& too_late = w.connect("TooLate", "CTL-1");
        Client& too_late_block = w.connect_creating("TooLate2", "CTL-1", block_of());
        w.run(300);
        ASSERT_TRUE(too_late.lobby->phase() == net::ClientLobby::Phase::Rejected);
        ASSERT_EQ(too_late.lobby->reject_reason(), net::RejectReason::NoSuchRoom);
        ASSERT_TRUE(too_late_block.lobby->phase() == net::ClientLobby::Phase::Rejected);
        ASSERT_EQ(too_late_block.lobby->reject_reason(), net::RejectReason::NoSuchRoom);
        ASSERT_TRUE(w.status("CTL-1").state == RoomState::Failed);
    } TEST_END();
}

void run_hardening_tests() {
    TEST_CASE("S3.11 The Server's Clock Is Its Uptime: A Room Starts And Plays When The 32-Bit Clock Is Past Its Signed Half, And Across The Wrap") {
        for (const uint32_t origin : {0x7FFFFE00u, 0x80000100u, 0xFFFFFC18u}) {          // just before the signed flip, just after it, a second before the wrap
            World w;
            w.now = origin;
            ASSERT_TRUE(w.mgr.create_room(spec_of("CLOCK-1", 2), w.now).ok);
            w.connect("Ann", "CLOCK-1");
            w.connect("Bob", "CLOCK-1");
            w.run(2500);
            ASSERT_MSG(w.status("CLOCK-1").state == RoomState::Running, "the room started at origin " + std::to_string(origin));
            w.run(20000 + kPre);
            const RoomStatus s = w.status("CLOCK-1");
            ASSERT_MSG(s.state == RoomState::Running && s.ticks > 300, "the match runs at origin " + std::to_string(origin));
            ASSERT_FALSE(w.clients[0]->session->desynced());
            ASSERT_TRUE(w.clients[0]->sim.state_hash() == w.clients[1]->sim.state_hash() || w.clients[0]->sim.current_tick() != w.clients[1]->sim.current_tick());
        }
    } TEST_END();

    TEST_CASE("S3.12 A Seat That Stops Executing Turns Does Not Hold A Room Even For A Moment: The Others Play On At Their Pace And Are Told, It Is Dropped After 30 s Without An Ack; A Running Room Has A Wall-Clock Limit") {
        {
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("LAG-1", 3), w.now).ok);
            Client& a = w.connect("Ann", "LAG-1");
            Client& b = w.connect("Bob", "LAG-1");
            Client& c = w.connect("Cat", "LAG-1");
            w.run(3000 + kPre);
            ASSERT_TRUE(w.status("LAG-1").state == RoomState::Running);
            const uint8_t bob = b.lobby->my_seat();
            const uint32_t ticks_before = w.status("LAG-1").ticks;
            b.freeze = true;                                                              // Bob's program hangs: it neither acks nor answers (a hostile client would still ping)
            w.run(8000);
            // the room did not wait for Bob (it used to, 3 s after he stopped, until the 20 s were up): 8 s are 160 ticks, and Ann and Cat played them
            const uint32_t ticks_while_stuck = w.status("LAG-1").ticks;
            ASSERT_TRUE(ticks_while_stuck - ticks_before >= 150);
            ASSERT_TRUE(a.session->lagging_seat() == bob && c.session->lagging_seat() == bob);      // ... and they are told who lags
            ASSERT_TRUE(a.session->lagging_behind_ms() >= 3000);
            ASSERT_TRUE(w.status("LAG-1").state == RoomState::Running);
            w.run(4000);
            ASSERT_TRUE(w.status("LAG-1").ticks - ticks_while_stuck >= 75);               // and on: 4 s, 80 ticks
            w.run(14000);                                                                 // 26 s since Bob stopped: still a lagger, not yet dropped (30 s without an ack)
            ASSERT_FALSE(a.sim.is_player_dropped(bob));
            w.run(7000);                                                                  // 33 s: dropped, at one tick for both of the others
            ASSERT_TRUE(a.sim.is_player_dropped(bob) && c.sim.is_player_dropped(bob));
            ASSERT_TRUE(a.sim.state_hash() == c.sim.state_hash());
            const uint32_t after_drop = w.status("LAG-1").ticks;
            w.run(5000);
            ASSERT_TRUE(w.status("LAG-1").ticks >= after_drop + 95);                      // Ann and Cat play on
            ASSERT_TRUE(w.status("LAG-1").state == RoomState::Running);
            ASSERT_FALSE(a.lost || c.lost);
            ASSERT_EQ(a.session->lagging_seat(), 255);                                    // the notice ended with the drop
        }
        {                                                                                 // two players: the one that is left has won, the room ends (it is not held for ever)
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("LAG-2", 2), w.now).ok);
            w.connect("Ann", "LAG-2");
            Client& b = w.connect("Bob", "LAG-2");
            w.run(3000 + kPre);
            ASSERT_TRUE(w.status("LAG-2").state == RoomState::Running);
            b.freeze = true;
            w.run(40000);
            ASSERT_TRUE(w.status("LAG-2").state == RoomState::Finished);
        }
        {
            World w;
            RoomSpec spec = spec_of("RUN-1", 2);
            spec.run_ms = 5000;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            w.connect("Ann", "RUN-1");
            w.connect("Bob", "RUN-1");
            w.run(2500);
            ASSERT_TRUE(w.status("RUN-1").state == RoomState::Running);
            w.run(6000);
            const RoomStatus s = w.status("RUN-1");
            ASSERT_TRUE(s.state == RoomState::Failed);
            ASSERT_TRUE(s.reason.find("longer") != std::string::npos);
        }
    } TEST_END();

    TEST_CASE("S3.34 The End Of A Match Does Not Cut Off A Laggard: A Player Who Is Far Behind When The Match Ends (Its Slow Link Brought It 55 s Behind) Is Still Answered And Served Until It Has Run The Last Turn, Then The Room Closes (It Closed 15 s After The End: The Player Was Cut Off 10 s Into Its Catch-Up, Never Saw The Results)") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("END-1", 3), w.now).ok);
        Client& a = w.connect("Ann", "END-1");
        Client& b = w.connect("Bob", "END-1");
        Throttled* slow = nullptr;
        Client& c = w.connect("Cat", "END-1", 255, {20, 10}, &slow);
        w.run(4000 + kPre);
        ASSERT_TRUE(w.status("END-1").state == RoomState::Running);
        slow->period_ms = 150;                                                       // Cat's downlink hands the game 7 of the 21 messages a second that the room sends
        w.run(82000);                                                                // it falls behind by two thirds of a second each second: 55 s behind (not 60: not dropped), and it still makes progress
        RoomStatus s = w.status("END-1");
        ASSERT_TRUE(s.state == RoomState::Running);
        ASSERT_TRUE(a.sim.current_tick() > 1500 && c.sim.current_tick() + 900 < a.sim.current_tick());
        ASSERT_FALSE(a.lost || b.lost || c.lost);
        // the match ends for everybody else: Ann and Bob go, Cat is the last team and has won; its link is healthy again
        w.net.cut(a.end);
        w.net.cut(b.end);
        slow->period_ms = 0;
        for (int i = 0; i < 100 && w.status("END-1").state == RoomState::Running; ++i) w.run(100);
        ASSERT_TRUE(w.status("END-1").state == RoomState::Finished);
        const uint32_t ended_at = w.now;
        const uint32_t final_turns = w.status("END-1").turns;
        ASSERT_TRUE(final_turns > 1600);
        ASSERT_TRUE(c.sim.current_tick() + 800 < final_turns);                        // it is more than 40 s short of the end: its catch-up takes more than 10 s (the first frame back 4 s, then 4 s a second)
        // ... it is answered all the while (pings), and runs every turn to the last one
        uint32_t level_after_ms = 0;
        while (w.now - ended_at < 40000 && c.end->is_open()) {
            w.run(100);
            if (level_after_ms == 0 && c.sim.is_match_over()) level_after_ms = w.now - ended_at;
        }
        if (level_after_ms == 0 || c.lost) std::cout << "\n    Cat: lost " << c.lost << ", at turn " << c.session->runner().next_turn_to_execute() << " of " << final_turns << " after " << (w.now - ended_at) << " ms\n";
        ASSERT_FALSE(c.lost);
        ASSERT_TRUE(level_after_ms > 10000 && level_after_ms < 30000);                // it needed more than the 10 s of silence that cut it off, and the room waited
        ASSERT_EQ(c.session->runner().next_turn_to_execute(), final_turns);           // every turn of the match
        ASSERT_TRUE(c.sim.is_match_over());
        // the room closes: not before the grace period (a client that is level finds the results in peace), and not long after the last acknowledgement
        ASSERT_TRUE(w.now - ended_at >= Room::kGraceMs);
        ASSERT_FALSE(c.end->is_open());
        ASSERT_TRUE(w.now - ended_at <= level_after_ms + Room::kGraceMs + 1000 || w.now - ended_at <= Room::kEndWaitMs + 1000);
    } TEST_END();

    TEST_CASE("S3.35 A Finished Room Closes Its Connections At The Latest After kEndWaitMs, Whoever Is Still Behind, And At The Earliest After kGraceMs; Everybody Level Closes It At kGraceMs") {
        ASSERT_EQ(Room::kGraceMs, 15000u);
        ASSERT_EQ(Room::kEndWaitMs, 30000u);
        {   // everybody level: the connections stay open for the grace period and then close
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("END-2", 2), w.now).ok);
            Client& a = w.connect("Ann", "END-2");
            Client& b = w.connect("Bob", "END-2");
            w.run(4000 + kPre);
            w.net.cut(b.end);
            for (int i = 0; i < 100 && w.status("END-2").state == RoomState::Running; ++i) w.run(100);
            ASSERT_TRUE(w.status("END-2").state == RoomState::Finished);
            w.run(Room::kGraceMs - 1500);
            ASSERT_TRUE(a.server_end->is_open());
            w.run(3000);
            ASSERT_FALSE(a.server_end->is_open());
        }
        {   // a client that never acknowledges (a process that was stopped): the room does not wait for it for ever, and it is not dropped by the idle rule after the end
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("END-3", 3), w.now).ok);
            Client& a = w.connect("Ann", "END-3");
            Client& b = w.connect("Bob", "END-3");
            Client& c = w.connect("Cat", "END-3");
            w.run(4000);
            c.freeze = true;
            w.net.cut(a.end);
            w.net.cut(b.end);
            for (int i = 0; i < 100 && w.status("END-3").state == RoomState::Running; ++i) w.run(100);
            ASSERT_TRUE(w.status("END-3").state == RoomState::Finished);
            w.run(Room::kEndWaitMs - 2000);
            ASSERT_TRUE(c.server_end->is_open());                                    // 28 s: the room still waits for it
            w.run(4000);
            ASSERT_FALSE(c.server_end->is_open());                                   // 32 s: it does not wait any longer
        }
    } TEST_END();

    TEST_CASE("S3.13 The Door Cannot Be Locked By Silent Connections, And The End Of A Room That Is Forgotten At Once Is Still Reported") {
        {
            ServerLimits limits;
            limits.max_pending = 4;
            World w(limits);
            ASSERT_TRUE(w.mgr.create_room(spec_of("DOOR-1", 2), w.now).ok);
            std::vector<std::pair<net::Connection*, net::Connection*>> silent;
            for (int i = 0; i < 4; ++i) {
                auto ends = w.net.connect({5, 0});
                w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "x", w.now);
                silent.push_back(ends);
            }
            ASSERT_EQ(w.mgr.pending_count(), size_t{4});
            Client& ann = w.connect("Ann", "DOOR-1");                                    // the fifth connection: it takes the place of the oldest silent one
            w.run(400);
            ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);
            ASSERT_FALSE(silent[0].second->is_open());                                    // (the evicted one was closed)
            ASSERT_TRUE(silent[3].second->is_open());
            ASSERT_TRUE(w.mgr.connections_refused() >= 1);
        }
        {
            World w;
            RoomSpec spec = spec_of("GONE-1", 2);
            spec.wait_ms = 1000;
            spec.keep_ms = 0;                                                             // forgotten in the pass in which it fails
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            std::vector<RoomStatus> ended;
            for (int i = 0; i < 300; ++i) {
                w.run(10);
                for (const RoomStatus& s : w.mgr.take_ended(w.now)) ended.push_back(s);
            }
            ASSERT_EQ(w.mgr.room_count(), size_t{0});
            ASSERT_EQ(ended.size(), size_t{1});
            ASSERT_EQ(ended[0].code, std::string("GONE-1"));
            ASSERT_TRUE(ended[0].state == RoomState::Failed);
        }
    } TEST_END();
}

void run_map_tests() {
    TEST_CASE("S3.14 A Map That Is Playable For Some Seats And Not For Others: A Start Marker Outside The Grid Fails The Room When That Seat Plays, And Does Not When It Does Not") {
        // TINY with the green start marker (tile 154) moved to row 200, outside the 40 x 40 grid: the engine loads it, the original would crash when green plays
        const std::string dir = temp_dir_for("badmarker");
        {
            std::ifstream in(maps_dir() + "/TINY.LVL", std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            assets::LevelData tiny;
            ASSERT_TRUE(tiny.load_from_memory(bytes.data(), bytes.size()));
            const assets::AnthillSpawn* green = nullptr;
            for (const auto& sp : tiny.anthill_spawns) {
                if (sp.tile_id == 154) green = &sp;
            }
            ASSERT_TRUE(green != nullptr);
            const uint8_t pattern[6] = {154, 0, static_cast<uint8_t>(green->y & 0xFF), static_cast<uint8_t>(green->y >> 8), static_cast<uint8_t>(green->x & 0xFF), static_cast<uint8_t>(green->x >> 8)};
            size_t at = bytes.size();
            for (size_t i = 0; i + 6 <= bytes.size() && at == bytes.size(); ++i) {
                if (std::equal(pattern, pattern + 6, bytes.begin() + static_cast<std::ptrdiff_t>(i))) at = i;
            }
            ASSERT_TRUE(at < bytes.size());
            bytes[at + 2] = 200;                                                          // the row: outside the grid
            bytes[at + 3] = 0;
            std::ofstream out(fs::path(dir) / "BAD.LVL", std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        const auto run_room = [&](uint8_t first_seat, uint8_t second_seat, RoomStatus& s) {
            net::LoopbackNetwork net{5};
            RoomManager mgr{MapStore(dir)};
            uint32_t now = 1000;
            RoomSpec spec = spec_of("BAD-1", 2, "BAD.LVL");
            ASSERT_TRUE(mgr.create_room(spec, now).ok);                                   // it loads: the room is made
            std::vector<std::unique_ptr<Client>> clients;
            const uint8_t seats[2] = {first_seat, second_seat};
            for (int i = 0; i < 2; ++i) {
                auto ends = net.connect({20, 10});
                mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", now);
                clients.push_back(std::make_unique<Client>());
                clients.back()->name = i == 0 ? "Ann" : "Bob";
                clients.back()->room = "BAD-1";
                clients.back()->want_seat = seats[i];
                clients.back()->start(ends.second, 11u + static_cast<uint32_t>(i));
            }
            for (int step = 0; step < 300; ++step) {                                      // 3 s
                now += 10;
                net.set_time(now);
                mgr.update(now);
                for (auto& c : clients) c->update(now, dir);
            }
            ASSERT_TRUE(mgr.status("BAD-1", s, now));
        };
        RoomStatus with_green;
        run_room(0, 1, with_green);                                                       // green (seat 0) plays: refused, and the room says why
        ASSERT_TRUE(with_green.state == RoomState::Failed);
        ASSERT_TRUE(with_green.reason.find("outside") != std::string::npos && with_green.reason.find("green") != std::string::npos);
        RoomStatus without_green;
        run_room(1, 2, without_green);                                                    // red and blue play: the marker is never used, the match starts
        ASSERT_TRUE(without_green.state == RoomState::Running || without_green.state == RoomState::Loading);
    } TEST_END();
}

void run_match_tests() {
    TEST_CASE("S3.4 A Whole Match: Three Clients Join By Code, The Match Starts By Itself, The Referee Plays Along Bit-Identically To The End, The Result Is Kept") {
        World w;
        RoomSpec spec = spec_of("MATCH-1", 3);
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        Client& a = w.connect("Ann", "MATCH-1", 2);                                     // asks for seat 2
        Client& b = w.connect("Bob", "MATCH-1");                                        // any: the first free one, seat 0
        w.run(300);
        ASSERT_TRUE(w.status("MATCH-1").state == RoomState::Waiting && w.status("MATCH-1").joined == 2);
        Client& c = w.connect("Cat", "MATCH-1", 3, {40, 20});                           // asks for seat 3: the roster is 0, 2, 3
        w.run(1500);
        RoomStatus s = w.status("MATCH-1");
        ASSERT_TRUE(s.state == RoomState::Running);                                     // nobody pressed START: the room started itself
        ASSERT_EQ(a.lobby->my_seat(), 2);
        ASSERT_EQ(c.lobby->my_seat(), 3);
        ASSERT_EQ(b.lobby->my_seat(), 0);
        ASSERT_EQ(s.names[2], std::string("Ann"));
        ASSERT_EQ(s.names[0], std::string("Bob"));
        ASSERT_TRUE(a.session != nullptr && b.session != nullptr && c.session != nullptr);
        // play until the match is over (the clock of the map)
        for (int guard = 0; guard < 4000 && w.status("MATCH-1").state == RoomState::Running; ++guard) w.run(250);
        s = w.status("MATCH-1");
        ASSERT_TRUE(s.state == RoomState::Finished);
        ASSERT_TRUE(s.ticks > 1000 && s.turns > 1000);                                   // (a turn is a tick: turns of 50 ms)
        ASSERT_EQ(s.rows.size(), size_t{3});                                            // three teams, no alliances: three rows
        int winners = 0;
        for (const RoomRow& r : s.rows) winners += r.winner ? 1 : 0;
        ASSERT_TRUE(winners >= 1);
        ASSERT_TRUE(s.rows[0].score >= s.rows[1].score && s.rows[1].score >= s.rows[2].score);     // best first
        // the end is reported once
        ASSERT_EQ(w.mgr.take_ended(w.now).size(), size_t{1});
        ASSERT_EQ(w.mgr.take_ended(w.now).size(), size_t{0});
        // after the grace period the connections are closed (every client has executed the last turn by then); after the keep time the room is forgotten
        w.run(Room::kGraceMs + 500);
        ASSERT_FALSE(a.end->is_open());
        for (Client* p : {&a, &b, &c}) ASSERT_FALSE(p->session->desynced());              // the referee saw no desync
        ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash() && b.sim.state_hash() == c.sim.state_hash());
        ASSERT_TRUE(a.sim.is_match_over() && b.sim.is_match_over() && c.sim.is_match_over());
        w.run(11 * 60 * 1000);
        ASSERT_EQ(w.mgr.room_count(), size_t{0});
    } TEST_END();

    TEST_CASE("S3.81 The Busy Count (The Public /busy Answer): Rooms Whose Match Loads Or Runs, And The People In Rooms That Wait, Load Or Run; A Bot Is No Person; A Room That Is Closed Or Over Counts Nothing") {
        World w;
        const auto busy = [&w]() { return w.mgr.busy(w.now); };
        ASSERT_TRUE(busy().matches == 0 && busy().players == 0);                          // a server that holds nothing
        ASSERT_TRUE(w.mgr.create_room(spec_of("BUSY-A", 2), w.now).ok);
        ASSERT_TRUE(busy().matches == 0 && busy().players == 0);                          // a room that waits, nobody in it
        w.connect("Ann", "BUSY-A");
        w.run(300);
        ASSERT_TRUE(w.status("BUSY-A").state == RoomState::Waiting);
        ASSERT_TRUE(busy().matches == 0 && busy().players == 1);                          // a person who waits in a lobby: no match yet
        w.connect("Bob", "BUSY-A");
        bool saw_loading = false;
        for (int guard = 0; guard < 400 && w.status("BUSY-A").state != RoomState::Running; ++guard) {
            w.run(10);
            if (w.status("BUSY-A").state == RoomState::Loading) {
                saw_loading = true;
                ASSERT_TRUE(busy().matches == 1 && busy().players == 2);                  // the match is loading: a restart now would cancel the start, it counts
            }
        }
        ASSERT_TRUE(saw_loading);
        ASSERT_TRUE(w.status("BUSY-A").state == RoomState::Running);
        ASSERT_TRUE(busy().matches == 1 && busy().players == 2);                          // the match runs
        ASSERT_TRUE(w.mgr.create_room(spec_of("BUSY-B", 4), w.now).ok);
        w.connect("Cy", "BUSY-B");
        w.run(300);
        ASSERT_TRUE(busy().matches == 1 && busy().players == 3);                          // a second room only waits
        RoomSpec with_bot = spec_of("BUSY-C", 3);
        with_bot.bots = {ai::BotSpec{2, "standard", ai::Level::Medium}};
        ASSERT_TRUE(w.mgr.create_room(with_bot, w.now).ok);
        w.connect("Di", "BUSY-C");
        w.run(300);
        ASSERT_TRUE(w.status("BUSY-C").joined == 2 && w.status("BUSY-C").bots.size() == 1);
        ASSERT_TRUE(busy().matches == 1 && busy().players == 4);                          // the bot of room C is not counted: Ann, Bob, Cy and Di
        ASSERT_TRUE(w.mgr.close_room("BUSY-A", w.now));                                   // the owner closes the running match
        ASSERT_TRUE(w.status("BUSY-A").state == RoomState::Failed);
        ASSERT_TRUE(busy().matches == 0 && busy().players == 2);                          // it counts nothing now: Cy and Di still wait
        ASSERT_TRUE(w.mgr.close_room("BUSY-B", w.now) && w.mgr.close_room("BUSY-C", w.now));
        ASSERT_TRUE(busy().matches == 0 && busy().players == 0);
        // a match that ends by itself stops counting
        ASSERT_TRUE(w.mgr.create_room(spec_of("BUSY-D", 2), w.now).ok);
        w.connect("Ed", "BUSY-D");
        w.connect("Flo", "BUSY-D");
        w.run(1500);
        ASSERT_TRUE(w.status("BUSY-D").state == RoomState::Running && busy().matches == 1 && busy().players == 2);
        for (int guard = 0; guard < 4000 && w.status("BUSY-D").state == RoomState::Running; ++guard) w.run(250);
        ASSERT_TRUE(w.status("BUSY-D").state == RoomState::Finished);
        ASSERT_TRUE(busy().matches == 0 && busy().players == 0);                          // over: nobody plays, a restart would cut nothing
    } TEST_END();

    TEST_CASE("S3.5 A Failed Start Is Cancelled And Tried Again: A Client That Cannot Load The Map Does Not Spoil The Room; Too Many Failed Starts End It") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("RETRY-1", 2), w.now).ok);
        Client& a = w.connect("Ann", "RETRY-1");
        Client& b = w.connect("Bob", "RETRY-1");
        b.fail_load = true;
        w.run(1500);
        ASSERT_TRUE(w.status("RETRY-1").state == RoomState::Waiting);                   // cancelled: back to waiting (and a pause before the next try)
        b.fail_load = false;
        w.run(4000);                                                                     // the room tries again by itself
        ASSERT_TRUE(w.status("RETRY-1").state == RoomState::Running);
        (void)a;
        // a client that always fails: the room gives up after a few starts
        World v;
        ASSERT_TRUE(v.mgr.create_room(spec_of("RETRY-2", 2), v.now).ok);
        v.connect("Ann", "RETRY-2");
        Client& bad = v.connect("Bad", "RETRY-2");
        bad.fail_load = true;
        v.run(25000);                                                                    // five starts, two seconds apart
        const RoomStatus s = v.status("RETRY-2");
        ASSERT_TRUE(s.state == RoomState::Failed);
        ASSERT_TRUE(s.reason.find("start failed") != std::string::npos);
    } TEST_END();

    TEST_CASE("S3.6 Nobody Came, And The Owner Closes: A Room That Does Not Fill Fails After Its Wait; Closing A Running Room Drops Everybody") {
        World w;
        RoomSpec spec = spec_of("WAIT-1", 2);
        spec.wait_ms = 3000;
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        Client& a = w.connect("Ann", "WAIT-1");
        w.run(2000);
        ASSERT_TRUE(w.status("WAIT-1").state == RoomState::Waiting);
        w.run(2000);
        RoomStatus s = w.status("WAIT-1");
        ASSERT_TRUE(s.state == RoomState::Failed && s.reason.find("1 of 2") != std::string::npos);
        ASSERT_FALSE(a.end->is_open());                                                 // the waiting client is let go
        // closing a running match
        World x;
        ASSERT_TRUE(x.mgr.create_room(spec_of("CLOSE-1", 2), x.now).ok);
        Client& p = x.connect("P", "CLOSE-1");
        Client& q = x.connect("Q", "CLOSE-1");
        x.run(3000);
        ASSERT_TRUE(x.status("CLOSE-1").state == RoomState::Running);
        ASSERT_TRUE(x.mgr.close_room("CLOSE-1", x.now));
        x.run(200);
        ASSERT_TRUE(x.status("CLOSE-1").state == RoomState::Failed && x.status("CLOSE-1").reason == "closed by the owner");
        ASSERT_FALSE(p.end->is_open());
        ASSERT_FALSE(q.end->is_open());
    } TEST_END();

    TEST_CASE("S3.7 A Player Who Leaves During The Match Is Dropped For Everybody At The Same Tick; When Everybody Has Left The Room Finishes") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("DROP-1", 3), w.now).ok);
        Client& a = w.connect("A", "DROP-1");
        Client& b = w.connect("B", "DROP-1");
        Client& c = w.connect("C", "DROP-1");
        w.run(4000);
        ASSERT_TRUE(w.status("DROP-1").state == RoomState::Running);
        w.net.cut(b.end);                                                                // B's link dies
        w.run(3000);
        ASSERT_TRUE(a.sim.is_player_dropped(1) && c.sim.is_player_dropped(1));
        ASSERT_TRUE(b.lost);                                                             // B has no host to elect: its match is over
        ASSERT_TRUE(w.status("DROP-1").state == RoomState::Running);                     // the others play on
        w.net.cut(a.end);
        w.net.cut(c.end);
        w.run(3000);
        ASSERT_TRUE(w.status("DROP-1").state == RoomState::Finished);
        ASSERT_TRUE(w.status("DROP-1").reason == "everybody left" || w.status("DROP-1").reason == "the match ended");
    } TEST_END();
}

// The room's leader (protocol 7): the first player who joined may start the match before every seat is taken
void run_leader_tests() {
    TEST_CASE("S3.26 The Leader Starts A Room For Four With Two Players: The Match Runs With Their Two Seats To The End, The Referee And Both Clients Agree, The Status Keeps What Was Asked For And Shows Who Joined; A Player Who Is Not The Leader Is Ignored") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("LEAD-1", 4), w.now).ok);
        ASSERT_TRUE(w.status("LEAD-1").early_start);                                     // on by default
        ASSERT_EQ(w.status("LEAD-1").leader, 255);                                       // nobody has joined yet
        Client& ann = w.connect("Ann", "LEAD-1");
        Client& bob = w.connect("Bob", "LEAD-1");
        w.run(500);
        RoomStatus s = w.status("LEAD-1");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 2 && s.expected == 4);
        ASSERT_EQ(s.leader, 0);                                                          // Ann was welcomed first: seat 0
        ASSERT_TRUE(ann.lobby->room().leader == 0 && bob.lobby->room().leader == 0);     // everybody is told
        ASSERT_TRUE(ann.lobby->is_leader() && !bob.lobby->is_leader());
        // Bob does not lead: a request of his (from a client that is not the game's) is ignored and counted, and does not cost him his seat
        bob.end->send(net::encode(net::StartRequestMsg{}));
        w.run(1500);
        s = w.status("LEAD-1");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.ignored_start_requests == 1);
        ASSERT_EQ(bob.lobby->phase(), net::ClientLobby::Phase::InRoom);
        // Ann's request starts the match at once with the two of them
        ASSERT_TRUE(ann.lobby->request_start());
        w.run(1500);
        s = w.status("LEAD-1");
        ASSERT_TRUE(s.state == RoomState::Running);
        ASSERT_EQ(s.expected, 4);                                                         // as asked
        ASSERT_EQ(s.joined, 2);                                                           // who is there
        ASSERT_TRUE(s.names[0] == "Ann" && s.names[1] == "Bob" && s.names[2].empty() && s.names[3].empty());
        ASSERT_EQ(s.ignored_start_requests, 1u);                                          // (Ann's was honoured, not ignored)
        for (int i = 0; i < 3; ++i) ann.end->send(net::encode(net::StartRequestMsg{}));   // the leader's second, third and fourth click cross the Start: the running match counts them too
        w.run(500);
        ASSERT_EQ(w.status("LEAD-1").ignored_start_requests, 4u);
        ASSERT_TRUE(w.status("LEAD-1").state == RoomState::Running && ann.session != nullptr && !ann.lost);
        ASSERT_TRUE(ann.session != nullptr && bob.session != nullptr);
        ASSERT_TRUE(ann.sim.roster_mask() == 0x03 && bob.sim.roster_mask() == 0x03);      // the roster is the seats that are taken
        Client& late = w.connect("Late", "LEAD-1");                                      // a running match takes nobody new
        w.run(500);
        ASSERT_TRUE(late.lobby->phase() == net::ClientLobby::Phase::Rejected && late.lobby->reject_reason() == net::RejectReason::MatchRunning);
        // played to the end: the room finished (it would have failed with a desync) and both clients stand where the referee stands
        for (int guard = 0; guard < 4000 && w.status("LEAD-1").state == RoomState::Running; ++guard) w.run(250);
        s = w.status("LEAD-1");
        ASSERT_TRUE(s.state == RoomState::Finished);
        ASSERT_TRUE(s.ticks > 1000 && s.turns > 1000);                                    // (a turn is a tick since protocol 8: this said 500 for turns of two ticks)
        ASSERT_EQ(s.rows.size(), size_t{2});                                              // two teams: two rows
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());
        w.run(Room::kGraceMs + 500);
        ASSERT_TRUE(ann.sim.state_hash() == bob.sim.state_hash());
        ASSERT_TRUE(ann.sim.is_match_over() && bob.sim.is_match_over());
        ASSERT_EQ(ann.sim.current_tick(), bob.sim.current_tick());
    } TEST_END();

    TEST_CASE("S3.26b The Leader Moves The Colours (Protocol 14, The Swap And The Guard Of 15) In A Real Room: Cat And Bob Change Places And Bob Is Put In Black, Everybody Who Moved Is Told, The Status Shows The Seats And The Counts, The Match Runs With The Seats As They Are Now (Each Client Plays The Colour It Was Moved To) And The Clients Agree With The Referee; A Player Who Is Not The Leader Moves Nobody, Nor Does A Press That Was Made For Other Seats; A Press After The Start Is Ignored And Counted") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("MOVE-1", 4), w.now).ok);
        Client& ann = w.connect("Ann", "MOVE-1");
        w.run(100);                                                                        // (one after the other: the order of the Welcomes is the order of the seats and the lead)
        Client& bob = w.connect("Bob", "MOVE-1");
        w.run(100);
        Client& cat = w.connect("Cat", "MOVE-1");
        for (Client* c : {&ann, &bob, &cat}) c->record_hashes = true;
        w.run(500);
        RoomStatus s = w.status("MOVE-1");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 3 && s.leader == 0);
        ASSERT_TRUE(s.names[0] == "Ann" && s.names[1] == "Bob" && s.names[2] == "Cat" && s.names[3].empty());
        ASSERT_TRUE(s.seat_moves == 0 && s.ignored_seat_moves == 0);
        // Bob does not lead: a press of his (a client that is not the game's) is ignored and counted, and nobody moves
        bob.end->send(net::encode(net::SeatMoveMsg{2, 3, net::seating_hash(bob.lobby->room())}));
        w.run(500);
        s = w.status("MOVE-1");
        ASSERT_TRUE(s.names[1] == "Bob" && s.names[2] == "Cat" && s.names[3].empty() && s.seat_moves == 0 && s.ignored_seat_moves == 1);
        ASSERT_EQ(bob.lobby->phase(), net::ClientLobby::Phase::InRoom);
        for (Client* c : {&ann, &bob, &cat}) c->room_chat.clear();
        // the leader's press that was made for other seats than these (a guard that is not the room's) is ignored and counted: nobody is displaced, nobody is told
        ann.end->send(net::encode(net::SeatMoveMsg{2, 1, net::seating_hash(ann.lobby->room()) ^ 0x5A5A5A5Au}));
        w.run(500);
        s = w.status("MOVE-1");
        ASSERT_TRUE(s.names[1] == "Bob" && s.names[2] == "Cat" && s.seat_moves == 0 && s.ignored_seat_moves == 2);
        ASSERT_TRUE(ann.room_chat.empty() && bob.room_chat.empty() && cat.room_chat.empty());
        // Ann puts Cat (blue) onto Bob's colour (red): the two change places and both are told; then Bob (blue now) in black (nobody holds it)
        ASSERT_TRUE(ann.lobby->request_seat_move(2, 1));
        w.run(500);
        s = w.status("MOVE-1");
        ASSERT_TRUE(s.names[0] == "Ann" && s.names[1] == "Cat" && s.names[2] == "Bob" && s.names[3].empty() && s.seat_moves == 1 && s.ignored_seat_moves == 2);
        ASSERT_TRUE(cat.lobby->my_seat() == 1 && bob.lobby->my_seat() == 2);
        ASSERT_TRUE(bob.room_chat.size() == 1 && bob.room_chat[0].notice() && bob.room_chat[0].text == "Ann moved you to Blue.");
        ASSERT_TRUE(cat.room_chat.size() == 1 && cat.room_chat[0].notice() && cat.room_chat[0].text == "Ann moved you to Red.");
        ASSERT_TRUE(ann.lobby->request_seat_move(2, 3));
        w.run(500);
        s = w.status("MOVE-1");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 3 && s.leader == 0);
        ASSERT_TRUE(s.names[0] == "Ann" && s.names[1] == "Cat" && s.names[2].empty() && s.names[3] == "Bob");
        ASSERT_TRUE(s.seat_moves == 2 && s.ignored_seat_moves == 2);
        ASSERT_TRUE(ann.lobby->my_seat() == 0 && cat.lobby->my_seat() == 1 && bob.lobby->my_seat() == 3);
        for (Client* c : {&ann, &bob, &cat}) {                                            // every client sees the room that the server has
            ASSERT_TRUE(c->lobby->room().slots[0].name == "Ann" && c->lobby->room().slots[1].name == "Cat" && c->lobby->room().slots[3].name == "Bob");
            ASSERT_TRUE(c->lobby->room().slots[2].state == net::SlotState::Empty && c->lobby->room().leader == 0);
        }
        ASSERT_TRUE(ann.room_chat.empty());                                                // the leader is told nothing
        ASSERT_TRUE(bob.room_chat.size() == 2 && bob.room_chat[1].notice() && bob.room_chat[1].text == "Ann moved you to Black.");      // (Bob's first notice is the swap's)
        ASSERT_TRUE(cat.room_chat.size() == 1 && cat.room_chat[0].notice() && cat.room_chat[0].text == "Ann moved you to Red.");
        // the match starts from the seats as they are now
        ASSERT_TRUE(ann.lobby->request_start());
        w.run(1500);
        s = w.status("MOVE-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.joined == 3);
        ASSERT_TRUE(s.names[0] == "Ann" && s.names[1] == "Cat" && s.names[2].empty() && s.names[3] == "Bob");
        ASSERT_TRUE(ann.sim.roster_mask() == 0x0B && bob.sim.roster_mask() == 0x0B && cat.sim.roster_mask() == 0x0B);       // green, red and black play
        ASSERT_TRUE(ann.session != nullptr && bob.session != nullptr && cat.session != nullptr);
        ASSERT_TRUE(ann.session->player() == 0 && cat.session->player() == 1 && bob.session->player() == 3);              // each client plays the colour it was moved to
        // the leader's press that crossed the Start: the running match ignores it and counts it, and the match is as it was
        ann.end->send(net::encode(net::SeatMoveMsg{3, 2, net::seating_hash(ann.lobby->room())}));
        w.run(500);
        ASSERT_EQ(w.status("MOVE-1").ignored_seat_moves, 3u);
        ASSERT_TRUE(w.status("MOVE-1").state == RoomState::Running && ann.session != nullptr && !ann.lost && !bob.lost && !cat.lost);
        w.run(30000);                                                                      // some 500 turns of play (after the start dialog's five seconds)
        s = w.status("MOVE-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.ticks > 400 && s.seat_moves == 2 && s.leader == 255);
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced() || cat.session->desynced());
        size_t compared = 0;                                                               // the clients stand where each other stand: every tick that two of them ran has one state
        for (const auto& pair : {std::make_pair(&ann, &bob), std::make_pair(&ann, &cat), std::make_pair(&bob, &cat)}) {
            for (const auto& at : pair.first->hash_at) {
                const auto other = pair.second->hash_at.find(at.first);
                if (other == pair.second->hash_at.end()) continue;
                ASSERT_EQ(other->second, at.second);
                ++compared;
            }
        }
        ASSERT_TRUE(compared > 600);
    } TEST_END();

    TEST_CASE("S3.33 The Leader And The Lag Policy: A Waiting Room Has No Lag And No Notice; A Leader Whose Window Stops After The Start Does Not Hold The Room Up, The Other Player Is Told That It Lags, Its Late Clicks On START Are Counted And Cost It Nothing, It Catches Up (\"Catching Up\") And Is Not Dropped; Both End Bit-Identical") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("LEAD-LAG", 4), w.now).ok);
        Client& ann = w.connect("Ann", "LEAD-LAG");
        Client& bob = w.connect("Bob", "LEAD-LAG");
        w.run(500);
        ASSERT_TRUE(ann.lobby->is_leader() && !bob.lobby->is_leader());
        w.run(20000);                                                                       // twenty seconds in the waiting room: nothing is sealed, so nobody is behind, nobody is told, nobody is dropped
        ASSERT_TRUE(w.status("LEAD-LAG").state == RoomState::Waiting && w.status("LEAD-LAG").joined == 2);
        ASSERT_TRUE(ann.session == nullptr && bob.session == nullptr);
        ASSERT_TRUE(ann.lobby->is_leader() && ann.lobby->phase() == net::ClientLobby::Phase::InRoom);
        ASSERT_TRUE(ann.lobby->request_start());
        w.run(3000 + kPre);
        ASSERT_TRUE(w.status("LEAD-LAG").state == RoomState::Running);
        ASSERT_TRUE(ann.session != nullptr && bob.session != nullptr);
        ASSERT_EQ(w.status("LEAD-LAG").leader, 255);                                        // once the match runs nobody leads: the lobby is over
        ASSERT_EQ(bob.session->lagging_seat(), 255);
        // the leader's window stops for 8 s; three clicks on START that it made in its last moment reach the server late
        const uint32_t ticks_before = w.status("LEAD-LAG").ticks;
        ann.freeze = true;
        for (int i = 0; i < 3; ++i) ann.end->send(net::encode(net::StartRequestMsg{}));
        w.run(8000);
        ASSERT_TRUE(w.status("LEAD-LAG").ticks - ticks_before >= 150);                      // the room did not wait for it: 8 s are 160 ticks, and Bob played them
        ASSERT_EQ(bob.session->lagging_seat(), 0);                                          // Bob is told who lags: the seat of the first player
        ASSERT_TRUE(bob.session->lagging_behind_ms() >= 3000);
        ASSERT_EQ(w.status("LEAD-LAG").ignored_start_requests, 3u);                         // the three clicks are counted, they were free (16 are), nothing else came of them
        ASSERT_TRUE(w.status("LEAD-LAG").state == RoomState::Running);
        ann.freeze = false;
        w.run(300);
        ASSERT_TRUE(ann.session->catching_up());                                            // "Catching up..." on the leader's screen, until it is within a second
        w.run(7000);
        ASSERT_FALSE(ann.session->catching_up());
        ASSERT_EQ(bob.session->lagging_seat(), 255);                                        // and the notice of the other player ended
        ASSERT_FALSE(ann.lost || bob.lost);
        ASSERT_TRUE(w.status("LEAD-LAG").state == RoomState::Running);
        ASSERT_TRUE(ann.sim.current_tick() + 40 >= bob.sim.current_tick());                 // level (within the turns that are on the way)
        // played to the end: both clients stand where the referee stands, and the room did not fail with a desync
        for (int guard = 0; guard < 4000 && w.status("LEAD-LAG").state == RoomState::Running; ++guard) w.run(250);
        ASSERT_TRUE(w.status("LEAD-LAG").state == RoomState::Finished);
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());
        w.run(Room::kGraceMs + 500);
        ASSERT_TRUE(ann.sim.state_hash() == bob.sim.state_hash());
        ASSERT_EQ(ann.sim.current_tick(), bob.sim.current_tick());
    } TEST_END();

    TEST_CASE("S3.27 A Request That Cannot Be Honoured Does Nothing: One Player Alone, A Room That Does Not Allow An Early Start (No Leader); A Room Whose Seats Are All Taken Still Starts By Itself") {
        {   // one player alone: nothing happens however often the leader asks, and the leader's screen stays (it is not thrown out)
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("ONE-1", 4), w.now).ok);
            Client& ann = w.connect("Ann", "ONE-1");
            w.run(300);
            for (int i = 0; i < 5; ++i) {
                ASSERT_TRUE(ann.lobby->request_start());
                w.run(100);
            }
            w.run(1500);
            RoomStatus s = w.status("ONE-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 1 && s.ignored_start_requests == 5);
            ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);
            w.connect("Bob", "ONE-1");                                                    // a second player: the next request works
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start());
            w.run(1500);
            ASSERT_TRUE(w.status("ONE-1").state == RoomState::Running);
        }
        {   // early_start off: the room has no leader, the request of a modified client is ignored and counted, and the room starts when every seat is taken
            World w;
            RoomSpec spec = spec_of("NOEARLY-1", 3);
            spec.early_start = false;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            Client& ann = w.connect("Ann", "NOEARLY-1");
            Client& bob = w.connect("Bob", "NOEARLY-1");
            w.run(500);
            RoomStatus s = w.status("NOEARLY-1");
            ASSERT_TRUE(!s.early_start && s.leader == 255);
            ASSERT_TRUE(ann.lobby->room().leader == 255 && !ann.lobby->is_leader());     // nobody is told that they lead
            ASSERT_FALSE(ann.lobby->request_start());                                     // so the client sends nothing
            ann.end->send(net::encode(net::StartRequestMsg{}));
            bob.end->send(net::encode(net::StartRequestMsg{}));
            w.run(2000);
            s = w.status("NOEARLY-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.ignored_start_requests == 2);
            ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);
            w.connect("Cat", "NOEARLY-1");                                                // the third seat: it starts by itself, as ever
            w.run(1500);
            ASSERT_TRUE(w.status("NOEARLY-1").state == RoomState::Running);
        }
        {   // protocol 15's rule that a full room waits for its leader needs a leader: a room that allows no early start has none, so the rule is no rule there (not told, not shown) and the full room starts by itself
            World w;
            RoomSpec spec = spec_of("NOLEAD-1", 3);
            spec.early_start = false;
            spec.leader_starts = true;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            Client& ann = w.connect("Ann", "NOLEAD-1");
            w.connect("Bob", "NOLEAD-1");
            w.run(500);
            const RoomStatus s = w.status("NOLEAD-1");
            ASSERT_TRUE(!s.early_start && s.leader == 255 && !s.leader_starts);
            ASSERT_TRUE(!ann.lobby->room().leader_starts() && !ann.lobby->is_leader());
            w.connect("Cat", "NOLEAD-1");
            w.run(1500);
            ASSERT_TRUE(w.status("NOLEAD-1").state == RoomState::Running);
        }
        {   // every seat taken: the room starts by itself, nobody has to ask (and nothing is counted)
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("FULL-1", 3), w.now).ok);
            for (const char* name : {"Ann", "Bob", "Cat"}) w.connect(name, "FULL-1");
            w.run(2000);
            const RoomStatus s = w.status("FULL-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 3 && s.ignored_start_requests == 0);
        }
        {   // two of three with no request: the room waits (min_players is for the leader's request, not for the room)
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("TWO-1", 3), w.now).ok);
            w.connect("Ann", "TWO-1");
            w.connect("Bob", "TWO-1");
            w.run(5000);
            ASSERT_TRUE(w.status("TWO-1").state == RoomState::Waiting);
        }
    } TEST_END();

    TEST_CASE("S3.28 The Leader Leaves: The Earliest Player Who Is Left Leads And Can Start; The Seat That Was Left Is Free And Is Not In The Roster") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("LEFT-1", 4), w.now).ok);
        Client& ann = w.connect("Ann", "LEFT-1");
        Client& bob = w.connect("Bob", "LEFT-1");
        Client& cat = w.connect("Cat", "LEFT-1");
        w.run(500);
        ASSERT_EQ(w.status("LEFT-1").leader, 0);
        ann.lobby->leave();                                                               // the leader goes
        w.run(500);
        RoomStatus s = w.status("LEFT-1");
        ASSERT_TRUE(s.joined == 2 && s.leader == 1 && s.state == RoomState::Waiting);     // Bob, the earlier of Bob and Cat
        ASSERT_TRUE(bob.lobby->is_leader() && !cat.lobby->is_leader());
        ASSERT_TRUE(bob.lobby->room().leader == 1 && cat.lobby->room().leader == 1);      // both were told with the next Room message
        cat.end->send(net::encode(net::StartRequestMsg{}));                               // Cat does not lead: ignored
        w.run(1500);
        s = w.status("LEFT-1");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.ignored_start_requests == 1);
        ASSERT_TRUE(bob.lobby->request_start());                                          // Bob starts with himself and Cat: seats 1 and 2
        w.run(1500);
        s = w.status("LEFT-1");
        ASSERT_TRUE(s.state == RoomState::Running);
        ASSERT_TRUE(s.names[0].empty() && s.names[1] == "Bob" && s.names[2] == "Cat" && s.names[3].empty());
        ASSERT_TRUE(bob.sim.roster_mask() == 0x06 && cat.sim.roster_mask() == 0x06);
        w.run(5000 + kPre);
        ASSERT_FALSE(bob.session->desynced() || cat.session->desynced());
        ASSERT_TRUE(w.status("LEFT-1").state == RoomState::Running && w.status("LEFT-1").ticks > 60);
        {   // a leader who asks and leaves in the same breath (both reach the room in one pass) asked for nothing: the room does not start with the two who are left
            World v;
            ASSERT_TRUE(v.mgr.create_room(spec_of("LEFT-2", 4), v.now).ok);
            Client& a = v.connect("Ann", "LEFT-2", 255, {20, 0});                          // (no jitter: the two messages arrive together)
            Client& b = v.connect("Bob", "LEFT-2");
            v.connect("Cat", "LEFT-2");
            v.run(500);
            a.end->send(net::encode(net::StartRequestMsg{}));
            a.end->send(net::encode_leave());
            v.run(2500);
            const RoomStatus t = v.status("LEFT-2");
            ASSERT_TRUE(t.state == RoomState::Waiting && t.joined == 2 && t.leader == 1);
            ASSERT_TRUE(b.lobby->is_leader());                                              // Bob leads now, and did not ask for anything
            ASSERT_TRUE(b.lobby->request_start());                                          // when he does, the room starts
            v.run(1500);
            ASSERT_TRUE(v.status("LEFT-2").state == RoomState::Running);
        }
    } TEST_END();

    TEST_CASE("S3.29 The Early Start Meets The Room's Other Rules: A Client That Cannot Load Cancels It And The Leader Asks Again After The Pause (A Request In The Pause Is Lost); Seats The Map Cannot Be Played By Do Not Start Early And Do Not Fail The Room") {
        {
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("PAUSE-1", 4), w.now).ok);
            Client& ann = w.connect("Ann", "PAUSE-1");
            Client& bob = w.connect("Bob", "PAUSE-1");
            bob.fail_load = true;                                                          // Bob's copy of the map is no good
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start());
            w.run(1000);
            ASSERT_TRUE(w.status("PAUSE-1").state == RoomState::Waiting);                  // the start was cancelled, the room waits a moment (two seconds) before another try
            bob.fail_load = false;                                                         // Bob's map is fine now: a start would work, if the room tried
            ASSERT_TRUE(ann.lobby->request_start());                                       // a request in the pause is lost (it does not start the match, and it is not kept for later)
            w.run(500);
            ASSERT_TRUE(w.status("PAUSE-1").state == RoomState::Waiting);
            w.run(2500);                                                                   // the pause is over, and nobody asked again: two of four players do not start by themselves
            ASSERT_TRUE(w.status("PAUSE-1").state == RoomState::Waiting);
            ASSERT_TRUE(ann.lobby->request_start());
            w.run(2000);
            ASSERT_TRUE(w.status("PAUSE-1").state == RoomState::Running);
        }
        // TINY with the green start marker moved outside the grid (as in S3.14): the engine loads it, but green cannot play it
        const std::string dir = temp_dir_for("early_marker");
        {
            std::ifstream in(maps_dir() + "/TINY.LVL", std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            assets::LevelData tiny;
            ASSERT_TRUE(tiny.load_from_memory(bytes.data(), bytes.size()));
            const assets::AnthillSpawn* green = nullptr;
            for (const auto& sp : tiny.anthill_spawns) {
                if (sp.tile_id == 154) green = &sp;
            }
            ASSERT_TRUE(green != nullptr);
            const uint8_t pattern[6] = {154, 0, static_cast<uint8_t>(green->y & 0xFF), static_cast<uint8_t>(green->y >> 8), static_cast<uint8_t>(green->x & 0xFF), static_cast<uint8_t>(green->x >> 8)};
            size_t at = bytes.size();
            for (size_t i = 0; i + 6 <= bytes.size() && at == bytes.size(); ++i) {
                if (std::equal(pattern, pattern + 6, bytes.begin() + static_cast<std::ptrdiff_t>(i))) at = i;
            }
            ASSERT_TRUE(at < bytes.size());
            bytes[at + 2] = 200;
            bytes[at + 3] = 0;
            std::ofstream out(fs::path(dir) / "BAD.LVL", std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        const auto run_in = [&dir](World& w, uint32_t ms) {                                // (the clients load the map from this folder)
            for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {
                w.now += 10;
                w.net.set_time(w.now);
                w.mgr.update(w.now);
                for (auto& c : w.clients) c->update(w.now, dir);
            }
        };
        {   // green (seat 0) and red in a room for four: the leader's request is not honoured (green cannot play this map), the room does not fail, nobody is thrown out
            World w(ServerLimits(), dir);
            ASSERT_TRUE(w.mgr.create_room(spec_of("MARK-1", 4, "BAD.LVL"), w.now).ok);
            Client& ann = w.connect("Ann", "MARK-1", 0);
            w.connect("Bob", "MARK-1", 1);
            run_in(w, 500);
            ASSERT_TRUE(ann.lobby->request_start());
            run_in(w, 2000);
            RoomStatus s = w.status("MARK-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 2);
            ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);
            w.connect("Cat", "MARK-1", 2);                                                // with more players still waiting, the room goes on waiting
            w.connect("Dan", "MARK-1", 3);                                                // when every seat is taken (green among them) the room fails, as it always did
            run_in(w, 2000);
            s = w.status("MARK-1");
            ASSERT_TRUE(s.state == RoomState::Failed && s.reason.find("outside") != std::string::npos);
        }
        {   // red and blue: the roster does not contain green, the same map starts early
            World w(ServerLimits(), dir);
            ASSERT_TRUE(w.mgr.create_room(spec_of("MARK-2", 4, "BAD.LVL"), w.now).ok);
            Client& bob = w.connect("Bob", "MARK-2", 1);
            w.connect("Cat", "MARK-2", 2);
            run_in(w, 500);
            ASSERT_TRUE(bob.lobby->request_start());
            run_in(w, 2000);
            ASSERT_TRUE(w.status("MARK-2").state == RoomState::Running);
        }
        std::error_code ignore;
        fs::remove_all(dir, ignore);
    } TEST_END();

    TEST_CASE("S3.30 The Control Interface Knows The Early Start: early_start Goes In And Comes Out (On By Default; A Wrong Type Is A 400), The Status Names The Leader And Counts The Requests That Were Ignored; Public Rooms Have It On And Their Leader Starts Them") {
        ServerLimits limits;
        limits.demo_rooms = 2;
        limits.demo_map = "TINY.LVL";
        World w(limits);
        const auto call = [&w](const char* method, const std::string& path, const std::string& body = std::string()) {
            ctl::HttpRequest rq;
            rq.method = method;
            rq.path = path;
            rq.body = body;
            return handle_control(w.mgr, rq, w.now);
        };
        const auto json_of = [](const ctl::HttpResponse& r) {
            ctl::JsonValue v;
            std::string why;
            ctl::parse_json(r.body, v, &why);
            return v;
        };
        ctl::HttpResponse r = call("POST", "/rooms", R"({"map":"TINY.LVL","players":4,"code":"J-1"})");
        ASSERT_EQ(r.status, 201);
        ctl::JsonValue v = json_of(r);
        ASSERT_TRUE(v.get("early_start").is_bool() && v.get("early_start").as_bool_or(false));   // the default: on
        ASSERT_TRUE(v.get("leader").is_null() && v.get("ignored_start_requests").as_int_or(9) == 0);
        ASSERT_TRUE(v.get("seat_moves").as_int_or(9) == 0 && v.get("ignored_seat_moves").as_int_or(9) == 0);       // (protocol 14: the colours that the leader moved, and the moves that were heard and not done)
        r = call("POST", "/rooms", R"({"map":"TINY.LVL","players":4,"code":"J-2","early_start":false})");
        ASSERT_EQ(r.status, 201);
        v = json_of(r);
        ASSERT_TRUE(v.get("early_start").is_bool() && !v.get("early_start").as_bool_or(true));
        r = call("POST", "/rooms", R"({"map":"TINY.LVL","players":4,"code":"J-3","early_start":true})");
        ASSERT_EQ(r.status, 201);
        ASSERT_TRUE(json_of(r).get("early_start").as_bool_or(false));
        for (const char* bad : {R"("yes")", "1", "0", "null", "[true]", "{}", R"("")"}) {          // only true or false
            r = call("POST", "/rooms", std::string(R"({"map":"TINY.LVL","early_start":)") + bad + "}");
            ASSERT_MSG(r.status == 400, bad);
            ASSERT_TRUE(json_of(r).get("error").str().find("early_start") != std::string::npos);
        }
        ASSERT_EQ(w.mgr.room_count(), size_t{3});                                         // (none of the bad ones made a room)
        r = call("GET", "/rooms/J-2");
        ASSERT_TRUE(r.status == 200 && !json_of(r).get("early_start").as_bool_or(true));
        r = call("GET", "/rooms");
        ASSERT_TRUE(r.status == 200 && json_of(r).get("rooms").size() == 3);
        int on = 0;
        const ctl::JsonValue all = json_of(r);
        for (size_t i = 0; i < all.get("rooms").size(); ++i) on += all.get("rooms").at(i).get("early_start").as_bool_or(false) ? 1 : 0;
        ASSERT_EQ(on, 2);
        // the leader and the ignored requests, seen through the interface
        Client& ann = w.connect("Ann", "J-1");
        Client& bob = w.connect("Bob", "J-1");
        w.run(500);
        v = json_of(call("GET", "/rooms/J-1"));
        ASSERT_TRUE(v.get("leader").as_int_or(9) == 0 && v.get("joined").as_int_or(0) == 2 && v.get("players").size() == 2);
        bob.end->send(net::encode(net::StartRequestMsg{}));
        w.run(500);
        ASSERT_EQ(json_of(call("GET", "/rooms/J-1")).get("ignored_start_requests").as_int_or(0), 1);
        bob.end->send(net::encode(net::SeatMoveMsg{1, 3, net::seating_hash(bob.lobby->room())}));       // the same for a move: Bob does not lead, the press is counted and nobody moves
        w.run(500);
        v = json_of(call("GET", "/rooms/J-1"));
        ASSERT_TRUE(v.get("ignored_seat_moves").as_int_or(0) == 1 && v.get("seat_moves").as_int_or(9) == 0 && v.get("players").size() == 2);
        ASSERT_TRUE(ann.lobby->request_seat_move(1, 3));                                  // the leader's does move him, and the interface counts it
        w.run(500);
        v = json_of(call("GET", "/rooms/J-1"));
        ASSERT_TRUE(v.get("seat_moves").as_int_or(0) == 1 && v.get("ignored_seat_moves").as_int_or(0) == 1 && v.get("leader").as_int_or(9) == 0);
        ASSERT_EQ(v.get("players").at(1).get("seat").as_int_or(9), 3);
        ASSERT_EQ(v.get("players").at(1).get("name").str(), std::string("Bob"));
        ASSERT_TRUE(ann.lobby->request_start());
        w.run(1500);
        v = json_of(call("GET", "/rooms/J-1"));
        ASSERT_TRUE(v.get("state").str() == "running" && v.get("expected").as_int_or(0) == 4 && v.get("joined").as_int_or(0) == 2);
        ASSERT_TRUE(v.get("early_start").as_bool_or(false));
        ASSERT_TRUE(v.get("leader").is_null());                                           // the lead means something until the match runs
        // a room with early_start off names no leader, and says so
        Client& cat = w.connect("Cat", "J-2");
        w.connect("Dan", "J-2");
        w.run(500);
        v = json_of(call("GET", "/rooms/J-2"));
        ASSERT_TRUE(v.get("leader").is_null() && !v.get("early_start").as_bool_or(true) && v.get("joined").as_int_or(0) == 2);
        ASSERT_FALSE(cat.lobby->is_leader());
        // a public room has the early start on, and its leader can start it
        Client& eve = w.connect_creating("Eve", "early0001", block_of("", 4));
        w.run(100);                                                                       // (Eve is first: the Hellos of two connections made in one tick arrive in either order, by the links' jitter)
        w.connect("Fay", "early0001");
        w.run(500);
        v = json_of(call("GET", "/rooms/early0001"));
        ASSERT_TRUE(v.get("early_start").as_bool_or(false) && v.get("leader").as_int_or(9) == 0 && v.get("state").str() == "waiting");
        ASSERT_TRUE(eve.lobby->is_leader());
        ASSERT_TRUE(eve.lobby->request_start());
        w.run(1500);
        ASSERT_TRUE(json_of(call("GET", "/rooms/early0001")).get("state").str() == "running");
    } TEST_END();

    TEST_CASE("S3.31 Over Real Sockets: The Leader Of A Room For Four Starts It With Two Players, Both Play Bit-Identically") {
        RoomManager mgr{MapStore(maps_dir())};
        auto listener = net::TcpListener::listen(0, true);
        ASSERT_TRUE(listener != nullptr);
        uint32_t now = 1000;
        ASSERT_TRUE(mgr.create_room(spec_of("SOCK-4", 4), now).ok);
        std::vector<std::unique_ptr<Client>> clients;
        std::vector<std::unique_ptr<net::TcpConnection>> links;
        const auto pump = [&]() {
            now += 10;
            for (int k = 0; k < 4; ++k) {
                auto c = listener->accept();
                if (!c) break;
                mgr.add_connection(std::move(c), "127.0.0.1", now);
            }
            mgr.update(now);
            for (auto& c : clients) c->update(now, maps_dir());
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        };
        for (const char* name : {"Ann", "Bob"}) {
            links.push_back(net::TcpConnection::connect("127.0.0.1", listener->port()));
            ASSERT_TRUE(links.back() != nullptr);
            clients.push_back(std::make_unique<Client>());
            clients.back()->name = name;
            clients.back()->room = "SOCK-4";
            clients.back()->start(links.back().get(), 55u);
            // Ann is in, and the leader, before Bob connects: two Hellos sent at once over two sockets can be read in either order (a Mac's loopback is handed over by a kernel thread), and the first one in is the leader
            for (int i = 0; i < 3000 && !clients[0]->lobby->is_leader(); ++i) pump();
        }
        for (int i = 0; i < 3000; ++i) {                                                  // both are in the room (the room waits for four)
            pump();
            RoomStatus s;
            mgr.status("SOCK-4", s, now);
            if (s.joined == 2 && clients[0]->lobby->room().slots[1].state == net::SlotState::Client && clients[0]->lobby->is_leader()) break;
        }
        RoomStatus s;
        ASSERT_TRUE(mgr.status("SOCK-4", s, now));
        ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 2 && s.leader < 4);
        ASSERT_TRUE(clients[0]->lobby->is_leader() && !clients[1]->lobby->is_leader());
        for (int i = 0; i < 100; ++i) pump();
        ASSERT_TRUE(clients[0]->lobby->request_start());
        for (int i = 0; i < 6000; ++i) {
            pump();
            mgr.status("SOCK-4", s, now);
            if (s.state == RoomState::Running && s.ticks > 400) break;
        }
        ASSERT_TRUE(s.state == RoomState::Running && s.ticks > 400 && s.joined == 2 && s.expected == 4);
        ASSERT_FALSE(clients[0]->session->desynced() || clients[1]->session->desynced());
        ASSERT_TRUE(clients[0]->sim.roster_mask() == 0x03 && clients[1]->sim.roster_mask() == 0x03);
        bool compared = false;
        for (int i = 0; i < 400 && !compared; ++i) {
            pump();
            if (clients[0]->sim.current_tick() == clients[1]->sim.current_tick()) {
                ASSERT_TRUE(clients[0]->sim.state_hash() == clients[1]->sim.state_hash());
                compared = true;
            }
        }
        ASSERT_TRUE(compared);
    } TEST_END();
}

void run_control_tests() {
    TEST_CASE("S3.8 Control Interface: Make, Look At, List And Close Rooms With JSON; Every Mistake Gets An Error Object And The Right Status") {
        RoomManager mgr{MapStore(maps_dir())};
        auto call = [&](const char* method, const std::string& path, const std::string& body = std::string()) {
            ctl::HttpRequest rq;
            rq.method = method;
            rq.path = path;
            rq.body = body;
            return handle_control(mgr, rq, 5000);
        };
        auto json_of = [](const ctl::HttpResponse& r) {
            ctl::JsonValue v;
            std::string why;
            ctl::parse_json(r.body, v, &why);                                            // (a body that does not parse gives null: the asserts below then fail)
            return v;
        };
        ctl::HttpResponse r = call("POST", "/rooms", R"({"map":"TINY.LVL","players":3,"fog":true,"code":"CTL-1","seed":7,"wait_seconds":30})");
        ASSERT_EQ(r.status, 201);
        ctl::JsonValue v = json_of(r);
        ASSERT_TRUE(v.get("code").str() == "CTL-1" && v.get("state").str() == "waiting" && v.get("expected").as_int_or(0) == 3 && v.get("fog").as_bool_or(false));
        ASSERT_TRUE(v.get("map").str() == "TINY.LVL" && v.get("joined").as_int_or(9) == 0 && v.get("players").size() == 0);
        r = call("GET", "/rooms/CTL-1");
        ASSERT_EQ(r.status, 200);
        ASSERT_TRUE(json_of(r).get("code").str() == "CTL-1");
        r = call("POST", "/rooms", R"({"map":"SMALL.LVL"})");                           // the code is drawn
        ASSERT_EQ(r.status, 201);
        const std::string drawn = json_of(r).get("code").str();
        ASSERT_TRUE(drawn.size() == kDrawnRoomCodeChars && net::valid_room_code(drawn));
        r = call("GET", "/rooms");
        ASSERT_TRUE(r.status == 200 && json_of(r).get("rooms").size() == 2);
        r = call("GET", "/stats");
        ASSERT_TRUE(r.status == 200 && json_of(r).get("rooms").as_int_or(0) == 2 && json_of(r).get("created").as_int_or(0) == 2);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","code":"CTL-1"})").status, 409);
        ASSERT_EQ(call("POST", "/rooms", "").status, 400);
        ASSERT_EQ(call("POST", "/rooms", "{not json").status, 400);
        ASSERT_EQ(call("POST", "/rooms", "[1,2]").status, 400);
        ASSERT_EQ(call("POST", "/rooms", R"({"players":2})").status, 400);               // no map
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","players":"two"})").status, 400);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","players":9})").status, 400);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","fog":"yes"})").status, 400);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","wait_seconds":0})").status, 400);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","max_run_seconds":59})").status, 400);          // a match lasts at least a minute
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","max_run_seconds":86401})").status, 400);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","seed":-1})").status, 400);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"../../etc/passwd"})").status, 404);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"NOSUCH.LVL"})").status, 404);
        ASSERT_EQ(call("GET", "/rooms/NOPE").status, 404);
        ASSERT_EQ(call("GET", "/rooms/").status, 404);
        ASSERT_EQ(call("GET", "/rooms/../stats").status, 404);
        ASSERT_EQ(call("GET", "/rooms/a%2Fb").status, 404);
        ASSERT_EQ(call("PUT", "/rooms/CTL-1").status, 405);
        ASSERT_EQ(call("DELETE", "/rooms").status, 405);
        ASSERT_EQ(call("GET", "/nothing").status, 404);
        r = call("DELETE", "/rooms/CTL-1");
        ASSERT_TRUE(r.status == 200 && json_of(r).get("state").str() == "failed" && json_of(r).get("reason").str() == "closed by the owner");
        ASSERT_EQ(call("DELETE", "/rooms/NOPE").status, 404);
        // every error body is a JSON object with an "error" text
        r = call("POST", "/rooms", "{not json");
        ASSERT_TRUE(json_of(r).get("error").is_string());
    } TEST_END();
}

void run_socket_tests() {
    TEST_CASE("S3.9 Over Real Sockets: Two Clients Connect To The Server's TCP Port, Join A Room By Its Code And Play Bit-Identically") {
        RoomManager mgr{MapStore(maps_dir())};
        auto listener = net::TcpListener::listen(0, true);
        ASSERT_TRUE(listener != nullptr);
        uint32_t now = 1000;
        ASSERT_TRUE(mgr.create_room(spec_of("SOCK-1", 2), now).ok);
        std::vector<std::unique_ptr<Client>> clients;
        std::vector<std::unique_ptr<net::TcpConnection>> links;
        for (const char* name : {"Ann", "Bob"}) {
            links.push_back(net::TcpConnection::connect("127.0.0.1", listener->port()));
            ASSERT_TRUE(links.back() != nullptr);
            clients.push_back(std::make_unique<Client>());
            clients.back()->name = name;
            clients.back()->room = "SOCK-1";
            clients.back()->start(links.back().get(), 77u);
        }
        for (int i = 0; i < 6000; ++i) {
            now += 10;
            for (int k = 0; k < 4; ++k) {
                auto c = listener->accept();
                if (!c) break;
                mgr.add_connection(std::move(c), "127.0.0.1", now);
            }
            mgr.update(now);
            for (auto& c : clients) c->update(now, maps_dir());
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            RoomStatus s;
            mgr.status("SOCK-1", s, now);
            if (s.state == RoomState::Running && s.ticks > 400) break;
        }
        RoomStatus s;
        ASSERT_TRUE(mgr.status("SOCK-1", s, now));
        ASSERT_TRUE(s.state == RoomState::Running);
        ASSERT_TRUE(s.ticks > 400);
        ASSERT_FALSE(clients[0]->session->desynced() || clients[1]->session->desynced());
        // the two machines are bit-identical at any moment when they stand at the same tick
        bool compared = false;
        for (int i = 0; i < 400 && !compared; ++i) {
            now += 10;
            mgr.update(now);
            for (auto& c : clients) c->update(now, maps_dir());
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            if (clients[0]->sim.current_tick() == clients[1]->sim.current_tick()) {
                ASSERT_TRUE(clients[0]->sim.state_hash() == clients[1]->sim.state_hash());
                compared = true;
            }
        }
        ASSERT_TRUE(compared);
    } TEST_END();

#ifndef _WIN32
    TEST_CASE("S3.32 Over Real Sockets: A Raw Client That Floods The Server With Valid Messages (StartRequests, Pings) Is Dropped Within Two Seconds; The Server Does Not Grow, Never Stalls, Falls Back To Idle, And A Match In Another Room Keeps Its Clock Of 20 Ticks A Second") {
        std::signal(SIGPIPE, SIG_IGN);
        for (const std::string kind : {"startreq", "ping"}) {
            RoomManager mgr{MapStore(maps_dir())};
            auto listener = net::TcpListener::listen(0, true);
            ASSERT_TRUE(listener != nullptr);
            const auto t0 = std::chrono::steady_clock::now();
            const auto clock_ms = [&]() { return 1000u + static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()); };
            uint32_t now = clock_ms();
            ASSERT_TRUE(mgr.create_room(spec_of("VICTIM", 2), now).ok);
            ASSERT_TRUE(mgr.create_room(spec_of("FLOOD", 4), now).ok);
            std::vector<std::unique_ptr<Client>> clients;
            std::vector<std::unique_ptr<net::TcpConnection>> links;
            for (const char* name : {"Ann", "Bob"}) {
                links.push_back(net::TcpConnection::connect("127.0.0.1", listener->port()));
                ASSERT_TRUE(links.back() != nullptr);
                clients.push_back(std::make_unique<Client>());
                clients.back()->name = name;
                clients.back()->room = "VICTIM";
                clients.back()->start(links.back().get(), 91u);
            }
            double max_iteration_ms = 0;
            const auto pump = [&]() {                                                   // the server's main loop: one pass, then the 2 ms nap of ants_server
                const auto begin = std::chrono::steady_clock::now();
                now = clock_ms();
                for (int k = 0; k < 4; ++k) {
                    auto c = listener->accept();
                    if (!c) break;
                    mgr.add_connection(std::move(c), "127.0.0.1", now);
                }
                mgr.update(now);
                for (auto& c : clients) c->update(now, maps_dir());
                max_iteration_ms = std::max(max_iteration_ms, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            };
            const auto ticks_of_victim = [&]() {
                RoomStatus v;
                mgr.status("VICTIM", v, now);
                return v.ticks;
            };
            RoomStatus s;
            for (int i = 0; i < 4000; ++i) {
                pump();
                mgr.status("VICTIM", s, now);
                if (s.state == RoomState::Running && s.ticks > 60) break;
            }
            ASSERT_TRUE(s.state == RoomState::Running && s.ticks > 60);
            // the clock of the victim's referee without a flood: 20 ticks a second
            uint32_t ticks_a = ticks_of_victim();
            const auto timer_a = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - timer_a < std::chrono::milliseconds(1500)) pump();
            const double quiet_rate = (ticks_of_victim() - ticks_a) / std::chrono::duration<double>(std::chrono::steady_clock::now() - timer_a).count();
            ASSERT_TRUE(quiet_rate > 17.0 && quiet_rate < 23.0);
            // the flood: a raw client in the other room
            max_iteration_ms = 0;
            const double memory_before = peak_memory_mb();
            ticks_a = ticks_of_victim();
            const auto timer_b = std::chrono::steady_clock::now();
            const std::vector<uint8_t> message = kind == "startreq" ? net::encode(net::StartRequestMsg{}) : net::encode_ping(net::PingMsg{1, 0});
            FloodPeer flood(listener->port(), "FLOOD", message);
            ASSERT_TRUE(flood.sock >= 0);
            double dropped_after = -1.0;                                                // when the room lost the flooder it had seated, seconds after the flood began
            bool seated = false;
            uint32_t pongs = 0;
            while (std::chrono::steady_clock::now() - timer_b < std::chrono::milliseconds(2500)) {       // (a fixed window: the flooder is gone long before it ends)
                pump();
                flood.read_server(pongs);
                RoomStatus r;
                mgr.status("FLOOD", r, now);
                seated = seated || r.joined > 0;
                if (seated && r.joined == 0 && dropped_after < 0.0) dropped_after = std::chrono::duration<double>(std::chrono::steady_clock::now() - timer_b).count();
            }
            const double flood_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - timer_b).count();
            ASSERT_EQ(mgr.connections_refused(), uint64_t{0});                          // (the flooder's Hello was taken: it had a seat)
            if (kind == "ping") {
                // the pings need at least 16 updates (64 of a connection per update) to use up the budget, so the seat is seen, and then the drop: within two seconds
                ASSERT_TRUE(seated);
                // a second's worth was answered, not the millions that were sent: the burst, and what the budget gave back while the flood lasted (kMessagesPerSecond a second of the clock: the drain took 0.1 to 0.7 s
                // on the macOS runners, 1,100 to 1,600 pongs, and the loop may stall for 400 ms below, which gives back all it lasts in one update); the clock above (timer_b) began before the flooder did (its connect and
                // its Hello are inside the time), and the pongs that the reset takes away are not counted, so the bound is a little wide
                const double most_pongs = net::kMessageBurst + net::kMessagesPerSecond * dropped_after + 2;
                std::cout << "\n      [measured] the flooder was answered with " << pongs << " pongs (from " << net::kMessageBurst << " to " << most_pongs << ") and dropped after " << dropped_after << " s" << std::flush;
                ASSERT_TRUE(dropped_after >= 0.0 && dropped_after < 2.0);
                ASSERT_TRUE(pongs >= net::kMessageBurst);
                ASSERT_TRUE(pongs <= most_pongs);
            }
            const double flood_rate = (ticks_of_victim() - ticks_a) / flood_seconds;
            // after the drop the loop has nothing to digest: it is idle again, the victim keeps its clock, and nothing grew
            const double cpu_before = process_cpu_seconds();
            const uint32_t ticks_c = ticks_of_victim();
            const auto timer_c = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - timer_c < std::chrono::milliseconds(1500)) pump();
            const double idle_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - timer_c).count();
            const double idle_cpu = (process_cpu_seconds() - cpu_before) / idle_seconds;
            const double after_rate = (ticks_of_victim() - ticks_c) / idle_seconds;
            const double memory_growth = peak_memory_mb() - memory_before;
            RoomStatus f;
            ASSERT_TRUE(mgr.status("FLOOD", f, now));
            ASSERT_TRUE(f.state == RoomState::Waiting && f.joined == 0);                // it left the room (a flooder's seat is free again)
            ASSERT_EQ(f.ignored_start_requests, kind == "startreq" ? net::kIgnoredStartRequestsAllowed + 8u : 0u);   // it had a seat, and was out at its 24th request: the rest was not even read
            ASSERT_TRUE(flood_rate > 16.0);                                             // the other room kept its clock while the flood lasted (the loop never stalled)
            ASSERT_TRUE(after_rate > 17.0 && after_rate < 23.0);
            ASSERT_TRUE(max_iteration_ms < 400.0);                                      // no pass of the main loop took long, with the flood on
            ASSERT_TRUE(idle_cpu < 0.5);                                                // the process does not stay busy digesting a backlog (it had one of gigabytes)
            ASSERT_TRUE(memory_growth < 200.0);                                         // and did not grow by the flood (gigabytes, before the inbox was bounded)
            ASSERT_TRUE(mgr.status("VICTIM", s, now) && s.state == RoomState::Running);
            ASSERT_FALSE(clients[0]->session->desynced() || clients[1]->session->desynced());
        }
    } TEST_END();
#endif
}

namespace {

#ifndef _WIN32
// umask 0 for the life of the object: a mode that the code asks for is then the mode that the file gets. A umask can only take bits away, so under a strict umask
// (077, as hardened hosts have) a wrong mode would hide behind it and the check on the mode could not fail.
struct ZeroUmask {
    mode_t previous;
    ZeroUmask() : previous(::umask(0)) {}
    ~ZeroUmask() { ::umask(previous); }
    ZeroUmask(const ZeroUmask&) = delete;
    ZeroUmask& operator=(const ZeroUmask&) = delete;
};

// The highest resident memory of this process so far (macOS counts bytes, Linux kilobytes)
size_t peak_rss_bytes() {
    struct rusage usage;
    if (::getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#ifdef __APPLE__
    return static_cast<size_t>(usage.ru_maxrss);
#else
    return static_cast<size_t>(usage.ru_maxrss) * 1024;
#endif
}
#endif


}  // namespace

// The control secret: from the environment, or made once and kept in a file (secret.hpp)
void run_secret_tests() {
    auto fresh_dir = [](const char* tag) {
        const fs::path d = scratch_root() / (std::string("secret_") + tag);
        std::error_code ec;
        fs::remove_all(d, ec);
        fs::create_directories(d, ec);
        return d;
    };
    auto slurp = [](const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    auto put = [](const fs::path& p, const std::string& text) {
        std::ofstream out(p, std::ios::binary);
        out << text;
    };
    auto is_hex64 = [](const std::string& t) {
        if (t.size() != 64) return false;
        for (const char c : t) if (!std::isxdigit(static_cast<unsigned char>(c)) || std::isupper(static_cast<unsigned char>(c))) return false;
        return true;
    };
    // the control interface's own verdict: it refuses to start with a secret that it does not take
    auto http_takes = [](const std::string& s) { return ctl::HttpServer::listen(0, s) != nullptr; };

    TEST_CASE("S3.15 The Control Secret: The Environment Wins And Never Touches The File; Without Both The Server Refuses; Without The Environment It Makes A Random Secret Once, Keeps It Owner-Only (Under Any umask), And Reads The Same One Next Time") {
#ifndef _WIN32
        const ZeroUmask zero_umask;
#endif
        const fs::path dir = fresh_dir("make");
        const std::string file = (dir / "state" / "control-secret").string();      // the folder does not exist yet: it is made
        // the environment wins; no file is made, an existing one is not read
        auto env = server::resolve_secret("from-the-environment-0123456789", file);
        ASSERT_TRUE(env.ok);
        ASSERT_EQ(env.secret, std::string("from-the-environment-0123456789"));
        ASSERT_TRUE(env.source == server::SecretSource::Environment);
        ASSERT_FALSE(fs::exists(file));
        // an environment variable that is set but empty counts as not set
        ASSERT_FALSE(server::resolve_secret("", "").ok);
        auto empty_env = server::resolve_secret("", file);
        ASSERT_TRUE(empty_env.ok);
        ASSERT_TRUE(empty_env.source == server::SecretSource::Generated);
#ifndef _WIN32
        struct stat made;
        ASSERT_EQ(::stat(file.c_str(), &made), 0);
        ASSERT_EQ(static_cast<unsigned>(made.st_mode & 0777), 0600u);
#endif
        ASSERT_TRUE(fs::remove(file));
        // no environment and no place for a file: refused, with a sentence that says what to do
        auto none = server::resolve_secret(nullptr, "");
        ASSERT_FALSE(none.ok);
        ASSERT_TRUE(none.error.find("ANTS_SERVER_SECRET") != std::string::npos);
        ASSERT_TRUE(none.error.find("--secret-file") != std::string::npos);
        // the first start makes it: 64 hex digits, in the file, one line
        auto first = server::resolve_secret(nullptr, file);
        ASSERT_TRUE(first.ok);
        ASSERT_TRUE(first.source == server::SecretSource::Generated);
        ASSERT_EQ(first.path, file);
        ASSERT_TRUE(is_hex64(first.secret));
        ASSERT_EQ(slurp(file), first.secret + "\n");
        ASSERT_TRUE(server::usable_secret_text(first.secret));
        ASSERT_EQ(entries_in(dir / "state"), size_t{1});                            // the temporary file of the making is gone: nothing but the secret file
#ifndef _WIN32
        struct stat st;
        ASSERT_EQ(::stat(file.c_str(), &st), 0);
        ASSERT_EQ(static_cast<unsigned>(st.st_mode & 0777), 0600u);                 // nobody but its owner can read it, and the umask did not have to help
        ASSERT_EQ(static_cast<unsigned>(st.st_nlink), 1u);                          // no second name (the temporary one) is left behind
#endif
        // every later start reads the same secret and does not write the file again
        const auto before = fs::last_write_time(file);
        for (int i = 0; i < 3; ++i) {
            auto again = server::resolve_secret(nullptr, file);
            ASSERT_TRUE(again.ok);
            ASSERT_TRUE(again.source == server::SecretSource::File);
            ASSERT_EQ(again.secret, first.secret);
        }
        ASSERT_TRUE(fs::last_write_time(file) == before);
        ASSERT_EQ(entries_in(dir / "state"), size_t{1});
        // a secret in the environment still wins over a file that exists
        auto over = server::resolve_secret("the-environment-is-stronger-0123456789", file);
        ASSERT_EQ(over.secret, std::string("the-environment-is-stronger-0123456789"));
        ASSERT_EQ(slurp(file), first.secret + "\n");
        // two servers do not share a secret: a second file gets a different one
        auto other = server::resolve_secret(nullptr, (dir / "second").string());
        ASSERT_TRUE(other.ok);
        ASSERT_TRUE(other.secret != first.secret);
        // the HTTP server takes the generated secret
        ASSERT_TRUE(http_takes(first.secret));
    } TEST_END();

    TEST_CASE("S3.16 A Secret File That Is Not A Usable Secret Is Never Overwritten: An Empty, Short, Long, Spaced, Two-Line, Padded Or Binary File, A Directory, A Device, A FIFO And A Folder That Cannot Be Made All Stop The Server With A Sentence") {
        const fs::path dir = fresh_dir("refuse");
        const std::string hex = server::generate_secret_text();
        struct Case {
            const char* name;
            std::string content;
            const char* says = "does not hold a usable secret";
        };
        const std::vector<Case> cases = {
            {"empty", ""},
            {"only a line end", "\n"},
            {"31 characters", std::string(31, 'a') + "\n"},
            {"257 characters", std::string(257, 'a') + "\n"},
            {"a space inside", hex.substr(0, 20) + " " + hex.substr(20) + "\n"},
            {"a space in front", " " + hex + "\n"},
            {"a tab in front", "\t" + hex + "\n"},
            {"a line end in front", "\n" + hex + "\n"},
            {"two lines", hex + "\n" + hex + "\n"},
            {"a control character", hex.substr(0, 40) + std::string(1, '\x01') + hex.substr(40) + "\n"},
            {"a DEL", hex.substr(0, 40) + std::string(1, '\x7f') + hex.substr(40) + "\n"},
            {"a byte above 127", hex.substr(0, 40) + std::string(1, static_cast<char>(0xC3)) + hex.substr(40) + "\n"},
            {"a tab inside", hex.substr(0, 30) + "\t" + hex.substr(30) + "\n"},
            {"1024 bytes that are no secret", std::string(1024, 'x')},                                 // the biggest file that is looked at: still too long a secret
            {"1025 bytes", std::string(1025, 'x'), "is too big to be a secret"},
            {"5000 bytes", std::string(5000, 'x'), "is too big to be a secret"},
            {"a secret and 1100 blanks", hex + std::string(1100, ' ') + "\n", "is too big to be a secret"},       // only the size branch can refuse this one
            {"a secret and 1100 line ends", hex + std::string(1100, '\n'), "is too big to be a secret"},
        };
        for (const Case& c : cases) {
            const fs::path f = dir / "control-secret";
            put(f, c.content);
            auto r = server::resolve_secret(nullptr, f.string());
            ASSERT_MSG(!r.ok, c.name);
            ASSERT_MSG(r.error.find("nothing was changed") != std::string::npos, c.name);
            ASSERT_MSG(r.error.find(c.says) != std::string::npos, std::string(c.name) + ": " + r.error);
            ASSERT_MSG(r.secret.empty(), c.name);
            ASSERT_MSG(slurp(f) == c.content, c.name);                                // not repaired, not replaced
            // the environment is still all it takes to start, and it does not look at the file at all
            ASSERT_MSG(server::resolve_secret("a-good-secret-from-the-environment-0123", f.string()).ok, c.name);
        }
        // a directory where the file should be
        const fs::path as_dir = dir / "is-a-directory";
        fs::create_directories(as_dir);
        auto d = server::resolve_secret(nullptr, as_dir.string());
        ASSERT_FALSE(d.ok);
        ASSERT_TRUE(d.error.find("not a regular file") != std::string::npos);
        // a "folder" that is a file: nothing can be made below it
        const fs::path blocker = dir / "blocker";
        put(blocker, "x");
        auto b = server::resolve_secret(nullptr, (blocker / "control-secret").string());
        ASSERT_FALSE(b.ok);
        ASSERT_TRUE(b.error.find("ANTS_SERVER_SECRET") != std::string::npos);
        ASSERT_EQ(slurp(blocker), std::string("x"));
#ifndef _WIN32
        // a device and a FIFO: refused as "not a regular file" before anything is read (a FIFO that nobody writes to must not make the server wait: the alarm ends a hang)
        ::alarm(30);
        auto z = server::resolve_secret(nullptr, "/dev/zero");
        ASSERT_FALSE(z.ok);
        ASSERT_TRUE(z.error.find("not a regular file") != std::string::npos);
        const fs::path fifo = dir / "a-fifo";
        ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
        auto q = server::resolve_secret(nullptr, fifo.string());
        ASSERT_FALSE(q.ok);
        ASSERT_TRUE(q.error.find("not a regular file") != std::string::npos);
        ::alarm(0);
        // a folder that may not be written (not for root, which may write anywhere)
        if (::geteuid() != 0) {
            const fs::path ro = dir / "readonly";
            fs::create_directories(ro);
            ASSERT_EQ(::chmod(ro.c_str(), 0500), 0);
            auto w = server::resolve_secret(nullptr, (ro / "control-secret").string());
            ASSERT_FALSE(w.ok);
            ASSERT_TRUE(w.error.find("cannot be created") != std::string::npos);
            ASSERT_EQ(entries_in(ro), size_t{0});                                     // no file, no temporary file
            ASSERT_EQ(::chmod(ro.c_str(), 0700), 0);
        }
#endif
    } TEST_END();

    TEST_CASE("S3.17 A Secret File That Somebody Wrote By Hand Is Used As It Is: Its Line End, A Carriage Return Or Trailing Blanks Do Not Belong To The Secret; 32 And 256 Characters Are Both Fine") {
        const fs::path dir = fresh_dir("hand");
        const std::string secret = "My-own-secret_with.punctuation/and+symbols=0123456789";
        const std::vector<std::string> shapes = {secret, secret + "\n", secret + "\r\n", secret + " \t\n\n", secret + "\r\n\r\n"};
        for (const std::string& shape : shapes) {
            put(dir / "control-secret", shape);
            auto r = server::resolve_secret(nullptr, (dir / "control-secret").string());
            ASSERT_TRUE(r.ok);
            ASSERT_TRUE(r.source == server::SecretSource::File);
            ASSERT_EQ(r.secret, secret);
        }
        for (const size_t n : {size_t{32}, size_t{256}}) {
            put(dir / "control-secret", std::string(n, 'k') + "\n");
            auto r = server::resolve_secret(nullptr, (dir / "control-secret").string());
            ASSERT_TRUE(r.ok);
            ASSERT_EQ(r.secret.size(), n);
        }
        // the largest file that is looked at: 1024 bytes, a secret and blanks that are not part of it
        put(dir / "control-secret", std::string(256, 'k') + std::string(768, ' '));
        auto padded = server::resolve_secret(nullptr, (dir / "control-secret").string());
        ASSERT_TRUE(padded.ok);
        ASSERT_EQ(padded.secret, std::string(256, 'k'));
        ASSERT_TRUE(server::usable_secret_text(std::string(32, '!')));
        ASSERT_TRUE(server::usable_secret_text(std::string(256, '~')));
        ASSERT_FALSE(server::usable_secret_text(std::string(32, ' ')));
        ASSERT_FALSE(server::usable_secret_text(std::string(31, 'a')));
        ASSERT_FALSE(server::usable_secret_text(std::string(257, 'a')));
    } TEST_END();

    TEST_CASE("S3.18 The Generated Secret Has All Its Strength, And The Rule For A Secret In A File Is The Control Interface's Own: 512 Secrets Show Every Digit At Every Position, No Position Copies Another, No Half Is Zero; All 256 Byte Values At The Start, In The Middle And At The End Agree With The HTTP Server") {
        std::vector<std::string> many;
        for (int i = 0; i < 512; ++i) many.push_back(server::generate_secret_text());
        for (const std::string& t : many) ASSERT_TRUE(is_hex64(t));
        std::vector<std::string> sorted = many;
        std::sort(sorted.begin(), sorted.end());
        ASSERT_TRUE(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());       // never the same twice
        // every one of the 64 positions shows at least 10 of the 16 digits (a position that is fixed or mostly zero means missing random bytes)
        for (size_t p = 0; p < 64; ++p) {
            std::set<char> seen;
            for (const std::string& t : many) seen.insert(t[p]);
            ASSERT_MSG(seen.size() >= 10, "position " + std::to_string(p) + " shows only " + std::to_string(seen.size()) + " digits");
        }
        // no position is a copy of another one (a byte that is written twice, or random bytes that are used again further on)
        for (size_t a = 0; a < 64; ++a) {
            for (size_t b = a + 1; b < 64; ++b) {
                bool differ = false;
                for (const std::string& t : many) {
                    if (t[a] != t[b]) {
                        differ = true;
                        break;
                    }
                }
                ASSERT_MSG(differ, "positions " + std::to_string(a) + " and " + std::to_string(b) + " are always equal");
            }
        }
        // no secret has a half of zeros
        const std::string zeros(32, '0');
        for (const std::string& t : many) ASSERT_MSG(t.substr(0, 32) != zeros && t.substr(32) != zeros, t);

        // The rule for a file: what the reader gives for every byte value in front of, inside and behind a secret, written out again here
        const fs::path dir = fresh_dir("bytes");
        const std::string core(40, 'k');
        for (int b = 0; b < 256; ++b) {
            const std::string c(1, static_cast<char>(b));
            const std::string shapes[3] = {c + core, core.substr(0, 20) + c + core.substr(20), core + c};
            for (const std::string& text : shapes) {
                const std::string tag = "byte " + std::to_string(b) + " in " + std::to_string(text.size()) + " characters";
                // the file reader's own rule and the HTTP server's agree: a usable text is always accepted by the control interface, and a refused one is not
                ASSERT_MSG(server::usable_secret_text(text) == http_takes(text), tag + ": usable_secret_text and the HTTP server disagree");
                // through a file: trailing line ends and blanks do not belong to the secret; the rest must be usable
                std::string trimmed = text;
                while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r' || trimmed.back() == ' ' || trimmed.back() == '\t')) trimmed.pop_back();
                put(dir / "control-secret", text);
                auto r = server::resolve_secret(nullptr, (dir / "control-secret").string());
                ASSERT_MSG(r.ok == server::usable_secret_text(trimmed), tag + ": the file reader decided otherwise than the rule");
                if (r.ok) {
                    ASSERT_MSG(r.secret == trimmed, tag);
                    ASSERT_MSG(http_takes(r.secret), tag + ": the control interface refuses a secret that the file reader gave");
                } else {
                    ASSERT_MSG(slurp(dir / "control-secret") == text, tag + ": a refused file was changed");
                }
            }
        }
    } TEST_END();

    TEST_CASE("S3.58 random_bytes, The Generator Of The Control Secret, Is There For The Server To Make The Keys Of The Seats With (Protocol 10): Every Length Is Filled Exactly And Nothing Beyond It Is Touched; 512 Keys Of 16 Bytes Show Many Values At Every Position, None Repeats, None Is Zero, No Position Copies Another; Zero Bytes Is Nothing To Do") {
        for (const size_t n : {size_t{0}, size_t{1}, size_t{2}, size_t{7}, size_t{16}, size_t{31}, size_t{32}, size_t{33}, size_t{255}, size_t{4096}, size_t{100000}}) {
            std::vector<uint8_t> buf(n + 16, 0xA5);
            ASSERT_TRUE(server::random_bytes(buf.data() + 8, n));
            for (size_t i = 0; i < 8; ++i) ASSERT_TRUE(buf[i] == 0xA5 && buf[n + 8 + i] == 0xA5);        // the bytes before and after the buffer are as they were
            if (n >= 32) {                                                                                 // a run of 32 bytes or more is not constant, and not left as it was
                std::set<uint8_t> values(buf.begin() + 8, buf.begin() + 8 + static_cast<std::ptrdiff_t>(n));
                ASSERT_TRUE(values.size() >= 16);
            }
        }
        ASSERT_TRUE(server::random_bytes(nullptr, 0));                                                    // nothing to do is done
        ASSERT_FALSE(server::random_bytes(nullptr, 1));                                                   // nowhere to write it is not
        // the keys of seats as the server makes them: 512 of 16 bytes
        std::vector<net::SeatKey> keys(512);
        for (net::SeatKey& k : keys) {
            ASSERT_TRUE(server::random_bytes(k.data(), k.size()));
            ASSERT_FALSE(net::key_is_zero(k));
        }
        std::vector<net::SeatKey> sorted = keys;
        std::sort(sorted.begin(), sorted.end());
        ASSERT_TRUE(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());                     // never the same key twice
        for (size_t p = 0; p < net::kKeyBytes; ++p) {                                                      // every position shows many of the 256 values (a position that is fixed means missing random bytes)
            std::set<uint8_t> seen;
            for (const net::SeatKey& k : keys) seen.insert(k[p]);
            ASSERT_MSG(seen.size() >= 150, "position " + std::to_string(p) + " shows only " + std::to_string(seen.size()) + " values");
        }
        for (size_t a = 0; a < net::kKeyBytes; ++a) {                                                      // no position is a copy of another
            for (size_t b = a + 1; b < net::kKeyBytes; ++b) {
                bool differ = false;
                for (const net::SeatKey& k : keys) differ = differ || k[a] != k[b];
                ASSERT_MSG(differ, "positions " + std::to_string(a) + " and " + std::to_string(b) + " are always equal");
            }
        }
    } TEST_END();

    TEST_CASE("S3.19 A File Bigger Than A Secret Is Refused Without Being Read: A Sparse File Of 256 MiB Is Called Too Big At Once (The Whole File Used To Be Read Into Memory First)") {
#ifndef _WIN32
        const fs::path dir = fresh_dir("big");
        const fs::path big = dir / "control-secret";
        put(big, "");
        std::error_code ec;
        fs::resize_file(big, std::uintmax_t{256} << 20, ec);                        // sparse: it costs no disk, and reading it whole costs a second and half a gigabyte
        ASSERT_FALSE(ec);
        const size_t peak_before = peak_rss_bytes();
        double best_ms = 1e9;
        for (int i = 0; i < 3; ++i) {                                               // the best of three: a stall of the machine does not fail the test
            const auto t0 = std::chrono::steady_clock::now();
            auto r = server::resolve_secret(nullptr, big.string());
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            ASSERT_FALSE(r.ok);
            ASSERT_TRUE(r.error.find("is too big to be a secret") != std::string::npos);
            best_ms = std::min(best_ms, ms);
        }
        ASSERT_MSG(best_ms < 250.0, "the refusal took " + std::to_string(best_ms) + " ms: the file was read");
        // and the memory never held the file (a read of all of it raises the highest resident size of this process by at least its size)
        const size_t peak_after = peak_rss_bytes();
        ASSERT_MSG(peak_after - peak_before < (std::size_t{32} << 20), "the refusal raised the peak memory by " + std::to_string((peak_after - peak_before) >> 20) + " MiB");
        ASSERT_EQ(fs::file_size(big), std::uintmax_t{256} << 20);                    // and left alone
        fs::remove(big, ec);
#endif
    } TEST_END();

#ifndef _WIN32
    TEST_CASE("S3.20 Symbolic Links: A Link To A Good File (A Mounted Secret) Is Followed, A Link To A Bad File Or A Directory Is Refused, A Link To Nothing Is Called That (And Nothing Is Made Behind It), A Loop Is Called A Loop") {
        const fs::path dir = fresh_dir("links");
        const std::string secret = server::generate_secret_text();
        put(dir / "real", secret + "\n");
        fs::create_symlink("real", dir / "link");                                   // relative, as the mounts of a secrets store make them
        auto ok = server::resolve_secret(nullptr, (dir / "link").string());
        ASSERT_TRUE(ok.ok);
        ASSERT_TRUE(ok.source == server::SecretSource::File);
        ASSERT_EQ(ok.secret, secret);
        ASSERT_EQ(ok.path, (dir / "link").string());
        ASSERT_TRUE(fs::is_symlink(dir / "link"));                                  // still a link
        // a chain of links through a folder that is a link too (a Kubernetes secret volume: key -> data/key, data -> a folder with a timestamp)
        const std::string secret2 = "A-mounted-secret-0123456789-abcdefghijklmnop";
        fs::create_directories(dir / "mount" / "ts-1");
        put(dir / "mount" / "ts-1" / "key", secret2);
        fs::create_directory_symlink("ts-1", dir / "mount" / "data");
        fs::create_symlink("data/key", dir / "mount" / "key");
        auto chain = server::resolve_secret(nullptr, (dir / "mount" / "key").string());
        ASSERT_TRUE(chain.ok);
        ASSERT_EQ(chain.secret, secret2);
        // a link to a file that is no secret: refused, the file is left as it was
        put(dir / "badreal", "short");
        fs::create_symlink("badreal", dir / "badlink");
        auto bad = server::resolve_secret(nullptr, (dir / "badlink").string());
        ASSERT_FALSE(bad.ok);
        ASSERT_TRUE(bad.error.find("nothing was changed") != std::string::npos);
        ASSERT_EQ(slurp(dir / "badreal"), std::string("short"));
        // a link to a directory
        fs::create_directories(dir / "somedir");
        fs::create_directory_symlink("somedir", dir / "dirlink");
        auto dl = server::resolve_secret(nullptr, (dir / "dirlink").string());
        ASSERT_FALSE(dl.ok);
        ASSERT_TRUE(dl.error.find("not a regular file") != std::string::npos);
        // a link to nothing, in the same folder and into a folder that exists: the truth, and nothing made (not the target, not a file instead of the link)
        fs::create_symlink("nowhere", dir / "dangling");
        fs::create_symlink("somedir/missing", dir / "dangling2");
        for (const char* name : {"dangling", "dangling2"}) {
            auto r = server::resolve_secret(nullptr, (dir / name).string());
            ASSERT_MSG(!r.ok, name);
            ASSERT_MSG(r.error.find("is a symbolic link to nothing") != std::string::npos, std::string(name) + ": " + r.error);
            ASSERT_MSG(r.error.find("keeps changing") == std::string::npos, name);
            ASSERT_MSG(fs::is_symlink(dir / name), name);
            ASSERT_MSG(!fs::exists(dir / "nowhere") && !fs::exists(dir / "somedir" / "missing"), name);
        }
        ASSERT_EQ(fs::read_symlink(dir / "dangling").string(), std::string("nowhere"));
        // the environment is all it takes to start, whatever is at the path
        ASSERT_TRUE(server::resolve_secret("a-good-secret-from-the-environment-0123", (dir / "dangling").string()).ok);
        // a loop
        fs::create_symlink("loop-b", dir / "loop-a");
        fs::create_symlink("loop-a", dir / "loop-b");
        auto lp = server::resolve_secret(nullptr, (dir / "loop-a").string());
        ASSERT_FALSE(lp.ok);
        ASSERT_TRUE(lp.error.find("cannot be looked at") != std::string::npos);
    } TEST_END();
#endif

    TEST_CASE("S3.21 Starts At The Same Moment: 8 Threads On One Fresh Folder, 200 Rounds (Half With A Folder That Does Not Exist Yet): Every Start Works, All Get The Same Secret, Exactly One Made It, The File Holds It Complete, Nothing Else Is Left In The Folder") {
        constexpr int kThreads = 8;
        constexpr int kRounds = 200;
        const fs::path base = fresh_dir("race");
        for (int round = 0; round < kRounds; ++round) {
            const fs::path folder = base / ("round-" + std::to_string(round));
            if (round % 2 == 0) fs::create_directories(folder);
            const std::string file = (folder / "control-secret").string();
            std::atomic<int> ready{0};
            std::atomic<bool> go{false};
            std::vector<server::SecretResult> results(kThreads);
            std::vector<std::thread> threads;
            for (int i = 0; i < kThreads; ++i) {
                threads.emplace_back([&, i] {
                    ready.fetch_add(1);
                    while (!go.load()) std::this_thread::yield();                  // released together
                    results[static_cast<size_t>(i)] = server::resolve_secret(nullptr, file);
                });
            }
            while (ready.load() < kThreads) std::this_thread::yield();
            go.store(true);
            for (std::thread& t : threads) t.join();
            int generated = 0;
            for (const server::SecretResult& r : results) {
                ASSERT_MSG(r.ok, "round " + std::to_string(round) + ": a start failed: " + r.error);
                ASSERT_MSG(r.secret == results[0].secret, "round " + std::to_string(round) + ": the starts did not get the same secret");
                if (r.source == server::SecretSource::Generated) ++generated;
            }
            ASSERT_MSG(generated == 1, "round " + std::to_string(round) + ": " + std::to_string(generated) + " starts made the secret");
            ASSERT_MSG(slurp(file) == results[0].secret + "\n", "round " + std::to_string(round) + ": the file does not hold the secret that the starts use");
            ASSERT_MSG(entries_in(folder) == 1, "round " + std::to_string(round) + ": something besides the secret file is left in the folder");
        }
    } TEST_END();

    TEST_CASE("S3.22 A Start That Dies While It Makes The File Leaves No Half Secret: Stale Temporary Files (Of Any Name A Simple Scheme Would Use) Never Block The Next Start, And A Process That Is Killed At Its First Write Leaves Nothing That Blocks It") {
        const fs::path dir = fresh_dir("crash");
        const std::string file = (dir / "control-secret").string();
        // what a crashed start can leave: empty or half written temporary files, with the names that a fixed or process-number scheme would pick
        const std::vector<std::string> stale = {"control-secret.tmp", "control-secret.tmp.1", "control-secret.1.tmp", ".control-secret.tmp", "control-secret.0123456789abcdef.tmp", "control-secret~"};
        for (const std::string& name : stale) put(dir / name, name == "control-secret.tmp.1" ? "0123456789abcdef0123" : "");
        auto r = server::resolve_secret(nullptr, file);
        ASSERT_TRUE(r.ok);
        ASSERT_TRUE(r.source == server::SecretSource::Generated);
        ASSERT_EQ(slurp(file), r.secret + "\n");
        for (const std::string& name : stale) ASSERT_MSG(fs::exists(dir / name), name);          // the leftovers of others are not the server's to remove
        ASSERT_EQ(entries_in(dir), stale.size() + 1);
        auto next = server::resolve_secret(nullptr, file);
        ASSERT_TRUE(next.ok);
        ASSERT_TRUE(next.source == server::SecretSource::File);
        ASSERT_EQ(next.secret, r.secret);
#ifndef _WIN32
        // a real death: a child whose files may not grow is killed (SIGXFSZ) by its first write
        const fs::path dir2 = fresh_dir("crash2");
        const std::string file2 = (dir2 / "control-secret").string();
        const pid_t pid = ::fork();
        ASSERT_TRUE(pid >= 0);
        if (pid == 0) {
            struct rlimit none = {0, 0};
            ::setrlimit(RLIMIT_CORE, &none);                                         // no core file for this death
            ::setrlimit(RLIMIT_FSIZE, &none);
            ::signal(SIGXFSZ, SIG_DFL);
            (void)server::resolve_secret(nullptr, file2);
            ::_exit(0);                                                              // not reached: the write kills the process
        }
        int wait_status = 0;
        ASSERT_EQ(::waitpid(pid, &wait_status, 0), pid);
        ASSERT_TRUE(WIFSIGNALED(wait_status));
        ASSERT_TRUE(WTERMSIG(wait_status) == SIGXFSZ);
        ASSERT_FALSE(fs::exists(file2));                                             // nothing under the name that the next start would find and refuse
        auto after = server::resolve_secret(nullptr, file2);
        ASSERT_TRUE(after.ok);
        ASSERT_TRUE(after.source == server::SecretSource::Generated);
        ASSERT_EQ(slurp(file2), after.secret + "\n");
        // what the dead start left is a temporary file and nothing else
        for (const auto& e : fs::directory_iterator(dir2)) {
            const std::string name = e.path().filename().string();
            ASSERT_MSG(name == "control-secret" || name.size() > 4, name);
            ASSERT_MSG(name == "control-secret" || name.compare(name.size() - 4, 4, ".tmp") == 0, name);
        }
#endif
    } TEST_END();
}

