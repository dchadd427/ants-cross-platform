// Tests of the online size limit and of the cost of a big map (online_limits.hpp, docs/SERVER.md "Online size limit"), and of what a lobby room says when one person's game cannot load the map of its START: a map
// over 65,536 cells is refused for a room with one sentence (the lobby's plan keeps its map, the control interface's room is a 422), a match takes one place of the server for every started 10,000 cells of its
// map (and never more than all of them), and a START that is cancelled because a game cannot load the map names that game's person and says how to start without them, instead of blocking the room without a word.
#if defined(_WIN32)
#include <process.h>
#define ANTS_ONLINE_LIMIT_GETPID _getpid
#else
#include <unistd.h>
#define ANTS_ONLINE_LIMIT_GETPID getpid
#endif

#include "big_map_fixtures.hpp"
#include "server_fixtures.hpp"
#include "ants_server/online_limits.hpp"

namespace {

using K = net::PlanKind;

ServerLimits limits_with(size_t places) {
    ServerLimits l;
    l.demo_rooms = places;
    l.demo_lobbies = 3;
    l.demo_map = "TINY.LVL";
    l.demo_maps = {"TINY.LVL", "SMALL.LVL"};
    return l;
}

net::PlanMsg plan_for(const std::string& map, const std::array<K, 4>& kinds = {K::Open, K::Open, K::Nobody, K::Nobody}) {
    net::PlanMsg m;
    m.map_name = map;
    m.plan = kinds;
    m.team_a = net::kNoTeam;
    m.team_b = net::kNoTeam;
    return m;
}

// An approved Extended map of the players for the server's store: the file is written into `dir` under `file_name` (the map's own name too when `as_file`: a machine of the newest version reads it there)
ApprovedMap approved_map(const fs::path& dir, const std::string& name, uint32_t rows, uint32_t columns, bool as_file = false) {
    const std::string bytes = grown_tiny(rows, columns);
    ApprovedMap a;
    if (bytes.empty()) return a;
    a.name = name;
    a.path = (dir / (name + ".bin")).string();
    { std::ofstream out(a.path, std::ios::binary); out << bytes; }
    if (as_file) { std::ofstream out(dir / name, std::ios::binary); out << bytes; }
    a.size = bytes.size();
    a.width = columns;
    a.height = rows;
    a.minutes = 10;
    a.players = 4;
    a.maker = "Mia";
    a.kind = "extended";
    net::hash_file(a.path, a.hash);
    return a;
}

fs::path fresh_dir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / (std::string("ants_online_limit_") + name + "_" + std::to_string(ANTS_ONLINE_LIMIT_GETPID()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// The answer a Hello got at the door after 300 ms: the reason of its Reject, 0 when it was seated
net::RejectReason door_answer(World& w, Client& c) {
    w.run(300);
    return c.lobby->phase() == net::ClientLobby::Phase::Rejected ? c.lobby->reject_reason() : static_cast<net::RejectReason>(0);
}

}  // namespace

void run_online_limit_tests() {
    TEST_CASE("S3.210 A Lobby Leader Who Chooses A Map Over 65,536 Cells Is Told So In One Sentence And The Room Keeps Its Map; A Map Of Exactly 65,536 Cells, A Shipped Map And An Unknown Name Behave As Before; A Control Interface Room On A Big Map Is A 422") {
        const fs::path dir = fresh_dir("s201");
        World w(limits_with(8));
        std::vector<ApprovedMap> approved = {approved_map(dir, "Edge 65536.lvl", 128, 512), approved_map(dir, "Wide 410.lvl", 160, 410)};
        ASSERT_TRUE(!approved[0].name.empty() && !approved[1].name.empty());
        w.mgr.mutable_store().set_approved(approved);
        Client& pia = w.connect_page("Pia", "big00001", lobby_block_of());
        w.run(300);
        ASSERT_TRUE(pia.lobby->is_leader());
        ASSERT_TRUE(pia.lobby->request_plan(plan_for("Wide 410.lvl")));
        w.run(300);
        ASSERT_EQ(w.status("big00001").map, std::string("TINY.LVL"));                                // (the map stays)
        ASSERT_EQ(said(pia.room_chat), (std::vector<std::string>{std::string("255||") + net::kNoticeMapTooBig}));
        ASSERT_TRUE(pia.lobby->request_plan(plan_for("wide 410.LVL", {K::Open, K::Easy, K::Nobody, K::Nobody})));      // (any letter case; the rest of the plan is taken)
        w.run(300);
        ASSERT_EQ(w.status("big00001").map, std::string("TINY.LVL"));
        ASSERT_TRUE(pia.lobby->room().plan[1] == K::Easy);
        ASSERT_EQ(said(pia.room_chat).size(), size_t{2});
        ASSERT_TRUE(pia.lobby->request_plan(plan_for("Edge 65536.lvl")));
        w.run(300);
        ASSERT_EQ(w.status("big00001").map, std::string("Edge 65536.lvl"));                          // (exactly the limit is in)
        ASSERT_EQ(said(pia.room_chat).size(), size_t{2});                                            // (and no word about it)
        ASSERT_TRUE(pia.lobby->request_plan(plan_for("SMALL.LVL")));
        w.run(300);
        ASSERT_EQ(w.status("big00001").map, std::string("SMALL.LVL"));
        ASSERT_TRUE(pia.lobby->request_plan(plan_for("NOT-HERE.LVL")));                              // (a name the server does not have: kept silently, as before)
        w.run(300);
        ASSERT_EQ(w.status("big00001").map, std::string("SMALL.LVL"));
        ASSERT_EQ(said(pia.room_chat).size(), size_t{2});
        {   // a page whose link names a test map: a draft that a room plays is the room's map, one that is over the limit is not (the room keeps the server's own map)
            ApprovedMap draft_big = approved_map(dir, "Big Draft ~test-0badf00d.lvl", 300, 300);
            ApprovedMap draft_fits = approved_map(dir, "Small Draft ~test-0badf00e.lvl", 100, 100);
            w.mgr.mutable_store().set_test_maps({draft_big, draft_fits});
            w.connect_page("Ann", "big00002", lobby_block_of("Big Draft ~test-0badf00d.lvl"));
            w.connect_page("Cat", "big00003", lobby_block_of("Small Draft ~test-0badf00e.lvl"));
            w.run(300);
            ASSERT_EQ(w.status("big00002").map, std::string("TINY.LVL"));
            ASSERT_EQ(w.status("big00003").map, std::string("Small Draft ~test-0badf00e.lvl"));
        }
        // the control interface
        const CreateResult refused = w.mgr.create_room(spec_of("ctl-big", 2, "Wide 410.lvl"), w.now);
        ASSERT_TRUE(!refused.ok && refused.http_status == 422 && refused.error.find("too big") != std::string::npos);
        ASSERT_TRUE(w.mgr.create_room(spec_of("ctl-edge", 2, "Edge 65536.lvl"), w.now).ok);
        fs::remove_all(dir);
    } TEST_END();

    TEST_CASE("S3.211 A Match Takes One Place Of The Server For Every Started 10,000 Cells Of Its Map, And Never More Than All Of Them: On Eight Places A Map Of 65,536 Cells (Seven) Leaves One; On Three It Takes All Three; A Lobby's START Asks For The Weight Of Its Own Map") {
        const fs::path dir = fresh_dir("s202");
        {   // eight places: the big room takes seven, one public room fits beside it, a second does not (before the weight, two did)
            World w(limits_with(8));
            w.mgr.mutable_store().set_approved({approved_map(dir, "Edge 65536.lvl", 128, 512)});
            RoomSpec big = spec_of("ctl-edge", 2, "Edge 65536.lvl");
            big.public_room = true;
            ASSERT_TRUE(w.mgr.create_room(big, w.now).ok);
            Client& ann = w.connect_creating("Ann", "pub00001", block_of("TINY.LVL", 2));
            ASSERT_EQ(door_answer(w, ann), static_cast<net::RejectReason>(0));                       // (7 + 1 = 8)
            Client& bob = w.connect_creating("Bob", "pub00002", block_of("TINY.LVL", 2));
            ASSERT_EQ(door_answer(w, bob), net::RejectReason::NoSuchRoom);                           // (7 + 1 + 1 = 9)
            ASSERT_EQ(w.mgr.room_count(), size_t{2});
        }
        {   // a public room that a visitor's block makes on a big map the operator offers (--demo-maps) takes seven too
            ServerLimits l = limits_with(8);
            l.demo_maps = {"TINY.LVL", "Edge 65536.lvl"};
            World w(l);
            w.mgr.mutable_store().set_approved({approved_map(dir, "Edge 65536.lvl", 128, 512)});
            Client& big = w.connect_creating("Big", "pub00401", block_of("Edge 65536.lvl", 2));
            ASSERT_EQ(door_answer(w, big), static_cast<net::RejectReason>(0));
            ASSERT_EQ(w.status("pub00401").map, std::string("Edge 65536.lvl"));
            Client& ann = w.connect_creating("Ann", "pub00402", block_of("TINY.LVL", 2));
            ASSERT_EQ(door_answer(w, ann), static_cast<net::RejectReason>(0));                       // (7 + 1 = 8)
            Client& bob = w.connect_creating("Bob", "pub00403", block_of("TINY.LVL", 2));
            ASSERT_EQ(door_answer(w, bob), net::RejectReason::NoSuchRoom);
            World crowded(l);                                                                         // two small rooms first: the big one asks for seven more of eight and is refused
            crowded.mgr.mutable_store().set_approved({approved_map(dir, "Edge 65536.lvl", 128, 512)});
            ASSERT_EQ(door_answer(crowded, crowded.connect_creating("A1", "pub00411", block_of("TINY.LVL", 2))), static_cast<net::RejectReason>(0));
            ASSERT_EQ(door_answer(crowded, crowded.connect_creating("A2", "pub00412", block_of("TINY.LVL", 2))), static_cast<net::RejectReason>(0));
            ASSERT_EQ(door_answer(crowded, crowded.connect_creating("Big", "pub00413", block_of("Edge 65536.lvl", 2))), net::RejectReason::NoSuchRoom);
        }
        {   // no big room: eight small rooms fit, a ninth does not
            World w(limits_with(8));
            for (int i = 1; i <= 8; ++i) {
                Client& c = w.connect_creating("P" + std::to_string(i), "pub0010" + std::to_string(i), block_of("TINY.LVL", 2));
                ASSERT_MSG(door_answer(w, c) == static_cast<net::RejectReason>(0), "room " + std::to_string(i));
            }
            Client& ninth = w.connect_creating("P9", "pub00109", block_of("TINY.LVL", 2));
            ASSERT_EQ(door_answer(w, ninth), net::RejectReason::NoSuchRoom);
        }
        {   // three places: the weight of the big map is cut to 3 (a server this small can still run it), and then it is full
            World w(limits_with(3));
            w.mgr.mutable_store().set_approved({approved_map(dir, "Edge 65536.lvl", 128, 512)});
            RoomSpec big = spec_of("ctl-edge", 2, "Edge 65536.lvl");
            big.public_room = true;
            ASSERT_TRUE(w.mgr.create_room(big, w.now).ok);
            Client& ann = w.connect_creating("Ann", "pub00201", block_of("TINY.LVL", 2));
            ASSERT_EQ(door_answer(w, ann), net::RejectReason::NoSuchRoom);
        }
        {   // a lobby's START on the big map on a server of three places: the weight is cut to 3, so an empty server runs it (a map is never refused for being heavier than the whole server)
            World w(limits_with(3));
            w.mgr.mutable_store().set_approved({approved_map(dir, "Edge 65536.lvl", 128, 512, true)});
            Client& pia = w.connect_page("Pia", "lobb0002", lobby_block_of());
            const net::SeatKey key = pia.lobby->key();
            ASSERT_TRUE(pia.lobby->request_plan(plan_for("Edge 65536.lvl", {K::Open, K::Medium, K::Nobody, K::Nobody})));
            w.run(300);
            ASSERT_TRUE(pia.lobby->request_start());
            w.run(200);
            Client& game = w.connect_game("Pia", "lobb0002", key, false, dir.string());
            w.run(4000);
            ASSERT_TRUE(said(game.room_chat).empty());
            ASSERT_TRUE(w.status("lobb0002").state == RoomState::Running);
            Client& ann = w.connect_creating("Ann", "pub00301", block_of("TINY.LVL", 2));        // (and then the three places are taken)
            ASSERT_EQ(door_answer(w, ann), net::RejectReason::NoSuchRoom);
        }
        {   // a lobby's START on the big map with a shipped match running on seven places: 1 + 7 > 7, no place; the other lobby on a shipped map has one (1 + 1)
            World w(limits_with(7));
            w.mgr.mutable_store().set_approved({approved_map(dir, "Edge 65536.lvl", 128, 512)});
            fs::copy_file(maps_dir() + "/TINY.LVL", dir / "TINY.LVL", fs::copy_options::overwrite_existing);       // (the newest machine reads the shipped map from the same folder)
            Client& ann = w.connect_creating("Ann", "occu0001", block_of("TINY.LVL", 2));
            Client& cat = w.connect("Cat", "occu0001");
            w.run(3000);
            ASSERT_TRUE(w.status("occu0001").state == RoomState::Running && ann.lobby->phase() == net::ClientLobby::Phase::Begun && cat.lobby->phase() == net::ClientLobby::Phase::Begun);
            Client& pia = w.connect_page("Pia", "lobb0001", lobby_block_of());
            const net::SeatKey key = pia.lobby->key();
            ASSERT_TRUE(pia.lobby->request_plan(plan_for("Edge 65536.lvl", {K::Open, K::Medium, K::Nobody, K::Nobody})));
            w.run(300);
            ASSERT_EQ(w.status("lobb0001").map, std::string("Edge 65536.lvl"));
            ASSERT_TRUE(pia.lobby->request_start());
            w.run(200);
            Client& game = w.connect_game("Pia", "lobb0001", key, false, dir.string());
            w.run(500);
            ASSERT_TRUE(w.status("lobb0001").state == RoomState::Waiting && !w.status("lobb0001").starting);
            ASSERT_EQ(said(game.room_chat), (std::vector<std::string>{std::string("255||") + net::kNoticeNoPlace}));
            ASSERT_TRUE(w.status("occu0001").state == RoomState::Running);
            ASSERT_TRUE(game.lobby->request_plan(plan_for("TINY.LVL", {K::Open, K::Medium, K::Nobody, K::Nobody})));       // the same lobby on a shipped map: a place
            w.run(300);
            ASSERT_TRUE(game.lobby->request_start());
            w.run(3000);
            ASSERT_TRUE(w.status("lobb0001").state == RoomState::Running);
        }
        fs::remove_all(dir);
    } TEST_END();

    TEST_CASE("S3.212 A START Cancelled Because One Person's Game Cannot Load The Map: Everybody Is Told Whose Game It Is And Why (Too Old For An Extended Map), The Leader How To Start Without Them; The Pause Of A Minute Names Them Again; Removing Them Ends The Pause At Once") {
        const fs::path dir = fresh_dir("s203");
        World w(limits_with(8));
        w.mgr.mutable_store().set_approved({approved_map(dir, "Small Ext.lvl", 100, 100, true)});       // (100 x 100: a room plays it; the machines of the newest version read it from `dir`)
        Client& pia = w.connect_page("Pia", "stal0001", lobby_block_of());
        Client& bob = w.connect_page("Bob", "stal0001", lobby_block_of());
        const net::SeatKey pia_key = pia.lobby->key();
        const net::SeatKey bob_key = bob.lobby->key();
        ASSERT_TRUE(pia.lobby->request_plan(plan_for("Small Ext.lvl")));
        w.run(300);
        ASSERT_EQ(w.status("stal0001").map, std::string("Small Ext.lvl"));
        ASSERT_TRUE(pia.lobby->request_start());
        w.run(100);
        Client& pia_game = w.connect_game("Pia", "stal0001", pia_key, false, dir.string());          // Pia's game is of the newest version
        Client& bob_game = w.connect_game("Bob", "stal0001", bob_key);                              // Bob's is an older one: it cannot read an Extended map
        w.run(500);
        const std::string too_old = "255||Bob's game is too old for this map: refresh the page or update the game.";
        const std::string remove = "255||To start without Bob, press Remove on their color.";
        ASSERT_TRUE(w.status("stal0001").state == RoomState::Waiting);
        ASSERT_MSG(said(pia_game.room_chat) == (std::vector<std::string>{too_old, remove}), "the leader was told: " + (said(pia_game.room_chat).empty() ? std::string("nothing") : said(pia_game.room_chat)[0]));            // the leader: whose game, and what to do
        ASSERT_EQ(said(bob_game.room_chat), (std::vector<std::string>{too_old}));                    // the person: what is wrong with their game (and no instruction for the leader)
        for (int round = 0; round < 3; ++round) {                                                    // the 2nd and 3rd cancel cost two seconds each; the 4th starts the pause of a minute
            ASSERT_TRUE(pia_game.lobby->request_start());
            w.run(2500);
        }
        const size_t told = said(pia_game.room_chat).size();
        ASSERT_TRUE(told >= 6);
        ASSERT_TRUE(pia_game.lobby->request_start());                                                // asked for in the long pause: the leader is told whose game it is, not only that the match could not start
        w.run(300);
        const std::vector<std::string> lines = said(pia_game.room_chat);
        ASSERT_TRUE(lines.size() == told + 2 && lines[told] == too_old && lines[told + 1] == remove);
        for (const std::string& line : lines) ASSERT_TRUE(line.find("could not start a few times") == std::string::npos);
        ASSERT_TRUE(w.status("stal0001").state == RoomState::Waiting && !w.status("stal0001").starting);
        // the leader removes Bob (the Remove that the lobby has): the pause that Bob caused is over, and the START goes through now
        ASSERT_TRUE(pia_game.lobby->request_remove(1));
        w.run(300);
        ASSERT_EQ(bob.lobby->reject_reason() == net::RejectReason::Kicked || bob_game.lobby->reject_reason() == net::RejectReason::Kicked, true);
        ASSERT_TRUE(pia_game.lobby->request_plan(plan_for("Small Ext.lvl", {K::Open, K::Medium, K::Nobody, K::Nobody})));
        w.run(300);
        ASSERT_TRUE(pia_game.lobby->request_start());
        w.run(4000);
        ASSERT_TRUE(w.status("stal0001").state == RoomState::Running);
        fs::remove_all(dir);
    } TEST_END();

    TEST_CASE("S3.214 A Game That Does Not Answer At All Within The Load Time Is Named As A Slow One (\"did not load the map in time\"), not as one that cannot read the map") {
        World w(limits_with(8));
        Client& pia = w.connect_page("Pia", "stal0003", lobby_block_of());
        Client& bob = w.connect_page("Bob", "stal0003", lobby_block_of());
        const net::SeatKey pia_key = pia.lobby->key();
        const net::SeatKey bob_key = bob.lobby->key();
        Client& pia_game = w.connect_game("Pia", "stal0003", pia_key);
        Client& bob_game = w.connect_game("Bob", "stal0003", bob_key);
        bob_game.asleep = true;                                                                       // Bob's machine says nothing: it neither loads nor fails
        ASSERT_TRUE(pia_game.lobby->request_start());
        w.run(2000);
        ASSERT_TRUE(w.status("stal0003").state == RoomState::Loading && said(pia_game.room_chat).empty());
        w.run(61000);                                                                                 // the 60 s of the load are over
        ASSERT_TRUE(w.status("stal0003").state == RoomState::Waiting);
        ASSERT_EQ(said(pia_game.room_chat), (std::vector<std::string>{"255||Bob's game did not load the map in time.", "255||To start without Bob, press Remove on their color."}));
    } TEST_END();

    TEST_CASE("S3.215 A Name Of 32 Characters With The Sentence Of An Old Game Is 101 Characters: The Name Is Cut so that the line is whole (100)") {
        const fs::path dir = fresh_dir("s215");
        World w(limits_with(8));
        w.mgr.mutable_store().set_approved({approved_map(dir, "Small Ext.lvl", 100, 100, true)});
        const std::string longest(32, 'B');
        Client& pia = w.connect_page("Pia", "stal0004", lobby_block_of());
        Client& bob = w.connect_page(longest, "stal0004", lobby_block_of());
        const net::SeatKey pia_key = pia.lobby->key();
        const net::SeatKey bob_key = bob.lobby->key();
        ASSERT_TRUE(pia.lobby->request_plan(plan_for("Small Ext.lvl")));
        w.run(300);
        Client& pia_game = w.connect_game("Pia", "stal0004", pia_key, false, dir.string());
        w.connect_game(longest, "stal0004", bob_key);
        ASSERT_TRUE(pia_game.lobby->request_start());
        w.run(600);
        const std::vector<std::string> lines = said(pia_game.room_chat);
        ASSERT_TRUE(lines.size() == 2);
        ASSERT_EQ(lines[0], "255||" + std::string(31, 'B') + "'s game is too old for this map: refresh the page or update the game.");
        ASSERT_EQ(lines[1], "255||To start without " + longest + ", press Remove on their color.");
        ASSERT_TRUE(lines[0].size() - 5 == net::kMaxChatChars);
        fs::remove_all(dir);
    } TEST_END();

    TEST_CASE("S3.213 A Game Of A Classic Map That Cannot Load It Is Named Too (\"could not load this map\"), the person who is the leader gets no instruction to remove themselves, and the notice is the room's, one line for each cancelled START") {
        World w(limits_with(8));
        Client& pia = w.connect_page("Pia", "stal0002", lobby_block_of());
        Client& bob = w.connect_page("Bob", "stal0002", lobby_block_of());
        const net::SeatKey pia_key = pia.lobby->key();
        const net::SeatKey bob_key = bob.lobby->key();
        ASSERT_TRUE(pia.lobby->request_start());
        Client& pia_game = w.connect_game("Pia", "stal0002", pia_key, true);                         // the LEADER's game cannot load the map
        Client& bob_game = w.connect_game("Bob", "stal0002", bob_key);
        w.run(500);
        ASSERT_EQ(said(pia_game.room_chat), (std::vector<std::string>{"255||Pia's game could not load this map."}));      // (no "press Remove" for the leader: it is their own game)
        ASSERT_EQ(said(bob_game.room_chat), (std::vector<std::string>{"255||Pia's game could not load this map."}));
        ASSERT_TRUE(w.status("stal0002").state == RoomState::Waiting && w.status("stal0002").joined == 2);
        for (int round = 0; round < 3; ++round) {                                                    // three more cancels, and the START in the minute's pause: the leader's own game is named, with no instruction to remove it
            ASSERT_TRUE(pia_game.lobby->request_start());
            w.run(2500);
        }
        const size_t before = said(pia_game.room_chat).size();
        ASSERT_TRUE(pia_game.lobby->request_start());
        w.run(300);
        const std::vector<std::string> lines = said(pia_game.room_chat);
        ASSERT_TRUE(before == 4 && lines.size() == 5 && lines[4] == "255||Pia's game could not load this map.");
    } TEST_END();
}
